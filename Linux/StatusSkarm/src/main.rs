//! statusskarm — fullskärmsstatus på robotens egen Pi (5" touchskärm i skåpet).
//!
//! Vid start: en stor nedräkning, 3:00 (modemet tar ~90 s, resten är uppe inom
//! 3 min). Tryck var som helst för att hoppa förbi. Sedan sju rutor som uppdateras
//! var 2:a sekund. Grön = OK, grå = inte OK. CPU-rutan är grön/gul/röd.
//!
//! Tryck på Car_Client-, Internet- eller RTK-rutan när den inte är grön för att starta om
//! efter en Ja/Nej-fråga. Pi-rutan går alltid att trycka på och frågar om Pi:n ska
//! startas om eller stängas av (stäng av innan strömmen bryts, skyddar SD-kortet).
//! Kommandona körs med
//! `sudo -n` och är begränsade av en sudoers-regel (se installera.sh).
//!
//! Allt läses lokalt på Pi:n och ingenting här tar Car_Client:s TCP-plats (8300),
//! så robotd/RControlStation påverkas inte:
//! - styrkortet: UDP till Car_Client (8300 → svar på 8301), kommandot
//!   CMD_AP_GET_ROUTE_PART med 0 punkter. Det ändrar inget och nollställer INTE
//!   styrkortets säkerhetstid (CMD_GET_STATE skulle göra det, och då skulle
//!   kortet aldrig stoppa maskinen om RControlStation-länken dör).
//! - RTK Float/Fix: NMEA GGA från rtkrcv på TCP 2948 (tål flera lyssnare).
//! - WireGuard: senaste handskakning (`wg show wg0 latest-handshakes`).
//!
//!   statusskarm            helskärm (på Pi:n)
//!   statusskarm --demo     påhittad status, omstarter bara låtsas (för test på datorn)
//!   statusskarm --fonster  fönster 720x1280 i stället för helskärm
//!   statusskarm --fonster=800x480   fönster i valfri storlek (t.ex. en 4,3"-skärm)
//!   statusskarm --utan-nedrakning   visa rutorna direkt
//!   statusskarm --text     ingen skärm: skriv status i terminalen var 2:a sekund (felsökning)

use std::io::{BufRead, BufReader};
use std::net::{TcpStream, UdpSocket};
use std::process::Command;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use eframe::egui;

const COUNTDOWN: Duration = Duration::from_secs(180);
const POLL: Duration = Duration::from_secs(2);
/// Styrkortet frågas mer sällan: Car_Client skriver en loggrad per svar.
const BOARD_POLL: Duration = Duration::from_secs(4);
/// Efter en omstart: rutan visar "Startar om..." och går inte att trycka på så länge.
const RESTART_LOCK: Duration = Duration::from_secs(10);
/// WireGuard räknas som uppe om senaste handskakning är yngre än så (servern
/// håller liv i tunneln var 25:e s, en ny handskakning sker varannan minut).
const HANDSHAKE_MAX_AGE: u64 = 180;
/// En GGA äldre än så räknas som ingen position.
const GGA_MAX_AGE: Duration = Duration::from_secs(5);

const CAR_CLIENT_ADDR: &str = "127.0.0.1:8300";
const BOARD_REPLY_ADDR: &str = "127.0.0.1:8301";
/// Unix-tid (s) för styrkortets senaste svar, skrivs av den instans som äger port
/// 8301 och läses av en andra instans (t.ex. `--text` över ssh).
const BOARD_SHARED_FILE: &str = "/tmp/statusskarm_styrkort_svar";
const RTKRCV_NMEA_ADDR: &str = "127.0.0.1:2948";
/// Skrivs av robot-control/scripts/rtk_bas.py (car_rtk.service).
const RTK_BAS_FIL: &str = "/run/rtk_bas.json";
const USB_DEVICE: &str = "/dev/vehicle";
/// ID 255 = alla, så kortet svarar oavsett vilket bil-ID Car_Client kör med.
const CMD_AP_GET_ROUTE_PART: u8 = 58;
const BOARD_QUERY: [u8; 7] = [255, CMD_AP_GET_ROUTE_PART, 0, 0, 0, 0, 0];
/// Vinkelgivaren (firmware 30.12+). Bara läsning, nollställer inte kortets säkerhetstid.
/// (143 är nödstoppets statuspaket från Car_Client, inte detta.)
const CMD_GET_VINKEL: u8 = 150;
const VINKEL_QUERY: [u8; 2] = [255, CMD_GET_VINKEL];

// Färger: mörk bakgrund, rutor lite ljusare i vila.
const BOX_IDLE: egui::Color32 = egui::Color32::from_rgb(52, 58, 68);
const BOX_OK: egui::Color32 = egui::Color32::from_rgb(40, 150, 70);
const BOX_WARN: egui::Color32 = egui::Color32::from_rgb(214, 160, 0);
const BOX_BAD: egui::Color32 = egui::Color32::from_rgb(190, 50, 40);
const TEXT: egui::Color32 = egui::Color32::WHITE;
const TEXT_DIM: egui::Color32 = egui::Color32::from_rgb(200, 205, 212);
// Utseende: mörkt "glas" med lysande kanter i statusfärgen.
const BG_DEEP: egui::Color32 = egui::Color32::from_rgb(7, 10, 16);
const PANEL: egui::Color32 = egui::Color32::from_rgb(16, 22, 32);
const GRID: egui::Color32 = egui::Color32::from_rgba_premultiplied(40, 70, 110, 22);
const NEON_OK: egui::Color32 = egui::Color32::from_rgb(46, 229, 157);
const NEON_WARN: egui::Color32 = egui::Color32::from_rgb(255, 184, 48);
const NEON_BAD: egui::Color32 = egui::Color32::from_rgb(255, 77, 94);
const NEON_IDLE: egui::Color32 = egui::Color32::from_rgb(92, 104, 124);
const NEON_CYAN: egui::Color32 = egui::Color32::from_rgb(56, 189, 248);
/// Dialogens lager: mörk bakgrund över rutorna, knapparna överst. Båda måste ta emot
/// tryck (egui:s Tooltip-lager gör det inte; då gick trycket på Ja/Stäng av rakt
/// igenom till bakgrunden och dialogen stängdes utan att något kördes).
const DIALOG_BG_ORDER: egui::Order = egui::Order::Middle;
const DIALOG_ORDER: egui::Order = egui::Order::Foreground;
/// Insamlingen räknas som hängd om ingen runda blivit klar på så länge.
const DATA_STALE: Duration = Duration::from_secs(8);

/// Senast kända status, skrivs av insamlingstråden och läses av gränssnittet.
#[derive(Clone, Default)]
struct Status {
    usb_present: bool,
    board_ok: bool,
    car_client_ok: bool,
    /// Sekunder sedan senaste WireGuard-handskakning.
    handshake_age: Option<u64>,
    /// Kunde inte läsa handskakningen (t.ex. sudoers-regeln saknas).
    handshake_error: bool,
    rtk_service_ok: bool,
    /// GGA fix-kvalitet: 0 ingen, 1 GPS, 2 DGPS, 4 RTK Fix, 5 RTK Float.
    fix_quality: Option<u8>,
    cpu_percent: Option<f32>,
    /// Vinkelgivaren enligt styrkortet, och när svaret kom.
    vinkel: Option<(Vinkel, Instant)>,
    /// När CPU:n senast gick över 90 % (för "röd efter 10 s").
    cpu_very_high_since: Option<Instant>,
    /// När insamlingstråden senast blev klar med en runda (hjärtslaget i toppremsan).
    updated: Option<Instant>,
}

/// Svar på CMD_GET_VINKEL.
#[derive(Clone, Copy, PartialEq, Debug)]
struct Vinkel {
    /// 0 = kortet har ingen vinkelgivare, 1 = OK, 2 = fel (ingen signal).
    status: u8,
    kalibrerad: bool,
    mv: u16,
    grader: f32,
}

