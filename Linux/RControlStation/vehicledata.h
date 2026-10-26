/*
 * Fordonsdata i statusrutan (2026-10-05, kundens önskemål, samma som Robotstyrning):
 * batteri i procent med laddningsindikering, räckvidd, fart, temperatur, styrvinkel,
 * lutning och kurs, felkoder i klartext och en körlogg (CSV) på den här datorn.
 *
 * - Spänning, fart, temperatur, felkod, roll/pitch/yaw: styrkortets CMD_GET_STATE.
 * - Effekt (för räckvidden): CMD_GET_VESC_STATUS (140), motorström × duty × spänning.
 * - Styrvinkel: terminalkommandot "vinkel" (RobAnt-firmware). Svaret visas inte i
 *   terminalen när det är statusrutan som frågat (hideAnglePrint).
 * - Batteriet: ställs in per maskin (knappen Batteri och fordon i statusrutan), samma
 *   alternativ som robotd:s config.json. Standard: 4 st 12,8 V 80 Ah LiFePO4 i serie
 *   (16 celler, 4096 Wh). Procenten tas ur vilospänningen när roboten står still och
 *   räknas sedan ner med förbrukad energi, så att spänningsfallet under körning inte syns.
 */

#ifndef VEHICLEDATA_H
#define VEHICLEDATA_H

#include <QObject>
#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QPair>
#include <QString>
#include "datatypes.h"

// Batteri och styrning för en maskin. Sparas per maskin (robotens adress) med QSettings.
struct VehicleConfig {
    QString batteryType = "lifepo4";   // "lifepo4" (cellernas vilospänning) eller "linear"
    int seriesCells = 16;               // LiFePO4: celler i serie
    double emptyV = 48.0;               // linear: 0 %
    double fullV = 54.4;                // linear: 100 %
    double capacityWh = 4096.0;         // 0 = okänd (ingen räckvidd)
    double steeringMaxDeg = 25.0;       // största styrvinkel åt varje håll

    // Inställningen för maskinen på adressen, annars standardinställningen, annars ovan.
    static VehicleConfig load(const QString &machine);
    void save(const QString &machine) const;  // tom machine = standard för alla maskiner
    QString describe() const;           // t.ex. "LiFePO4 16 celler, 4096 Wh"
};

class VehicleData : public QObject
{
    Q_OBJECT
public:
    explicit VehicleData(QObject *parent = nullptr);

    void setConfig(const VehicleConfig &cfg) { mCfg = cfg; mRestPercent = -1.0; }
    const VehicleConfig &config() const { return mCfg; }

    // Batteri: procent ur vilospänningen för en LiFePO4-cell (16 i serie).
    static double lifepo4Percent(double packVoltage, int cells = 16);
    static QString faultText(int code);
    // "Vinkelgivare: 2500 mV, vinkel 3.2 grader, OK" -> vinkel och om givaren är OK.
    static bool parseAngle(const QString &text, double &deg, bool &ok);

    void stateReceived(quint8 id, const CAR_STATE &state);
    void packetReceived(quint8 id, quint8 cmd, const QByteArray &pkt);
    // Var 500:e ms: räknar fram värdena, skriver körloggen. Ger frågor att skicka
    // till bilen (vinkel / VESC-status) via pending*().
    void tick(bool connected);
    QString html() const;

    bool wantAngleQuery();      // true = skicka "vinkel" nu
    bool wantVescQuery();       // true = skicka CMD_GET_VESC_STATUS nu
    quint8 carId() const { return mCarId; }

    // Används av terminalen: dölj vinkelsvar som statusrutan själv frågat efter.
    static bool hideAnglePrint(const QString &str);

private:
    void startLog();
    void writeLog();
    double batteryPercent(double v);

    VehicleConfig mCfg;
    quint8 mCarId = 0;
    bool mHaveState = false;
    CAR_STATE mState;
    QElapsedTimer mStateAge;
    QElapsedTimer mClock;           // sedan start, för tider i sekunder
    double mLastTick = -1.0;

    // VESC-status
    double mPowerW = 0.0;
    bool mHavePower = false;
    QElapsedTimer mPowerAge;
    int mVescFresh = 0;

    // Styrvinkel
    bool mHaveAngle = false;
    double mAngleDeg = 0.0;
    bool mAngleOk = false;
    int mAngleUnanswered = 0;
    QElapsedTimer mAngleAge;
    int mQueryCounter = 0;

    // Batteri, laddning, förbrukning
    double mPercent = -1.0;
    double mRestPercent = -1.0;
    double mRestEnergyWh = 0.0;
    double mStillSince = -1.0;
    QList<QPair<double, double>> mVolts; // (tid s, spänning)
    int mCharging = -1;             // -1 vet inte, 0 nej, 1 ja
    double mEnergyWh = 0.0;
    double mDistanceM = 0.0;
    double mWhPerKm = -1.0;
    double mRangeKm = -1.0;

    // Körlogg
    QFile mLog;
    int mLogCounter = 0;
};

#endif // VEHICLEDATA_H
