/*
    Copyright 2016-2017 Benjamin Vedder	benjamin@vedder.se

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QElapsedTimer>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QtWidgets>
#include <QItemSelection>
#include <QList>
#include <QTimer>
#include <tuple>
#include <QSerialPort>
#include <QLabel>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QUrlQuery>
#include "actionmanager.h"
#include "vehicletypedelegate.h"

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    #include <SDL2/SDL.h>
    #include <QAction>
#else
    #include <QGamepad>
#endif

#include "carinterface.h"
#include "packetinterface.h"
#include "ping.h"
#include "vehicledata.h"
#include "nmeaserver.h"
#include "rtcm3_simple.h"
#include "intersectiontest.h"
#include "tcpclientmulti.h"
#include "wireguard.h"
#include <memory>
#include "arduinoreader.h"
#include "routegenerator.h"
#include "checkboxdelegate.h"
#include "database.h"
#include "versionchecker.h"

#ifdef HAS_LIME_SDR
#include "gpssim.h"
#endif

#ifdef HAS_SIM_SCEN
#include "pagesimscen.h"
#endif

//#ifdef HAS_JOYSTICK
//#include "joystick.h"
//#endif

namespace Ui {
class MainWindow;
}

class CustomDelegate;
/*
class FocusEventFilter : public QObject
{
    Q_OBJECT

signals:
    void focusGained();
    void focusLost();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};

*/
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
    bool eventFilter(QObject *object, QEvent *e);
    // Webbserverns adress (gårdar, fält, banor, maskiner), t.ex. "http://192.168.200.1:8080".
    // Ställs in i fältet Server på fliken Farm.
    QString getServerBaseUrl() const;

    void addCar(int id, QString name, bool pollData = false);
    void removeCars();
    bool connectJoystick();
    void checkJoystickConnection();
    void addTcpConnection(QString ip, int port);
    void setNetworkTcpEnabled(bool enabled, int port = -1);
    void setNetworkUdpEnabled(bool enabled, int port = -1);
    MapWidget *map();
    void addField();
    int currentFarm();
    void setCurrentFarm(int farm);
    void updateFarms();
    QLabel* getLogLabel();
    void populateControllerComboBoxes();
    QList<QPair<int, QString>> getMotorTypesFromDatabase();
    QList<QPair<int, QString>> getActionsFromDatabase();
    QList<std::tuple<int, QString, QString>> getActionsWithColoursFromDatabase();
    QString generateColourForAction(int actionId);
    QList<QPair<int, QString>> getModesFromDatabase();

public slots:
    void checkForUpdates();
    void showAbout();

