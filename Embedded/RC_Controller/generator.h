/*
	Elverk med fjärrstart (RobAnt): Honda GX200 med en Goldenmotor 5 kW direkt på
	vevaxeln, driven av en VESC Maxim+ på CAN. Se generator.c.
 */

#ifndef GENERATOR_H_
#define GENERATOR_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	GEN_AV = 0,			// Hondan stoppad (stoppkabeln jordad), motorn släppt
	GEN_VEVAR = 1,		// stoppkabeln släppt, VESC:en drar runt Hondan
	GEN_KOLLAR = 2,		// vevningen släppt: går Hondan av sig själv?
	GEN_PAUS = 3,		// paus mellan två startförsök
	GEN_LADDAR = 4,		// Hondan går, Goldenmotorn bromsar = laddar batteriet
	GEN_STOPPAR = 5,	// stoppkabeln jordad, väntar på att Hondan stannar
	GEN_STOPPAD_FEL = 6,		// stoppad p.g.a. fel (orsak i generator_fel()); kvar tills ny start/stopp
} GEN_LAGE;

typedef enum {
	GEN_FEL_INGET = 0,
	GEN_FEL_STARTADE_INTE = 1,	// inga tändningar på alla försöken
	GEN_FEL_TJUVSTOPP = 2,		// Hondan dog under laddning (bränsle slut? för hög last?)
	GEN_FEL_OVERVARV = 3,
	GEN_FEL_CAN = 4,			// ingen status från Maxim+ (fel ID, kabel, ström?)
	GEN_FEL_NODSTOPP = 5,
	GEN_FEL_INGEN_KONTAKT = 6,	// inga kommandon från föraren på `gen tapp` sekunder
	GEN_FEL_EJ_KONFIG = 7,		// inget generator-ID satt
	GEN_FEL_VRIDS_INTE = 8,		// vevströmmen går ut men inget varv: fastkörd/fel koppling
} GEN_FEL;

void generator_init(void);
void generator_start(void);
void generator_stopp(void);
void generator_nodstopp(void);		// nödstoppet: stoppa direkt, även mitt i start
void generator_auto(bool pa);		// batteriautomatik: start under / stopp över spänningsgränser
GEN_LAGE generator_lage(void);
GEN_FEL generator_fel(void);
// 1.0 normalt; mindre (gen tak) medan elverket startar eller går, så att drivningen
// inte drar mer än Hondan + batteriet orkar.
float generator_effekt_faktor(void);
// Statuspaket [id][CMD_GENERATOR][...] till send_index; returnerar längden.
int generator_status_paket(unsigned char *buf, unsigned char id_ret);

#endif /* GENERATOR_H_ */
