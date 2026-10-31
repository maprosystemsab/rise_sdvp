# 🗺️ Master-Installationsguide: Från Noll till Full RTK-Drift för Drängen & MacTrac

Välkommen till den kompletta, uppdaterade installationsguiden för styrsystemet! 

Genom de **smarta och modulära installationsskripten** vi har byggt är processen nu extremt förenklad. Du behöver inte längre brottas med tunga IDE:er som Qt Creator eller komplicerade manuella kompileringssteg. Allt sköts nu direkt via terminalen med optimerade skript.

---

## 📐 Översikt: Hur hänger systemet ihop?

I din robot finns det totalt **endast två fysiska kretskort**:

1. **Raspberry Pi 4 (Hjärnan):** En liten enkortsdator som är ansluten till internet (via routern/4G). Den hämtar kontinuerligt RTK-korrektionsdata från Swepos och kör huvudprogrammet `Car_Client` i en bakgrunds-session (`screen`).
2. **Carcontroller-kortet (Integrerat Styrkort & GPS):** Detta är ett specialdesignat kretskort som har allt integrerat på samma platta! Det innehåller:
   - En **STM32F4-mikrodator** (som hanterar autopiloten, läser sensorer och skickar gas/styrning till motorerna).
   - Ett **u-blox ZED-F9P GPS-chip** (mottagaren för satelliter).
   
   *Eftersom u-blox-chippet och STM32-mikrodatorn sitter på samma fysiska kort, är de anslutna direkt på mönsterkortet. För att Raspberry Pi ska kunna prata med båda används två separata Micro-USB-portar direkt på kortet.*

---

## 🔌 Kopplingsschema & Kabeldragning

Det integrerade kortet har **två Micro-USB-portar** och båda ska vara kopplade till Raspberry Pi 4 med varsin USB-kabel under drift.

* **USB-Kabel 1 (Styrning):** Går till STM32-mikrodatorns port. Denna hanterar gas, styrning och autopilotsignaler (mappas automatiskt till `/dev/car`).
* **USB-Kabel 2 (GPS):** Går till u-blox-chippets port. Denna strömmar satellitdata och tar emot RTK-korrektioner (mappas automatiskt till `/dev/ublox`).

### 🗺️ Visuellt Kopplingsschema (ASCII)

```text
       +-------------------------------------------------------------+
       |                       RASPBERRY PI 4                        |
       |                     (Datorn på roboten)                     |
       |                                                             |
       |   [USB-Port 1]        [USB-Port 2]         [USB-Port 3]     |
       +--------#-------------------#--------------------#-----------+
                #                   #                    #
            USB-Kabel 1         USB-Kabel 2          USB-Kabel
             (Styrning)            (GPS)                 #
                #                   #                    #
                V                   V                    V
       +-----------------------------------------+  +----------------+
       |           INTEGRERAT STYRKORT           |  |   ST-LINK V2   |
       |         (Carcontroller & GPS)           |  | USB-PROGRAMMER |
       |                                         |  +-------#--------+
       |  [STM32 Micro-USB]    [u-blox Micro-USB]|          #
       |   (Blir /dev/car)      (Blir /dev/ublox)|          # (SWD 4 st)
       |                                         |          #
       +-----------------------------------------+          V
       |                                         |  +----------------+
       |                                         |  | SWD-Kontakt    |
       |                                         |  | på styrkortet  |
       |                                         |  |  - SWCLK <===  | (SWCLK)
       |                                         |  |  - SWDIO <===  | (SWDIO)
       |                                         |  |  - GND   <===  | (GND)
       |                                         |  |  - 3.3V  <===  | (3.3V)
       +-----------------------------------------+  +----------------+
```

---

## 🔄 Det kompletta arbetsflödet: Steg för steg

Här är den exakta ordningen du ska följa för att få igång hela systemet från grunden.

### FAS 1: Flasha och programmera styrkortet (STM32)

Det absolut snabbaste och smidigaste är att flasha styrkortet direkt från din **laptop/utvecklardator** med hjälp av en **ST-LINK V2** (eller i nödfall via en vanlig USB-kabel i DFU-läge).

