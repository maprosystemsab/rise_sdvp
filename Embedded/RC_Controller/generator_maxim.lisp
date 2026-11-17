; generator_maxim.lisp — LispBM för VESC Maxim+ på elverket (RobAnt, firmware 30.27)
;
; Styr Hondans stoppkabel via ett relä på +12VA (AUX1, pin 17) efter styrkortets
; CAN-meddelande (standard-ID 0x05E, var 100:e ms): [0x47 'G'][1 = får gå / 0 = stopp][generator-ID]
;
; Enligt Maxim+-databladet (rev. okt 2026): set-aux port 1 = STM AUX1 = +12VA (pin 17 och 30,
; 12 V 3 A, kortslutningsskyddad). Port 2 = AUX2 = SW HV (pin 6, oskyddad low-side) – används INTE.
;
; FELSÄKER KOPPLING:
;   12 V bilrelä med växlande kontakt (5 stift: 30, 87, 87a, 85, 86).
;   86 <- Maxim+ pin 17 (+12VA), 85 -> Maxim+ pin 16 (GND).  (Relä med inbyggd diod: + på 86.)
;   30 <- Hondans stoppkabel, 87a -> motorblocket (jord), 87 lämnas tomt.
;     spolen strömsatt  -> 87a öppen  -> stoppkabeln fri    -> Hondan får gå
;     spolen strömlös   -> 87a sluten -> stoppkabeln jordad -> Hondan stannar
;   Hondan stannar alltså också om Maxim+ blir strömlös, skriptet inte körs,
;   CAN-kabeln går av eller styrkortet dör. Reläet skiljer dessutom Hondans tändning
;   galvaniskt från Maxim+.
;
; Läggs in i VESC Tool: VESC Dev Tools -> Lisp -> öppna filen -> Upload (kör vid start).
; KONTROLLERA PÅ BÄNKEN med multimeter innan Hondan kopplas in:
;   - att pin 17 är 0 V när Maxim+ startat (skriptet släpper reläet direkt);
;   - att reläet drar när styrkortet kör `gen start` och släpper inom 0,5 s när
;     CAN-kabeln dras ur.

(def kill-port 1)        ; AUX1 = +12VA, pin 17 (relä 86)
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