private slots:
    void serialDataAvailable();
    void onUpdateCheckFinished(bool updateAvailable, const QVersionNumber &latestVersion, const QString &downloadUrl, const QString &releaseNotes);
    void onUpdateCheckError(const QString &errorMessage);
    void serialPortError(QSerialPort::SerialPortError error);
    void timerSlot();
    void sendHeartbeat();
    void showStatusInfo(QString info, bool isGood);
    void packetDataToSend(QByteArray &data);
    void stateReceived(quint8 id, CAR_STATE state);
    void mapPosSet(quint8 id, LocPoint pos);
    void ackReceived(quint8 id, CMD_PACKET cmd, QString msg);
    void rtcmReceived(QByteArray data);
    void rtcmRefPosGet();
    void pingRx(int time, QString msg);
    void pingError(QString msg, QString error);
    void enuRx(quint8 id, double lat, double lon, double height);
    void nmeaGgaRx(int fields, NmeaServer::nmea_gga_info_t gga);
    void routePointAdded(LocPoint pos);
    void routePointSelected(LocPoint pos);
    void activePointChanged(LocPoint point);
    void onControlSearchCriteriaChanged();
    void infoTraceChanged(int traceNow);
    void onMapCarBoxChanged(int value);
    void setJoystickControlEnabled(bool enabled);
    void loadControllerSettingsFromDatabase();
    void onMachinesTableClicked(const QModelIndex &index);
    void onMachinesTableDoubleClicked(const QModelIndex &index);
    void onMachinesSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
    void saveControllerSettingsToDatabase();
    void handleControllerInput(int controllerNumber, float value);

    void onSelectedFarm(const QModelIndex& current, const QModelIndex& previous);
    void onSelectedField(const QModelIndex& current, const QModelIndex& previous);
    void onSelectedFieldGeneral(QStandardItemModel *model, QStandardItemModel *modelPth, const QModelIndex& current, const QModelIndex& previous);
    void on_listLogFilesView_clicked(const QModelIndex& index);
    void onUnconnectedFieldsTableItemClicked(QTableWidgetItem *item);


    void handleAddFieldButton();
    void handleAddFarmButton();
    void addFarmToServer(const QString &name);
    void updateFarmOnServer(int farmId, const QString &name, double latitude, double longitude);
    void deleteFarmFromServer(int farmId);
    void addFieldToServer(const QString &name, int farmId, const QString &filename);
    void updateFieldOnServer(int fieldId, const QString &name, const QString &filename);
    void deleteFieldFromServer(int fieldId);
    void addPathToServer(const QString &name, int fieldId);
    void updatePathOnServer(int pathId, const QString &name);
    void deletePathFromServer(int pathId);
    void fetchFieldXml(int fieldId, const QString &fieldName);
    void onFieldDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles);
    
    // Log tab functions
    void setupLogTab();
    void fetchAllFarmsForLog();
    void parseAllFarmsXmlForLog(const QByteArray &xmlData);
    void fetchFieldsForLogFarm(int farmId);
    void parseAllFieldsXmlForLog(const QByteArray &xmlData);
    void fetchPathsForLogField(int fieldId);
    void parseAllPathsXmlForLog(const QByteArray &xmlData);
    void fetchLogsForPath(int pathId);
    void parseAllLogsXmlForLog(const QByteArray &xmlData);
    void fetchLogForLog(int logId);
    void loadPathAsLog(int pathId);
    void onFarmSelectedForLog(int index);
    void onFieldSelectedForLog(int index);
    void onPathSelectedForLog(int index);
    void onLogSelectedForLog(int index);
    void onLoadLogButtonClicked();

    void on_disconnectButton_clicked();
    void on_connectSelectedButton_clicked();
    void on_tcpConnectButton_clicked();
    void on_disconnectSelectedButton_clicked();
    void on_refreshMachinesButton_clicked();
    void on_mapRemoveTraceButton_clicked();
    void on_MapRemovePixmapsButton_clicked();
    void on_mapZeroButton_clicked();
    void on_mapRemoveRouteButton_clicked();
    void on_mapRouteSpeedBox_valueChanged(double arg1);
    void on_jsConnectButton_clicked();

    void on_jsDisconnectButton_clicked();
    void on_mapAntialiasBox_toggled(bool checked);
    void on_carsWidget_tabCloseRequested(int index);