#### Alternativ A: Via ST-LINK V2 (Rekommenderas!)
1. Koppla SWD-kablarna från din **ST-LINK V2** till styrkortets SWD-pinnar:
   * **SWCLK** ➡️ **SWCLK**
   * **SWDIO** ➡️ **SWDIO**
   * **GND** ➡️ **GND**
   * **3.3V** ➡️ **3.3V** *(Detta strömsätter hela styrkortet under flashningen, så du behöver inget batteri inkopplat!)*
2. Sätt i din ST-LINK i laptopens USB-port.
3. Öppna en terminal på din laptop, gå till projektkatalogen `/home/mapro/RControllStation/` och kör:
   ```bash
   ./flash_styrkort.sh
   ```
4. Välj om du vill bygga för **Drängen** eller **Mactrac**. Skriptet kompilerar källkoden och laddar upp den till styrkortet automatiskt via OpenOCD. 
5. Skriptet frågar därefter om du vill starta ett **interaktivt skrivbordstest**. Anslut de två USB-kablarna från styrkortet till laptopen så kan du se live-satellitdata direkt på din skärm för att verifiera att allt fungerar!

---

### FAS 2: Sätt upp laptopen/utvecklardatorn (`install_dator.sh`)

För att din laptop ska kunna köra det grafiska kart- och styrprogrammet **RControlStation** (som du använder för att skicka rutter till roboten och övervaka den), kör du det här skriptet på laptopen.

1. Öppna en terminal i mappen `/home/mapro/RControllStation/`.
2. Kör installationsskriptet:
   ```bash
   sudo ./install_dator.sh
   ```
3. Skriptet detekterar din Ubuntu-version (t.ex. Ubuntu 22.04), installerar rätt Qt6-, GDAL-, SDL2-bibliotek, och bygger hela applikationen med CMake.
4. **Hur du startar programmet:**
   När skriptet är klart startar du RControlStation på laptopen med kommandot:
   ```bash
   cd rise_sdvp/rise_sdvp/Linux/RControlStation/build/cmake_linux/build/lin && ./RControlStation
   ```

---

### FAS 3: Sätt upp Raspberry Pi 4 på roboten (`install_pi.sh`)

Eftersom Raspberry Pi:n är "hjärnan" på roboten och inte har någon skärm, styrs den trådlöst via SSH. Här beskriver vi hur du enkelt för över dina filer till Pi:n och kör installationsskriptet.

#### 1. Starta och anslut till robotens nätverk
1. Starta din Raspberry Pi 4 på roboten. Se till att den är ansluten till robotens Wi-Fi-router.
2. Koppla upp din laptop på **samma Wi-Fi-router**.
3. Hitta din Raspberry Pi:s IP-adress (t.ex. `192.168.1.150` eller liknande). Du kan se anslutna enheter i routerns gränssnitt eller scanna av Wi-Fi-nätverket med en gratis mobilapp som **Fing**.

#### 2. För över installationsfilerna till Raspberry Pi (Över Wi-Fi)
Eftersom du har alla skript och hela den fungerande källkoden redo på din laptop (`/home/mapro/RControllStation`), kan du skicka över hela mappen trådlöst till din Raspberry Pi med ett enda snabbt kommando i laptopens terminal!

Kör detta kommando på din laptop (ersätt `pi` med användarnamnet på din Pi, och `IP-ADRESS` med Pi:ns IP):
```bash
scp -r /home/mapro/RControllStation pi@IP-ADRESS:~/
```
*Detta kopierar över samtliga filer, skript och källkod till hemmamappen på din Raspberry Pi på under en minut!*

#### 3. Logga in på din Raspberry Pi via SSH
Öppna din laptops terminal och logga in på din Pi:
```bash
ssh pi@IP-ADRESS
```
*(Ange lösenordet till din Raspberry Pi när du blir tillfrågad).*

#### 4. Kör installationsskriptet på din Pi!
När du är inloggad via SSH på din Pi, gå till mappen du nyss skickade över:
```bash
cd ~/RControllStation
```
Gör skripten körbara:
```bash
chmod +x install_allt.sh install_pi.sh flash_styrkort.sh wireguard.sh
```
Kör Pi-installationen med sudo:
```bash
sudo ./install_pi.sh
```