/// Tolkar ett svarspaket [id][150][status][kal][mV u16][grader*100 i32].
fn parse_vinkel(p: &[u8]) -> Option<Vinkel> {
    if p.len() < 10 || p[1] != CMD_GET_VINKEL {
        return None;
    }
    Some(Vinkel {
        status: p[2],
        kalibrerad: p[3] != 0,
        mv: u16::from_be_bytes([p[4], p[5]]),
        grader: i32::from_be_bytes([p[6], p[7], p[8], p[9]]) as f32 / 100.0,
    })
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Action {
    RestartCarClient,
    RestartRtk,
    Reboot,
    RestartWireGuard,
    Shutdown,
    /// Pi-rutan: fråga om omstart eller avstängning.
    PiMenu,
}
const ACTIONS: usize = 6;

impl Action {
    fn question(self) -> &'static str {
        match self {
            Action::RestartCarClient => "Är du säker att du vill starta om car_service?",
            Action::RestartRtk => "Är du säker att du vill starta om RTK-tjänsten (car_rtk)?",
            Action::Reboot => "Är du säker att du vill starta om Pi:n?",
            Action::RestartWireGuard => "Är du säker att du vill starta om internet (WireGuard)?",
            Action::Shutdown => "Är du säker att du vill stänga av Pi:n?",
            Action::PiMenu => "Vad vill du göra med Pi:n?",
        }
    }

    fn warning(self) -> Option<&'static str> {
        match self {
            Action::Reboot => Some(
                "Allt stängs ner tills Pi:n är uppe igen: Car_Client, RTK och \
                 WireGuard-tunneln. Alla fjärranslutningar bryts. \
                 Därefter visas nedräkningen på nytt.",
            ),
            Action::RestartWireGuard => Some(
                "Tunneln tas ner och upp igen. Fjärranslutningar (RControlStation, ssh) \
                 bryts en kort stund och får anslutas på nytt.",
            ),
            Action::PiMenu => Some(
                "Stäng av innan strömmen bryts, det skyddar SD-kortet. Vänta tills skärmen \
                 är svart och den gröna lampan på Pi:n har slutat blinka. Efter avstängning \
                 startar Pi:n först när strömmen har varit bruten och slås på igen.",
            ),
            _ => None,
        }
    }

    fn command(self) -> &'static [&'static str] {
        match self {
            Action::RestartCarClient => &["sudo", "-n", "/usr/bin/systemctl", "restart", "car_client.service"],
            Action::RestartRtk => &["sudo", "-n", "/usr/bin/systemctl", "restart", "car_rtk.service"],
            Action::Reboot => &["sudo", "-n", "/usr/bin/systemctl", "reboot"],
            // Samma som wg-quick down + up, men via systemd som äger tunneln, så att
            // wg-quick@wg0 inte står som aktiv när tunneln i själva verket är nere.
            Action::RestartWireGuard => &["sudo", "-n", "/usr/bin/systemctl", "restart", "wg-quick@wg0.service"],
            Action::Shutdown => &["sudo", "-n", "/usr/bin/systemctl", "poweroff"],
            Action::PiMenu => &[],
        }
    }

    fn index(self) -> usize {
        self as usize
    }
}

fn main() -> eframe::Result<()> {
    let args: Vec<String> = std::env::args().collect();
    let demo = args.iter().any(|a| a == "--demo");
    let windowed = args.iter().any(|a| a == "--fonster" || a.starts_with("--fonster="));
    let window_size = args
        .iter()
        .find_map(|a| a.strip_prefix("--fonster="))
        .and_then(|s| s.split_once('x'))
        .and_then(|(w, h)| Some([w.parse::<f32>().ok()?, h.parse::<f32>().ok()?]))
        .unwrap_or([720.0, 1280.0]);
    let skip_countdown = args.iter().any(|a| a == "--utan-nedrakning");

    let status = Arc::new(Mutex::new(Status::default()));
    if demo {
        spawn_demo_collector(status.clone());
    } else {
        spawn_collector(status.clone());
        spawn_gga_reader(status.clone());
    }

    if args.iter().any(|a| a == "--text") {
        // Samma rutor som på skärmen, fast som text: för att kontrollera signalerna
        // över ssh på en Pi utan skärm.
        let app = App::new(status, demo);
        loop {
            thread::sleep(POLL);
            let line: Vec<String> = app
                .tiles()
                .iter()
                .map(|t| {
                    let mark = if t.color == BOX_OK { "GRÖN" } else if t.color == BOX_IDLE { "grå " } else if t.color == BOX_WARN { "GUL " } else { "RÖD " };
                    format!("[{mark}] {}: {}{}", t.title, t.big.clone().map(|b| format!("{b} ")).unwrap_or_default(), t.detail)
                })
                .collect();
            use std::io::Write;
            if writeln!(std::io::stdout(), "{}", line.join("  |  ")).is_err() {
                return Ok(()); // utdata stängd (t.ex. `| head`)
            }
        }
    }

    let viewport = if windowed {
        egui::ViewportBuilder::default().with_inner_size(window_size).with_title("Status")
    } else {
        egui::ViewportBuilder::default().with_fullscreen(true).with_title("Status")
    };
    eframe::run_native(
        "statusskarm",
        eframe::NativeOptions { viewport, ..Default::default() },
        Box::new(move |_cc| {
            let mut app = App::new(status, demo);
            app.countdown_skipped = skip_countdown;
            // Bara i demoläge: öppna en bekräftelseruta direkt, för att se hur den ser ut.
            if demo {
                app.dialog = match std::env::var("STATUSSKARM_DEMO_DIALOG").as_deref() {
                    Ok("car_client") => Some(Action::RestartCarClient),
                    Ok("rtk") => Some(Action::RestartRtk),
                    Ok("pi") => Some(Action::PiMenu),
                    Ok("wireguard") => Some(Action::RestartWireGuard),
                    _ => None,
                };
            }
            Box::new(app)
        }),
    )
}

// ---------------------------------------------------------------------------
// Insamling (bakgrundstrådar)