//    void on_genCircButton_clicked();
    void on_mapSetAbsYawButton_clicked();
    void on_mapAbsYawSlider_valueChanged(int value);
    void on_mapAbsYawSlider_sliderReleased();
    void on_stopButton_clicked();
    void on_mapUploadRouteButton_clicked();
    void on_mapGetRouteButton_clicked();
    void on_mapApButton_clicked();
    void on_mapKbButton_clicked();
    void on_mapOffButton_clicked();
    void on_mapUpdateSpeedButton_clicked();
    void on_mapOpenStreetMapBox_toggled(bool checked);
    void on_mapAntialiasOsmBox_toggled(bool checked);
    void on_mapOsmResSlider_valueChanged(int value);
    void on_mapChooseNmeaButton_clicked();
    void on_mapImportNmeaButton_clicked();
    void on_mapRemoveInfoAllButton_clicked();
    void on_traceInfoMinZoomBox_valueChanged(double arg1);
    void on_removeRouteExtraButton_clicked();
    void on_mapOsmClearCacheButton_clicked();
    void on_mapOsmServerOsmButton_toggled(bool checked);
    void on_mapOsmServerHiResButton_toggled(bool checked);
    void on_mapOsmMaxZoomBox_valueChanged(int arg1);
    void on_mapDrawGridBox_toggled(bool checked);
    void on_mapGetEnuButton_clicked();
    void on_mapSetEnuButton_clicked();
    void on_mapOsmStatsBox_toggled(bool checked);
    void on_removeTraceExtraButton_clicked();
    void on_mapEditHelpButton_clicked();
    void on_mapStreamNmeaConnectButton_clicked();
    void on_mapStreamNmeaDisconnectButton_clicked();
    void on_mapStreamNmeaClearTraceButton_clicked();
    void on_mapRouteBox_valueChanged(int arg1);
    void on_mapRemoveRouteAllButton_clicked();
    void on_mapUpdateTimeButton_clicked();
    void on_mapRouteTimeEdit_timeChanged(const QTime &time);
    void on_mapTraceMinSpaceCarBox_valueChanged(double arg1);
    void on_mapTraceMinSpaceGpsBox_valueChanged(double arg1);
    void on_mapInfoTraceBox_valueChanged(int arg1);
    void on_removeInfoTraceExtraButton_clicked();
    void on_pollIntervalBox_valueChanged(int arg1);
    void on_actionAbout_triggered();
    void on_actionAboutLibrariesUsed_triggered();
    void on_actionExit_triggered();
    void on_actionSaveRoutes_triggered();
    void on_actionSaveRouteswithIDs_triggered();
    void on_actionLoadRoutes_triggered();
    void on_actionTestIntersection_triggered();
    void on_actionSaveSelectedRouteAsDriveFile_triggered();
    void on_actionLoadDriveFile_triggered();
    void on_mapDrawRouteTextBox_toggled(bool checked);
    void on_actionGPSSimulator_triggered();
    void on_mapDrawUwbTraceBox_toggled(bool checked);
    void on_actionToggleFullscreen_triggered();
    void on_actionToggleCameraFullscreen_triggered();
    void on_tabWidget_currentChanged(int index);
    void on_routeZeroButton_clicked();
    void on_routeZeroAllButton_clicked();
    void on_mapRoutePosAttrBox_currentIndexChanged(int index);
    void on_comboBoxAction_currentIndexChanged(int index);
    void on_addControlStateButton_clicked();
    void on_removeControlStateButton_clicked();
    void on_removeControlStateRow_clicked();
    void on_controlStatesTable_cellChanged(int row, int column);
    void on_controlStateComboChanged(int index);
    void on_controlStateValueChanged(double value);

    void on_clearAnchorButton_clicked();
    void on_setBoundsRoutePushButton_clicked();
    void on_boundsFillPushButton_clicked();
    void on_lowerToolsCheckBox_stateChanged(int arg1);
    void on_raiseToolsCheckBox_stateChanged(int arg1);
    void on_AutopilotConfigurePushButton_clicked();
    void on_AutopilotStartPushButton_clicked();
    void on_AutopilotStopPushButton_clicked();
    void on_AutopilotRestartPushButton_clicked();
    void on_AutopilotPausePushButton_clicked();
    void onGeneratePathButtonClicked();
    void onGenerateLineButtonClicked();
    void onCutButtonClicked();
    void onTransformButtonClicked();
    void onAppendButtonClicked();
    void onPrependButtonClicked();
    bool onLoadShapefile();
    bool onShowShapefile();
    bool onLoadLogfile();
    void onRangeSliderLowerChanged(int value);
    void onRangeSliderUpperChanged(int value);
    void filterLogBasedOnRangeSlider();
    void cutCurrentLogByArea(double minX, double minY, double maxX, double maxY);
    void testAreaCutting(); // Test function with predefined area
    void loadAreaFromXML(); // Load area definition from XML file
    void applyAreaFiltering(); // Apply area filtering to current log
    void cutPathByArea(); // Actually cut the path to only show sections within area
    void onResultPathChanged(int pathIndex); // Handle result path spinbox changes
    void onAnalysisSelectionChanged(); // Handle analysis table selection changes
    void calculateAndDisplayPathLengths();
    void calculateAndDisplayPathAngles();
    void calculateAndDisplayPathRMS();
    void updateCurrentAnalysis(); // Update the currently selected analysis
    void updateStatisticsDisplay(const QList<double>& values, const QString& unit = ""); // Update statistics display

    // QSqlRelationalTableModel* setupFarmTable(QTableView* uiFarmtable,QString SqlTableName); // Replaced with webserver version
    // QSqlRelationalTableModel* setupFieldTable(QTableView* uiFieldtable,QString SqlTableName); // Replaced with webserver version
    QStandardItemModel* setupPathTable(QTableView* uiPathTable,QString sqlTablename);

