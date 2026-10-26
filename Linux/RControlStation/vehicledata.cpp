#include "vehicledata.h"

#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QtEndian>
#include <cmath>

namespace {
const int CMD_GET_VESC_STATUS = 140;
const int VESC_ENTRY_LEN = 11;          // id(1) ålder ms(2) rpm(4) ström*10(2) duty*1000(2)
const int VESC_FRESH_MS = 2000;
const double REST_FOR_PERCENT_S = 60.0; // stilla så länge -> ny procent ur spänningen
const double REST_BEFORE_CHARGE_S = 90.0;
const double CHARGE_WINDOW_S = 60.0;
const double CHARGE_SLOPE_V_PER_MIN = 0.05;
const double STILL_SPEED_MS = 0.1;
const double STILL_POWER_W = 30.0;
const double MIN_DISTANCE_FOR_RATE_M = 200.0;
const double MAX_DT_S = 5.0;
const int ANGLE_GIVE_UP = 10;           // obesvarade "vinkel" i rad -> sluta fråga

// Senaste automatiska "vinkel"-fråga (ms sedan epok), för att dölja svaret i terminalen.
qint64 gLastAutoAngleQuery = 0;

const double LIFEPO4_CELL[][2] = {
    {2.50, 0.0}, {3.00, 10.0}, {3.20, 20.0}, {3.22, 30.0}, {3.25, 40.0}, {3.26, 50.0},
    {3.27, 60.0}, {3.30, 70.0}, {3.32, 80.0}, {3.35, 90.0}, {3.40, 100.0},
};
}

namespace {
// Gruppen i QSettings för en maskin: "fordon/<adress>", eller "fordon/standard".
QString machineKey(const QString &machine)
{
    QString key = machine.trimmed().isEmpty() ? QString("standard") : machine.trimmed();
    return key.replace('/', '_');
}

void readConfig(QSettings &s, const QString &key, VehicleConfig &c)
{
    s.beginGroup("fordon/" + key);
    c.batteryType = s.value("batterityp", c.batteryType).toString();
    c.seriesCells = s.value("celler", c.seriesCells).toInt();
    c.emptyV = s.value("tom_v", c.emptyV).toDouble();
    c.fullV = s.value("full_v", c.fullV).toDouble();
    c.capacityWh = s.value("kapacitet_wh", c.capacityWh).toDouble();
    c.steeringMaxDeg = s.value("styrvinkel_max", c.steeringMaxDeg).toDouble();
    s.endGroup();
}
}

VehicleConfig VehicleConfig::load(const QString &machine)
{
    QSettings s("RControlStation", "fordon");
    VehicleConfig c;                         // inbyggda värden
    readConfig(s, "standard", c);            // standard för alla maskiner, om sparad
    if (!machine.trimmed().isEmpty()) {
        s.beginGroup("fordon");
        const bool finns = s.childGroups().contains(machineKey(machine));
        s.endGroup();
        if (finns) {
            readConfig(s, machineKey(machine), c);   // just den här maskinen
        }
    }
    return c;
}

void VehicleConfig::save(const QString &machine) const
{
    QSettings s("RControlStation", "fordon");
    s.beginGroup("fordon/" + machineKey(machine));
    s.setValue("batterityp", batteryType);
    s.setValue("celler", seriesCells);
    s.setValue("tom_v", emptyV);
    s.setValue("full_v", fullV);
    s.setValue("kapacitet_wh", capacityWh);
    s.setValue("styrvinkel_max", steeringMaxDeg);
    s.endGroup();
}

QString VehicleConfig::describe() const
{
    QString t = batteryType == "linear"
            ? QString("linjärt %1–%2 V").arg(emptyV, 0, 'f', 1).arg(fullV, 0, 'f', 1)
            : QString("LiFePO4 %1 celler").arg(seriesCells);
    if (capacityWh > 0.0) {
        t += QString(", %1 Wh").arg(capacityWh, 0, 'f', 0);
    }
    return t;
}

VehicleData::VehicleData(QObject *parent) : QObject(parent)
{
    mClock.start();
}

double VehicleData::lifepo4Percent(double packVoltage, int cells)
{
    if (cells <= 0) {
        return 0.0;
    }
    double v = packVoltage / cells;
    const int n = sizeof(LIFEPO4_CELL) / sizeof(LIFEPO4_CELL[0]);
    if (v <= LIFEPO4_CELL[0][0]) {
        return 0.0;
    }
    for (int i = 1; i < n; i++) {
        if (v <= LIFEPO4_CELL[i][0]) {
            double v0 = LIFEPO4_CELL[i - 1][0], p0 = LIFEPO4_CELL[i - 1][1];
            double v1 = LIFEPO4_CELL[i][0], p1 = LIFEPO4_CELL[i][1];
            return p0 + (p1 - p0) * (v - v0) / (v1 - v0);
        }
    }
    return 100.0;
}