fn service_active(name: &str) -> bool {
    Command::new("systemctl")
        .args(["is-active", "--quiet", name])
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

/// Sekunder sedan styrkortet senast svarade enligt den instans som äger port 8301.
fn shared_board_reply_age() -> Option<u64> {
    let t: u64 = std::fs::read_to_string(BOARD_SHARED_FILE).ok()?.trim().parse().ok()?;
    let now = SystemTime::now().duration_since(UNIX_EPOCH).ok()?.as_secs();
    Some(now.saturating_sub(t))
}

/// Sekunder sedan senaste handskakning med någon peer på wg0.
fn wireguard_handshake_age() -> Result<Option<u64>, ()> {
    let out = Command::new("sudo")
        .args(["-n", "/usr/bin/wg", "show", "wg0", "latest-handshakes"])
        .output()
        .map_err(|_| ())?;
    if !out.status.success() {
        return Err(());
    }
    let now = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
    let newest = String::from_utf8_lossy(&out.stdout)
        .lines()
        .filter_map(|l| l.split_whitespace().nth(1)?.parse::<u64>().ok())
        .filter(|&t| t > 0)
        .max();
    Ok(newest.map(|t| now.saturating_sub(t)))
}

/// CPU-användning i procent sedan förra anropet, från /proc/stat.
struct CpuMeter {
    last: Option<(u64, u64)>,
    samples: Vec<f32>,
}

impl CpuMeter {
    fn read() -> Option<(u64, u64)> {
        let s = std::fs::read_to_string("/proc/stat").ok()?;
        let v: Vec<u64> = s.lines().next()?.split_whitespace().skip(1).filter_map(|x| x.parse().ok()).collect();
        let idle = v.get(3)? + v.get(4).unwrap_or(&0);
        Some((v.iter().sum(), idle))
    }

    /// Medel över de senaste ~6 s (tre mätningar med 2 s mellanrum).
    fn sample(&mut self) -> Option<f32> {
        let now = Self::read()?;
        let result = self.last.and_then(|(t0, i0)| {
            let dt = now.0.saturating_sub(t0);
            (dt > 0).then(|| 100.0 * (1.0 - now.1.saturating_sub(i0) as f32 / dt as f32))
        });
        self.last = Some(now);
        if let Some(p) = result {
            self.samples.push(p.clamp(0.0, 100.0));
            if self.samples.len() > 3 {
                self.samples.remove(0);
            }
        }
        (!self.samples.is_empty()).then(|| self.samples.iter().sum::<f32>() / self.samples.len() as f32)
    }
}

fn spawn_collector(status: Arc<Mutex<Status>>) {
    thread::spawn(move || {
        let mut cpu = CpuMeter { last: None, samples: Vec::new() };
        // Svaren från styrkortet kommer till den adress som senast skickade UDP
        // till Car_Client, port 8300 + 1.
        let socket = UdpSocket::bind(BOARD_REPLY_ADDR).ok();
        if let Some(s) = &socket {
            let _ = s.set_read_timeout(Some(Duration::from_millis(1200)));
        }
        let mut last_board_query: Option<Instant> = None;
        let mut last_board_reply: Option<Instant> = None;
        let mut vinkel: Option<(Vinkel, Instant)> = None;

        loop {
            let started = Instant::now();

            let usb_present = std::path::Path::new(USB_DEVICE).exists();
            if let Some(s) = &socket {
                if last_board_query.map_or(true, |t| t.elapsed() >= BOARD_POLL) {
                    last_board_query = Some(Instant::now());
                    let mut buf = [0u8; 2048];
                    // Car_Client skickar ALLT kortet säger hit (GPS-text, tillståndssvar
                    // till RControlStation, ~25 paket/s). Mellan frågorna fylls bufferten,
                    // och då kastades kortets svar: "svarar inte" fast kortet svarade.
                    // Töm bufferten först (ett sent svar på förra frågan räknas också).
                    if s.set_nonblocking(true).is_ok() {
                        while let Ok(n) = s.recv(&mut buf) {
                            if n >= 2 && buf[1] == CMD_AP_GET_ROUTE_PART {
                                last_board_reply = Some(Instant::now());
                            } else if let Some(v) = parse_vinkel(&buf[..n]) {
                                vinkel = Some((v, Instant::now()));
                            }
                        }
                        let _ = s.set_nonblocking(false);
                    }
                    let _ = s.send_to(&VINKEL_QUERY, CAR_CLIENT_ADDR);
                    let _ = s.send_to(&BOARD_QUERY, CAR_CLIENT_ADDR);
                    // Läs tills svaret kommer, hoppa över allt annat.
                    let deadline = Instant::now() + Duration::from_millis(1200);
                    while Instant::now() < deadline {
                        match s.recv(&mut buf) {
                            Ok(n) if n >= 2 && buf[1] == CMD_AP_GET_ROUTE_PART => {
                                last_board_reply = Some(Instant::now());
                                break;
                            }
                            Ok(n) if parse_vinkel(&buf[..n]).is_some() => {
                                vinkel = parse_vinkel(&buf[..n]).map(|v| (v, Instant::now()));
                                continue;
                            }
                            Ok(_) => continue,
                            Err(_) => break,
                        }
                    }
                    if last_board_reply.map_or(false, |t| t.elapsed() < BOARD_POLL) {
                        let now = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
                        let _ = std::fs::write(BOARD_SHARED_FILE, now.to_string());
                    }
                }
            }
            // Porten upptagen (den grafiska statusskärmen kör redan, t.ex. vid --text
            // över ssh): använd dess senaste svar i stället för att visa "svarar inte".
            let board_ok = usb_present
                && match &socket {
                    Some(_) => last_board_reply.map_or(false, |t| t.elapsed() < BOARD_POLL * 2),
                    None => shared_board_reply_age().map_or(false, |age| age < (BOARD_POLL * 2).as_secs()),
                };

            let car_client_ok = service_active("car_client.service");
            let rtk_service_ok = service_active("car_rtk.service");
            let handshake = wireguard_handshake_age();
            let cpu_percent = cpu.sample();

            {
                let mut st = status.lock().unwrap();
                st.usb_present = usb_present;
                st.board_ok = board_ok;
                st.car_client_ok = car_client_ok;
                st.rtk_service_ok = rtk_service_ok;
                st.handshake_error = handshake.is_err();
                st.handshake_age = handshake.ok().flatten();
                st.cpu_percent = cpu_percent;
                st.vinkel = vinkel;
                st.cpu_very_high_since = match cpu_percent {
                    Some(p) if p > 90.0 => st.cpu_very_high_since.or(Some(Instant::now())),
                    _ => None,
                };
                st.updated = Some(Instant::now());
            }

            thread::sleep(POLL.saturating_sub(started.elapsed()));
        }
    });
}

/// Läser GGA från rtkrcv och sparar fix-kvaliteten. Återansluter själv.
fn spawn_gga_reader(status: Arc<Mutex<Status>>) {
    let last_gga: Arc<Mutex<Option<(u8, Instant)>>> = Arc::new(Mutex::new(None));
    {
        let last_gga = last_gga.clone();
        thread::spawn(move || loop {
            if let Ok(stream) = TcpStream::connect(RTKRCV_NMEA_ADDR) {
                let _ = stream.set_read_timeout(Some(Duration::from_secs(10)));
                for line in BufReader::new(stream).lines() {
                    let Ok(line) = line else { break };
                    if let Some(q) = parse_gga_quality(&line) {
                        *last_gga.lock().unwrap() = Some((q, Instant::now()));
                    }
                }
            }
            thread::sleep(Duration::from_secs(3));
        });
    }
    thread::spawn(move || loop {
        let q = last_gga.lock().unwrap().filter(|(_, t)| t.elapsed() < GGA_MAX_AGE).map(|(q, _)| q);
        status.lock().unwrap().fix_quality = q;
        thread::sleep(Duration::from_millis(500));
    });
}

/// Läget ur rtk_bas.py:s statusfil, t.ex. {"lage": "vantar_gps", ...}.
fn rtk_bas_lage(text: Option<&str>) -> Option<String> {
    let rest = text?.split("\"lage\"").nth(1)?;
    let v = rest.split('"').nth(1)?;
    Some(v.to_string())
}

/// Fix-kvaliteten (sjätte fältet) ur en $GxGGA-mening, t.ex. $GNGGA eller $GPGGA.
fn parse_gga_quality(line: &str) -> Option<u8> {
    let line = line.trim();
    let f: Vec<&str> = line.split(',').collect();
    if f.len() < 7 || !f[0].starts_with('$') || !f[0].ends_with("GGA") {
        return None;
    }
    f[6].parse().ok()
}

/// Påhittad status för att prova utseendet utan Pi.
fn spawn_demo_collector(status: Arc<Mutex<Status>>) {
    thread::spawn(move || {
        let t0 = Instant::now();
        loop {
            let t = t0.elapsed().as_secs();
            let mut st = status.lock().unwrap();
            st.usb_present = true;
            st.board_ok = t % 60 < 50;
            st.car_client_ok = t % 40 >= 10;
            st.handshake_age = Some(t % 150);
            st.handshake_error = false;
            st.rtk_service_ok = t % 90 < 70;
            st.fix_quality = Some([0u8, 1, 5, 4][((t / 10) % 4) as usize]);
            let p = 50.0 + 45.0 * ((t as f32) / 15.0).sin();
            st.cpu_percent = Some(p);
            st.vinkel = Some((
                Vinkel { status: if t % 50 < 42 { 1 } else { 2 }, kalibrerad: true, mv: 3440, grader: 25.0 * ((t as f32) / 7.0).sin() },
                Instant::now(),
            ));
            st.cpu_very_high_since = if p > 90.0 { st.cpu_very_high_since.or(Some(Instant::now())) } else { None };
            st.updated = Some(Instant::now());
            drop(st);
            thread::sleep(POLL);
        }
    });
}

// ---------------------------------------------------------------------------
// Gränssnitt

struct App {
    status: Arc<Mutex<Status>>,
    demo: bool,
    started: Instant,
    countdown_skipped: bool,
    dialog: Option<Action>,
    /// När respektive omstart trycktes (index = Action::index()).
    restarted_at: [Option<Instant>; ACTIONS],
    hostname: String,
    /// Bara i demoläge: skärmbild till STATUSSKARM_SKARMBILD (PPM) efter 3 s, sedan stäng.
    screenshot_requested: bool,
}

/// En ruta som ska ritas.
struct Tile {
    title: &'static str,
    detail: String,
    /// Stort värde i mitten (CPU-procent), annars None.
    big: Option<String>,
    color: egui::Color32,
    pulsing: bool,
    action: Option<Action>,
    /// Styrvinkelrutan: −1 (fullt vänster) … +1 (fullt höger), ritas som en ratt-
    /// indikator i stället för CPU-mätaren.
    steer: Option<f32>,
}

impl App {
    fn new(status: Arc<Mutex<Status>>, demo: bool) -> Self {
        Self {
            status,
            demo,
            started: Instant::now(),
            countdown_skipped: false,
            dialog: None,
            restarted_at: [None; ACTIONS],
            hostname: std::fs::read_to_string("/etc/hostname")
                .map(|s| s.trim().to_uppercase())
                .unwrap_or_else(|_| "ROBOT".into()),
            screenshot_requested: false,
        }
    }

    fn run(&mut self, action: Action) {
        self.restarted_at[action.index()] = Some(Instant::now());
        let cmd = action.command();
        if cmd.is_empty() {
            return;
        }
        if self.demo {
            println!("(demo) skulle köra: {}", cmd.join(" "));
            return;
        }
        // Vänta inte på kommandot i gränssnittet; en omstart kan ta några sekunder.
        let cmd: Vec<String> = cmd.iter().map(|s| s.to_string()).collect();
        thread::spawn(move || {
            match Command::new(&cmd[0]).args(&cmd[1..]).status() {
                Ok(s) if s.success() => {}
                Ok(s) => eprintln!("{} misslyckades: {s}", cmd.join(" ")),
                Err(e) => eprintln!("{} kunde inte köras: {e}", cmd.join(" ")),
            }
        });
    }

    fn restarting(&self, action: Action) -> bool {
        self.restarted_at[action.index()].map_or(false, |t| t.elapsed() < RESTART_LOCK)
    }

    fn tiles(&self) -> Vec<Tile> {
        let st = self.status.lock().unwrap().clone();
        let ok = |b: bool| if b { BOX_OK } else { BOX_IDLE };
        let mut tiles = Vec::new();

        tiles.push(Tile {
            title: "Styrkort",
            detail: if !st.usb_present {
                "ingen USB".into()
            } else if st.board_ok {
                "svarar".into()
            } else {
                "svarar inte".into()
            },
            big: None,
            color: ok(st.board_ok),
            pulsing: false,
            action: None,
            steer: None,
        });

        // Nödstoppet (Car_Client --nodstopp-gpio skriver filen). Ingen ruta utan nödstopp.
        if let Some(t) = nodstopp_tile(std::fs::read_to_string(NODSTOPP_FIL).ok().as_deref(), unix_now()) {
            tiles.push(t);
        }

        if let Some(t) = vinkel_tile(st.vinkel.map(|(v, t)| (v, t.elapsed()))) {
            tiles.push(t);
        }

        tiles.push(self.service_tile("Car_Client", st.car_client_ok, Action::RestartCarClient));

        let internet_ok = st.handshake_age.map_or(false, |a| a < HANDSHAKE_MAX_AGE);
        let mut internet = self.service_tile("Internet", internet_ok, Action::RestartWireGuard);
        if internet_ok {
            internet.detail = format!("WireGuard {} s", st.handshake_age.unwrap_or(0));
        } else if !self.restarting(Action::RestartWireGuard) {
            let why = if st.handshake_error {
                "kan inte läsa WireGuard".to_string()
            } else {
                match st.handshake_age {
                    Some(a) => format!("tyst i {} min", a / 60),
                    None => "ingen kontakt".into(),
                }
            };
            internet.detail = format!("{why} – tryck för omstart");
        }
        tiles.push(internet);

        let mut rtk = self.service_tile("RTK-tjänst", st.rtk_service_ok, Action::RestartRtk);
        if st.rtk_service_ok && !self.restarting(Action::RestartRtk) {
            // rtk_bas.py (robot-control): Swepos kopplas upp först när roboten har
            // en egen GPS-position, så att basen hamnar där roboten står.
            match rtk_bas_lage(std::fs::read_to_string(RTK_BAS_FIL).ok().as_deref()).as_deref() {
                Some("vantar_gps") => {
                    rtk.detail = "väntar på GPS-position".into();
                    rtk.color = BOX_WARN;
                }
                Some("ansluten") => rtk.detail = "Swepos, bas här".into(),
                _ => {}
            }
        }
        tiles.push(rtk);

        let q = st.fix_quality;
        let fix = q == Some(4);
        let float_or_better = fix || q == Some(5);
        let fix_text = match q {
            None => "ingen position".to_string(),
            Some(0) => "ingen fix".into(),
            Some(1) => "vanlig GPS".into(),
            Some(2) => "DGPS".into(),
            Some(4) => "RTK Fix".into(),
            Some(5) => "RTK Float".into(),
            Some(n) => format!("kvalitet {n}"),
        };
        tiles.push(Tile { title: "RTK Float", detail: fix_text.clone(), big: None, color: ok(float_or_better), pulsing: false, action: None, steer: None });
        tiles.push(Tile { title: "RTK Fix", detail: fix_text, big: None, color: ok(fix), pulsing: false, action: None, steer: None });

        let reboot = self.restarting(Action::Reboot);
        let shutdown = self.restarted_at[Action::Shutdown.index()].is_some();
        let (cpu_color, cpu_big) = match st.cpu_percent {
            None => (BOX_IDLE, "–".to_string()),
            Some(p) => {
                let red = st.cpu_very_high_since.map_or(false, |t| t.elapsed() >= Duration::from_secs(10));
                let c = if red { BOX_BAD } else if p > 70.0 { BOX_WARN } else { BOX_OK };
                (c, format!("{p:.0} %"))
            }
        };
        tiles.push(Tile {
            title: "Pi",
            detail: if shutdown {
                "Stängs av...".into()
            } else if reboot {
                "Startar om...".into()
            } else {
                "tryck för omstart / stäng av".into()
            },
            big: Some(cpu_big),
            color: if shutdown { BOX_BAD } else if reboot { BOX_WARN } else { cpu_color },
            pulsing: reboot || shutdown,
            action: (!reboot && !shutdown).then_some(Action::PiMenu),
            steer: None,
        });
        tiles
    }

    /// Ruta för en tjänst som går att starta om när den inte är grön.
    fn service_tile(&self, title: &'static str, running: bool, action: Action) -> Tile {
        if self.restarting(action) {
            return Tile { title, detail: "Startar om...".into(), big: None, color: BOX_WARN, pulsing: true, action: None, steer: None };
        }
        Tile {
            title,
            detail: if running { "kör".into() } else { "kör inte – tryck för omstart".into() },
            big: None,
            color: if running { BOX_OK } else { BOX_IDLE },
            pulsing: false,
            action: (!running).then_some(action),
            steer: None,
        }
    }

    fn draw_countdown(&mut self, ui: &mut egui::Ui) {
        let rect = ui.max_rect();
        paint_background(ui.painter(), rect);
        let total = COUNTDOWN.as_secs_f32();
        let elapsed = self.started.elapsed().as_secs_f32().min(total);
        let left = COUNTDOWN.saturating_sub(self.started.elapsed()).as_secs();
        let t = self.started.elapsed().as_secs_f32();
        let center = rect.center() + egui::vec2(0.0, -rect.height() * 0.04);
        let r = rect.width().min(rect.height()) * 0.33;
        let painter = ui.painter();

        // Ring: spår, ifylld del och en "komet" som snurrar runt.
        painter.circle_stroke(center, r, egui::Stroke::new(r * 0.07, with_alpha(NEON_IDLE, 50)));
        let frac = elapsed / total;
        paint_arc(painter, center, r, -90.0, -90.0 + 360.0 * frac, egui::Stroke::new(r * 0.07, NEON_CYAN));
        let spin = t * 140.0;
        for k in 0..14 {
            let a = (spin - k as f32 * 4.0).to_radians();
            let p = center + egui::vec2(a.cos(), a.sin()) * (r * 1.12);
            painter.circle_filled(p, r * 0.022 * (1.0 - k as f32 / 14.0), with_alpha(NEON_CYAN, (220.0 * (1.0 - k as f32 / 14.0)) as u8));
        }

        let text = format!("{}:{:02}", left / 60, left % 60);
        painter.text(center, egui::Align2::CENTER_CENTER, text, egui::FontId::proportional(r * 0.62), TEXT);
        painter.text(
            center + egui::vec2(0.0, r * 0.5),
            egui::Align2::CENTER_CENTER,
            "STARTAR SYSTEM",
            egui::FontId::proportional(r * 0.11),
            with_alpha(NEON_CYAN, 220),
        );
        painter.text(
            egui::pos2(rect.center().x, rect.bottom() - rect.height() * 0.06),
            egui::Align2::CENTER_CENTER,
            "tryck för att hoppa över",
            egui::FontId::proportional(r * 0.1),
            with_alpha(TEXT_DIM, 140),
        );
        // Tryck var som helst för att hoppa över.
        if ui.interact(rect, egui::Id::new("countdown"), egui::Sense::click()).clicked() {
            self.countdown_skipped = true;
        }
    }

    /// Toppremsan: robotens namn, klocka och ett EKG-hjärtslag. Hjärtslaget rör sig så
    /// länge skärmen ritas, och blir rött ("INGEN DATA") om insamlingen slutat leverera.
    fn draw_header(&self, ui: &egui::Ui, bar: egui::Rect) {
        let painter = ui.painter();
        let t = self.started.elapsed().as_secs_f32();
        let fresh = self.status.lock().unwrap().updated.map_or(false, |u| u.elapsed() < DATA_STALE);
        let color = if fresh { NEON_OK } else { NEON_BAD };
        let h = bar.height();

        // Tunn linje under remsan.
        painter.line_segment(
            [bar.left_bottom(), bar.right_bottom()],
            egui::Stroke::new(1.0_f32, with_alpha(NEON_CYAN, 60)),
        );
        // Vänster: statuslampa + namn.
        let dot = egui::pos2(bar.left() + h * 0.4, bar.center().y);
        painter.circle_filled(dot, h * 0.14, NEON_CYAN);
        painter.circle_stroke(dot, h * 0.14 + (t * 2.0).sin().abs() * h * 0.12, egui::Stroke::new(1.0_f32, with_alpha(NEON_CYAN, 90)));
        painter.text(
            egui::pos2(dot.x + h * 0.35, bar.center().y),
            egui::Align2::LEFT_CENTER,
            format!("{}  ·  STATUS", self.hostname),
            egui::FontId::proportional(h * 0.42),
            with_alpha(TEXT, 230),
        );

        // Höger: klocka, sedan EKG.
        let clock = chrono::Local::now().format("%H:%M:%S").to_string();
        let clock_rect = painter.text(
            egui::pos2(bar.right() - h * 0.3, bar.center().y),
            egui::Align2::RIGHT_CENTER,
            clock,
            egui::FontId::monospace(h * 0.46),
            TEXT,
        );
        let label = if fresh { "LIVE" } else { "INGEN DATA" };
        let label_rect = painter.text(
            egui::pos2(clock_rect.left() - h * 0.45, bar.center().y),
            egui::Align2::RIGHT_CENTER,
            label,
            egui::FontId::proportional(h * 0.34),
            color,
        );
        let ecg_w = h * 3.2;
        let ecg = egui::Rect::from_min_max(
            egui::pos2(label_rect.left() - h * 0.3 - ecg_w, bar.top() + h * 0.18),
            egui::pos2(label_rect.left() - h * 0.3, bar.bottom() - h * 0.18),
        );
        paint_ecg(painter, ecg, t, color, fresh);
    }

    fn draw_tiles(&mut self, ui: &mut egui::Ui) -> bool {
        let tiles = self.tiles();
        let full = ui.max_rect();
        paint_background(ui.painter(), full);
        let header_h = (full.height() * 0.085).clamp(30.0, 60.0);
        let bar = egui::Rect::from_min_size(full.min, egui::vec2(full.width(), header_h));
        self.draw_header(ui, bar);

        let rect = egui::Rect::from_min_max(egui::pos2(full.left(), full.top() + header_h), full.max).shrink(12.0);
        let cols = if rect.width() > rect.height() { 4 } else { 2 };
        let rows = (tiles.len() + cols - 1) / cols;
        let gap = 12.0;
        let w = (rect.width() - gap * (cols as f32 - 1.0)) / cols as f32;
        let h = (rect.height() - gap * (rows as f32 - 1.0)) / rows as f32;
        let t = self.started.elapsed().as_secs_f32();
        let mut any_pulsing = false;
        let cpu = self.status.lock().unwrap().cpu_percent;

        for (i, tile) in tiles.iter().enumerate() {
            let (c, r) = (i % cols, i / cols);
            let min = rect.min + egui::vec2(c as f32 * (w + gap), r as f32 * (h + gap));
            // Sista rutan (Pi) fyller resten av sista raden i stället för att lämna ett hål.
            let span = if i + 1 == tiles.len() { cols - c } else { 1 };
            let tw = w * span as f32 + gap * (span as f32 - 1.0);
            let b0 = egui::Rect::from_min_size(min, egui::vec2(tw, h));
            // Pekskärm: ingen hover, men rutan lyser upp och krymper lite medan man trycker.
            let resp = tile.action.map(|_| ui.interact(b0, egui::Id::new(("tile", i)), egui::Sense::click()));
            let pressed = resp.as_ref().map_or(false, |r| r.is_pointer_button_down_on());
            let b = if pressed { b0.shrink(3.0) } else { b0 };
            let accent = neon(tile.color);
            let lit = tile.color != BOX_IDLE;
            any_pulsing |= tile.pulsing;
            // Pulserande rutor (omstart pågår) andas snabbare.
            let breathe = if tile.pulsing { 0.55 + 0.45 * (t * 5.0).sin().abs() } else { 1.0 };
            let painter = ui.painter();
            let rounding = 14.0;

            // Sken runt rutan i statusfärgen.
            if lit {
                for k in 1..=4 {
                    let a = (34.0 / k as f32 * breathe) as u8;
                    painter.rect_stroke(b.expand(k as f32 * 2.0), rounding + k as f32 * 2.0, egui::Stroke::new(2.0_f32, with_alpha(accent, a)));
                }
            }
            // Glas: mörk yta, ljusare överkant (ljusare medan man trycker).
            painter.rect_filled(b, rounding, if pressed { lighten(PANEL, 0.12) } else { PANEL });
            let sheen = egui::Rect::from_min_max(b.min, egui::pos2(b.right(), b.top() + h * 0.45));
            painter.rect_filled(
                sheen,
                egui::Rounding { nw: rounding, ne: rounding, sw: 0.0, se: 0.0 },
                egui::Color32::from_white_alpha(6),
            );
            // Ett ljus med svans som vandrar runt kanten. Rutorna går förskjutet.
            if lit || pressed {
                let lap = if tile.pulsing { 1.6 } else { 4.2 };
                let head = (t / lap + i as f32 * 0.137).fract();
                paint_border_comet(painter, b, rounding, head, accent);
            }
            // Kant och markering i vänsterkanten.
            painter.rect_stroke(b, rounding, egui::Stroke::new(1.5_f32, with_alpha(accent, if lit { (200.0 * breathe) as u8 } else { 90 })));
            let mark = egui::Rect::from_center_size(egui::pos2(b.left() + 7.0, b.center().y), egui::vec2(4.0, h * 0.5));
            painter.rect_filled(mark, 2.0, with_alpha(accent, if lit { 255 } else { 120 }));

            // Statuslampa uppe till höger som "andas" när rutan är grön/gul/röd.
            let led = egui::pos2(b.right() - 18.0, b.top() + 18.0);
            painter.circle_filled(led, 5.0, with_alpha(accent, if lit { 255 } else { 140 }));
            if lit {
                let p = (t / 1.8 + i as f32 * 0.13).fract();
                painter.circle_stroke(led, 5.0 + p * 10.0, egui::Stroke::new(1.5_f32, with_alpha(accent, ((1.0 - p) * 160.0) as u8)));
            }

            // Omstart pågår: ett ljus som vandrar längs nederkanten.
            if tile.pulsing {
                let x = b.left() + 14.0 + ((t * 0.8).fract()) * (tw - 28.0 - tw * 0.25);
                let seg = egui::Rect::from_min_size(egui::pos2(x, b.bottom() - 10.0), egui::vec2(tw * 0.25, 3.0));
                painter.rect_filled(seg, 2.0, accent);
            }

            // Text: stor även på en liten skärm (4,3" 800x480); detaljraden radbryts.
            let title_size = (h * 0.17).min(w * 0.15);
            let detail_size = title_size * 0.66;
            let wrap = tw * 0.86;
            let centered = |text: &str, size: f32, color: egui::Color32, y: f32| {
                let mut job = egui::text::LayoutJob::simple(text.to_string(), egui::FontId::proportional(size), color, wrap);
                job.halign = egui::Align::Center; // varje radbruten rad centreras
                let galley = painter.layout_job(job);
                let pos = egui::pos2(b.center().x, y - galley.size().y / 2.0);
                painter.galley(pos, galley, color);
            };
            let title = tile.title.to_uppercase();
            let detail_color = if lit { lighten(accent, 0.55) } else { with_alpha(TEXT_DIM, 170) };
            if let Some(big) = &tile.big {
                centered(&title, title_size, TEXT, b.top() + h * 0.17);
                centered(big, title_size * 1.7, TEXT, b.top() + h * 0.43);
                let mw = (tw * 0.6).min(320.0);
                let meter = egui::Rect::from_center_size(egui::pos2(b.center().x, b.top() + h * 0.66), egui::vec2(mw, h * 0.07));
                if tile.title == "Styrvinkel" {
                    // Rattindikator: spår med mittstreck, prick där hjulen står.
                    let y = meter.center().y;
                    painter.line_segment([egui::pos2(meter.left(), y), egui::pos2(meter.right(), y)], egui::Stroke::new(4.0_f32, with_alpha(NEON_IDLE, 90)));
                    painter.line_segment([egui::pos2(meter.center().x, meter.top() - 4.0), egui::pos2(meter.center().x, meter.bottom() + 4.0)], egui::Stroke::new(2.0_f32, with_alpha(TEXT_DIM, 160)));
                    if let Some(f) = tile.steer {
                        let x = meter.center().x + f * mw / 2.0;
                        painter.line_segment([egui::pos2(meter.center().x, y), egui::pos2(x, y)], egui::Stroke::new(4.0_f32, accent));
                        painter.circle_filled(egui::pos2(x, y), meter.height() * 0.9, accent);
                    }
                    centered(&tile.detail, detail_size, detail_color, b.bottom() - h * 0.14);
                    continue;
                }
                // CPU-mätare: tio segment.
                let segs = 10;
                let sg = 4.0;
                let sw = (mw - sg * (segs as f32 - 1.0)) / segs as f32;
                let filled = cpu.map_or(0, |p| ((p / 100.0) * segs as f32).ceil() as usize);
                for s in 0..segs {
                    let r = egui::Rect::from_min_size(
                        egui::pos2(meter.left() + s as f32 * (sw + sg), meter.top()),
                        egui::vec2(sw, meter.height()),
                    );
                    let on = s < filled;
                    painter.rect_filled(r, 2.0, if on { accent } else { with_alpha(NEON_IDLE, 60) });
                }
                centered(&tile.detail, detail_size, detail_color, b.bottom() - h * 0.14);
            } else {
                centered(&title, title_size, TEXT, b.center().y - h * 0.12);
                centered(&tile.detail, detail_size, detail_color, b.center().y + h * 0.17);
            }

            if let (Some(action), Some(resp)) = (tile.action, &resp) {
                if resp.clicked() && self.dialog.is_none() {
                    self.dialog = Some(action);
                }
            }
        }
        any_pulsing
    }

    /// Ja/Nej-dialogen, samma för alla omstarter. Tryck utanför = Nej.
    fn draw_dialog(&mut self, ctx: &egui::Context) {
        let Some(action) = self.dialog else { return };
        let screen = ctx.screen_rect();

        let mut close = false;
        let mut confirmed: Option<Action> = None;
        egui::Area::new(egui::Id::new("dialog_bg"))
            .order(DIALOG_BG_ORDER)
            .fixed_pos(screen.min)
            .show(ctx, |ui| {
                ui.painter().rect_filled(screen, 0.0, egui::Color32::from_black_alpha(190));
                if ui.interact(screen, egui::Id::new("dialog_bg_click"), egui::Sense::click()).clicked() {
                    close = true;
                }
            });

        let w = (screen.width() * 0.86).min(700.0);
        let big = (w * 0.06).max(22.0);
        egui::Area::new(egui::Id::new("dialog"))
            .order(DIALOG_ORDER)
            .anchor(egui::Align2::CENTER_CENTER, egui::vec2(0.0, 0.0))
            .show(ctx, |ui| {
                egui::Frame::none()
                    .fill(egui::Color32::from_rgb(38, 42, 50))
                    .rounding(18.0)
                    .inner_margin(egui::Margin::same(28.0))
                    .show(ui, |ui| {
                        ui.set_width(w);
                        ui.vertical_centered(|ui| {
                            ui.label(egui::RichText::new(action.question()).size(big).color(TEXT).strong());
                            if let Some(warn) = action.warning() {
                                ui.add_space(14.0);
                                ui.label(egui::RichText::new(warn).size(big * 0.7).color(egui::Color32::from_rgb(255, 190, 90)));
                            }
                            ui.add_space(26.0);
                            let btn = |txt: &str, fill, bw: f32| {
                                egui::Button::new(egui::RichText::new(txt).size(big).color(TEXT).strong())
                                    .fill(fill)
                                    .rounding(12.0)
                                    .min_size(egui::vec2(bw, big * 2.4))
                            };
                            if action == Action::PiMenu {
                                // Tre knappar: valet i sig är bekräftelsen.
                                let bw = (w - 40.0) / 3.0;
                                ui.horizontal(|ui| {
                                    if ui.add(btn("Starta om", BOX_WARN, bw)).clicked() {
                                        confirmed = Some(Action::Reboot);
                                    }
                                    if ui.add(btn("Stäng av", BOX_BAD, bw)).clicked() {
                                        confirmed = Some(Action::Shutdown);
                                    }
                                    if ui.add(btn("Avbryt", BOX_IDLE, bw)).clicked() {
                                        close = true;
                                    }
                                });
                            } else {
                                let bw = (w - 20.0) / 2.0;
                                ui.horizontal(|ui| {
                                    if ui.add(btn("Ja", BOX_BAD, bw)).clicked() {
                                        confirmed = Some(action);
                                    }
                                    if ui.add(btn("Nej", BOX_IDLE, bw)).clicked() {
                                        close = true;
                                    }
                                });
                            }
                        });
                    });
            });

        if let Some(a) = confirmed {
            self.dialog = None;
            self.run(a);
        } else if close {
            self.dialog = None;
        }
    }
}

impl App {
    fn demo_screenshot(&mut self, ctx: &egui::Context) {
        let Some(path) = std::env::var_os("STATUSSKARM_SKARMBILD") else { return };
        let image = ctx.input(|i| {
            i.raw.events.iter().find_map(|e| match e {
                egui::Event::Screenshot { image, .. } => Some(image.clone()),
                _ => None,
            })
        });
        if let Some(img) = image {
            let mut ppm = format!("P6\n{} {}\n255\n", img.size[0], img.size[1]).into_bytes();
            for p in &img.pixels {
                ppm.extend([p.r(), p.g(), p.b()]);
            }
            let _ = std::fs::write(path, ppm);
            ctx.send_viewport_cmd(egui::ViewportCommand::Close);
        } else if !self.screenshot_requested && self.started.elapsed() > Duration::from_secs(3) {
            self.screenshot_requested = true;
            ctx.send_viewport_cmd(egui::ViewportCommand::Screenshot);
        }
    }
}

impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        // Pekskärm: ingen muspekare mitt i bilden.
        ctx.set_cursor_icon(egui::CursorIcon::None);
        let in_countdown = !self.countdown_skipped && self.started.elapsed() < COUNTDOWN;
        egui::CentralPanel::default()
            .frame(egui::Frame::none().fill(BG_DEEP))
            .show(ctx, |ui| {
                if in_countdown {
                    self.draw_countdown(ui);
                } else {
                    self.draw_tiles(ui);
                }
            });
        self.draw_dialog(ctx);
        if self.demo {
            self.demo_screenshot(ctx);
        }
        // Ca 15 bilder/s: räcker för mjuka animationer (hjärtslag, lampor) och är lätt
        // för Pi:n. Står bilden still har skärmen hängt sig.
        ctx.request_repaint_after(Duration::from_millis(66));
    }
}

