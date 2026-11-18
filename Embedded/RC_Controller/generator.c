/*
	Elverk med fjärrstart (RobAnt, firmware 30.27)

	Honda GX200 med en Goldenmotor HPM 5 kW direkt på vevaxeln (1:1), driven av en
	VESC Labs Maxim+ 150 V. Allt går över CAN:
	  - veva:   CAN_PACKET_SET_CURRENT        (standard-VESC, ingen kod i Maxim+)
	  - ladda:  CAN_PACKET_SET_CURRENT_BRAKE  (Goldenmotorn bromsar = generator)
	  - varv/ström läses ur VESC:ens statusmeddelanden (STATUS 1 och 5 måste vara på)
	  - stoppkabeln: eget standard-ID GEN_CAN_SID var 100:e ms, [0x47 'G'][1 = får gå,
	    0 = jorda][generator-ID]. En LispBM-snutt på Maxim+ (generator_maxim.lisp) styr
	    +12VA (pin 17) som drar ett relä vars NC-kontakt annars jordar Hondans
	    stoppkabel; reläet släpper (= jordar) även om
	    meddelandena slutar komma (styrkortet dött, CAN-kabeln av).

	Förlopp (generator_start):
	  AV -> VEVAR (stoppkabeln släppt, vevström rampas upp)
	     -> varvet över `startvarv`  -> KOLLAR (strömmen 0: går Hondan själv?)
	        -> varvet håller över `gaarvarv` -> LADDAR (bromsström rampas upp till `ladd`)
	        -> annars PAUS och nytt försök, högst `forsok` gånger -> FEL
	  LADDAR -> stopp / batteriet fullt / tjuvstopp / övervarv -> STOPPAR -> AV (eller FEL)

	Säkerhet:
	  - stoppkabeln är jordad i alla lägen utom VEVAR/KOLLAR/PAUS/LADDAR;
	  - ingen status från Maxim+ på 0,5 s under drift -> FEL (stoppkabeln jordas);
	  - nödstopp (generator_nodstopp) -> direkt FEL;
	  - inga förarkommandon på `tapp` sekunder -> stopp (0 = av; automatiken undantagen);
	  - drivningens duty begränsas till `tak` medan elverket startar och går.

	Inställningarna ändras live med terminalkommandot `gen` och nollställs vid omstart
	(som `diff`). Standardvärdena står nedan.
 */

#include "generator.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ch.h"
#include "hal.h"
#include "conf_general.h"
#include "datatypes.h"
#include "commands.h"
#include "comm_can.h"
#include "buffer.h"
#include "terminal.h"
#include "timeout.h"
#include "nodstopp_di.h"

#ifndef GEN_VESC_ID
#define GEN_VESC_ID				105		// Maxim+:s CAN-ID (FMV .13: 105, avläst 18/11)
#endif
#define GEN_CAN_SID				0x05E	// stoppkabel-meddelandet (ledigt standard-ID)
#define GEN_PERIOD_MS			20
#define GEN_SID_PERIOD_MS		100
#define GEN_STATUS_PERIOD_MS	1000
#define GEN_CAN_TYST_MS			500

// Inställningar (gen ...)
static int   m_id = GEN_VESC_ID;
static float m_polpar = 4.0f;		// Goldenmotorns polpar: ERPM / polpar = varv/min (kontrollera i VESC Tool)
static float m_riktning = 1.0f;		// vevriktning, byt med `gen riktning -1`
static float m_vev_a = 50.0f;		// vevström (A); GX200 har automatisk dekompression
static float m_vev_ramp_s = 0.3f;	// upp till vevströmmen
static float m_vev_tid_s = 3.0f;	// längsta vevning per försök
static int   m_forsok = 3;
static float m_paus_s = 3.0f;		// mellan försöken
static float m_startvarv = 1100.0f;	// varv/min: släpp vevningen och kolla
static float m_gaarvarv = 900.0f;	// varv/min som Hondan måste hålla själv
static float m_kolla_s = 0.8f;
static float m_ladd_a = 30.0f;		// bromsström = laddning (Marvin körde ca 1,5 kW)
static float m_ladd_ramp_s = 3.0f;
static float m_stoppvarv = 800.0f;	// under detta i 1 s under laddning = tjuvstopp
static float m_maxvarv = 4200.0f;	// GX200 är regulerad till ca 3600
static float m_tak = 0.5f;			// drivningens duty-faktor medan elverket är igång
static float m_full_v = 0.0f;		// stoppa laddningen över denna spänning (0 = av)
static float m_start_v = 0.0f;		// automatiken: starta under denna spänning (0 = av)
static float m_tapp_s = 120.0f;		// stoppa efter så lång tid utan förarkommandon (0 = av)

