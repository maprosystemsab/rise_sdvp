#!/bin/bash
# installera.sh — installerar statusskärmen på robotens Pi. Körs PÅ Pi:n, som den
# användare som loggar in i skrivbordet (t.ex. macbot-slu), från den här mappen:
#
#   cd ~/rise_sdvp/Linux/StatusSkarm && bash installera.sh
#
# Gör:
#   1. Rust via rustup om det saknas (i hemkatalogen, ingen sudo)
#   2. Bygger statusskarm och lägger den i ~/.local/bin
#   3. sudoers-regel: EXAKT dessa kommandon utan lösenord för den här användaren,
#      inget annat: omstart av car_client, car_rtk och WireGuard (wg-quick@wg0),
#      omstart av Pi:n och läsning av WireGuards senaste handskakning
#   4. Startar automatiskt i skrivbordet (helskärm), startar om sig själv om den kraschar
#
# Säkert att köra igen efter git pull (bygger om, skriver om regeln och autostarten).
# Ta bort:  bash installera.sh --ta-bort
#
# Utan skrivbord (MP101/CM5 med Debian Lite):  bash installera.sh --kiosk [--dsi 0|1] [--rotera 180]
#   I stället för steg 4: Waveshare 4,3" DSI i config.txt (skärmen härmar Raspberrys
#   7"-skärm), cage från apt och tjänsten statusskarm-kiosk som kör appen i helskärm.
#   --dsi: skärmporten, 0 som standard (MP101: J1 = DPHY0 + SCL0/SDA0 = I2C-buss 0).
#   Ladda bara porten som skärmen sitter på: en port utan skärm kan visa en spökskärm.
#   Tjänsten startar BARA när skärmens pekkrets (ft5x06) svarar; utan skärm hoppas den
#   över och kortet fungerar som vanligt. --rotera 90/180/270 vrider bild och touch.
#   Overlayn läses vid uppstart: starta om kortet efter första installationen.

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
ANV="$(id -un)"
SUDOERS=/etc/sudoers.d/statusskarm
AUTOSTART="$HOME/.config/autostart/statusskarm.desktop"
KIOSK_TJANST=/etc/systemd/system/statusskarm-kiosk.service
KIOSK_VILLKOR=/usr/local/bin/statusskarm-har-skarm
TOUCH_REGEL=/etc/udev/rules.d/99-statusskarm-touch.rules
BAKLJUS_REGEL=/etc/udev/rules.d/99-zz-statusskarm-bakljus.rules
CONFIG=/boot/firmware/config.txt
CMDLINE=/boot/firmware/cmdline.txt
KIOSK=0
ROTERA=0
DSI=0

while [ $# -gt 0 ]; do
  case "$1" in
    --ta-bort)
      pkill -x statusskarm || true
      if [ -f "$KIOSK_TJANST" ]; then
        sudo systemctl disable --now statusskarm-kiosk.service || true
        sudo rm -f "$KIOSK_TJANST" "$KIOSK_VILLKOR" "$TOUCH_REGEL" "$BAKLJUS_REGEL"
        sudo systemctl daemon-reload
      fi
      rm -f "$AUTOSTART" "$HOME/.local/bin/statusskarm" "$HOME/.local/bin/statusskarm-loop"
      sudo rm -f "$SUDOERS"
      echo "Statusskärmen borttagen. (Raderna i $CONFIG och $CMDLINE lämnas kvar; de gör inget utan skärm.)"
      exit 0 ;;
    --kiosk) KIOSK=1 ;;
    --rotera) ROTERA="${2:-}"; shift ;;
    --dsi) DSI="${2:-}"; shift ;;
    *) echo "Okänt argument: $1" >&2; exit 1 ;;
  esac
  shift
done
case "$ROTERA" in 0|90|180|270) ;; *) echo "--rotera ska vara 0, 90, 180 eller 270" >&2; exit 1 ;; esac
case "$DSI" in 0|1) ;; *) echo "--dsi ska vara 0 eller 1" >&2; exit 1 ;; esac