// ---------------------------------------------------------------------------
// Ritverktyg

fn with_alpha(c: egui::Color32, a: u8) -> egui::Color32 {
    egui::Color32::from_rgba_unmultiplied(c.r(), c.g(), c.b(), a)
}

/// Blanda mot vitt (k = 0 oförändrad, 1 vitt).
fn lighten(c: egui::Color32, k: f32) -> egui::Color32 {
    let m = |x: u8| (x as f32 + (255.0 - x as f32) * k) as u8;
    egui::Color32::from_rgb(m(c.r()), m(c.g()), m(c.b()))
}

/// Statusfärg (logik) -> lysande färg (utseende).
fn neon(c: egui::Color32) -> egui::Color32 {
    if c == BOX_OK {
        NEON_OK
    } else if c == BOX_WARN {
        NEON_WARN
    } else if c == BOX_BAD {
        NEON_BAD
    } else {
        NEON_IDLE
    }
}

/// Mörk bakgrund med ett svagt rutnät.
fn paint_background(painter: &egui::Painter, rect: egui::Rect) {
    painter.rect_filled(rect, 0.0, BG_DEEP);
    let step = 32.0;
    let mut x = rect.left() + step;
    while x < rect.right() {
        painter.line_segment([egui::pos2(x, rect.top()), egui::pos2(x, rect.bottom())], egui::Stroke::new(1.0_f32, GRID));
        x += step;
    }
    let mut y = rect.top() + step;
    while y < rect.bottom() {
        painter.line_segment([egui::pos2(rect.left(), y), egui::pos2(rect.right(), y)], egui::Stroke::new(1.0_f32, GRID));
        y += step;
    }
}