static volatile GEN_LAGE m_lage = GEN_AV;
static volatile GEN_FEL m_fel = GEN_FEL_INGET;
static volatile bool m_vill_start = false;
static volatile bool m_vill_stopp = false;
static volatile bool m_auto = false;
static volatile bool m_vill_nodstopp = false;
// Båda nödstoppen (Car_Clients GPIO och MP101:s DI1) anropar generator_nodstopp var
// 200:e ms så länge knappen är intryckt. Start och automatik vägras tills det varit
// tyst i GEN_NODSTOPP_SPARR_MS, oavsett om de kommer från RCS, terminalen eller tråden.
#define GEN_NODSTOPP_SPARR_MS	1000
static volatile systime_t m_nodstopp_tid = 0;
static volatile bool m_nodstopp_hord = false;
static int m_forsok_nr = 0;
static systime_t m_lage_tid = 0;
static float m_varv = 0.0f;			// senaste varv/min (mekaniskt, utan tecken)
static float m_strom = 0.0f;		// senaste motorström (A)
static float m_vin = 0.0f;			// batterispänningen enligt Maxim+ (V)
static bool m_status_ok = false;

static THD_WORKING_AREA(gen_thread_wa, 1024);
static THD_FUNCTION(gen_thread, arg);
static void terminal_gen(int argc, const char **argv);
static void skicka_stoppkabel(void);
static void skicka_stoppkabel_varde(bool far_ga_nu);

static const char *lage_namn(GEN_LAGE l) {
	switch (l) {
	case GEN_AV: return "AV";
	case GEN_VEVAR: return "VEVAR";
	case GEN_KOLLAR: return "KOLLAR";
	case GEN_PAUS: return "PAUS";
	case GEN_LADDAR: return "LADDAR";
	case GEN_STOPPAR: return "STOPPAR";
	case GEN_STOPPAD_FEL: return "FEL";
	}
	return "?";
}

static const char *fel_namn(GEN_FEL f) {
	switch (f) {
	case GEN_FEL_INGET: return "inget";
	case GEN_FEL_STARTADE_INTE: return "startade inte";
	case GEN_FEL_TJUVSTOPP: return "Hondan stannade under laddning";
	case GEN_FEL_OVERVARV: return "overvarv";
	case GEN_FEL_CAN: return "ingen status fran Maxim+ pa CAN";
	case GEN_FEL_NODSTOPP: return "nodstopp";
	case GEN_FEL_INGEN_KONTAKT: return "ingen kontakt med foraren";
	case GEN_FEL_EJ_KONFIG: return "inget generator-ID";
	case GEN_FEL_VRIDS_INTE: return "motorn vrids inte vid vevning";
	}
	return "?";
}

static float sek_i_lage(void) {
	return (float)ST2MS(chVTTimeElapsedSinceX(m_lage_tid)) / 1000.0f;
}

static void skicka_status(void) {
	unsigned char buf[24];
	int len = generator_status_paket(buf, (unsigned char)main_id);
	commands_send_packet(buf, len);
}

static void satt_lage(GEN_LAGE l) {
	if (l != m_lage) {
		commands_printf("Elverk: %s -> %s", lage_namn(m_lage), lage_namn(l));
	}
	m_lage = l;
	m_lage_tid = chVTGetSystemTimeX();
	skicka_stoppkabel();	// direkt, inte först vid nästa 100 ms
	skicka_status();
}

static void satt_fel(GEN_FEL f) {
	m_fel = f;
	commands_printf("Elverk FEL: %s", fel_namn(f));
	satt_lage(GEN_STOPPAD_FEL);
}

// --- CAN ----------------------------------------------------------------------