##### Vad `install_pi.sh` gör automatiskt:
- **Paket:** Installerar alla nödvändiga Linux-paket (`rtklib`, `screen`, `udev`, etc.).
- **USB-regler (udev):** Lägger till udev-regler så att `/dev/car` och `/dev/ublox` alltid tilldelas rätt USB-port automatiskt oavsett vilken ordning de kopplas in i.
- **Swepos RTK:** Ber dig mata in dina Swepos-uppgifter (användarnamn, lösenord och din basstations position) och installerar en automatisk bakgrundstjänst (`car_rtk.service`) som strömmar RTK-korrektioner.
- **Bygger klienten:** Kompilerar `Car_Client` från källkoden på din Pi.
- **Autostart:** Installerar en systemtjänst (`car_client.service`) som automatiskt startar `Car_Client` i en bakgrundssession (`screen`) varje gång roboten startar!

---

### FAS 4: Konfigurera VPN för fjärrstyrning över 4G (WireGuard)

Om du vill kunna övervaka och köra roboten på fältet via 4G-mobilnätet (istället för att behöva stå nära robotens lokala Wi-Fi) sätter du enkelt upp en privat och säker krypterad WireGuard VPN-tunnel.

#### 1. Sätt upp din VPN-server hemma
Har du en liten mini-PC eller server hemma kör du administratörsskriptet där:
1. Kör skriptet på din hemmaserver:
   ```bash
   sudo ./wireguard_admin.sh
   ```
2. Välj **Alternativ 1 (Initiera Server)**. Skriptet genererar krypterade servernycklar, aktiverar IP-forwarding/NAT-routing i Linux och startar servern.
3. Se till att öppna UDP-port `51820` i din hemmaserver/router (Port Forwarding).

#### 2. Konfigurera klienterna (Laptop och Robot-Pi)
På både din laptop och din robot-Pi kör du installationsskriptet:
1. Kör klient-skriptet:
   ```bash
   sudo ./wireguard.sh
   ```
2. Skriptet genererar unika nycklar för den aktuella maskinen och visar dess **Public Key** i klarblå text. Kopiera denna nyckel!
3. Välj vilken roll maskinen har (t.ex. *Gunnars dator* för laptopen eller *Nya Drängen* för robot-Pi:n) för att automatiskt tilldela rätt fasta VPN IP-adresser (`192.168.200.x`).

#### 3. Registrera klienterna på din hemmaserver
1. Öppna administratörsverktyget på din hemmaserver igen (`sudo ./wireguard_admin.sh`).
2. Välj **Alternativ 2 (Registrera ny klient)**.
3. Ange maskinens namn, klistra in den **Public Key** du nyss kopierade från klienten, och välj dess VPN-IP.
4. Servern laddar automatiskt om konfigurationen sömlöst utan avbrott. **Klart!** Nu kan din laptop och roboten prata krypterat med varandra oavsett var i världen de befinner sig.

---

## 🏁 Drift & Start: Nu kör vi!

1. Placera roboten utomhus med fri sikt mot himlen.
2. Koppla in de två USB-kablarna mellan Pi 4 och styrkortet.
3. Starta roboten (Swepos-korrektionerna och `Car_Client` går igång helt automatiskt i bakgrunden vid boot!).
4. Om du vill se vad `Car_Client` gör i realtid, logga in på din Pi via SSH och kör:
   ```bash
   screen -r car
   ```
   *(För att gå ur screen-vyn igen utan att stänga av programmet, tryck `Ctrl + A` följt av `D`).*
5. Starta **RControlStation** på din laptop. GPS-indikatorn kommer snabbt gå från *3D Fix* till *RTK Float* och slutligen till en lysande grön **RTK Fix** (med ca 3 centimeters precision!).

---

## 🔍 Snabb Felsökning

### Hur kontrollerar jag att Swepos-strömmen fungerar?
Logga in på din Pi via SSH och kontrollera bakgrundstjänsten:
```bash
sudo systemctl status car_rtk.service
```
Vill du se dataströmmen live för att se om den ansluter till Swepos, kör:
```bash
journalctl -u car_rtk -f
```

### udev hittar inte USB-enheterna `/dev/car` eller `/dev/ublox`
Se till att USB-kablarna fungerar och är hela. Du kan lista alla anslutna USB-enheter med kommandot:
```bash
lsusb
```
Om u-blox eller STM32 saknas där får inte kortet tillräckligt med ström eller så är en USB-kabel trasig.