/// Båge från a0 till a1 grader (0 = höger, medurs).
fn paint_arc(painter: &egui::Painter, c: egui::Pos2, r: f32, a0: f32, a1: f32, stroke: egui::Stroke) {
    if a1 <= a0 {
        return;
    }
    let n = (((a1 - a0) / 4.0).ceil() as usize).max(2);
    let pts: Vec<egui::Pos2> = (0..=n)
        .map(|i| {
            let a = (a0 + (a1 - a0) * i as f32 / n as f32).to_radians();
            c + egui::vec2(a.cos(), a.sin()) * r
        })
        .collect();
    painter.add(egui::Shape::line(pts, stroke));
}

/// Punkt på kanten av en rundad rektangel, s = 0..1 ett varv medurs från övre vänstra
/// rakan.
fn rrect_point(b: egui::Rect, r: f32, s: f32) -> egui::Pos2 {
    let r = r.min(b.width() / 2.0).min(b.height() / 2.0);
    let (w, h) = (b.width() - 2.0 * r, b.height() - 2.0 * r);
    let arc = std::f32::consts::FRAC_PI_2 * r;
    let perim = 2.0 * (w + h) + 4.0 * arc;
    let mut d = s.rem_euclid(1.0) * perim;
    let corner = |c: egui::Pos2, a0: f32, d: f32| {
        let a = (a0 + 90.0 * d / arc).to_radians();
        c + egui::vec2(a.cos(), a.sin()) * r
    };
    // överkant
    if d < w { return egui::pos2(b.left() + r + d, b.top()); }
    d -= w;
    if d < arc { return corner(egui::pos2(b.right() - r, b.top() + r), -90.0, d); }
    d -= arc;
    // högerkant
    if d < h { return egui::pos2(b.right(), b.top() + r + d); }
    d -= h;
    if d < arc { return corner(egui::pos2(b.right() - r, b.bottom() - r), 0.0, d); }
    d -= arc;
    // nederkant
    if d < w { return egui::pos2(b.right() - r - d, b.bottom()); }
    d -= w;
    if d < arc { return corner(egui::pos2(b.left() + r, b.bottom() - r), 90.0, d); }
    d -= arc;
    // vänsterkant
    if d < h { return egui::pos2(b.left(), b.bottom() - r - d); }
    d -= h;
    corner(egui::pos2(b.left() + r, b.top() + r), 180.0, d.min(arc))
}