QString VehicleData::faultText(int code)
{
    switch (code) {
    case 0: return QString();
    case 1: return "Överspänning – batterispänningen är för hög";
    case 2: return "Underspänning – batteriet är för lågt (ladda)";
    case 3: return "Fel i motordrivaren (DRV)";
    case 4: return "För hög motorström";
    case 5: return "VESC överhettad (transistorerna)";
    case 6: return "Motorn överhettad";
    default: return QString("VESC-fel, kod %1").arg(code);
    }
}

bool VehicleData::parseAngle(const QString &text, double &deg, bool &ok)
{
    static const QRegularExpression re("Vinkelgivare:.*vinkel\\s+(-?[0-9.]+)");
    QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        return false;
    }
    bool num = false;
    deg = m.captured(1).toDouble(&num);
    ok = text.contains(", OK");
    return num;
}

bool VehicleData::hideAnglePrint(const QString &str)
{
    return (str.contains("Vinkelgivare:") || str.contains("Invalid command: vinkel"))
            && QDateTime::currentMSecsSinceEpoch() - gLastAutoAngleQuery < 1500;
}

void VehicleData::stateReceived(quint8 id, const CAR_STATE &state)
{
    mCarId = id;
    mState = state;
    mHaveState = true;
    mStateAge.restart();
}

void VehicleData::packetReceived(quint8 id, quint8 cmd, const QByteArray &pkt)
{
    (void)id;
    if (cmd == CMD_GET_VESC_STATUS) {
        QByteArray rest = pkt.mid(2);
        if (rest.size() % VESC_ENTRY_LEN != 0) {
            return;
        }
        double power = 0.0;
        int fresh = 0;
        double vin = mHaveState ? mState.vin : 0.0;
        for (int i = 0; i + VESC_ENTRY_LEN <= rest.size(); i += VESC_ENTRY_LEN) {
            const uchar *e = reinterpret_cast<const uchar*>(rest.constData()) + i;
            quint16 age = qFromBigEndian<quint16>(e + 1);
            qint16 cur10 = qFromBigEndian<qint16>(e + 7);
            qint16 duty1000 = qFromBigEndian<qint16>(e + 9);
            if (age < VESC_FRESH_MS) {
                fresh++;
                power += std::fabs((cur10 / 10.0) * (duty1000 / 1000.0)) * vin;
            }
        }
        mPowerW = power;
        mVescFresh = fresh;
        mHavePower = true;
        mPowerAge.restart();
    } else if (cmd == CMD_PRINTF) {
        double deg;
        bool ok;
        if (parseAngle(QString::fromLatin1(pkt.mid(2)), deg, ok)) {
            mAngleDeg = deg;
            mAngleOk = ok;
            mHaveAngle = true;
            mAngleUnanswered = 0;
            mAngleAge.restart();
        }
    }
}

bool VehicleData::wantAngleQuery()
{
    if (mAngleUnanswered >= ANGLE_GIVE_UP && !mHaveAngle) {
        return false; // firmware utan vinkelgivare
    }
    if (mQueryCounter % 2 != 0) {
        return false; // varannan tick = en gång per sekund
    }
    mAngleUnanswered++;
    gLastAutoAngleQuery = QDateTime::currentMSecsSinceEpoch();
    return true;
}

bool VehicleData::wantVescQuery()
{
    return mQueryCounter % 2 == 1;
}

double VehicleData::batteryPercent(double v)
{
    if (mCfg.batteryType == "linear") {
        if (mCfg.fullV <= mCfg.emptyV) {
            return 0.0;
        }
        return qBound(0.0, (v - mCfg.emptyV) / (mCfg.fullV - mCfg.emptyV) * 100.0, 100.0);
    }
    return lifepo4Percent(v, mCfg.seriesCells);
}