static void skicka_strom(uint8_t cmd, float ampere) {
	if (m_id < 0 || m_id > 253) {
		return;
	}
	uint8_t d[4];
	int32_t ind = 0;
	buffer_append_int32(d, (int32_t)(ampere * 1000.0f), &ind);
	comm_can_transmit_eid((uint32_t)m_id | ((uint32_t)cmd << 8), d, 4);
}

static void veva(float ampere) {
	skicka_strom(CAN_PACKET_SET_CURRENT, ampere * m_riktning);
}

static void bromsa(float ampere) {
	skicka_strom(CAN_PACKET_SET_CURRENT_BRAKE, ampere);
}

static void slapp(void) {
	skicka_strom(CAN_PACKET_SET_CURRENT, 0.0f);
}

static bool far_ga(void) {
	return m_lage == GEN_VEVAR || m_lage == GEN_KOLLAR || m_lage == GEN_PAUS ||
			m_lage == GEN_LADDAR;
}

static void skicka_stoppkabel(void) {
	skicka_stoppkabel_varde(far_ga());
}

static void skicka_stoppkabel_varde(bool far_ga_nu) {
	uint8_t d[3];
	d[0] = 0x47;
	d[1] = far_ga_nu ? 1 : 0;
	d[2] = (uint8_t)(m_id < 0 ? 255 : m_id);
	comm_can_transmit_sid(GEN_CAN_SID, d, 3);
}

static void las_status(void) {
	m_status_ok = false;
	if (m_id < 0) {
		return;
	}
	can_status_msg *st = comm_can_get_status_msg_id(m_id);
	if (!st) {
		return;
	}
	if (ST2MS(chVTTimeElapsedSinceX(st->rx_time)) > GEN_CAN_TYST_MS) {
		return;
	}
	m_status_ok = true;
	m_varv = fabsf(st->rpm) / (m_polpar > 0.5f ? m_polpar : 1.0f);
	m_strom = st->current;
	m_vin = st->v_in;
}

// --- Gränssnitt ---------------------------------------------------------------

static bool nodstopp_sparrar(void) {
	if (!nodstopp_di_tillater_start()) {
		return true;	// MP101:s DI1: intryckt eller spärrad (väntar på stoppkommando)
	}
	return m_nodstopp_hord &&
			ST2MS(chVTTimeElapsedSinceX(m_nodstopp_tid)) < GEN_NODSTOPP_SPARR_MS;
}

void generator_start(void) {
	if (nodstopp_sparrar()) {
		commands_printf("Elverk: start vagras, nodstoppet ar intryckt");
		return;
	}
	m_vill_start = true;
	m_vill_stopp = false;
}

void generator_stopp(void) {
	m_vill_stopp = true;
	m_vill_start = false;
}

void generator_nodstopp(void) {
	// Läget byts i tråden (m_vill_nodstopp hanteras först i varje varv), men
	// stoppkabeln jordas och motorn släpps direkt härifrån.
	m_vill_start = false;
	m_vill_stopp = false;
	m_auto = false;
	m_vill_nodstopp = true;
	m_nodstopp_tid = chVTGetSystemTimeX();
	m_nodstopp_hord = true;
	slapp();
	skicka_stoppkabel_varde(false);
}

void generator_auto(bool pa) {
	if (pa && nodstopp_sparrar()) {
		commands_printf("Elverk: automatik vagras, nodstoppet ar intryckt");
		return;
	}
	m_auto = pa;
}

GEN_LAGE generator_lage(void) {
	return m_lage;
}

GEN_FEL generator_fel(void) {
	return m_fel;
}

float generator_effekt_faktor(void) {
	GEN_LAGE l = m_lage;
	if (l == GEN_VEVAR || l == GEN_KOLLAR || l == GEN_PAUS || l == GEN_LADDAR) {
		return m_tak;
	}
	return 1.0f;
}

/*
 * [id][CMD_GENERATOR][läge u8][fel u8][varv/min i16][ström A*10 i16]
 * [batteri V*10 u16][försök u8][auto u8][laddström A*10 u16][status ok u8]
 */