/// Ljus med svans som går runt kanten: huvudet vid `head` (0..1), svansen tonar ut.
fn paint_border_comet(painter: &egui::Painter, b: egui::Rect, r: f32, head: f32, color: egui::Color32) {
    let tail = 0.22; // andel av varvet
    let n = 28;
    let mut prev: Option<egui::Pos2> = None;
    for k in 0..=n {
        let u = k as f32 / n as f32; // 0 = svansens slut, 1 = huvudet
        let p = rrect_point(b, r, head - tail * (1.0 - u));
        if let Some(q) = prev {
            // Ett brett, svagt sken under en smal, stark linje.
            painter.line_segment([q, p], egui::Stroke::new(6.0_f32 * u, with_alpha(color, (40.0 * u * u) as u8)));
            painter.line_segment([q, p], egui::Stroke::new(1.0_f32 + 2.0 * u, with_alpha(lighten(color, 0.25 * u), (255.0 * u * u) as u8)));
        }
        prev = Some(p);
    }
    if let Some(p) = prev {
        painter.circle_filled(p, 7.0, with_alpha(color, 45));
        painter.circle_filled(p, 3.0, lighten(color, 0.5));
    }
}

/// EKG-kurva som rullar från höger till vänster; nyaste delen är starkast.
fn paint_ecg(painter: &egui::Painter, rect: egui::Rect, t: f32, color: egui::Color32, alive: bool) {
    // En hjärtslagsperiod: P-våg, QRS-topp, T-våg (0..1 -> -1..1).
    fn beat(x: f32) -> f32 {
        let bump = |c: f32, w: f32, a: f32| a * (-((x - c) / w).powi(2)).exp();
        bump(0.18, 0.04, 0.15) - bump(0.30, 0.012, 0.25) + bump(0.33, 0.012, 1.0) - bump(0.36, 0.014, 0.35)
            + bump(0.58, 0.06, 0.28)
    }
    let n = 60;
    let speed = if alive { 1.1 } else { 0.0 }; // ingen data: platt linje
    let mut prev: Option<egui::Pos2> = None;
    for i in 0..=n {
        let u = i as f32 / n as f32;
        let phase = (t * speed - (1.0 - u) * 1.5).rem_euclid(1.0);
        let y = if alive { beat(phase) } else { 0.0 };
        let p = egui::pos2(rect.left() + u * rect.width(), rect.center().y - y * rect.height() * 0.48);
        if let Some(q) = prev {
            painter.line_segment([q, p], egui::Stroke::new(1.8_f32, with_alpha(color, (40.0 + 215.0 * u) as u8)));
        }
        prev = Some(p);
    }
    if let Some(p) = prev {
        painter.circle_filled(p, 3.0, color);
        painter.circle_filled(p, 6.0, with_alpha(color, 50));
    }
}

