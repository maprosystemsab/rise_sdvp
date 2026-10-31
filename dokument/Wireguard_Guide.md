# 🛡️ Komplett Guide: Sätt upp WireGuard VPN för Fjärrstyrning

Denna guide beskriver hur du sätter upp en privat, säker och supersnabb krypterad VPN-tunnel för att övervaka och fjärrstyra robotarna (**Drängen** och **Macbot**) ute på fältet över 4G-mobilnätet.

Vi använder de två skräddarsydda skripten:
1. **`wireguard_admin.sh`** (Körs på VPN-servern hemma).
2. **`wireguard.sh`** (Körs på klienterna: laptopen och robotens Raspberry Pi).

---

## 📐 VPN-Nätverkets IP-struktur

Alla enheter i vårt privata VPN-nätverk tilldelas en fast IP-adress i intervallet `192.168.200.x`:

* **VPN-Server (Hemma):** `192.168.200.1`
* **Gunnars dator (Laptop):** `192.168.200.3`
* **Gamla Drängen (Pi):** `192.168.200.4`
* **MacTrac EIP (Jetson):** `192.168.200.7`
* **Nya Drängen (Pi):** `192.168.200.8`

---

## 🖥️ FAS 1: Konfigurera VPN-Servern (Hemma på Ubuntu Server)

Din hemmaserver (t.ex. en mini-PC/Thin Client med Ubuntu Server) kommer att agera "växel" och skicka all VPN-trafik säkert mellan laptopen och robotarna.

### 1. För över administrationsskriptet till servern
Från din utvecklingsdator/laptop, skicka över `wireguard_admin.sh` till din nya hemmaserver:
```bash
scp /home/mapro/RControllStation/uppdaterade_filer/wireguard_admin.sh användarnamn@SERVER-IP:~/
```
*(Ersätt `användarnamn` och `SERVER-IP` med inloggningsuppgifterna för din server).*

### 2. Initiera VPN-servern
Logga in på din hemmaserver via terminalen och kör:
```bash
chmod +x wireguard_admin.sh
sudo ./wireguard_admin.sh
```
Välj **Alternativ 1) Initiera Server**. Skriptet kommer automatiskt att:
- Installera alla nödvändiga WireGuard-paket.
- Detektera ditt aktiva nätverkskort (t.ex. `enp3s0` eller `eno1`) för korrekt vidarebefordran av trafik.
- Generera serverns nyckelpar (`server_private.key` & `server_public.key`).
- Konfigurera brandväggsregler (NAT/routing) samt starta tjänsten.

**⚠️ VIKTIGT:** Kopiera serverns **Public Key** som skriptet skriver ut på slutet! Du behöver den i nästa fas.

### 3. Öppna porten i din hemma-router (Port Forwarding)
För att dina klienter ute på fältet ska kunna ansluta hem till servern måste du öppna dörren i din router:
1. Logga in på din hemma-routers administrationssida.
2. Leta efter **Port Forwarding** (Portvidarebefordran).
3. Skapa en ny regel:
   - **Protokoll:** `UDP` (Mycket viktigt, ej TCP!).
   - **Extern/Intern Port:** `51820`.
   - **Mottagare (IP):** Din hemmaservers lokala IP-adress (t.ex. `192.168.1.120`).

---

## 🍓 FAS 2: Konfigurera Klienterna (Robot-Pi & Laptop)

Kör detta på varje enhet som ska ansluta till vårt VPN-nätverk (t.ex. din laptop eller robotens Raspberry Pi 4).

### 1. Starta installationsskriptet för klienten
Gå till mappen där skripten finns och kör:
```bash
sudo ./wireguard.sh
```
Skriptet genererar unika kryptonycklar för den här enheten och visar maskinens **Public Key** i klarblå text.

**⚠️ VIKTIGT:** Kopiera den här enhetens **Public Key**! Du ska strax klistra in den på servern.

### 2. Välj roll och mata in serveruppgifter
Välj den roll som motsvarar enheten (t.ex. *Gunnars dator* för laptopen eller *Nya Drängen* för robot-Pi:n) för att automatiskt tilldela rätt fasta VPN IP-adresser (`192.168.200.x`).

Mata därefter in uppgifterna till din VPN-server hemma:
- **Serverns Public Key:** (Den du kopierade i FAS 1, steg 2).
- **Serverns IP/Domän:** Din hemmaservers publika IP-adress (eller DDNS-adress, t.ex. `mitthem.duckdns.org`).
- **Serverns Port:** `51820`.

Skriptet genererar nu en komplett och färdig klientkonfiguration i `/etc/wireguard/wg0.conf`.

---

## 🤝 FAS 3: Registrera Klienterna på Servern

Nu måste du berätta för din hemmaserver att dina klienter har tillåtelse att ansluta.

1. Gå tillbaka till din hemmaserver och starta administrationsverktyget igen:
   ```bash
   sudo ./wireguard_admin.sh
   ```
2. Välj **Alternativ 2) Registrera ny klient (Peer)**.
3. Fyll i klientens uppgifter:
   - **Namn:** (T.ex. `Nya Drängen` eller `Gunnars Laptop`).
   - **Public Key:** Klistra in klientens Public Key (som du kopierade i FAS 2, steg 1).
   - **IP-adress:** Välj exakt samma IP som du tilldelade klienten i FAS 2 (t.ex. `192.168.200.8`).
4. Skriptet sparar uppgifterna i `/etc/wireguard/wg0.conf` och laddar automatiskt om VPN-servern live helt sömlöst (`wg syncconf wg0`).

---

## 🏁 FAS 4: Starta anslutningen och verifiera!

### 1. Starta WireGuard på klienten
Kör detta kommando på din laptop eller robot-Pi:
```bash
sudo wg-quick up wg0
```
*(För att stänga av anslutningen i framtiden kör du: `sudo wg-quick down wg0`)*

### 2. Verifiera anslutningen (Ping)
Från din laptop kan du nu testa att pinga din VPN-server:
```bash
ping 192.168.200.1
```
Om roboten är igång och ansluten kan du pinga den direkt över mobilnätet:
```bash
ping 192.168.200.8
```

### 3. Kontrollera live-status på servern
Vill du se vilka enheter som är anslutna, hur mycket data de skickar och när de senast skickade en signal ("handshake"), kör du bara administrationsskriptet på servern:
```bash
sudo ./wireguard_admin.sh
```
och väljer **Alternativ 3) Visa registrerade användare & live-status**.

---

## 💡 Tips: Gratis Dynamic DNS (DuckDNS) om din hemma-IP ändras
Om du inte har en fast IP-adress från din internetleverantör (vilket är vanligast för hemmaabonnemang) kommer din publika IP-adress att ändras då och då. Detta gör att dina klienter tappar bort servern.

Lös detta enkelt med en gratis DDNS-tjänst:
1. Gå till [duckdns.org](https://www.duckdns.org/) och logga in.
2. Skapa en egen domän (t.ex. `mittrobotvpn.duckdns.org`).
3. Följ deras enkla installationsinstruktioner för "Install ➡️ linux cron" på din Ubuntu-server. 
4. Nu körs ett litet skript i bakgrunden på din server var 5:e minut som rapporterar din nuvarande hemma-IP till DuckDNS.
5. Ange bara `mittrobotvpn.duckdns.org` som server-IP på dina klienter så hittar de alltid rätt helt automatiskt!
