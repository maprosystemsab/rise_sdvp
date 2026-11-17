; generator_maxim.lisp — LispBM för VESC Maxim+ på elverket (RobAnt, firmware 30.27)
;
; Styr Hondans stoppkabel via pin 6 "SW HV" (low-side-switch) efter styrkortets
; CAN-meddelande (standard-ID 0x05E, var 100:e ms): [0x47 'G'][1 = får gå / 0 = stopp][generator-ID]
;
; FELSÄKER KOPPLING (viktigt):
;   SW HV driver spolen på ett relä (48 V-spole, mellan VBAT+ och pin 6, med
;   frihjulsdiod). Reläets NC-kontakt (normalt sluten) går mellan Hondans
;   stoppkabel och motorblocket (jord).
;     spolen strömsatt  -> NC öppen  -> stoppkabeln fri  -> Hondan får gå
;     spolen strömlös   -> NC sluten -> stoppkabeln jordad -> Hondan stannar
;   Alltså stannar Hondan också om Maxim+ blir strömlös, skriptet inte körs,
;   CAN-kabeln går av eller styrkortet dör. (Kopplar man stoppkabeln direkt till
;   pin 6 blir det tvärtom: död Maxim+ = Hondan går vidare. Gör inte så.)
;
; Läggs in i VESC Tool: VESC Dev Tools -> Lisp -> öppna filen -> Upload (kör vid start).
; KONTROLLERA PÅ BÄNKEN med multimeter innan Hondan kopplas in:
;   - att set-aux port 1 verkligen är SW HV (pin 6) på Maxim+ (står i VESC Tool
;     under hårdvarans beskrivning; annars byt kill-port eller använd gpio-write);
;   - att reläet drar när styrkortet kör `gen start` och släpper inom 0,5 s när
;     CAN-kabeln dras ur.

(def kill-port 1)        ; AUX-utgången som är SW HV
(def tyst-grans 0.5)     ; sekunder utan meddelande -> stopp
(def far-ga 0)
(def sist-ok (systime))
(def mitt-id (conf-get 'controller-id))

(defun proc-sid (id data)
    (if (and (= id 0x5E)
             (>= (buflen data) 3)
             (= (bufget-u8 data 0) 0x47)
             (= (bufget-u8 data 2) mitt-id))
        (progn
            (setq far-ga (bufget-u8 data 1))
            (setq sist-ok (systime)))))

(defun event-handler ()
    (loopwhile t
        (recv
            ((event-can-sid (? id) . (? data)) (proc-sid id data))
            (_ nil))))

(event-register-handler (spawn event-handler))
(event-enable 'event-can-sid)

(loopwhile t
    (progn
        (if (and (= far-ga 1) (< (secs-since sist-ok) tyst-grans))
            (set-aux kill-port 1)    ; relä draget: Hondan får gå
            (set-aux kill-port 0))   ; relä släppt: stoppkabeln jordad
        (sleep 0.05)))