echo "--- Rust ---"
if ! command -v cargo >/dev/null 2>&1 && [ ! -x "$HOME/.cargo/bin/cargo" ]; then
  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal
fi
# shellcheck disable=SC1091
source "$HOME/.cargo/env" 2>/dev/null || true

echo "--- Bygger (några minuter första gången) ---"
cargo build --release
mkdir -p "$HOME/.local/bin"
install -m 755 target/release/statusskarm "$HOME/.local/bin/statusskarm"

# Startar om appen om den skulle krascha (men inte i en snabb loop).
cat > "$HOME/.local/bin/statusskarm-loop" <<'LOOP'
#!/bin/bash
while true; do
  "$HOME/.local/bin/statusskarm" "$@"
  sleep 3
done
LOOP
chmod 755 "$HOME/.local/bin/statusskarm-loop"

echo "--- sudoers-regel för $ANV (bara de sex kommandona) ---"
TMP="$(mktemp)"
cat > "$TMP" <<RULE
# statusskärmen (rise_sdvp/Linux/StatusSkarm): exakt dessa kommandon, inget annat.
$ANV ALL=(root) NOPASSWD: /usr/bin/systemctl restart car_client.service
$ANV ALL=(root) NOPASSWD: /usr/bin/systemctl restart car_rtk.service
$ANV ALL=(root) NOPASSWD: /usr/bin/systemctl reboot
$ANV ALL=(root) NOPASSWD: /usr/bin/systemctl poweroff
$ANV ALL=(root) NOPASSWD: /usr/bin/systemctl restart wg-quick@wg0.service
$ANV ALL=(root) NOPASSWD: /usr/bin/wg show wg0 latest-handshakes
RULE
# Kontrollera syntaxen innan den läggs på plats: en trasig sudoers-fil kan låsa ute sudo.
sudo visudo -cf "$TMP"
sudo install -m 440 -o root -g root "$TMP" "$SUDOERS"
rm -f "$TMP"

if [ "$KIOSK" = 1 ]; then
  echo "--- Skärmen i config.txt: Waveshare 4,3\" DSI på DSI$DSI ---"
  # Skriv om blocket mellan markeringarna (och tidigare versioners rader) så att porten stämmer.
  sudo sed -i -e '/^# --- statusskarm (installera.sh --kiosk)/,/^# --- slut statusskarm/d' \
              -e '/^# Statusskärmen: Waveshare 4,3" DSI/d' -e '/^dtoverlay=vc4-kms-dsi-7inch/d' "$CONFIG"
  OVERLAY="dtoverlay=vc4-kms-dsi-7inch"
  [ "$DSI" = 0 ] && OVERLAY="$OVERLAY,dsi0"
  sudo tee -a "$CONFIG" >/dev/null <<CFG