void VehicleData::tick(bool connected)
{
    mQueryCounter++;
    bool fresh = connected && mHaveState && mStateAge.isValid() && mStateAge.elapsed() < 3000;
    if (!fresh) {
        if (mLog.isOpen()) {
            mLog.close();
        }
        mLastTick = -1.0;
        if (!connected) {
            mHaveState = false;
            mHaveAngle = false;
            mAngleUnanswered = 0;
            mHavePower = false;
            mStillSince = -1.0;
            mVolts.clear();
            mRestPercent = -1.0;
            mCharging = -1;
        }
        return;
    }
    if (mHaveAngle && mAngleAge.elapsed() > 5000) {
        mHaveAngle = false;
    }
    if (mHavePower && mPowerAge.elapsed() > 5000) {
        mHavePower = false;
    }

    double now = mClock.elapsed() / 1000.0;
    double v = mState.vin;
    double speed = std::fabs(mState.speed);
    double power = mHavePower ? mPowerW : 0.0;

    if (mLastTick >= 0.0) {
        double dt = now - mLastTick;
        if (dt > 0.0 && dt <= MAX_DT_S) {
            mEnergyWh += power * dt / 3600.0;
            mDistanceM += speed * dt;
        }
    }
    mLastTick = now;

    bool still = speed < STILL_SPEED_MS && power < STILL_POWER_W;
    if (still) {
        if (mStillSince < 0.0) {
            mStillSince = now;
        }
    } else {
        mStillSince = -1.0;
        mVolts.clear();
    }
    mVolts.append(qMakePair(now, v));
    while (!mVolts.isEmpty() && now - mVolts.first().first > CHARGE_WINDOW_S) {
        mVolts.removeFirst();
    }

    // Procent: ur vilospänningen, sedan nedräknad med förbrukad energi.
    double voltPercent = batteryPercent(v);
    if (still && (mRestPercent < 0.0 || now - mStillSince >= REST_FOR_PERCENT_S)) {
        mRestPercent = voltPercent;
        mRestEnergyWh = mEnergyWh;
    }
    mPercent = (mRestPercent < 0.0 || mCfg.capacityWh <= 0.0) ? voltPercent
            : qBound(0.0, mRestPercent - (mEnergyWh - mRestEnergyWh) / mCfg.capacityWh * 100.0, 100.0);

    // Laddning: stilla minst 90 s och spänningen stiger mer än 0,05 V/min.
    mCharging = -1;
    if (mStillSince >= 0.0 && now - mStillSince >= REST_BEFORE_CHARGE_S) {
        QList<QPair<double, double>> pts;
        for (const auto &p: mVolts) {
            if (p.first >= mStillSince + REST_BEFORE_CHARGE_S - CHARGE_WINDOW_S) {
                pts.append(p);
            }
        }
        if (pts.size() >= 5 && pts.last().first - pts.first().first >= CHARGE_WINDOW_S / 2.0) {
            double mx = 0, my = 0;
            for (const auto &p: pts) { mx += p.first; my += p.second; }
            mx /= pts.size(); my /= pts.size();
            double sxy = 0, sxx = 0;
            for (const auto &p: pts) {
                sxy += (p.first - mx) * (p.second - my);
                sxx += (p.first - mx) * (p.first - mx);
            }
            if (sxx > 0.0) {
                mCharging = (sxy / sxx * 60.0 > CHARGE_SLOPE_V_PER_MIN) ? 1 : 0;
            }
        }
    }

    mWhPerKm = -1.0;
    mRangeKm = -1.0;
    if (mDistanceM >= MIN_DISTANCE_FOR_RATE_M && mEnergyWh > 0.0) {
        mWhPerKm = mEnergyWh / (mDistanceM / 1000.0);
        if (mCfg.capacityWh > 0.0) {
            mRangeKm = mPercent / 100.0 * mCfg.capacityWh / mWhPerKm;
        }
    }

    // Körlogg: en rad per sekund.
    if (!mLog.isOpen()) {
        startLog();
    }
    if (++mLogCounter % 2 == 0) {
        writeLog();
    }
}

void VehicleData::startLog()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
            + "/RControlStation-korloggar";
    QDir().mkpath(dir);
    mLog.setFileName(dir + "/" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".csv");
    if (mLog.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream(&mLog) << "tid;fart_kmh;spanning_v;batteri_procent;laddar;effekt_w;wh_per_km;"
                              "rackvidd_km;styrvinkel_grad;styrning_procent;roll_grad;pitch_grad;"
                              "kurs_grad;vesc_temp_c;vesc_svarar;fel\n";
    }
}