/// Läget som Car_Client skriver: "<ok|intryckt|sparrad|fel> <unix-tid>", varje sekund.
const NODSTOPP_FIL: &str = "/tmp/nodstopp_status";

/// Styrvinkelrutan: syns bara på maskiner med vinkelgivare (firmware 30.12+).
/// Grön = givaren svarar och är kalibrerad, gul = okalibrerad, röd = ingen
/// signal (kabel/magnet), grå = inget svar från kortet på ett tag.
fn vinkel_tile(v: Option<(Vinkel, Duration)>) -> Option<Tile> {
    let (v, age) = v?;
    if v.status == 0 {
        return None;
    }
    let stale = age > BOARD_POLL * 3;
    let (color, detail) = if stale {
        (BOX_IDLE, "inget svar från styrkortet".to_string())
    } else if v.status == 2 {
        (BOX_BAD, format!("ingen signal ({} mV) – kabel/magnet?", v.mv))
    } else if !v.kalibrerad {
        (BOX_WARN, format!("{} mV – ej kalibrerad", v.mv))
    } else {
        (BOX_OK, format!("givaren OK, {} mV", v.mv))
    };
    let big = if stale || v.status == 2 {
        "–".to_string()
    } else if v.grader.abs() < 1.5 {
        "rakt".to_string()
    } else {
        format!("{} {:.0}°", if v.grader > 0.0 { "H" } else { "V" }, v.grader.abs())
    };
    let steer = (!stale && v.status == 1).then(|| (v.grader / 30.0).clamp(-1.0, 1.0));
    Some(Tile { title: "Styrvinkel", detail, big: Some(big), color, pulsing: false, action: None, steer })
}

fn unix_now() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

fn nodstopp_tile(text: Option<&str>, now: u64) -> Option<Tile> {
    let text = text?;
    let mut delar = text.split_whitespace();
    let lage = delar.next().unwrap_or("");
    let skriven: Option<u64> = delar.next().and_then(|t| t.parse().ok());
    let farsk = skriven.map_or(false, |w| now.saturating_sub(w) <= 3);
    let (detail, color, pulsing) = match (farsk, lage) {
        (true, "ok") => ("OK", BOX_OK, false),
        (true, "intryckt") => ("INTRYCKT", BOX_BAD, true),
        (true, "sparrad") => ("utdragen – väntar på stopp från föraren", BOX_WARN, false),
        (true, _) => ("kan inte läsas – kontrollera kabeln", BOX_BAD, true),
        (false, _) => ("inget svar från Car_Client", BOX_BAD, false),
    };
    Some(Tile { title: "Nödstopp", detail: detail.into(), big: None, color, pulsing, action: None, steer: None })
}