int generator_status_paket(unsigned char *buf, unsigned char id_ret) {
	int32_t ind = 0;
	buf[ind++] = id_ret;
	buf[ind++] = CMD_GENERATOR;
	buf[ind++] = (uint8_t)m_lage;
	buf[ind++] = (uint8_t)m_fel;
	buffer_append_int16(buf, (int16_t)m_varv, &ind);
	buffer_append_int16(buf, (int16_t)(m_strom * 10.0f), &ind);
	buffer_append_uint16(buf, (uint16_t)(m_vin * 10.0f), &ind);
	buf[ind++] = (uint8_t)m_forsok_nr;
	buf[ind++] = m_auto ? 1 : 0;
	buffer_append_uint16(buf, (uint16_t)(m_ladd_a * 10.0f), &ind);
	buf[ind++] = m_status_ok ? 1 : 0;
	return ind;
}

void generator_init(void) {
	m_lage_tid = chVTGetSystemTimeX();
	chThdCreateStatic(gen_thread_wa, sizeof(gen_thread_wa), NORMALPRIO, gen_thread, NULL);
	terminal_register_command_callback(
			"gen",
			"Elverk med fjarrstart (Honda + Goldenmotor + Maxim+ pa CAN). Utan argument: visa lage.\n"
			"  start | stopp | auto pa|av\n"
			"  id N, polpar N, riktning 1|-1, vev A, vevramp S, vevtid S, forsok N, paus S,\n"
			"  startvarv R, gaarvarv R, kolla S, ladd A, laddramp S, stoppvarv R, maxvarv R,\n"
			"  tak X (drivningens duty-faktor under drift), full V, startspanning V, tapp S\n"
			"  (installningarna nollstalls vid omstart)",
			"[start|stopp|auto pa|av|NAMN VARDE]",
			terminal_gen);
}

// --- Förloppet ----------------------------------------------------------------