# --- statusskarm (installera.sh --kiosk): Waveshare 4,3" DSI. Utan skärm gör raden ingenting.
[all]
$OVERLAY
# --- slut statusskarm
CFG
  echo "$OVERLAY i $CONFIG (gäller efter omstart)."

  echo "--- Rotation: $ROTERA° ---"
  sudo sed -i -E 's/ ?video=DSI-[0-9]:[^ ]*//g' "$CMDLINE"
  sudo rm -f "$TOUCH_REGEL"
  if [ "$ROTERA" != 0 ]; then
    sudo sed -i -E "1s/\$/ video=DSI-1:800x480M@60,rotate=$ROTERA video=DSI-2:800x480M@60,rotate=$ROTERA/" "$CMDLINE"
    case "$ROTERA" in
      90)  MATRIS="0 -1 1 1 0 0" ;;
      180) MATRIS="-1 0 1 0 -1 1" ;;
      270) MATRIS="0 1 0 -1 0 1" ;;
    esac
    echo "ENV{ID_INPUT_TOUCHSCREEN}==\"1\", ENV{LIBINPUT_CALIBRATION_MATRIX}=\"$MATRIS\"" | sudo tee "$TOUCH_REGEL" >/dev/null
  fi

  # Skärmens bakljus-regulator (I2C 0x45) binds av drivrutinen även utan skärm, och då
  # misslyckas systemd-backlight och systemet blir "degraded". Ljusstyrkan behöver inte sparas.
  # Filnamnet sorterar efter 99-systemd.rules, så regeln tar bort det den lade till.
  echo 'SUBSYSTEM=="backlight", KERNEL=="*-0045", ENV{SYSTEMD_WANTS}=""' | sudo tee "$BAKLJUS_REGEL" >/dev/null
  sudo systemctl reset-failed 'systemd-backlight@*' 2>/dev/null || true

  echo "--- cage (minimal Wayland för en app i helskärm) ---"
  command -v cage >/dev/null || sudo apt-get install -y cage

  echo "--- Tjänsten statusskarm-kiosk ---"
  # Villkoret väntar upp till 20 s på en riktig skärm: en DSI-utgång OCH skärmens pekkrets
  # (ft5x06), som bara finns när den svarar på I2C. DSI har ingen inkopplingsdetektering och
  # säger "connected" även utan skärm. Ingen skärm = tjänsten hoppas över (inget fel).
  sudo tee "$KIOSK_VILLKOR" >/dev/null <<'VILL'
#!/bin/sh
for _ in $(seq 20); do
  if ls /sys/class/drm/card*-DSI-* >/dev/null 2>&1 && grep -qi ft5x06 /proc/bus/input/devices; then
    exit 0
  fi
  sleep 1
done
exit 1
VILL
  sudo chmod 755 "$KIOSK_VILLKOR"
  sudo tee "$KIOSK_TJANST" >/dev/null <<UNIT
[Unit]
Description=Statusskärmen i helskärm (cage), bara när en DSI-skärm är ansluten
After=systemd-user-sessions.service network-online.target
Conflicts=getty@tty7.service
# Ingen omstartsloop om något ändå går fel: högst 5 försök på 2 minuter.
StartLimitIntervalSec=120
StartLimitBurst=5

[Service]
User=$ANV
PAMName=login
TTYPath=/dev/tty7
TTYReset=yes
TTYVHangup=yes
TTYVTDisallocate=yes
StandardInput=tty-fail
StandardOutput=journal
UtmpIdentifier=tty7
UtmpMode=user
ExecCondition=$KIOSK_VILLKOR
ExecStart=/usr/bin/cage -s -- $HOME/.local/bin/statusskarm
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
UNIT
  sudo systemctl daemon-reload
  sudo systemctl enable statusskarm-kiosk.service
  # Kör den nu också (hoppas över direkt om skärmen inte sitter i eller overlayn inte är laddad).
  sudo systemctl restart statusskarm-kiosk.service || true
  rm -f "$AUTOSTART"
  echo
  echo "Klart. Kiosk-läget startar statusskärmen vid uppstart när skärmen sitter i."
  echo "Första gången: starta om kortet så att skärmen i config.txt läses in."
  echo "Kolla signaler: statusskarm --text     (utan skärm, över ssh)"
  echo "Ta bort:        bash installera.sh --ta-bort"
  exit 0
fi

echo "--- Autostart i skrivbordet ---"
mkdir -p "$(dirname "$AUTOSTART")"
cat > "$AUTOSTART" <<DESK
[Desktop Entry]
Type=Application
Name=Statusskärm
Comment=Robotens status i helskärm (5" touch)
Exec=$HOME/.local/bin/statusskarm-loop
X-GNOME-Autostart-enabled=true
DESK

echo
echo "Klart. Startar automatiskt vid nästa inloggning i skrivbordet."
echo "Starta nu:      $HOME/.local/bin/statusskarm-loop &"
echo "Kolla signaler: statusskarm --text     (utan skärm, över ssh)"
echo "Ta bort:        bash installera.sh --ta-bort"