private:
    // Helper methods
    bool isPointInsideBorder(const LocPoint& point, const MapRoute& border); // Check if point is inside border
    void controllerAction(int car, int iAction,float value);
    void updateCurrentRoutePointControlStates();
    void updateControlStatesTableFromRoutePoint();
    void updateControlStatesTable(const QList<ControlState> &controlStates);
    void populateControlStateComboBoxes();
    void populateControlStateComboBox(QComboBox* comboBox, int selectedControllerId = -1);
    void fetchMachinesData(int retryCount = 0);
    void fetchAllMachinesData(int retryCount = 0);
    void fetchVehicleTypes(int retryCount = 0);
    void fetchAllFarmsData(int retryCount = 0);
    void fetchAllFieldsData(int farmId, int retryCount = 0);
    void fetchAllPathsData(int fieldId, int retryCount = 0);
    void fetchUnconnectedFieldsData(int retryCount = 0);
    void parseUnconnectedFieldsXml(const QByteArray &xmlData);
    void parseVehicleTypesXml(const QByteArray &xmlData);
    void parseAllMachinesXml(const QByteArray &xmlData);
    void parseMachinesXml(const QByteArray &xmlData);
    void parseAllFarmsXml(const QByteArray &xmlData);
    void parseAllFieldsXml(const QByteArray &xmlData);
    void parseAllPathsXml(const QByteArray &xmlData);
    void onAddMachineButtonClicked();
    QStandardItemModel* setupFarmsTable(QTableView* uiFarmTable);
    QStandardItemModel* setupFieldsTable(QTableView* uiFieldTable);

    // Area definition storage - using existing border infrastructure
    bool mAreaLoaded = false;
    int mAreaBorderIndex = -1; // Index of the border used for area filtering

    QStandardItemModel *modelPath;
    QStandardItemModel *machinesModel;
    QStandardItemModel *vehicleTypesModel;
    QStandardItemModel *farmsModel;
    QStandardItemModel *fieldsModel;
    
    // Models for log tab dropdowns
    QStandardItemModel *logFarmsModel;
    QStandardItemModel *logFieldsModel;
    QStandardItemModel *logPathsModel;
    QStandardItemModel *logLogsModel;
    VehicleTypeDelegate *vehicleTypeDelegate;
    CheckBoxDelegate* checkboxdelegate;
    // QStringListModel* fileModel;  // Model to hold filenames - removed, replaced with combo boxes
    // QStringList fileList;         // Underlying data - removed, replaced with combo boxes
    ActionManager *actionManager;

    Ui::MainWindow *ui;
    QLineEdit *mServerEdit = nullptr;
    QString mVehicleMachine;        // maskinen vars batteriinställning gäller (adress)
    // Dosan: av vid start, slås på med knappen "Aktivera dosa" (se activateGamepad).
    QPushButton *mGamepadButton = nullptr;
    QElapsedTimer mGamepadActivity;  // senaste spak-/knapprörelse medan dosan är aktiv
    void activateGamepad();
    void lockGamepad(const QString &why);
    void updateGamepadButton();
    void openVehicleSettings();
    void calibrateSteeringAngle();
    bool mConnectingSelected = false; // Guard flag to prevent double connection
    QTimer *mTimer;
    QTimer *mHeartbeatTimer; // periodic heartbeat to vehicles for safety
    const int mHeartbeatMS = 300;
    QSerialPort *mSerialPort;
    PacketInterface *mPacketInterface;
    QList<CarInterface*> mCars;
    QLabel *mStatusLabel;
    int mStatusInfoTime;
    int mActiveCarId;
    bool mJoystickControlEnabled;
    bool mKeyUp;
    bool mKeyDown;
    bool mKeyRight;
    bool mKeyLeft;
    double mThrottle;
    double mSteering;
    Ping *mPing;
    NmeaServer *mNmea;
    QUdpSocket *mUdpSocket;
    TcpClientMulti *mTcpClientMulti;
    QNetworkAccessManager *mNetworkManager;
    
    // File administration tab widgets
    QTableWidget *mUnconnectedFieldsTable;
    MapWidget *mMapWidgetFileAdmin;
    QString mVersion;
    rtcm3_state mRtcmState;
    IntersectionTest *mIntersectionTest;
    std::unique_ptr<WireGuard> mWireGuard;
    QString mLastImgFileName;
    QList<QPair<int, int> > mSupportedFirmwares;
    ArduinoReader serialReader;
    database db;
    bool activeCarExists;
    QTimer* mJoystickPollTimer = nullptr;
    int mLastJoystickCount = 0;  // For hot-plugging detection

