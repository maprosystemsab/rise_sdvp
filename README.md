# 🛰️ RISE SDVP - Optimerat & Helautomatiserat Styrsystem (Mapro Systems AB & Claude branch) 

Styrsystemet för Mapro Systems AB:s maskiner: styrkortets firmware, Car_Client på
maskinens Raspberry Pi, RControlStation på datorn och webbservern med gårdar, fält och
banor. Bygger på [RISE SDVP](https://github.com/vedderb/rise_sdvp) av Benjamin Vedder
(GPLv3, se `LICENSE`).

Robotstyrning (fjärrkörning med kamera och handkontroll, `robotd`) finns i
[maprosystemsab/robot-control](https://github.com/maprosystemsab/robot-control).

---

## Maskiner och kort

Firmwaren byggs per maskin. Samma källkod, maskinen väljs vid bygget.

| Maskin | Byggmål | Kommentar |
|---|---|---|
| RobAnt | `make robant` | VESC-drift höger/vänster + styrning; hastighetsgivare på TX (PA2), vinkelgivare på RX (PA3) |
| MacTrac / MacBot | `make mactrac` | Hydraulik via CAN, gas via servoutgång |
| Drängen | `make drangen` | |

| Kort | Byggval | Kommentar |
|---|---|---|
| CarController (STM32F405, u-blox ZED-F9P) | (standard) | Kortet som sitter i maskinerna i dag |
| Upwis MP101 / ROV_MCU (STM32F415, CM5, BMI270) | `BOARD=mp101` | T.ex. `make robant BOARD=mp101`. Se `Embedded/RC_Controller/ROVMCU.md` |

Kör `make clean` mellan byggen för olika maskiner eller kort (de delar `build/`).

---

## Delarna

| Mapp | Vad |
|---|---|
| `Embedded/RC_Controller/` | Styrkortets firmware (ChibiOS) |
| `Linux/Car_Client/` | Länken mellan Pi:n och styrkortet (USB), RTK via rtklib, TCP/UDP 8300 till RControlStation och robotd. `--nodstopp-gpio 27` läser en nödstoppsknapp (NC) på Pi:n |
| `Linux/RControlStation/` | Kontrollstationen på datorn: karta, banor, autopilot, statusruta. Webbserverns adress ställs in i fältet **Server** på fliken Farm |
| `Linux/StatusSkarm/` | Statusskärmen på maskinens Pi (styrkort, Car_Client, internet, RTK, nödstopp) |
| `Linux/PI/` | udev-regler, MP101-installation och annat för Pi:n |
| `Linux/tools/` | Felsökning: `fw_version.py`, `las_styrkort.py`, `testa_write.py`, `kort_hangt.sh`, `kort_tradar.py` |
| `web/` | Webbservern (gårdar, fält, banor, maskiner) med kundisolering (`kunder.exempel.json`, `kundisolering.sh`) |
| `Hardware/` | Kretskortsritningar (KiCad) från RISE SDVP |

---

## Installera

| Skript | Var | Vad |
|---|---|---|
| `install_pi.sh` | Maskinens Pi | Paket, udev, Swepos-RTK, bygger Car_Client och startar den automatiskt. `sudo CAR_ID=4 bash install_pi.sh`; `--board mp101` för MP101-kortet; `NODSTOPP_GPIO=27` om en nödstoppsknapp är inkopplad |
| `install_dator.sh` | Datorn | Bygger och installerar RControlStation |
| `install_allt.sh` | Maskinens Pi | Grundsystemet (`install_pi.sh`) plus flashning av styrkortet |
| `wireguard.sh` | Pi eller dator | WireGuard-klient mot Mapros server (`192.168.200.x`) |

## Bygga och flasha firmwaren

```bash
cd Embedded/RC_Controller && make clean && make -j4 robant      # bara bygga
./flash_styrkort.sh                                             # bygga och flasha (frågar efter maskin)
./flash_styrkort.sh --maskin rovmcu --profile robant            # MP101-kortet
./flash_styrkort_macbot.sh                                      # MacBot
```

Flashning görs med ST-Link V2 och OpenOCD. Ny firmware provas först på ett styrkort utan
maskin, sedan med maskinen upphissad och sist på underlag.

**Versioner:** det som körs ute är märkt med taggar, t.ex. `fw-30.3-robant` och
`fw-30.3-mactrac`. `mactrac-slut` är den gamla MacTrac-grenens sista version.

---

## Samarbete

`master` är den gemensamma koden som vi vet fungerar; maskinerna och kundernas datorer
uppdaterar sig därifrån. Ändringar görs i en egen gren och går in via Pull Request som
Mapro godkänner. Se **[CONTRIBUTING.md](CONTRIBUTING.md)**.

Lägg aldrig in lösenord, Swepos/NTRIP-uppgifter, WireGuard-nycklar eller `data.db` – repot
är publikt.

## Dokumentation

* `installationsguide.md` – ST-Link och SWD-kopplingar, SSH till Pi:n
* `Wireguard_Guide.md` – sätta upp en WireGuard-server
* `Sakerhetsanalys.md` – säkerhetsanalys (VPN, timeout i firmwaren, nödstopp)
* `Embedded/RC_Controller/ROVMCU.md` – MP101/ROV_MCU-kortets stiftkarta
* `Documentation/` – RISE SDVP:s ursprungliga manual

## Ändringar mot RISE SDVP i korthet

* RTCM skickas rått direkt till u-bloxen, som ställs in med UART2 i 115200 baud vid uppstart; större buffert i `ublox_send`.
* Autopiloten: skydd mot division med noll och NaN, sätter körriktningen själv.
* Heartbeat i sekunder; autopiloten stannar vid timeout och vid Write.
* Aktuatorer per aktivitet (`CMD_RC_CONTROL_ADV`), CAN-diagnostik (`CMD_GET_VESC_STATUS`).
* Car_Client: skickar u-blox-inställningarna igen om RAWX uteblir; nödstopp via GPIO.
* Kundisolering i webbservern, installationsskript för Pi och dator.