static THD_FUNCTION(gen_thread, arg) {
	(void)arg;
	chRegSetThreadName("Generator");

	systime_t sid_tid = 0, status_tid = 0, lag_varv_tid = 0;
	bool lag_varv = false;
	systime_t kontakt_tapp = 0;
	bool hade_kontakt = true;

	for (;;) {
		chThdSleepMilliseconds(GEN_PERIOD_MS);
		las_status();

		// Kommandon. Nödstoppet först.
		if (m_vill_nodstopp) {
			m_vill_nodstopp = false;
			m_vill_start = false;
			if (m_lage != GEN_AV && m_lage != GEN_STOPPAD_FEL) {
				slapp();
				satt_fel(GEN_FEL_NODSTOPP);
			}
		}
		if (m_vill_stopp) {
			m_vill_stopp = false;
			m_auto = false;
			if (m_lage == GEN_STOPPAD_FEL) {
				m_fel = GEN_FEL_INGET;
				satt_lage(GEN_AV);	// kvittera felet
			} else if (m_lage != GEN_AV) {
				satt_lage(GEN_STOPPAR);
			}
		}
		if (m_vill_start) {
			m_vill_start = false;
			if (nodstopp_sparrar()) {
				// nödstopp mellan kommandot och tråden: inget
			} else if (m_lage == GEN_AV || m_lage == GEN_STOPPAD_FEL || m_lage == GEN_STOPPAR) {
				m_fel = GEN_FEL_INGET;
				if (m_id < 0) {
					satt_fel(GEN_FEL_EJ_KONFIG);
				} else if (!m_status_ok) {
					satt_fel(GEN_FEL_CAN);
				} else if (m_varv > m_gaarvarv) {
					m_forsok_nr = 0;
					satt_lage(GEN_LADDAR);	// den går redan (startad för hand)
				} else {
					m_forsok_nr = 1;
					satt_lage(GEN_VEVAR);
				}
			}
		}

		// Batteriautomatiken
		if (m_auto && !nodstopp_sparrar() && m_status_ok && m_start_v > 1.0f && m_lage == GEN_AV &&
				m_vin > 1.0f && m_vin < m_start_v) {
			commands_printf("Elverk: automatisk start (%.1f V < %.1f V)",
					(double)m_vin, (double)m_start_v);
			m_forsok_nr = 1;
			satt_lage(GEN_VEVAR);
		}

		// Kontakt med föraren (inte i automatiken: där ska den gå obemannad)
		if (timeout_has_timeout()) {
			if (hade_kontakt) {
				kontakt_tapp = chVTGetSystemTimeX();
				hade_kontakt = false;
			}
		} else {
			hade_kontakt = true;
		}
		if (!m_auto && m_tapp_s > 0.0f && !hade_kontakt && far_ga() &&
				(float)ST2MS(chVTTimeElapsedSinceX(kontakt_tapp)) > m_tapp_s * 1000.0f) {
			m_fel = GEN_FEL_INGEN_KONTAKT;
			commands_printf("Elverk: ingen kontakt med foraren pa %.0f s, stoppar",
					(double)m_tapp_s);
			satt_lage(GEN_STOPPAR);
		}

		// Tyst CAN under drift
		if (far_ga() && !m_status_ok) {
			satt_fel(GEN_FEL_CAN);
		}

		float t = sek_i_lage();
		switch (m_lage) {
		case GEN_AV:
		case GEN_STOPPAD_FEL:
			// Inga strömkommandon: VESC:ens egen timeout släpper motorn.
			break;

		case GEN_VEVAR: {
			float a = m_vev_a;
			if (m_vev_ramp_s > 0.01f && t < m_vev_ramp_s) {
				a *= t / m_vev_ramp_s;
			}
			veva(a);
			if (m_varv > m_maxvarv) {
				satt_fel(GEN_FEL_OVERVARV);
			} else if (m_varv > m_startvarv) {
				slapp();
				satt_lage(GEN_KOLLAR);
			} else if (t > 1.0f && m_varv < 30.0f && fabsf(m_strom) > 0.5f * m_vev_a) {
				// full ström men stillastående: fastkörd eller fel inkopplad
				slapp();
				satt_fel(GEN_FEL_VRIDS_INTE);
			} else if (t > m_vev_tid_s) {
				slapp();
				satt_lage(GEN_PAUS);
			}
		} break;

		case GEN_KOLLAR:
			slapp();
			if (m_varv > m_maxvarv) {
				satt_fel(GEN_FEL_OVERVARV);
			} else if (t > m_kolla_s) {
				if (m_varv > m_gaarvarv) {
					commands_printf("Elverk: Hondan gar (%.0f varv/min), forsok %d",
							(double)m_varv, m_forsok_nr);
					satt_lage(GEN_LADDAR);
				} else {
					satt_lage(GEN_PAUS);
				}
			}
			break;

		case GEN_PAUS:
			slapp();
			if (m_varv > m_gaarvarv && t > 1.0f) {
				satt_lage(GEN_LADDAR);	// tände sent
			} else if (t > m_paus_s) {
				if (m_forsok_nr >= m_forsok) {
					satt_fel(GEN_FEL_STARTADE_INTE);
				} else {
					m_forsok_nr++;
					satt_lage(GEN_VEVAR);
				}
			}
			break;

		case GEN_LADDAR: {
			float a = m_ladd_a;
			if (m_ladd_ramp_s > 0.01f && t < m_ladd_ramp_s) {
				a *= t / m_ladd_ramp_s;
			}
			bromsa(a);
			if (m_varv > m_maxvarv) {
				satt_fel(GEN_FEL_OVERVARV);
				break;
			}
			if (m_varv < m_stoppvarv && t > 1.0f) {
				if (!lag_varv) {
					lag_varv = true;
					lag_varv_tid = chVTGetSystemTimeX();
				} else if (ST2MS(chVTTimeElapsedSinceX(lag_varv_tid)) > 1000) {
					lag_varv = false;
					slapp();
					satt_fel(GEN_FEL_TJUVSTOPP);
					break;
				}
			} else {
				lag_varv = false;
			}
			if (m_full_v > 1.0f && m_vin > m_full_v) {
				commands_printf("Elverk: batteriet fullt (%.1f V > %.1f V), stoppar",
						(double)m_vin, (double)m_full_v);
				satt_lage(GEN_STOPPAR);
			}
		} break;

		case GEN_STOPPAR:
			slapp();
			if ((m_varv < 100.0f && t > 1.0f) || t > 6.0f || !m_status_ok) {
				if (m_fel != GEN_FEL_INGET) {
					satt_lage(GEN_STOPPAD_FEL);
				} else {
					satt_lage(GEN_AV);
				}
			}
			break;
		}

		if (chVTTimeElapsedSinceX(sid_tid) >= MS2ST(GEN_SID_PERIOD_MS)) {
			sid_tid = chVTGetSystemTimeX();
			skicka_stoppkabel();
		}
		if (m_lage != GEN_AV && chVTTimeElapsedSinceX(status_tid) >= MS2ST(GEN_STATUS_PERIOD_MS)) {
			status_tid = chVTGetSystemTimeX();
			skicka_status();
		}
	}
}