#ifdef HAS_JOYSTICK_CHECK
    bool JSconnected();

    #if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        SDL_GameController* mController = nullptr;
#else
        QGamepad *mJoystick;
    #endif
#endif

#ifdef HAS_SIM_SCEN
    PageSimScen *mSimScen;
#endif

    void saveRoutes(bool withId);

    CustomDelegate *statustocolourDelegate;

//    FocusEventFilter filterFieldtable;
//    FocusEventFilter filterPathtable;
    int mRangeSliderLowerValue = 20; // Default lower value for range slider
    int mRangeSliderUpperValue = 80; // Default upper value for range slider
    QList<MapRoute> mOriginalLogs; // Store original logs for non-destructive filtering
    VersionChecker *m_versionChecker;
    QAction *m_checkForUpdatesAction;
    QMenu *m_helpMenu;
    QMap<int, int> mCachedControllerActions; // Cache for database-free controller mapping
    QMap<int, float> mCachedControllerValues; // Cache for preventing network flooding from joystick jitter
    QLabel *mStatusBoxLabel = nullptr;      // Statusruta under anslutningslistan
    NmeaServer::nmea_gga_info_t mLastGga;
    bool mHaveGga = false;
    // Nollpunkt vid anslutning: sätts på robotens första GPS-position (nmeaGgaRx),
    // så att kartan visar rätt var man än är, utan att välja gård först.
    bool mAutoEnuRefPending = false;
    void applyAutoEnuRef(double lat, double lon, double height);
    void saveMapPosition(double lat, double lon);
    QElapsedTimer mGgaAge;                  // Tid sedan senaste GGA från RTK-strömmen
    QElapsedTimer mStateAge;                // Tid sedan senaste statuspaket från bilen
    void updateStatusBox();
    VehicleData *mVehicle = nullptr;       // Fordonsdata i statusrutan + körlogg
    QString mConnectedIp;                   // IP till bilen vi senast anslöt till
    // Bil-ID automatiskt: vid anslutning frågas styrkortet på ID_ALL vilket ID
    // det har, och bilfliken byts till det om maskinlistans ID inte stämmer.
    bool mAutoIdPending = false;
    int mAutoIdTries = 0;
    QTimer *mAutoIdTimer = nullptr;
    void applyAutoId(quint8 id);
    bool mNmeaDrawTrace = true;             // rita GPS-positionerna som spår (inte vid automatisk anslutning)
    QNetworkAccessManager *mRouterNet = nullptr; // Egen hanterare: mNetworkManager har globala finished-kopplingar
    QString mRouterLine;                    // Färdig HTML-rad för 4G/5G-mottagningen
    QElapsedTimer mRouterAge;
    void pollRouterSignal();
    QMap<int, float> mLastActionValues; // Senast skickade värde per action (≠ 0), skickas om av mRcResendTimer
    QTimer *mRcResendTimer = nullptr;
    void rcResendTick();

    // Nödstoppet från Car_Client (paket 143): -1 = okänt/inget, 0 ok, 1 intryckt,
    // 2 spärrad (utdragen, väntar på Stopp), 3 fel. Visas på Stopp-knappen.
    int mNodstoppLage = -1;
    QElapsedTimer mNodstoppSenast;
    QIcon mStopIcon;
    void nodstoppVisa(int lage);
    bool gamepadAttached();

    // Armar (L1/L2 fram, R1/R2 bak). Skickas två vägar, varje maskin använder den
    // den förstår: CMD_HYDRAULIC_MOVE (hydraulik via firmware, t.ex. MacTrac) och
    // kontroll 1/3 via dosabindningarna (t.ex. VESC-armar på RobAnt).
    bool mArmL1 = false, mArmL2 = false, mArmR1 = false, mArmR2 = false;
    // RobAnt: håll in O (cirkel) = vänd på stället, styrspaken ger håll och fart
    bool mPivotHeld = false;
    void setPivotHeld(bool held);
    int mArmFrontMove = 0, mArmRearMove = 0;   // senast skickat, i firmwarens värden
    QElapsedTimer mArmResendAge;
    void updateArms();
    void sendArmHydraulics(bool force);

