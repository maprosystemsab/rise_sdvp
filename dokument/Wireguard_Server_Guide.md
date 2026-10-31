# 🛡️ WireGuard Global VPN Guide: Router, Laptop & 4G-Robotar

Denna guide beskriver principen för hur du får din VPN-tunnel att fungera **överallt i hela världen**, inklusive med robotarnas 4G/5G-modem, med din nya Lenovo-dator hemma som det centrala navet (Servern).

---

## Principen
**Din nya Lenovo-dator hemma fungerar som VPN-servern (navet).** Alla robotar och bärbara datorer (Klienter) ansluter *till* den. När de är anslutna är de på samma "virtuella nätverk" (`192.168.200.x`) som om de satt ihop med en osynlig nätverkskabel, oavsett var de befinner sig fysiskt.

---

## 📶 Steg 1: Hemma-Routern (Port Forward)
*Detta görs en gång i din router hemma där Lenovo-servern står.*

Eftersom robotarna är ute i fältet och ansluter över internet, måste din hemma-router veta var den ska skicka VPN-trafiken när den kommer fram till huset.

1. **Logga in på din hemma-router** via din webbläsare (adressen brukar vara t.ex. `192.168.1.1` eller `192.168.0.1`).
2. Leta efter fliken **Port Forwarding** (Portvidarebefordran / Virtuell Server).
3. Skapa en ny regel med följande exakta inställningar:
   * **Namn:** `WireGuard`
   * **Protokoll:** `UDP` (Jätteviktigt! Inte TCP)
   * **Extern Port:** `51820`
   * **Intern Port:** `51820`
   * **Skicka till IP:** `192.168.1.57` *(Detta är Lenovo-serverns lokala IP!)*
4. Spara och starta om routern om den ber om det.

---

## 🌐 Steg 2: Skaffa en gratis "Adress" (DuckDNS)
Eftersom din internetleverantör hemma byter ut din hem-IP då och då (t.ex. när routern startas om), vill vi inte skriva in ett IP-nummer som slutar fungera imorgon.

1. Gå till **[DuckDNS.org](https://www.duckdns.org/)** på din bärbara dator.
2. Logga in med Google/Github och välj ett valfritt namn, t.ex. `maprorobot`. Du har nu skapat adressen: **`maprorobot.duckdns.org`**.
3. Denna adress kommer nu **alltid** att peka på ditt hem, oavsett om ditt hem-IP ändras!

---

## 🍓 Steg 3: Installera på Roboten (eller din laptop)
*När du kör `./wireguard.sh` på en robot eller din laptop, svarar du så här på frågorna:*

1. **Namn:** Ge enheten ett namn, t.ex. `Drangen_1` eller `Gunnars_Laptop`.
2. **IP-adress (Sista siffran X):** 
   * Ge din laptop t.ex. **`3`** (vilket ger IP `192.168.200.3`)
   * Ge roboten t.ex. **`8`** (vilket ger IP `192.168.200.8`)
3. **Server-IP:** 
   * Skriptet kommer fråga om du vill anpassa serverinställningarna. Svara **`y`**.
   * När den ber om Server-IP, skriver du in din DuckDNS-adress: **`maprorobot.duckdns.org`**.
4. Spara! Skriptet genererar nu en **Public Key** för enheten. Skriv ner eller kopiera den!

---

## 🖥️ Steg 4: Godkänn enheten på Lenovo-Servern
*Varje gång du skapar en ny klient måste du berätta för Lenovo-servern att den får lov att ansluta.*

Kör detta på din Lenovo-server (via SSH eller direkt på skärmen):
1. Starta admin-verktyget: `sudo ./wireguard_admin.sh`
2. Välj **Alternativ 2** (Registrera ny klient).
3. Skriv in namnet du valde (t.ex. `Drangen_1`).
4. Klistra in enhetens **Public Key** som du fick i Steg 3.
5. Välj samma IP-adress som du gav enheten (t.ex. `192.168.200.8`).
6. Välj **Alternativ 4** i menyn för att ladda om servern live. **KLART!**

---

## 📶 Saker att tänka på med 4G/5G-modemen i robotarna

Mobilnätet fungerar lite annorlunda än vanligt Wi-Fi, men vårt WireGuard-system är **byggt för att klara detta perfekt**. Här är tre saker som är bra att känna till:

1. **Brandväggar i 4G-nätet (CGNAT):**
   Mobiloperatörer (som Telia, Tele2 etc.) blockerar nästan alltid inkommande trafik till SIM-kort. Du kan aldrig "ringa upp" en robot utifrån fältet. 
   * *Varför vårt VPN löser detta:* Det är därför vi kör VPN! Det är roboten som ringer *hem* till din Lenovo-server. När tunneln väl är öppen inifrån roboten kan trafiken flöda fritt åt båda hållen. Du "tunnlar" helt förbi mobiloperatörens spärrar!
2. **Keepalive (Hålla anslutningen vid liv):**
   Mobilmaster stänger ner "inaktiva" anslutningar efter bara några sekunder för att spara ström. 
   * *Hur vi löst det:* Våra genererade klientfiler har inställningen `PersistentKeepalive = 25` inlagd. Det betyder att roboten skickar ett litet osynligt "ping" var 25:e sekund. Detta lurar mobilmasten att tro att anslutningen är aktiv hela tiden, så roboten tappar aldrig kontakten!
3. **Dataförbrukning:**
   WireGuard är otroligt lättviktigt. Det drar nästan ingen extra mobildata alls (till skillnad från tunga krypteringar som OpenVPN), vilket sparar på dina mobilabonnemang.