// --- Terminal -----------------------------------------------------------------

typedef struct {
	const char *namn;
	float *varde;
	float min, max;
} gen_param;

static void terminal_gen(int argc, const char **argv) {
	static float id_f, forsok_f;
	id_f = (float)m_id;
	forsok_f = (float)m_forsok;
	const gen_param p[] = {
		{"id", &id_f, -1.0f, 253.0f},
		{"polpar", &m_polpar, 1.0f, 30.0f},
		{"riktning", &m_riktning, -1.0f, 1.0f},
		{"vev", &m_vev_a, 0.0f, 300.0f},
		{"vevramp", &m_vev_ramp_s, 0.0f, 3.0f},
		{"vevtid", &m_vev_tid_s, 0.5f, 10.0f},
		{"forsok", &forsok_f, 1.0f, 10.0f},
		{"paus", &m_paus_s, 0.5f, 30.0f},
		{"startvarv", &m_startvarv, 100.0f, 4000.0f},
		{"gaarvarv", &m_gaarvarv, 100.0f, 4000.0f},
		{"kolla", &m_kolla_s, 0.2f, 5.0f},
		{"ladd", &m_ladd_a, 0.0f, 150.0f},
		{"laddramp", &m_ladd_ramp_s, 0.0f, 20.0f},
		{"stoppvarv", &m_stoppvarv, 100.0f, 4000.0f},
		{"maxvarv", &m_maxvarv, 1000.0f, 6000.0f},
		{"tak", &m_tak, 0.0f, 1.0f},
		{"full", &m_full_v, 0.0f, 150.0f},
		{"startspanning", &m_start_v, 0.0f, 150.0f},
		{"tapp", &m_tapp_s, 0.0f, 3600.0f},
	};
	const int n = sizeof(p) / sizeof(p[0]);

	if (argc >= 2) {
		if (strcmp(argv[1], "start") == 0) {
			generator_start();
		} else if (strcmp(argv[1], "stopp") == 0) {
			generator_stopp();
		} else if (strcmp(argv[1], "auto") == 0 && argc >= 3) {
			generator_auto(strcmp(argv[2], "pa") == 0);
		} else if (argc >= 3) {
			float x = 0.0f;
			bool hittad = false;
			if (sscanf(argv[2], "%f", &x) == 1) {
				for (int i = 0;i < n;i++) {
					if (strcmp(argv[1], p[i].namn) == 0) {
						hittad = true;
						if (x >= p[i].min && x <= p[i].max) {
							*p[i].varde = x;
						} else {
							commands_printf("%s: %.1f-%.1f", p[i].namn,
									(double)p[i].min, (double)p[i].max);
						}
					}
				}
			}
			if (!hittad) {
				commands_printf("Okant: gen %s %s", argv[1], argv[2]);
			}
			if (m_lage == GEN_AV || m_lage == GEN_STOPPAD_FEL) {
				m_id = (int)id_f;	// byt bara ID när elverket står
			}
			m_forsok = (int)forsok_f;
			m_riktning = m_riktning < 0.0f ? -1.0f : 1.0f;
		}
		chThdSleepMilliseconds(60);
	}

	commands_printf("Elverk: %s, fel: %s, forsok %d/%d, auto %s",
			lage_namn(m_lage), fel_namn(m_fel), m_forsok_nr, m_forsok, m_auto ? "pa" : "av");
	commands_printf("  Maxim+ ID %d: %s, %.0f varv/min, %.1f A, %.1f V",
			m_id, m_status_ok ? "hors" : "HORS INTE", (double)m_varv, (double)m_strom,
			(double)m_vin);
	for (int i = 0;i < n;i++) {
		commands_printf("  %-13s %.2f", p[i].namn, (double)*p[i].varde);
	}
}