private slots:
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    void pollGamepad();
    void handleButtonEvent(const SDL_ControllerButtonEvent& event);
    void handleAxisEvent(const SDL_ControllerAxisEvent& event);
#endif

};

class CustomDelegate : public QStyledItemDelegate {
public:
    explicit CustomDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

// Forward declaration for VehicleTypeDelegate
class VehicleTypeDelegate;

#include <QDoubleValidator>

class PrecisionDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit PrecisionDelegate(int column, int decimals = 10, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_column(column), m_decimals(decimals) {}
    // Display high-precision numbers as text
    QString displayText(const QVariant &value, const QLocale &locale) const override {
        if (value.canConvert<double>())
            return locale.toString(value.toDouble(), 'f', m_decimals);
        return QStyledItemDelegate::displayText(value, locale);
    }

    // Use a QLineEdit for editing (not a spinbox)
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &index) const override {
        if (index.column() == m_column) {
            QLineEdit *editor = new QLineEdit(parent);
            auto *validator = new QDoubleValidator(editor);
            validator->setNotation(QDoubleValidator::StandardNotation);
            validator->setDecimals(m_decimals);
            editor->setValidator(validator);
            editor->setAlignment(Qt::AlignRight);
            editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            return editor;
        }
        return QStyledItemDelegate::createEditor(parent, {}, index);
    }

    // Convert text back to double
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override {
        if (index.column() == m_column) {
            auto *line = qobject_cast<QLineEdit *>(editor);
            if (!line)
                return;
            bool ok;
            double d = line->text().toDouble(&ok);
            if (ok)
                model->setData(index, d, Qt::EditRole);
            return;
        }
        QStyledItemDelegate::setModelData(editor, model, index);
    }
private:
    int m_column;
    int m_decimals;
};


void selectRowByPrimaryKey(QTableView* tableView, QSqlRelationalTableModel* model, const QString& primaryKeyColumnName, const QVariant& primaryKeyValue);
MainWindow* findMainWindow();


#endif // MAINWINDOW_H