#[cfg(test)]
mod tests {
    #[test]
    fn rtk_bas_lage_tolkas() {
        use super::rtk_bas_lage;
        assert_eq!(rtk_bas_lage(Some(r#"{"lage": "vantar_gps", "tid": 1}"#)).as_deref(), Some("vantar_gps"));
        assert_eq!(rtk_bas_lage(Some(r#"{"lage": "ansluten", "tid": 1, "lat": 57.7}"#)).as_deref(), Some("ansluten"));
        assert_eq!(rtk_bas_lage(None), None);
    }

    #[test]
    fn vinkelsvaret_tolkas() {
        // [id 4][150][OK][kalibrerad][3441 mV][-12,34°]
        let mut p = vec![4u8, 150, 1, 1];
        p.extend_from_slice(&3441u16.to_be_bytes());
        p.extend_from_slice(&(-1234i32).to_be_bytes());
        let v = parse_vinkel(&p).unwrap();
        assert_eq!((v.status, v.kalibrerad, v.mv), (1, true, 3441));
        assert!((v.grader + 12.34).abs() < 1e-4);
        assert!(parse_vinkel(&p[..8]).is_none());
        assert!(parse_vinkel(&[4, 58, 1, 1, 0, 0, 0, 0, 0, 0]).is_none());
        // Nödstoppets statuspaket [id][143][läge] får aldrig tolkas som vinkel.
        assert!(parse_vinkel(&[4, 143, 1, 1, 0, 0, 0, 0, 0, 0]).is_none());
    }

    #[test]
    fn vinkelrutan() {
        let ok = Vinkel { status: 1, kalibrerad: true, mv: 3441, grader: -12.3 };
        let t = vinkel_tile(Some((ok, Duration::from_secs(1)))).unwrap();
        assert_eq!((t.color, t.big.as_deref()), (BOX_OK, Some("V 12°")));
        let fel = Vinkel { status: 2, ..ok };
        assert_eq!(vinkel_tile(Some((fel, Duration::from_secs(1)))).unwrap().color, BOX_BAD);
        let okal = Vinkel { kalibrerad: false, ..ok };
        assert_eq!(vinkel_tile(Some((okal, Duration::from_secs(1)))).unwrap().color, BOX_WARN);
        assert_eq!(vinkel_tile(Some((ok, Duration::from_secs(60)))).unwrap().color, BOX_IDLE);
        assert!(vinkel_tile(Some((Vinkel { status: 0, ..ok }, Duration::from_secs(1)))).is_none());
        assert!(vinkel_tile(None).is_none());
    }

    #[test]
    fn nodstopp_rutan() {
        use super::*;
        assert!(nodstopp_tile(None, 100).is_none());
        assert_eq!(nodstopp_tile(Some("ok 100"), 101).unwrap().color, BOX_OK);
        assert_eq!(nodstopp_tile(Some("intryckt 100"), 100).unwrap().color, BOX_BAD);
        assert_eq!(nodstopp_tile(Some("sparrad 100"), 100).unwrap().color, BOX_WARN);
        assert_eq!(nodstopp_tile(Some("ok 100"), 110).unwrap().color, BOX_BAD);
    }

    use super::*;

    #[test]
    fn gga_kvalitet_tolkas() {
        assert_eq!(parse_gga_quality("$GNGGA,081530.00,6003.79,N,01804.75,E,4,12,0.9,41.0,M,23.1,M,1.0,0000*5A"), Some(4));
        assert_eq!(parse_gga_quality("$GPGGA,081530.00,6003.79,N,01804.75,E,5,08,1.1,41.0,M,,,,*00"), Some(5));
        assert_eq!(parse_gga_quality("$GNGGA,,,,,,0,00,,,,,,,*66"), Some(0));
        assert_eq!(parse_gga_quality("$GNRMC,081530.00,A,6003.79,N,01804.75,E,0.0,,280926,,,A*00"), None);
        assert_eq!(parse_gga_quality("skräp"), None);
    }

    #[test]
    fn styrkortsfragan_ar_ofarlig() {
        // 58 = CMD_AP_GET_ROUTE_PART (läser ruttlängd, ingen timeout_reset). 59 vore
        // CMD_AP_SET_ACTIVE (startar autopiloten) — får aldrig skickas härifrån.
        assert_eq!(BOARD_QUERY, [255, 58, 0, 0, 0, 0, 0]);
    }

    #[test]
    fn fix_tander_bade_float_och_fix() {
        let st = Status { fix_quality: Some(4), ..Default::default() };
        let app = App::new(Arc::new(Mutex::new(st)), true);
        let t = app.tiles();
        let float = t.iter().find(|t| t.title == "RTK Float").unwrap();
        let fix = t.iter().find(|t| t.title == "RTK Fix").unwrap();
        assert_eq!(float.color, BOX_OK);
        assert_eq!(fix.color, BOX_OK);
        let st = Status { fix_quality: Some(5), ..Default::default() };
        let t = App::new(Arc::new(Mutex::new(st)), true).tiles();
        assert_eq!(t.iter().find(|t| t.title == "RTK Float").unwrap().color, BOX_OK);
        assert_eq!(t.iter().find(|t| t.title == "RTK Fix").unwrap().color, BOX_IDLE);
    }

    #[test]
    fn tjanst_gar_bara_att_trycka_pa_nar_den_inte_ar_gron() {
        let app = App::new(Arc::new(Mutex::new(Status::default())), true);
        assert_eq!(app.service_tile("Car_Client", false, Action::RestartCarClient).action, Some(Action::RestartCarClient));
        assert_eq!(app.service_tile("Car_Client", true, Action::RestartCarClient).action, None);
        // Internet (WireGuard) likadant, Pi-rutan går alltid att trycka på.
        let t = app.tiles();
        assert_eq!(t.iter().find(|t| t.title == "Internet").unwrap().action, Some(Action::RestartWireGuard));
        let st = Status { handshake_age: Some(20), ..Default::default() };
        let t = App::new(Arc::new(Mutex::new(st)), true).tiles();
        assert_eq!(t.iter().find(|t| t.title == "Internet").unwrap().action, None);
        let t = app.tiles();
        assert_eq!(t.iter().find(|t| t.title == "Pi").unwrap().action, Some(Action::PiMenu));
    }

    #[test]
    fn dialogens_knappar_tar_emot_tryck() {
        assert!(DIALOG_ORDER.allow_interaction());
        assert!(DIALOG_BG_ORDER.allow_interaction());
        // Knapparna ska ligga över bakgrunden, och bakgrunden över rutorna.
        assert!(DIALOG_ORDER > DIALOG_BG_ORDER && DIALOG_BG_ORDER > egui::Order::Background);
    }

    #[test]
    fn avstangning_last_pi_rutan() {
        let mut app = App::new(Arc::new(Mutex::new(Status::default())), true);
        assert_eq!(Action::Shutdown.command(), &["sudo", "-n", "/usr/bin/systemctl", "poweroff"]);
        assert!(Action::PiMenu.command().is_empty());
        app.run(Action::Shutdown);
        let t = app.tiles();
        let pi = t.iter().find(|t| t.title == "Pi").unwrap();
        assert_eq!(pi.detail, "Stängs av...");
        assert_eq!(pi.action, None);
    }

    #[test]
    fn ljuset_följer_kanten_utan_hopp() {
        let b = egui::Rect::from_min_size(egui::pos2(10.0, 20.0), egui::vec2(180.0, 200.0));
        let n = 2000;
        let pts: Vec<egui::Pos2> = (0..=n).map(|k| rrect_point(b, 14.0, k as f32 / n as f32)).collect();
        // Varje punkt ligger på kanten (inom rutan, nära någon sida eller ett hörn).
        for p in &pts {
            assert!(b.expand(0.01).contains(*p), "utanför rutan: {p:?}");
        }
        // Inga hopp: steget är litet överallt, och varvet slutar där det började.
        let max_step = pts.windows(2).map(|w| w[0].distance(w[1])).fold(0.0, f32::max);
        assert!(max_step < 1.0, "hopp på {max_step}");
        assert!(pts[0].distance(pts[n]) < 0.01);
    }

    #[test]
    fn omstart_spärras_i_tio_sekunder() {
        let mut app = App::new(Arc::new(Mutex::new(Status::default())), true);
        app.run(Action::RestartCarClient);
        let tile = app.service_tile("Car_Client", false, Action::RestartCarClient);
        assert_eq!(tile.detail, "Startar om...");
        assert_eq!(tile.action, None);
        assert!(tile.pulsing);
    }
}