void VehicleData::writeLog()
{
    if (!mLog.isOpen()) {
        return;
    }
    auto f = [](double v, int d) { return QString::number(v, 'f', d); };
    QStringList r;
    r << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
      << f(std::fabs(mState.speed) * 3.6, 1)
      << f(mState.vin, 2)
      << f(mPercent, 1)
      << (mCharging < 0 ? QString() : (mCharging ? "ja" : "nej"))
      << (mHavePower ? f(mPowerW, 0) : QString())
      << (mWhPerKm > 0 ? f(mWhPerKm, 1) : QString())
      << (mRangeKm >= 0 ? f(mRangeKm, 2) : QString())
      << (mHaveAngle ? f(mAngleDeg, 1) : QString())
      << (mHaveAngle ? f(qBound(-100.0, mAngleDeg / mCfg.steeringMaxDeg * 100.0, 100.0), 0) : QString())
      << f(mState.roll, 1) << f(mState.pitch, 1) << f(mState.yaw, 1)
      << f(mState.temp_fet, 1)
      << (mHavePower ? QString::number(mVescFresh) : QString())
      << faultText(mState.mc_fault).replace(';', ' ');
    QTextStream(&mLog) << r.join(';') << "\n";
    mLog.flush();
}

QString VehicleData::html() const
{
    if (!mHaveState || !mStateAge.isValid() || mStateAge.elapsed() > 3000) {
        return QString();
    }
    QString out;
    auto tiltCol = [](double d) {
        double a = std::fabs(d);
        return a > 25.0 ? "#c00" : (a > 15.0 ? "#c70" : "#000");
    };

    // Batteri
    QString col = mPercent < 20.0 ? "#c00" : (mPercent < 40.0 ? "#c70" : "#080");
    out += QString("<b>Batteri:</b> <span style='color:%1'><b>%2 %</b></span> (%3 V)")
            .arg(col).arg(mPercent, 0, 'f', 0).arg(mState.vin, 0, 'f', 1);
    if (mCharging == 1) {
        out += " <span style='color:#080'><b>⚡ laddar</b></span>";
    } else if (mCharging == 0) {
        out += " <span style='color:#777'>laddar inte</span>";
    }
    if (mPercent < 15.0) {
        out += "<br><span style='color:#c00'><b>⚠ Lågt batteri – kör hem och ladda</b></span>";
    }
    out += QString("<br><small style='color:#777'>%1</small>").arg(mCfg.describe().toHtmlEscaped());

    // Räckvidd
    if (mRangeKm >= 0.0) {
        out += QString("<br><b>Räckvidd:</b> ≈ %1 km (%2 Wh/km)").arg(mRangeKm, 0, 'f', 1).arg(mWhPerKm, 0, 'f', 0);
    } else {
        out += "<br><b>Räckvidd:</b> – <span style='color:#777'>(efter 200 m körning)</span>";
    }

    // Fart och temperatur
    double t = mState.temp_fet;
    QString tcol = t > 80.0 ? "#c00" : (t > 65.0 ? "#c70" : "#000");
    out += QString("<br><b>Fart:</b> %1 km/h &nbsp;<b>Temp:</b> <span style='color:%2'>%3 °C</span>")
            .arg(std::fabs(mState.speed) * 3.6, 0, 'f', 1).arg(tcol).arg(t, 0, 'f', 0);

    // Styrning
    if (mHaveAngle && mAngleOk) {
        double pct = qBound(-100.0, mAngleDeg / mCfg.steeringMaxDeg * 100.0, 100.0);
        QString side = std::fabs(pct) < 3.0 ? QString("rakt")
                : (pct < 0 ? QString("V %1 %").arg(-pct, 0, 'f', 0) : QString("H %1 %").arg(pct, 0, 'f', 0));
        out += QString("<br><b>Styrning:</b> %1 (%2°)").arg(side).arg(mAngleDeg, 0, 'f', 0);
    } else if (mHaveAngle) {
        out += "<br><b>Styrning:</b> <span style='color:#c00'>vinkelgivaren svarar inte</span>";
    }

    // Lutning och kurs
    double yaw = std::fmod(std::fmod(mState.yaw, 360.0) + 360.0, 360.0);
    out += QString("<br><b>Lutning:</b> sida <span style='color:%1'>%2°</span>, fram/bak <span style='color:%3'>%4°</span>"
                   " &nbsp;<b>Kurs:</b> %5°")
            .arg(tiltCol(mState.roll)).arg(mState.roll, 0, 'f', 0)
            .arg(tiltCol(mState.pitch)).arg(mState.pitch, 0, 'f', 0)
            .arg(yaw, 0, 'f', 0);

    // Fel
    QString fault = faultText(mState.mc_fault);
    if (!fault.isEmpty()) {
        out += QString("<br><span style='color:#c00'><b>⚠ %1</b></span>").arg(fault.toHtmlEscaped());
    }
    return out;
}
