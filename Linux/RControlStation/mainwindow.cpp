/*
    Copyright 2016 - 2017 Benjamin Vedder	benjamin@vedder.se

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

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QSerialPortInfo>
#include <QLatin1String>
#include <QDebug>
#include <cmath>
#include <array>
#include <algorithm>
#include <QMessageBox>
#include <QPointer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QHostInfo>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>

// Define version if not already defined by CMake
#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif


#include <QXmlStreamWriter>
#include <QXmlStreamReader>
#include <QStringList>
#include <QElapsedTimer>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>
#include <QSettings>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QDialog>

// Mapros webbserver i WireGuard-tunneln, används när inget annat är inställt.
static const char *STANDARD_SERVER = "192.168.200.1:8080";
#include <QLoggingCategory>
#include <QtSql>
#include <QtCharts>
#include <QtWidgets>
#include <QDir>
#include <iostream>
#include <fstream>

//using namespace QtCharts;

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QChart>
#include <QListView>
#include <QStringListModel>

#include "utility.h"
#include "routemagic.h"
#include "wireguard.h"
#include "attributes_masks.h"
#include "datatypes.h"
#include "arduinoreader.h"
#include "checkboxdelegate.h"
#include "shapefile.h"

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    #include <SDL2/SDL.h>
#else
    #include <QGamepad>
#endif

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#define FRONT_UP 5
#define FRONT_DOWN 7
#define REAR_UP 4
#define REAR_DOWN 6
// 7: Front Up
// 5: Front Down
// 6: Rear up
// 4: Rear down
#else
#define FRONT_UP 5
#define FRONT_DOWN 7
#define REAR_UP 4
#define REAR_DOWN 6
// 5: Front Up
// 7: Front Down
// 4: Rear up
// 6: Rear down
#endif




namespace {
void stepTowards(double &value, double goal, double step) {
    if (value < goal) {
        if ((value + step) < goal) {
            value += step;
        } else {
            value = goal;
        }
    } else if (value > goal) {
        if ((value - step) > goal) {
            value -= step;
        } else {
            value = goal;
        }
    }
}

void deadband(double &value, double tres, double max) {
    if (fabs(value) < tres) {
        value = 0.0;
    } else {
        double k = max / (max - tres);
        if (value > 0.0) {
            value = k * value + max * (1.0 - k);
        } else {
            value = -(k * -value + max * (1.0 - k));
        }

    }
}
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    serialReader("/dev/arduino", 9600),
    ui(new Ui::MainWindow),
    db(this),
    logFarmsModel(nullptr),
    logFieldsModel(nullptr),
    logPathsModel(nullptr),
    logLogsModel(nullptr)
{
    ui->setupUi(this);

    // Webbserverns adress: inställbar för organisationer med egen server (idé och första
    // version: Gunnar Larsson, SLU). Sparas direkt när den ändras. Tomt = vår server.
    {
        QString sparad = QSettings("RControlStation", "server").value("adress").toString();
        if (sparad.isEmpty()) {
            sparad = QSettings("SLU", "RControlStation").value("serverip").toString(); // Gunnars tidigare inställning
        }
        mServerEdit = ui->serveripEdit;     // fältet "Server ip" överst på fliken Farm
        mServerEdit->setText(sparad);
        mServerEdit->setPlaceholderText(QString(STANDARD_SERVER) + " (Mapro)");
        mServerEdit->setToolTip("Webbserverns adress för gårdar, fält, banor och maskiner, "
                                "t.ex. 192.168.200.1:8080. Tomt = Mapros server. Port 8080 om ingen anges.");
        connect(mServerEdit, &QLineEdit::editingFinished, this, [this]() {
            QSettings("RControlStation", "server").setValue("adress", mServerEdit->text().trimmed());
        });
    }

    // Initialize File Administration tab widgets from UI
    mUnconnectedFieldsTable = ui->unconnectedFieldsTable;
    mMapWidgetFileAdmin = ui->mapWidgetFileAdmin;
    
    // Set up table columns for unconnected fields
    mUnconnectedFieldsTable->setColumnCount(1);
    mUnconnectedFieldsTable->setHorizontalHeaderItem(0, new QTableWidgetItem("Filename"));
    
    // Connect the table item click signal
    connect(mUnconnectedFieldsTable, &QTableWidget::itemClicked, this, &MainWindow::onUnconnectedFieldsTableItemClicked);
    
    // Create Help menu with Check for Updates action
    m_helpMenu = menuBar()->addMenu(tr("Help"));
    m_checkForUpdatesAction = new QAction(tr("Check for Updates"), this);
    connect(m_checkForUpdatesAction, &QAction::triggered, this, &MainWindow::checkForUpdates);
    m_helpMenu->addAction(m_checkForUpdatesAction);
    
    QAction *aboutAction = new QAction(tr("About"), this);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    m_helpMenu->addAction(aboutAction);
    
    // Initialize version checker
    m_versionChecker = new VersionChecker(this);
    connect(m_versionChecker, &VersionChecker::updateCheckFinished, 
            this, &MainWindow::onUpdateCheckFinished);
    connect(m_versionChecker, &VersionChecker::updateCheckError, 
            this, &MainWindow::onUpdateCheckError);
    
    // Initialize analysis table items (they are defined in UI but need content)
    ui->tableAnalysis->setItem(0, 0, new QTableWidgetItem("Length"));
    ui->tableAnalysis->setItem(1, 0, new QTableWidgetItem("Angle"));
    ui->tableAnalysis->setItem(2, 0, new QTableWidgetItem("Root-Mean-Square"));

    // Initialize comboBoxAction from database
    populateControlStateComboBoxes();
    
    // Set default value if there are items
    if (ui->comboBoxAction->count() > 0) {
        ui->comboBoxAction->setCurrentIndex(0);
    }
    
    // Connect comboBoxAction signal to slot
    connect(ui->comboBoxAction, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::on_comboBoxAction_currentIndexChanged);
    
    // Connect targetvalueSpinBox signal to control search slot
    connect(ui->targetvalueSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MainWindow::onControlSearchCriteriaChanged);
    
    // Also connect comboBoxAction to control search for immediate feedback
    connect(ui->comboBoxAction, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onControlSearchCriteriaChanged);
    
    // Initialize the map widget with the default value
    on_comboBoxAction_currentIndexChanged(0);
    
    ui->mapLiveWidget->setMousePressEventHandler([this](QMouseEvent *e) {
        ui->mapLiveWidget->mousePressEventPaths(e);
    });
    ui->mapLiveWidget->setMouseReleaseEventHandler([this](QMouseEvent *e) {
        ui->mapLiveWidget->mouseReleaseEventPaths(e);
    });
    ui->mapLiveWidget->setWheelEventHandler([this](QWheelEvent *e) {
        ui->mapLiveWidget->wheelEventPaths(e);
    });

    ui->mapWidgetFields->setMousePressEventHandler([this](QMouseEvent *e) {
        ui->mapWidgetFields->mousePressEventFields(e);
    });
    ui->mapWidgetFields->setMouseReleaseEventHandler([this](QMouseEvent *e) {
        ui->mapWidgetFields->mousePressEventFields(e);
    });

    ui->mapWidgetFields->setWheelEventHandler([this](QWheelEvent *e) {
        ui->mapWidgetFields->wheelEventFields(e);
    });

    ui->mapWidgetAnalysis->setMousePressEventHandler([this](QMouseEvent *e) {
        ui->mapWidgetAnalysis->mousePressEventAnalysis(e);
    });
    ui->mapWidgetAnalysis->setMouseReleaseEventHandler([this](QMouseEvent *e) {
        ui->mapWidgetAnalysis->mousePressEventFields(e);
    });

    ui->mapWidgetAnalysis->setWheelEventHandler([this](QWheelEvent *e) {
        ui->mapWidgetAnalysis->wheelEventFields(e);
    });

    // Initialize the result map widget
    ui->mapWidgetAnalysisResult->setMousePressEventHandler([this](QMouseEvent *e) {
        ui->mapWidgetAnalysisResult->mousePressEventAnalysis(e);
    });

    // Spinbox is now in the UI file, no need for programmatic creation
    // Connect the UI spinbox signal
    connect(ui->spinBoxResultPath, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &MainWindow::onResultPathChanged);

    // Connect analysis table selection
    connect(ui->tableAnalysis, &QTableWidget::itemSelectionChanged, 
            this, &MainWindow::onAnalysisSelectionChanged);

    ui->mapWidgetAnalysisResult->setMouseReleaseEventHandler([this](QMouseEvent *e) {
        ui->mapWidgetAnalysisResult->mousePressEventFields(e);
    });

    ui->mapWidgetAnalysisResult->setWheelEventHandler([this](QWheelEvent *e) {
        ui->mapWidgetAnalysisResult->wheelEventFields(e);
    });

    ui->mapWidgetAnalysisResult->setAnalysisActive(true);
    
    // Debug: Check result map widget initialization
    qDebug() << "DEBUG: Result map widget initialized:" << ui->mapWidgetAnalysisResult;
    qDebug() << "DEBUG: Result map widget paths:" << ui->mapWidgetAnalysisResult->mPaths->size();
    qDebug() << "DEBUG: Result map widget fields:" << ui->mapWidgetAnalysisResult->getFieldNum();
    
    // Connect RangeSlider signals to slots
    connect(ui->rangeSlider, &RangeSlider::lowerValueChanged, this, &MainWindow::onRangeSliderLowerChanged);
    connect(ui->rangeSlider, &RangeSlider::upperValueChanged, this, &MainWindow::onRangeSliderUpperChanged);
    
    // Connect area loading button
    connect(ui->loadAreaButton, &QPushButton::clicked, this, &MainWindow::loadAreaFromXML);
    
    // Connect cut path button
    connect(ui->cutPathButton, &QPushButton::clicked, this, &MainWindow::cutPathByArea);

    ui->mapWidgetFields->setMainWindow(this);
    ui->mapWidgetFields->setBorderFocus(true);
    ui->mapWidgetAnalysis->setAnalysisActive(true);
    ui->mapWidgetAnalysisResult->setMainWindow(this);
    
    // Connect the pathsUpdated signal to refresh the graph
    connect(ui->mapWidgetAnalysisResult, &MapWidget::pathsUpdated, this, &MainWindow::updateCurrentAnalysis);

    // Note: listLogfilesView and fileModel have been replaced with combo box dropdowns in the UI
    // fileModel = new QStringListModel(this);
    // ui->listLogfilesView->setModel(fileModel);  // Link the model to the view
    // ui->listLogfilesView->installEventFilter(this);  // Install event filter for delete key

    mVersion = "0.8";
    mSupportedFirmwares.append(qMakePair(12, 3));
    mSupportedFirmwares.append(qMakePair(20, 1));
    mSupportedFirmwares.append(qMakePair(30, 1));
    mSupportedFirmwares.append(qMakePair(30, 2)); // Write-hängningen rättad, heartbeat i sekunder
    mSupportedFirmwares.append(qMakePair(30, 3)); // + autopiloten sätter körriktningen
    mSupportedFirmwares.append(qMakePair(30, 4)); // + VESC-ping och provsnurr (Robotstyrning)

    ui->mapStreamNmeaFollowBox->setChecked(true);

    qRegisterMetaType<LocPoint>("LocPoint");
    mTimer = new QTimer(this);
    mTimer->start(ui->pollIntervalBox->value());
    mHeartbeatTimer = new QTimer(this);
    mHeartbeatTimer->start(mHeartbeatMS);
    mStatusLabel = new QLabel(this);
    ui->statusBar->addPermanentWidget(mStatusLabel);
    mStatusInfoTime = 0;
    mActiveCarId = 0;
    mJoystickControlEnabled = false; // dosan aktiveras med knappen "Aktivera dosa"
    mPacketInterface = new PacketInterface(this);
    mSerialPort = new QSerialPort(this);
    mNetworkManager = new QNetworkAccessManager(this);
    mThrottle = 0.0;
    mSteering = 0.0;
    activeCarExists = false;
    mLastJoystickCount = 0;

//    static MainWindow *mThis=this;          // Just to be able to get the lambdas to work
#ifdef HAS_JOYSTICK
    // Connect joystick by default
    bool connectJs = connectJoystick();

    if (connectJs) {
        on_jsConnectButton_clicked();
    }
    
    // Set up joystick polling timer for hot-plugging support
    mJoystickPollTimer = new QTimer(this);
    connect(mJoystickPollTimer, &QTimer::timeout, this, &MainWindow::checkJoystickConnection);
    mJoystickPollTimer->start(5000); // Check every 5 seconds

    // Statusruta i sidopanelen (i det tomma området under anslutningsknapparna)
    {
        QGroupBox *statusBox = new QGroupBox("Status", this);
        QVBoxLayout *statusLayout = new QVBoxLayout(statusBox);
        statusLayout->setContentsMargins(6, 4, 6, 4);
        mStatusBoxLabel = new QLabel(statusBox);
        mStatusBoxLabel->setTextFormat(Qt::RichText);
        mStatusBoxLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        statusLayout->addWidget(mStatusBoxLabel);
        QPushButton *vehicleButton = new QPushButton("Batteri och fordon…", statusBox);
        vehicleButton->setToolTip("Batterityp, celler, kapacitet och största styrvinkel för maskinen du är ansluten till");
        connect(vehicleButton, &QPushButton::clicked, this, &MainWindow::openVehicleSettings);
        statusLayout->addWidget(vehicleButton, 0, Qt::AlignLeft);
        QPushButton *angleButton = new QPushButton("Kalibrera styrvinkel…", statusBox);
        angleButton->setToolTip("Vinkelgivaren: rakt fram, fullt höger och fullt vänster. Sparas i styrkortet.");
        connect(angleButton, &QPushButton::clicked, this, &MainWindow::calibrateSteeringAngle);
        statusLayout->addWidget(angleButton, 0, Qt::AlignLeft);
        int idx = ui->verticalLayout->indexOf(ui->verticalSpacer);
        ui->verticalLayout->insertWidget(idx < 0 ? 3 : idx, statusBox);

        QTimer *statusTimer = new QTimer(this);
        connect(statusTimer, &QTimer::timeout, this, &MainWindow::updateStatusBox);
        mRouterNet = new QNetworkAccessManager(this);
        QTimer *routerTimer = new QTimer(this);
        connect(routerTimer, &QTimer::timeout, this, &MainWindow::pollRouterSignal);
        routerTimer->start(5000);

        statusTimer->start(500); // Första uppdateringen först när konstruktorn är klar (mTcpClientMulti skapas senare)
        mStatusBoxLabel->setText("<b>Lösning:</b> –<br><b>Ping (bil):</b> –");
    }

    // Kartan börjar där man var senast (sparas när nollpunkten sätts eller en gård
    // väljs), i stället för den inbyggda startpunkten i Uppsala.
    {
        QSettings s("RControlStation", "karta");
        if (s.contains("senasteLat") && s.contains("senasteLon")) {
            double lat = s.value("senasteLat").toDouble();
            double lon = s.value("senasteLon").toDouble();
            ui->mapLiveWidget->setEnuRef(lat, lon, 0);
            ui->mapLiveWidget->moveView(0.0, 0.0);
            qDebug() << "Kartan startar på senaste position:" << lat << lon;
        }
    }

    // Redskapets mått (arbetsbredd = avstånd mellan körningarna i ZigZag, och GL-fill)
    // sparas så att de inte går tillbaka till standard vid varje start.
    // Standard: MacBot/MacTrac, aggregatet 2,4 m brett och 3,4 m långt.
    {
        QSettings s("RControlStation", "redskap");
        ui->boundsFillSpacingSpinBox->setValue(s.value("zigzagBredd", 2.4).toDouble());
        ui->implementWidthLineEdit->setText(s.value("bredd", "2.4").toString());
        ui->implementLengthLineEdit->setText(s.value("langd", "3.4").toString());
        connect(ui->boundsFillSpacingSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [](double v) { QSettings("RControlStation", "redskap").setValue("zigzagBredd", v); });
        connect(ui->implementWidthLineEdit, &QLineEdit::textChanged,
                this, [](const QString &t) { QSettings("RControlStation", "redskap").setValue("bredd", t); });
        connect(ui->implementLengthLineEdit, &QLineEdit::textChanged,
                this, [](const QString &t) { QSettings("RControlStation", "redskap").setValue("langd", t); });
    }

    // Styrkortet kan byta ENU-referens (t.ex. till basstationen när RTCM 1005 kommer),
    // så hämta den regelbundet så att kartan och bilen räknar från samma nollpunkt.
    QTimer *enuSyncTimer = new QTimer(this);
    connect(enuSyncTimer, &QTimer::timeout, this, [this]() {
        if (mTcpClientMulti->isAnyConnected() && !ui->mapStreamNmeaZeroEnuBox->isChecked()) {
            mPacketInterface->getEnuRef(mActiveCarId);
        }
    });
    enuSyncTimer->start(10000);

    // Skicka om aktiva reglagevärden, och nödstoppa om dosan försvinner
    mRcResendTimer = new QTimer(this);
    connect(mRcResendTimer, &QTimer::timeout, this, &MainWindow::rcResendTick);
    mRcResendTimer->start(100);
#endif

    checkboxdelegate=new CheckBoxDelegate(ui->fieldTable);

    mPing = new Ping(this);
    mNmea = new NmeaServer(this);
    mUdpSocket = new QUdpSocket(this);
    mTcpClientMulti = new TcpClientMulti(this);
    mUdpSocket->setSocketOption(QAbstractSocket::LowDelayOption, true);

    mKeyUp = false;
    mKeyDown = false;
    mKeyLeft = false;
    mKeyRight = false;

    ui->mapLiveWidget->setRoutePointSpeed(ui->mapRouteSpeedBox->value() / 3.6);
    ui->networkLoggerWidget->setMap(ui->mapLiveWidget);
    ui->networkInterface->setMap(ui->mapLiveWidget);
    ui->networkInterface->setPacketInterface(mPacketInterface);
    ui->networkInterface->setCars(&mCars);
    ui->moteWidget->setPacketInterface(mPacketInterface);
    ui->nComWidget->setMap(ui->mapLiveWidget);
    ui->baseStationWidget->setMap(ui->mapLiveWidget);

    connect(mTimer, SIGNAL(timeout()), this, SLOT(timerSlot()));
    connect(mSerialPort, SIGNAL(readyRead()),
            this, SLOT(serialDataAvailable()));
/*    connect(mSerialPort, SIGNAL(error(QSerialPort::SerialPortError)),
            this, SLOT(serialPortError(QSerialPort::SerialPortError)));*/
    connect(mHeartbeatTimer, SIGNAL(timeout()), this, SLOT(sendHeartbeat()));
    connect(mPacketInterface, SIGNAL(dataToSend(QByteArray&)),
            this, SLOT(packetDataToSend(QByteArray&)));
    connect(mPacketInterface, SIGNAL(stateReceived(quint8,CAR_STATE)),
            this, SLOT(stateReceived(quint8,CAR_STATE)));
    mVehicle = new VehicleData(this);
    connect(mPacketInterface, &PacketInterface::packetReceived, this,
            [this](quint8 id, CMD_PACKET cmd, const QByteArray &pkt) {
        mVehicle->packetReceived(id, (quint8)cmd, pkt);
        if ((quint8)cmd == 143 && pkt.size() >= 3) {
            mNodstoppSenast.start();
            nodstoppVisa((quint8)pkt[2]);
        }
    });

    // Stopp-knappen visar nödstoppet. Hörs inget från Car_Client på 3 s
    // (gammal Car_Client, inget nödstopp, frånkopplad) blir den vanlig igen.
    mStopIcon = ui->stopButton->icon();
    nodstoppVisa(-1);
    QTimer *nodstoppTimer = new QTimer(this);
    connect(nodstoppTimer, &QTimer::timeout, this, [this]() {
        if (mNodstoppLage >= 0 && (!mNodstoppSenast.isValid() || mNodstoppSenast.elapsed() > 3000)) {
            nodstoppVisa(-1);
        }
    });
    nodstoppTimer->start(1000);
    connect(ui->mapLiveWidget, SIGNAL(posSet(quint8,LocPoint)),
            this, SLOT(mapPosSet(quint8,LocPoint)));
    connect(mPacketInterface, SIGNAL(ackReceived(quint8,CMD_PACKET,QString)),
            this, SLOT(ackReceived(quint8,CMD_PACKET,QString)));
    connect(ui->rtcmWidget, SIGNAL(rtcmReceived(QByteArray)),
            this, SLOT(rtcmReceived(QByteArray)));
    connect(ui->baseStationWidget, SIGNAL(rtcmOut(QByteArray)),
            this, SLOT(rtcmReceived(QByteArray)));
    connect(ui->rtcmWidget, SIGNAL(refPosGet()), this, SLOT(rtcmRefPosGet()));
    connect(mPing, SIGNAL(pingRx(int,QString)), this, SLOT(pingRx(int,QString)));
    connect(mPing, SIGNAL(pingError(QString,QString)), this, SLOT(pingError(QString,QString)));
    connect(mPacketInterface, SIGNAL(enuRefReceived(quint8,double,double,double)),
            this, SLOT(enuRx(quint8,double,double,double)));
    connect(mNmea, SIGNAL(clientGgaRx(int,NmeaServer::nmea_gga_info_t)),
            this, SLOT(nmeaGgaRx(int,NmeaServer::nmea_gga_info_t)));
    connect(mNmea, &NmeaServer::clientLineRx, this, [this](QByteArray line) {
        if (line.contains("GGA")) {
            for(QList<CarInterface*>::Iterator it_car = mCars.begin(); it_car != mCars.end(); it_car++) {
                CarInterface *car = *it_car;
                if (car->getId() == mActiveCarId || car->getId() == 0) {
                    car->nmeaReceived(mActiveCarId, line);
                }
            }
        }
    });
    connect(ui->mapLiveWidget, SIGNAL(routePointAdded(LocPoint)),
            this, SLOT(routePointAdded(LocPoint)));
    connect(ui->mapLiveWidget, SIGNAL(infoTraceChanged(int)),
            this, SLOT(infoTraceChanged(int)));
    connect(ui->mapLiveWidget, SIGNAL(activePointChanged(LocPoint)),
            this, SLOT(activePointChanged(LocPoint)));

    connect(ui->buttonGenerate, &QPushButton::clicked, this, &MainWindow::onGeneratePathButtonClicked);
    connect(ui->buttonGenerateLine, &QPushButton::clicked, this, &MainWindow::onGenerateLineButtonClicked);
    connect(ui->buttonShowShapefile, &QPushButton::clicked, this, &MainWindow::onShowShapefile);
    // Connect the button's clicked signal to a lambda that opens a file dialog
    connect(ui->pushButton_load_shapefile, &QPushButton::clicked, this, &MainWindow::onLoadShapefile);
    // Note: pushButtonLoadLogFile and listLogfilesView have been replaced with combo box dropdowns
    // connect(ui->pushButtonLoadLogFile, &QPushButton::clicked, this, &MainWindow::onLoadLogfile);
    connect(ui->buttonAppendRoute, &QPushButton::clicked, this, &MainWindow::onAppendButtonClicked);
    connect(ui->buttonPrependRoute, &QPushButton::clicked, this, &MainWindow::onPrependButtonClicked);
    connect(ui->buttonTransform, &QPushButton::clicked, this, &MainWindow::onTransformButtonClicked);
    connect(ui->buttonCut, &QPushButton::clicked, this, &MainWindow::onCutButtonClicked);
    // connect(ui->listLogfilesView, &QListView::clicked, this, &MainWindow::on_listLogFilesView_clicked);
    connect(ui->mapCarBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onMapCarBoxChanged);
    connect(ui->refreshMachinesButton, &QPushButton::clicked, this, &MainWindow::on_refreshMachinesButton_clicked);
    connect(ui->connectSelectedButton, &QPushButton::clicked, this, &MainWindow::on_tcpConnectButton_clicked);
    connect(ui->connectSelectedTextButton, &QPushButton::clicked, this, &MainWindow::on_tcpConnectButton_clicked);
    connect(ui->disconnectSelectedButton, &QPushButton::clicked, this, &MainWindow::on_disconnectSelectedButton_clicked);
    connect(ui->pushButtonAddMachine, &QPushButton::clicked, this, &MainWindow::onAddMachineButtonClicked);

    connect(ui->actionAboutQt, SIGNAL(triggered(bool)),
            qApp, SLOT(aboutQt()));

    connect(mTcpClientMulti, &TcpClientMulti::packetRx, [this](QByteArray data) {
        mPacketInterface->processPacket((unsigned char*)data.data(), data.size());
    });

    connect(mTcpClientMulti, &TcpClientMulti::stateChanged, [this](QString msg, QString ip, bool isError) {
        showStatusInfo(msg, !isError);

        if (isError) {
            qWarning() << "TCP Error:" << msg << ", ip: " << ip;
            QString all=msg  + ", ip: " + ip;
            // Bara en felruta i taget, och den blockerar inte. Tidigare öppnade varje
            // fel en ny modal QMessageBox, och vid död anslutning kom tusentals fel
            // per minut vilket fick hela skrivbordet att frysa.
            static QPointer<QMessageBox> tcpErrorBox;
            if (tcpErrorBox) {
                tcpErrorBox->setText(all);
            } else {
                tcpErrorBox = new QMessageBox(QMessageBox::Warning, "TCP Error", all, QMessageBox::Ok, this);
                tcpErrorBox->setAttribute(Qt::WA_DeleteOnClose);
                tcpErrorBox->setModal(false);
                tcpErrorBox->show();
            }
            /*
            for (int i = 0; i < ui->carsWidget->count(); ++i) {
                if (ui->carsWidget->tabText(i) == ip) {
                    ui->carsWidget->removeTab(i);
                    break; // Exit the loop once the tab is found and removed
                }
            }*/
        }
    });
    QObject::connect(&serialReader, &ArduinoReader::signalLost, [this]() {
            on_stopButton_clicked();
            qWarning() << "Signal lost! Did not receive '1'.";
    });

#ifdef HAS_SIM_SCEN
    mSimScen = new PageSimScen;
    ui->mainTabWidget->addTab(mSimScen, QIcon(":/models/Icons/Sedan-96.png"), "");
    ui->mainTabWidget->setTabToolTip(ui->mainTabWidget->count() - 1,
                                     "Simulation Scenarios");
#endif

    // Add Actions Management tab
    actionManager = new ActionManager(this);
    actionManager->setupUi(db.getDb());
    ui->mainTabWidget->addTab(actionManager, QIcon(":/models/Icons/Waypoint Map-96.png"), "");
    ui->mainTabWidget->setTabToolTip(ui->mainTabWidget->count() - 1,
                                     "Actions Management");

    ui->mainTabWidget->removeTab(8);
    ui->mainTabWidget->removeTab(7);
    ui->mainTabWidget->removeTab(6);
    ui->mainTabWidget->removeTab(5);
    ui->mainTabWidget->removeTab(4);
    // Öppna på kartan (i .ui var Farm vald). Valt via fliken, inte index, eftersom
    // flikar läggs till och tas bort ovan.
    ui->mainTabWidget->setCurrentWidget(ui->tabMap);
    if (!QSqlDatabase::drivers().contains("QSQLITE"))
            QMessageBox::critical(
                this,
                "Unable to load database",
                "This program needs the SQLITE driver");
    farmsModel = setupFarmsTable(ui->farmTable);
    if (!farmsModel) {
        qDebug() << "ERROR: farmsModel setup failed!";
    }
    fieldsModel = setupFieldsTable(ui->fieldTable);
    if (!fieldsModel) {
        qDebug() << "ERROR: fieldsModel setup failed!";
    }
    modelPath=setupPathTable(ui->pathTable,"paths");
    
    // Setup log tab
    setupLogTab();
    
    // Setup model for tableViewMachines
    machinesModel = new QStandardItemModel(this);
    machinesModel->setHorizontalHeaderLabels(QStringList() << "Name" << "IP Address" << "Vehicle Type");
    ui->tableViewMachines->setModel(machinesModel);
    ui->tableViewMachines->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableViewMachines->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableViewMachines->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableViewMachines->installEventFilter(this);
    connect(ui->tableViewMachines, &QTableView::clicked, this, &MainWindow::onMachinesTableClicked);
    connect(ui->tableViewMachines, &QTableView::doubleClicked, this, &MainWindow::onMachinesTableDoubleClicked);
    connect(ui->tableViewMachines->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onMachinesSelectionChanged);

    // Configure and connect the sidebar machinesTable (QTableWidget)
    ui->machinesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->machinesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->machinesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(ui->machinesTable, &QTableWidget::clicked, this, &MainWindow::onMachinesTableClicked);
    connect(ui->machinesTable, &QTableWidget::doubleClicked, this, &MainWindow::onMachinesTableDoubleClicked);
    connect(ui->machinesTable->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onMachinesSelectionChanged);

    // Make Follow Car and Follow NMEA GPS mutually exclusive to prevent map view conflicts
    connect(ui->mapFollowBox, &QCheckBox::clicked, this, [this](bool checked) {
        if (checked) {
            ui->mapStreamNmeaFollowBox->setChecked(false);
        }
    });
    connect(ui->mapStreamNmeaFollowBox, &QCheckBox::clicked, this, [this](bool checked) {
        if (checked) {
            ui->mapFollowBox->setChecked(false);
        }
    });

    // Setup model for vehicle types
    vehicleTypesModel = new QStandardItemModel(this);
    ui->comboBoxVehicleType->setModel(vehicleTypesModel);
    
    // Setup delegate for vehicle type column in tableViewMachines
    vehicleTypeDelegate = new VehicleTypeDelegate(vehicleTypesModel, this);
    ui->tableViewMachines->setItemDelegateForColumn(2, vehicleTypeDelegate); // Vehicle Type is column 2
    


    // Connect the signal from the first table view to a custom slot
    QObject::connect(ui->farmTable->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onSelectedFarm);
    QObject::connect(ui->fieldTable->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onSelectedField);

//    ui->fieldTable->installEventFilter(&filterFieldtable);

    MapWidget *mapFields=ui->mapWidgetFields;

    /*
    QObject::connect(&filterFieldtable, &FocusEventFilter::focusGained, [mapFields]() {
        qDebug() << "Focus gained Fields";
    });
*/
    /* ui->pathTable->installEventFilter(&filterPathtable);

  QObject::connect(&filterPathtable, &FocusEventFilter::focusGained, [mapFields]() {
      mapFields->setBorderFocus(false);
      mapFields->update();
      qDebug() << "Focus gained Paths";
  });
*/
    connect(ui->pushButton_farm, &QPushButton::released, this, &MainWindow::handleAddFarmButton);
    connect(ui->pushButton_field, &QPushButton::released, this, &MainWindow::handleAddFieldButton);

//    ui->farmTable->setFocus();
    ui->farmTable->installEventFilter(this);    
    ui->fieldTable->installEventFilter(this);

    //    ui->farmTable->selectRow(0);

    if (ui->fieldTable->model()->rowCount()>0)
    {
        ui->fieldTable->selectRow(0);
    }
    if (ui->pathTable->model()->rowCount()>0)
    {
        ui->pathTable->selectRow(0);
    }

    qApp->installEventFilter(this);
    
    // Connect checkBoxActiveKB to joystick control
    ui->checkBoxActiveKB->setChecked(false);
    connect(ui->checkBoxActiveKB, &QCheckBox::stateChanged, this, [this](int state) {
        setJoystickControlEnabled(state == Qt::Checked);
        updateGamepadButton();
    });

    // Joystick och Control bredvid varandra, med knappen "Aktivera dosa" ovanför.
    {
        QGroupBox *jsBox = qobject_cast<QGroupBox*>(ui->jsConnectButton->parentWidget());
        QGroupBox *ctrlBox = ui->groupBox_3;
        QBoxLayout *parentLayout = ui->verticalLayout;   // vänstra spalten
        if (jsBox && parentLayout && parentLayout->indexOf(jsBox) >= 0 && parentLayout->indexOf(ctrlBox) >= 0) {
            const int idx = parentLayout->indexOf(jsBox);
            parentLayout->removeWidget(jsBox);
            parentLayout->removeWidget(ctrlBox);

            // Joystick-rutan: knapparna under varandra.
            if (QGridLayout *g = qobject_cast<QGridLayout*>(jsBox->layout())) {
                g->removeWidget(ui->jsConnectButton);
                g->removeWidget(ui->jsDisconnectButton);
                g->removeWidget(ui->jsConnectedLabel);
                g->addWidget(ui->jsConnectButton, 0, 0);
                g->addWidget(ui->jsDisconnectButton, 1, 0);
                g->addWidget(ui->jsConnectedLabel, 2, 0);
                g->setRowStretch(3, 1);
            }

            QWidget *box = new QWidget(this);
            QVBoxLayout *v = new QVBoxLayout(box);
            v->setContentsMargins(0, 0, 0, 0);
            mGamepadButton = new QPushButton(box);
            mGamepadButton->setMinimumHeight(36);
            connect(mGamepadButton, &QPushButton::clicked, this, [this]() {
                if (mJoystickControlEnabled) {
                    lockGamepad("Dosan låst");
                } else {
                    activateGamepad();
                }
            });
            v->addWidget(mGamepadButton);
            QHBoxLayout *h = new QHBoxLayout();
            h->addWidget(jsBox, 0, Qt::AlignTop);
            h->addWidget(ctrlBox, 1, Qt::AlignTop);
            v->addLayout(h);
            parentLayout->insertWidget(idx, box);
        } else {
            qWarning() << "Hittade inte Joystick- och Control-rutorna; knappen Aktivera dosa saknas.";
        }
        updateGamepadButton();
    }

    // Populate controller combo boxes from database
    populateControllerComboBoxes();
    
    // Initial refresh of machines data
    fetchMachinesData();
    fetchVehicleTypes(); // This will call fetchAllMachinesData() when done
    
    // Initial refresh of unconnected fields data - call after UI is fully initialized
    QTimer::singleShot(100, this, [this]() { fetchUnconnectedFieldsData(); });
}


MainWindow::~MainWindow()
{
    // Remove all vehicles before this window is destroyed to not get segfaults
    // in their destructors.
    while (mCars.size() > 0) {
        QWidget *w = ui->carsWidget->currentWidget();

        if (dynamic_cast<CarInterface*>(w) != NULL) {
            CarInterface *car = (CarInterface*)w;

            ui->carsWidget->removeTab(ui->carsWidget->currentIndex());
            mCars.removeOne(car);
            delete car;
        }
    }

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    // Stoppa allt som anropar SDL innan SDL stängs, annars kan en timer
    // använda en redan frigjord controller (segfault i libSDL2 vid avslut).
    if (mRcResendTimer) {
        mRcResendTimer->stop();
    }
    if (mTimer) {
        mTimer->stop();
    }
    mLastActionValues.clear();
    if (mController) {
        SDL_GameControllerClose(mController);
        mController = nullptr;
    }
    SDL_Quit();
#endif
    
#ifdef HAS_JOYSTICK
    // Clean up joystick poll timer
    if (mJoystickPollTimer) {
        mJoystickPollTimer->stop();
        delete mJoystickPollTimer;
    }
#endif
    
    delete ui;
}

void MainWindow::checkForUpdates()
{
    // Get current version from PROJECT_VERSION
    QVersionNumber currentVersion = QVersionNumber::fromString(PROJECT_VERSION);
    
    // Disable the action while checking
    m_checkForUpdatesAction->setEnabled(false);
    m_checkForUpdatesAction->setText(tr("Checking for updates..."));
    
    // Start the version check
    // You should replace these with your actual GitHub repo details
    m_versionChecker->checkForUpdates(currentVersion, "SLU-Agrosystem", "RControlStation");
}

void MainWindow::showAbout()
{
    QString aboutText = tr("<h2>RControlStation</h2>") +
            tr("<p>Version: %1</p>").arg(PROJECT_VERSION) +
            tr("<p>Remote Control Station for vehicle monitoring and control.</p>") +
            tr("<p>Developed by RISE and SLU.</p>") +
            tr("<p>© 2024 RISE Research Institutes of Sweden</p>");
    
    QMessageBox::about(this, tr("About RControlStation"), aboutText);
}

void MainWindow::onUpdateCheckFinished(bool updateAvailable, const QVersionNumber &latestVersion, 
                                         const QString &downloadUrl, const QString &releaseNotes)
{
    m_checkForUpdatesAction->setEnabled(true);
    m_checkForUpdatesAction->setText(tr("Check for Updates"));
    
    if (updateAvailable) {
        QString message = tr("<p>A new version (%1) is available!</p>").arg(latestVersion.toString());
        
        if (!releaseNotes.isEmpty()) {
            // Truncate release notes if too long
            QString notes = releaseNotes;
            if (notes.length() > 500) {
                notes = notes.left(500) + "...";
            }
            message += tr("<p><b>Release Notes:</b></p><p>%1</p>").arg(notes.replace("\n", "<br>"));
        }
        
        QMessageBox *msgBox = new QMessageBox(this);
        msgBox->setWindowTitle(tr("Update Available"));
        msgBox->setTextFormat(Qt::RichText);
        msgBox->setText(message);
        
        if (!downloadUrl.isEmpty()) {
            QPushButton *downloadButton = msgBox->addButton(tr("Download Now"), QMessageBox::ActionRole);
            connect(downloadButton, &QPushButton::clicked, [downloadUrl]() {
                QDesktopServices::openUrl(QUrl(downloadUrl));
            });
        }
        
        msgBox->addButton(QMessageBox::Ok);
        msgBox->exec();
    } else {
        QMessageBox::information(this, tr("Up to Date"), 
                                tr("You are running the latest version (%1).").arg(PROJECT_VERSION));
    }
}

void MainWindow::onUpdateCheckError(const QString &errorMessage)
{
    m_checkForUpdatesAction->setEnabled(true);
    m_checkForUpdatesAction->setText(tr("Check for Updates"));
    
    QMessageBox::warning(this, tr("Update Check Failed"), 
                        tr("Could not check for updates: %1").arg(errorMessage));
}

bool MainWindow::eventFilter(QObject *object, QEvent *e)
{
    if (not(object->objectName()==""))
    {
   //     qDebug() << "DEBUG: Event filter called for:" << object->objectName() << "event type:" << e->type();
    }

    // Emergency stop on escape
    if (e->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(e);
        if (keyEvent->key() == Qt::Key_Escape) {
            on_stopButton_clicked();
            return true;
        }
        if ((object == ui->farmTable || object == ui->farmTable->viewport()) && ui->farmTable->hasFocus())
        {
            qDebug() << "in farmtable";
            switch (keyEvent->key())
            {
            case Qt::Key_Delete: {
                QModelIndexList selectedRows = ui->farmTable->selectionModel()->selectedRows();
                if (!selectedRows.isEmpty()) {
                    QModelIndex selectedIndex = selectedRows.first();
                    int row = selectedIndex.row();
                    
                    // Get the farm ID from the first column (stored as user data)
                    QStandardItem* nameItem = farmsModel->item(row, 0);
                    if (nameItem) {
                        QString farmId = nameItem->data(Qt::UserRole).toString();
                        
                        if (!farmId.isEmpty()) {
                            qDebug() << "Deleting farm with ID:" << farmId;
                            deleteFarmFromServer(farmId.toInt());
                        } else {
                            qDebug() << "No farm ID found for selected row";
                        }
                    } else {
                        qDebug() << "No farm name item found for row:" << row;
                    }
                }
                return true; // Event is handled, don't propagate further
            }
            case Qt::Key_F5:
                qDebug() << "Refreshing farms table";
                fetchAllFarmsData();
                return true;
            case Qt::Key_R:
                if (keyEvent->modifiers() & Qt::ControlModifier) {
                    qDebug() << "Refreshing farms table";
                    fetchAllFarmsData();
                    return true;
                }
                break;
            }
        }
        if (object == ui->fieldTable)
        {
            qDebug() << "in fieldtable";
            QItemSelectionModel* selectionModel = ui->fieldTable->selectionModel();
            QModelIndexList selection = selectionModel->selectedRows();
            QStandardItemModel* model = qobject_cast<QStandardItemModel*>(ui->fieldTable->model());
            switch (keyEvent->key())
            {
            case Qt::Key_Delete:
                // Multiple rows can be selected
                if (!selection.isEmpty()) {
                    for(int i=0; i< selection.count(); i++)
                    {
                        QModelIndex index = selection.at(i);
                        qDebug() << "Deleting field at row:" << index.row();
                        
                        // Get the field ID from the name column (stored as user data)
                        QStandardItem* nameItem = fieldsModel->item(index.row(), 1); // Name is column 1
                        if (nameItem) {
                            QString fieldId = nameItem->data(Qt::UserRole).toString();
                            
                            if (!fieldId.isEmpty()) {
                                qDebug() << "Deleting field with ID:" << fieldId;
                                deleteFieldFromServer(fieldId.toInt());
                            } else {
                                qDebug() << "No field ID found for row:" << index.row();
                            }
                        } else {
                            qDebug() << "No field name item found for row:" << index.row();
                        }
                    }
                }
                return true;
                break;
            case Qt::Key_Return:
                QByteArray byteArray;
                QXmlStreamWriter stream(&byteArray);
                ui->mapWidgetFields->saveXMLCurrentRoute(&stream);
                QString xmlString = QString::fromUtf8(byteArray);
                QModelIndexList selectedIndexes = selectionModel->selectedIndexes();
                if (selectedIndexes.isEmpty()) {
                    qDebug() << "No selection";
                    break;
                }

                int row = selectedIndexes.first().row();
                qDebug() << "Selected Row: " << row;

                // Retrieve the data of the selected row if needed
                if (model && row >= 0 && row < model->rowCount()) {
                    // For QStandardItemModel, we need to get the item and set its data
                    QStandardItem* fileItem = model->item(row, 3); // File is column 3
                    if (fileItem) {
                        fileItem->setText(xmlString);
                        qDebug() << "Updated field file data for row:" << row;
                    } else {
                        qDebug() << "ERROR: Could not get file item for row:" << row;
                    }
                } else {
                    qDebug() << "ERROR: Invalid model or row index:" << row;
                }
                ui->fieldTable->show();
                return true;
            }
        }
        if ((object == ui->tableViewMachines || object == ui->tableViewMachines->viewport()) && ui->tableViewMachines->hasFocus())
        {
            if (e->type() == QEvent::KeyPress) {
                QKeyEvent *keyEvent = static_cast<QKeyEvent *>(e);
                switch (keyEvent->key())
                {
                case Qt::Key_Delete: {
                    // Get the selected row
                    QModelIndexList selectedIndexes = ui->tableViewMachines->selectionModel()->selectedRows();
                    if (!selectedIndexes.isEmpty()) {
                        QModelIndex selectedIndex = selectedIndexes.first();
                        int row = selectedIndex.row();
                        
                        // Get the machine ID from the first column (stored as user data)
                        QStandardItem* nameItem = machinesModel->item(row, 0);
                        if (nameItem) {
                            QString machineId = nameItem->data(Qt::UserRole).toString();
                            
                            if (!machineId.isEmpty()) {
                                qDebug() << "Deleting machine with ID:" << machineId;
                                
                                // Send DELETE request to remove the machine
                                QUrl url(getServerBaseUrl() + "/remove_machine");
                                QUrlQuery query;
                                query.addQueryItem("id", machineId);
                                url.setQuery(query);
                                
                                QNetworkRequest request(url);
                                request.setTransferTimeout(10000);
                                
                                QNetworkReply* reply = mNetworkManager->get(request);
                                
                                connect(reply, &QNetworkReply::finished, this, [this, reply, row]() {
                                    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                                    qDebug() << "Remove machine HTTP Status Code:" << statusCode;
                                    
                                    if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
                                        qDebug() << "Machine removed successfully";
                                        // Remove the row from the model
                                        machinesModel->removeRow(row);
                                    } else {
                                        qDebug() << "Error removing machine:" << reply->errorString();
                                        qDebug() << "HTTP Status Code:" << statusCode;
                                    }
                                    reply->deleteLater();
                                });
                            } else {
                                qDebug() << "No machine ID found for selected row";
                            }
                        } else {
                            qDebug() << "No machine name item found for row:" << row;
                        }
                    }
                    return true; // Event is handled, don't propagate further
                }
                case Qt::Key_F5:
                    qDebug() << "Refreshing machines table";
                    fetchAllMachinesData(0);
                    return true;
                case Qt::Key_R:
                    if (keyEvent->modifiers() & Qt::ControlModifier) {
                        qDebug() << "Refreshing machines table";
                        fetchAllMachinesData(0);
                        return true;
                    }
                    break;
                }
            }
        }
    }
    #ifdef HAS_JOYSTICK
    if (JSconnected()) {
        return false;
    }
    #endif

    if (ui->throttleOffButton->isChecked()) {
        return false;
    }

    if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(e);
        bool isPress = e->type() == QEvent::KeyPress;

        // Note: listLogfilesView has been replaced with combo box dropdowns
        // Handle delete key for listLogfilesView - remove selected items immediately
        // if (object == ui->listLogfilesView && e->type() == QEvent::KeyPress && keyEvent->key() == Qt::Key_Delete) {
        //     QItemSelectionModel* selectionModel = ui->listLogfilesView->selectionModel();
        //     QModelIndexList selection = selectionModel->selectedIndexes();
        //     
        //     // Remove selected items from the end to avoid index shifting
        //     for(int i = selection.count() - 1; i >= 0; i--) {
        //         QModelIndex index = selection.at(i);
        //         if (index.isValid()) {
        //             fileList.removeAt(index.row());
        //         }
        //     }
        //     fileModel->setStringList(fileList);  // Update the model
        //     return true;  // Event handled
        // }

        switch(keyEvent->key()) {
            case Qt::Key_Up:
            case Qt::Key_Down:
            case Qt::Key_Left:
            case Qt::Key_Right:
                break;

            default:
                return false;
        }

        switch(keyEvent->key()) {
            case Qt::Key_Up: mKeyUp = isPress; break;
            case Qt::Key_Down: mKeyDown = isPress; break;
            case Qt::Key_Left: mKeyLeft = isPress; break;
            case Qt::Key_Right: mKeyRight = isPress; break;

            default:
                break;
        }

        // Return true to not pass the key event on
        return true;
    }

    return false;
}

void MainWindow::updateFarms()
{
    fetchAllFarmsData();
}

void MainWindow::populateControllerComboBoxes()
{
    // Query to get all actions from database
    QSqlQuery query("SELECT id, name FROM controls", db.getDb());
    
    if (!query.exec()) {
        qDebug() << "Query error:" << query.lastError().text();
        return;
    }

    // Clear existing items first
    ui->comboBox_controller1->clear();
    ui->comboBox_controller3->clear();
    ui->comboBox_controller5->clear();
    ui->comboBox_controller6->clear();
    ui->comboBox_controller7->clear();
    ui->comboBox_controller8->clear();

    // Add "-- None --" option first (index 0)
    ui->comboBox_controller1->addItem("-- None --", QVariant()); // NULL/QVariant() for empty
    ui->comboBox_controller3->addItem("-- None --", QVariant());
    ui->comboBox_controller5->addItem("-- None --", QVariant());
    ui->comboBox_controller6->addItem("-- None --", QVariant());
    ui->comboBox_controller7->addItem("-- None --", QVariant());
    ui->comboBox_controller8->addItem("-- None --", QVariant());

    // Populate each combo box with database data
    while (query.next()) {
        int id = query.value(0).toInt();
        QString name = query.value(1).toString();
        qDebug() << "id: " << id << ", name: " << name;
        
        // Add item to all combo boxes with name as display text and id as user data
        ui->comboBox_controller1->addItem(name, QVariant(id));
        ui->comboBox_controller3->addItem(name, QVariant(id));
        ui->comboBox_controller5->addItem(name, QVariant(id));
        ui->comboBox_controller6->addItem(name, QVariant(id));
        ui->comboBox_controller7->addItem(name, QVariant(id));
        ui->comboBox_controller8->addItem(name, QVariant(id));
    }
    
    // Connect combo box change signals to save function
    connect(ui->comboBox_controller1, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &MainWindow::saveControllerSettingsToDatabase);
/*    connect(ui->comboBox_controller2, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::saveControllerSettingsToDatabase);*/
    connect(ui->comboBox_controller3, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &MainWindow::saveControllerSettingsToDatabase);
 /*   connect(ui->comboBox_controller4, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::saveControllerSettingsToDatabase);*/
    connect(ui->comboBox_controller5, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &MainWindow::saveControllerSettingsToDatabase);
    connect(ui->comboBox_controller6, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &MainWindow::saveControllerSettingsToDatabase);
    connect(ui->comboBox_controller7, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::saveControllerSettingsToDatabase);
    connect(ui->comboBox_controller8, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::saveControllerSettingsToDatabase);

    // Load controller settings from database
    loadControllerSettingsFromDatabase();
}

QList<QPair<int, QString>> MainWindow::getMotorTypesFromDatabase()
{
    qDebug() << "in getMotorTypesFromDatabase()";
    QList<QPair<int, QString>> motorTypes;
    
    // Query to get all motor types from database
    QSqlQuery query("SELECT id, name FROM motor_types", db.getDb());
    
    if (!query.exec()) {
        qDebug() << "Motor types query error:" << query.lastError().text();
        return motorTypes;
    }

    // Populate the list with database data
    while (query.next()) {
        int id = query.value(0).toInt();
        QString name = query.value(1).toString();
        motorTypes.append(QPair<int, QString>(id, name));
    }

    qDebug() << "Loaded" << motorTypes.size() << "motor types from database";
    return motorTypes;
}

QList<QPair<int, QString>> MainWindow::getActionsFromDatabase()
{
    qDebug() << "in getActionsFromDatabase()";
    QList<QPair<int, QString>> actions;

    // Query to get all actions from database
    QSqlQuery query("SELECT id, name FROM controls", db.getDb());

    if (!query.exec()) {
        qDebug() << "Actions query error:" << query.lastError().text();
        return actions;
    }

    // Populate the list with database data
    while (query.next()) {
        int id = query.value(0).toInt();
        QString name = query.value(1).toString();
        actions.append(QPair<int, QString>(id, name));
    }

    qDebug() << "Loaded" << actions.size() << "actions from database";
    return actions;
}

QList<std::tuple<int, QString, QString>> MainWindow::getActionsWithColoursFromDatabase()
{
    qDebug() << "in getActionsWithColoursFromDatabase()";
    QList<std::tuple<int, QString, QString>> actions;

    // Query to get all actions with their colours from database
    QSqlQuery query("SELECT id, name, colour FROM controls", db.getDb());

    if (!query.exec()) {
        qDebug() << "Actions with colours query error:" << query.lastError().text();
        return actions;
    }

    // Populate the list with database data
    while (query.next()) {
        int id = query.value(0).toInt();
        QString name = query.value(1).toString();
        QString colour = query.value(2).toString();
        
        // If colour is empty, generate one
        if (colour.isEmpty()) {
            colour = generateColourForAction(id);
            
            // Update the database with the generated colour
            QSqlQuery updateQuery(db.getDb());
            updateQuery.prepare("UPDATE actions SET colour = ? WHERE id = ?");
            updateQuery.addBindValue(colour);
            updateQuery.addBindValue(id);
            updateQuery.exec();
        }
        
        actions.append(std::make_tuple(id, name, colour));
    }

    return actions;
}

QString MainWindow::generateColourForAction(int actionId)
{
    // Generate a distinct colour using HSV colour space for better visual distinction
    // Use golden ratio conjugation for good distribution
    int hue = (actionId * 137) % 360;  // Golden ratio (≈137.5°) for good distribution
    
    // Convert HSV to RGB and then to hex string
    QColor colour;
    colour.setHsv(hue, 200, 255);  // Saturate and brighten for vibrant colours
    
    return colour.name(QColor::HexRgb);  // Returns "#RRGGBB" format
}

QList<QPair<int, QString>> MainWindow::getModesFromDatabase()
{
    qDebug() << "in getModesFromDatabase()";
    QList<QPair<int, QString>> modes;

    // Query to get all motor types from database
    QSqlQuery query("SELECT id, name FROM modes", db.getDb());

    if (!query.exec()) {
        qDebug() << "Motor types query error:" << query.lastError().text();
        return modes;
    }

    // Populate the list with database data
    while (query.next()) {
        int id = query.value(0).toInt();
        QString name = query.value(1).toString();
        modes.append(QPair<int, QString>(id, name));
    }

    qDebug() << "Loaded" << modes.size() << "modes from database";
    return modes;
}


void MainWindow::loadControllerSettingsFromDatabase()
{
    qDebug() << "Loading controller settings from database";
    
    // Query to get all controller settings from database
    QSqlQuery query("SELECT id, action FROM controllers", db.getDb());
    
    if (!query.exec()) {
        qDebug() << "Controller query error:" << query.lastError().text();
        return;
    }
    
    // Create a map to store controller settings
    QMap<int, int> controllerSettings;
    mCachedControllerActions.clear();
    while (query.next()) {
        int controllerId = query.value(0).toInt();
        int actionId = query.value(1).toInt();
        controllerSettings[controllerId] = actionId;
        mCachedControllerActions[controllerId] = actionId;
    }
    
    // Set the combo box values based on database settings
    for (int i = 1; i <= 8; i++) {
        QComboBox* comboBox = nullptr;
        switch (i) {
            case 1: comboBox = ui->comboBox_controller1; break;
  //          case 2: comboBox = ui->comboBox_controller2; break;
            case 3: comboBox = ui->comboBox_controller3; break;
  //          case 4: comboBox = ui->comboBox_controller4; break;
            case 5: comboBox = ui->comboBox_controller5; break;
            case 6: comboBox = ui->comboBox_controller6; break;
            case 7: comboBox = ui->comboBox_controller7; break;
            case 8: comboBox = ui->comboBox_controller8; break;
        }
        
        if (comboBox) {
            if (controllerSettings.contains(i)) {
                int actionId = controllerSettings[i];
                // Find the index of the item with matching user data (actionId)
                for (int j = 0; j < comboBox->count(); j++) {
                    if (comboBox->itemData(j).toInt() == actionId) {
                        comboBox->setCurrentIndex(j);
                        qDebug() << "Controller" << i << "set to action" << actionId;
                        break;
                    }
                }
            } else {
                // No action configured for this controller - show nothing
                comboBox->setCurrentIndex(-1);
                qDebug() << "Controller" << i << "has no configured action - showing empty selection";
            }
        }
    }
}

void MainWindow::saveControllerSettingsToDatabase()
{
    qDebug() << "Saving controller settings to database";
    
    // Get database connection
    QSqlDatabase database = db.getDb();
    QSqlQuery query(database);
    
    // Prepare insert/replace query
    if (!query.prepare("INSERT OR REPLACE INTO controllers (id, action) VALUES (:id, :action)")) {
        qDebug() << "Prepare error:" << query.lastError().text();
        return;
    }
    
    // Save settings for each controller combo box
    for (int i = 1; i <= 8; i++) {
        QComboBox* comboBox = nullptr;
        switch (i) {
            case 1: comboBox = ui->comboBox_controller1; break;
//            case 2: comboBox = ui->comboBox_controller2; break;
            case 3: comboBox = ui->comboBox_controller3; break;
//            case 4: comboBox = ui->comboBox_controller4; break;
            case 5: comboBox = ui->comboBox_controller5; break;
            case 6: comboBox = ui->comboBox_controller6; break;
            case 7: comboBox = ui->comboBox_controller7; break;
            case 8: comboBox = ui->comboBox_controller8; break;
        }
        
        if (comboBox) {
            if (comboBox->currentIndex() > 0) { // Index 0 is "-- None --", so > 0 is a valid action!
                // Valid selection - save to database
                int actionId = comboBox->itemData(comboBox->currentIndex()).toInt();
                query.bindValue(":id", i);
                query.bindValue(":action", actionId);
                
                mCachedControllerActions[i] = actionId;

                if (!query.exec()) {
                    qDebug() << "Failed to save controller" << i << ":" << query.lastError().text();
                } else {
                    qDebug() << "Saved controller" << i << "-> action" << actionId;
                }
            } else {
                // No selection (index 0 or -1) - remove from database
                mCachedControllerActions.remove(i);
                
                QSqlQuery deleteQuery(db.getDb());
                if (!deleteQuery.prepare("DELETE FROM controllers WHERE id = :id")) {
                    qDebug() << "Failed to prepare delete for controller" << i;
                } else {
                    deleteQuery.bindValue(":id", i);
                    if (!deleteQuery.exec()) {
                        qDebug() << "Failed to delete controller" << i << ":" << deleteQuery.lastError().text();
                    } else {
                        qDebug() << "Cleared controller" << i << "from database (no action selected)";
                    }
                }
            }
        }
    }
}

static QString getControllerAsciiBar(float value, bool is_axis) {
    int width = 10;
    if (is_axis) {
        // Range -1.0 to 1.0
        int pos = (int)((value + 1.0f) / 2.0f * width);
        if (pos < 0) pos = 0;
        if (pos >= width) pos = width - 1;
        QString bar = "[";
        for (int i = 0; i < width; i++) {
            if (i == pos) bar += "O";
            else bar += "-";
        }
        bar += "]";
        return bar;
    } else {
        // Range 0.0 to 1.0
        int pos = (int)(value * width);
        if (pos < 0) pos = 0;
        if (pos > width) pos = width;
        QString bar = "[";
        for (int i = 0; i < width; i++) {
            if (i < pos) bar += "#";
            else bar += " ";
        }
        bar += "]";
        return bar;
    }
}

void MainWindow::handleControllerInput(int controllerNumber, float value)
{
    // Validate controller number (1-8)
    if (controllerNumber < 1 || controllerNumber > 8) {
        qDebug() << "Invalid controller number:" << controllerNumber << "- must be 1-8";
        return;
    }
    
    // Dödzon: små värden runt mitten blir exakt 0, så att ett släppt reglage alltid
    // hamnar på 0 hos roboten (annars kunde 0.015 bli kvar och roboten krypa).
    if (qAbs(value) < 0.02f) {
        value = 0.0f;
    }
    if (value != 0.0f && mJoystickControlEnabled) {
        mGamepadActivity.restart();
    }

    // Jitterfilter: skicka inte små ändringar, men en övergång till 0 skickas alltid.
    // Ett stillastående reglage skickas om av rcResendTick() så att VESC:ernas
    // timeout inte stänger av motorn medan spaken hålls stilla.
    if (mCachedControllerValues.contains(controllerNumber)) {
        float last = mCachedControllerValues[controllerNumber];
        bool unchanged = (value == 0.0f) ? (last == 0.0f) : (qAbs(last - value) < 0.02f);
        if (unchanged) {
            return;
        }
    }
    mCachedControllerValues[controllerNumber] = value;
    
    // qDebug() << "Controller idr:" << controllerNumber;

    // Update the label text with dynamic ASCII feedback bar in real-time
    switch (controllerNumber) {
        case 1:
            ui->label_9->setText(QString("Left flip %1").arg(getControllerAsciiBar(value, true))); // L1 +1, L2 -1
            break;
        case 3:
            ui->label_10->setText(QString("Right flip %1").arg(getControllerAsciiBar(value, true))); // R1 +1, R2 -1
            break;
        case 5:
            ui->label_4->setText(QString("Left control stick - up/down %1").arg(getControllerAsciiBar(value, true)));
            break;
        case 6:
            ui->label_5->setText(QString("Left control stick - right/left %1").arg(getControllerAsciiBar(value, true)));
            break;
        case 7:
            ui->label_7->setText(QString("Right control stick - up/down %1").arg(getControllerAsciiBar(value, true)));
            break;
        case 8:
            ui->label_8->setText(QString("Right control stick - right/left %1").arg(getControllerAsciiBar(value, true)));
            break;
    }

    // Get the action ID for this controller from the cache (avoids 100+ SQL queries/second which hangs the UI thread!)
    if (mCachedControllerActions.contains(controllerNumber)) {
        // Controller has a configured action
        int actionId = mCachedControllerActions[controllerNumber];
        
        // Call controllerAction with:
        // - car: mActiveCarId (the currently selected active car)
        // - iAction: the action ID from the database
        // - value: the input value passed to this function
        // Max-rutan gäller driften. Styrningen (Steering Control, aktivitet 11) har
        // en mindre motor och går alltid på STYRNING_MAX, oavsett Max-rutan.
        const int STYRNING_AKTIVITET = 11;
        const float STYRNING_MAX = 0.8f;
        float scaled = value * (actionId == STYRNING_AKTIVITET ? STYRNING_MAX : (float)ui->throttleMaxBox->value());
        if (scaled == 0.0f) {
            mLastActionValues.remove(actionId);
        } else {
            mLastActionValues[actionId] = scaled;
        }
        controllerAction(mActiveCarId, actionId, scaled);
    } else {
        // No action configured for this controller
        qDebug() << "Controller" << controllerNumber << "has no configured action - ignoring input";
        // Optionally: could play a sound or show notification that controller is unconfigured
    }
}

// QSqlRelationalTableModel* MainWindow::setupFarmTable(QTableView* uiFarmTable,QString sqlTablename) // Replaced with webserver version
// {
//     // Create the data model:
//     QSqlRelationalTableModel* model = new QSqlRelationalTableModel(uiFarmTable);
//     model->setEditStrategy(QSqlTableModel::OnFieldChange);
//     model->setTable(sqlTablename);
// 
//     // Set the localized header captions:
//     model->setHeaderData(model->fieldIndex("name"), Qt::Horizontal, tr("Name"));
//     model->setHeaderData(model->fieldIndex("longitude"), Qt::Horizontal, tr("Longitude"));
//     model->setHeaderData(model->fieldIndex("latitude"), Qt::Horizontal, tr("Latitude"));
//     model->setHeaderData(model->fieldIndex("ip"), Qt::Horizontal, tr("ip"));
//     model->setHeaderData(model->fieldIndex("port"), Qt::Horizontal, tr("port"));
//     model->setHeaderData(model->fieldIndex("NTRIP"), Qt::Horizontal, tr("NTRIP"));
//     model->setHeaderData(model->fieldIndex("user"), Qt::Horizontal, tr("user"));
//     model->setHeaderData(model->fieldIndex("password"), Qt::Horizontal, tr("password"));
// 
// 
//     // Populate the model:
//     if (!model->select()) {
//         db.showError(model->lastError());
//         return model;
//     }
// 
//     // Set the model and hide the ID column:
//     uiFarmTable->setModel(model);
//     //ui.locationTable->setItemDelegate(new BookDelegate(ui.locationTable));
//     uiFarmTable->setColumnHidden(model->fieldIndex("id"), true);
// //    uiFarmTable->setColumnHidden(model->fieldIndex("longitude"), true);
// //    uiFarmTable->setColumnHidden(model->fieldIndex("latitude"), true);
//     uiFarmTable->setColumnHidden(model->fieldIndex("ip"), true);
//     uiFarmTable->setColumnHidden(model->fieldIndex("port"), true);
//     uiFarmTable->setColumnHidden(model->fieldIndex("NTRIP"), true);
//     uiFarmTable->setColumnHidden(model->fieldIndex("user"), true);
//     uiFarmTable->setColumnHidden(model->fieldIndex("password"), true);
//     uiFarmTable->setColumnHidden(model->fieldIndex("stream"), true);
//     uiFarmTable->setColumnHidden(model->fieldIndex("autoconnect"), true);
//     uiFarmTable->setSelectionMode(QAbstractItemView::SingleSelection);
//     uiFarmTable->setSelectionBehavior(QAbstractItemView::SelectRows);
//     uiFarmTable->setCurrentIndex(model->index(0, 0));
//     uiFarmTable->resizeColumnsToContents();
//     uiFarmTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
//     uiFarmTable->setItemDelegateForColumn(2, new PrecisionDelegate(2, 14, uiFarmTable));
//     uiFarmTable->setItemDelegateForColumn(3, new PrecisionDelegate(3, 14, uiFarmTable));
// 
//     QDataWidgetMapper *mapperFarm = new QDataWidgetMapper(this);
//     mapperFarm->setModel(model);
//     mapperFarm->addMapping(this->findChild<QLineEdit*>("locationnameEdit"), model->fieldIndex("name"));
//     connect(uiFarmTable->selectionModel(),&QItemSelectionModel::currentRowChanged,mapperFarm,&QDataWidgetMapper::setCurrentModelIndex);
//     return model;
// }

QStandardItemModel* MainWindow::setupFarmsTable(QTableView* uiFarmTable)
{
    if (!uiFarmTable) {
        qDebug() << "ERROR: uiFarmTable is null in setupFarmsTable!";
        return nullptr;
    }
    
    // Create the data model:
    QStandardItemModel* model = new QStandardItemModel(this);
    
    // Set the header labels - same as the database columns we want to show
    model->setHorizontalHeaderLabels(QStringList() << "Name" << "Longitude" << "Latitude" << "ip" << "port" << "NTRIP" << "user" << "password" << "stream" << "autoconnect");
    
    // Set the model to the table view
    uiFarmTable->setModel(model);
    
    // Set the member variable to the local model
    farmsModel = model;
    
    // Hide the columns that should not be visible
    uiFarmTable->setColumnHidden(3, true); // ip
    uiFarmTable->setColumnHidden(4, true); // port
    uiFarmTable->setColumnHidden(5, true); // NTRIP
    uiFarmTable->setColumnHidden(6, true); // user
    uiFarmTable->setColumnHidden(7, true); // password
    uiFarmTable->setColumnHidden(8, true); // stream
    uiFarmTable->setColumnHidden(9, true); // autoconnect
    
    // Set table properties
    uiFarmTable->setSelectionMode(QAbstractItemView::SingleSelection);
    uiFarmTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    uiFarmTable->resizeColumnsToContents();
    uiFarmTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    
    // Set precision delegates for coordinate columns (same as original)
    uiFarmTable->setItemDelegateForColumn(1, new PrecisionDelegate(2, 14, uiFarmTable)); // Longitude
    uiFarmTable->setItemDelegateForColumn(2, new PrecisionDelegate(3, 14, uiFarmTable)); // Latitude
    
    // Set up data widget mapper for the name edit field
    QDataWidgetMapper *mapperFarm = new QDataWidgetMapper(this);
    mapperFarm->setModel(model);
    mapperFarm->addMapping(this->findChild<QLineEdit*>("locationnameEdit"), 0); // Map to Name column (index 0)
    connect(uiFarmTable->selectionModel(), &QItemSelectionModel::currentRowChanged, mapperFarm, &QDataWidgetMapper::setCurrentModelIndex);
    
    // Fetch initial data from webserver
    fetchAllFarmsData();
    
    return model;
}

// QSqlRelationalTableModel* MainWindow::setupFieldTable(QTableView* uiFieldTable,QString sqlTablename) // Replaced with webserver version
// {
//     // Create the data model:
//     QSqlRelationalTableModel *model = new QSqlRelationalTableModel(uiFieldTable);
//     model->setEditStrategy(QSqlTableModel::OnFieldChange);
//     model->setTable(sqlTablename);
// 
//     // Remember the indexes of the columns:
//     int locationIdx = model->fieldIndex("location");
// 
//     // Set the relations to the other database tables:
//     model->setRelation(locationIdx, QSqlRelation("locations", "id", "name"));
// 
//     // Set the localized header captions:
//     model->setHeaderData(locationIdx, Qt::Horizontal, tr("Location"));
//     model->setHeaderData(model->fieldIndex("name"),  Qt::Horizontal, tr("Field name"));
//     model->setHeaderData(model->fieldIndex("fenced"),  Qt::Horizontal, tr("Is fenced?"));
//     model->setHeaderData(model->fieldIndex("storedinfile"),  Qt::Horizontal, tr("File"));
//     model->setHeaderData(model->fieldIndex("location"),  Qt::Horizontal, tr("Location"));
//     model->setHeaderData(model->fieldIndex("id"),  Qt::Horizontal, tr("Id"));
// 
//     // Set the model and hide the ID column:
//     uiFieldTable->setModel(model);
//     uiFieldTable->setColumnHidden(model->fieldIndex("id"), true);
//     uiFieldTable->setColumnHidden(model->fieldIndex("location"), true);
// 
//     uiFieldTable->setSelectionMode(QAbstractItemView::SingleSelection);
//     uiFieldTable->setSelectionBehavior(QAbstractItemView::SelectRows);
//     uiFieldTable->setItemDelegateForColumn(1, checkboxdelegate);
//     uiFieldTable->resizeColumnsToContents();
//     uiFieldTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
// //    uiFieldTable->horizontalHeader()->setVisible(false); // Hide vertical headers
// 
//     QDataWidgetMapper *mapperField = new QDataWidgetMapper(this);
//     mapperField->setModel(model);
//     mapperField->addMapping(this->findChild<QLineEdit*>("fieldnameEdit"), model->fieldIndex("name"));
//     mapperField->addMapping(this->findChild<QLineEdit*>("filenameEdit"), model->fieldIndex("storedinfile"));
//     connect(uiFieldTable->selectionModel(),&QItemSelectionModel::currentRowChanged,mapperField,&QDataWidgetMapper::setCurrentModelIndex);
// 
//     return model;
// }

QStandardItemModel* MainWindow::setupFieldsTable(QTableView* uiFieldTable)
{
    if (!uiFieldTable) {
        qDebug() << "ERROR: uiFieldTable is null in setupFieldsTable!";
        return nullptr;
    }
    
    // Create the data model:
    QStandardItemModel* model = new QStandardItemModel(this);
    
    // Set the header labels - same as the database columns we want to show
    // Based on the original setupFieldTable: Location, Field name, Is fenced?, File, Id
    model->setHorizontalHeaderLabels(QStringList() << "Location" << "Field name" << "Is fenced?" << "File" << "Id");
    
    // Set the model to the table view
    uiFieldTable->setModel(model);
    
    // Hide the columns that should not be visible (same as in the original setupFieldTable)
    uiFieldTable->setColumnHidden(0, true); // Location (hidden as it was in the original)
    uiFieldTable->setColumnHidden(4, true); // Id (hidden as it was in the original)
    
    // Set table properties
    uiFieldTable->setSelectionMode(QAbstractItemView::SingleSelection);
    uiFieldTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    uiFieldTable->resizeColumnsToContents();
    uiFieldTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    
    // Set checkbox delegate for the "Is fenced?" column (column 2)
    uiFieldTable->setItemDelegateForColumn(2, checkboxdelegate);
    
    // Set up data widget mapper for the field edit fields
    QDataWidgetMapper *mapperField = new QDataWidgetMapper(this);
    mapperField->setModel(model);
    mapperField->addMapping(this->findChild<QLineEdit*>("fieldnameEdit"), 1); // Map to Field name column (index 1)
    mapperField->addMapping(this->findChild<QLineEdit*>("filenameEdit"), 3); // Map to File column (index 3)
    connect(uiFieldTable->selectionModel(), &QItemSelectionModel::currentRowChanged, mapperField, &QDataWidgetMapper::setCurrentModelIndex);
    
    // Set the member variable to the local model
    fieldsModel = model;
    
    // Connect dataChanged signal to handle field name changes
    connect(fieldsModel, &QStandardItemModel::dataChanged, this, &MainWindow::onFieldDataChanged);
    
    // Also connect to the fieldnameEdit widget's editingFinished signal
    // This handles the case when editing through the widget mapper
    QLineEdit* fieldnameEdit = this->findChild<QLineEdit*>("fieldnameEdit");
    if (fieldnameEdit) {
        qDebug() << "Found fieldnameEdit widget, connecting editingFinished signal";
        connect(fieldnameEdit, &QLineEdit::editingFinished, this, [this]() {
            qDebug() << "fieldnameEdit editingFinished signal received";
            // Get the current row from the selection
            QModelIndex currentIndex = ui->fieldTable->selectionModel()->currentIndex();
            if (currentIndex.isValid()) {
                qDebug() << "Current field row:" << currentIndex.row();
                // Trigger the data changed handler manually
                onFieldDataChanged(currentIndex, currentIndex, {Qt::EditRole});
            } else {
                qDebug() << "WARNING: No valid current index when fieldnameEdit editingFinished";
            }
        });
    } else {
        qDebug() << "WARNING: fieldnameEdit widget not found!";
    }
    
    return model;
}

void MainWindow::setupLogTab()
{
    qDebug() << "Setting up log tab...";
    
    // Initialize models for log tab dropdowns
    logFarmsModel = new QStandardItemModel(this);
    logFieldsModel = new QStandardItemModel(this);
    logPathsModel = new QStandardItemModel(this);
    logLogsModel = new QStandardItemModel(this);
    
    // Set up combo boxes
    ui->comboBoxLogFarm->setModel(logFarmsModel);
    ui->comboBoxLogField->setModel(logFieldsModel);
    ui->comboBoxLogPath->setModel(logPathsModel);
    ui->comboBoxLog->setModel(logLogsModel);
    
    // Connect signals
    connect(ui->comboBoxLogFarm, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onFarmSelectedForLog);
    connect(ui->comboBoxLogField, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onFieldSelectedForLog);
    connect(ui->comboBoxLogPath, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onPathSelectedForLog);
    connect(ui->comboBoxLog, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onLogSelectedForLog);
    connect(ui->pushButtonLoadLog, &QPushButton::clicked, this, &MainWindow::onLoadLogButtonClicked);
    
    // Fetch initial data
    fetchAllFarmsForLog();
    
    qDebug() << "Log tab setup complete";
}

// QSqlRelationalTableModel* MainWindow::setupPathTable(QTableView* uiPathTable,QString sqlTablename) // Replaced with webserver version
// {
//     QSqlRelationalTableModel *model= new QSqlRelationalTableModel(uiPathTable);
//     model->setEditStrategy(QSqlTableModel::OnFieldChange);
//     model->setTable(sqlTablename);
// 
//     // Remember the indexes of the columns:
//     int fieldIdx = model->fieldIndex("field");
// 
//     // Set the relations to the other database tables:
//     model->setRelation(fieldIdx, QSqlRelation("fields", "id", "name"));
// 
//     // Set the localized header captions:
//     model->setHeaderData(model->fieldIndex("name"), Qt::Horizontal, tr("Name"));
//     //    model->setHeaderData(model->fieldIndex("xml"), Qt::Horizontal, tr("XML"));
// 
//     // Populate the model:
//     if (!model->select()) {
//         db.showError(model->lastError());
//         return model;
//     }
// 
//     // Set the model and hide the ID column:
//     uiPathTable->setModel(model);
//     uiPathTable->setColumnHidden(model->fieldIndex("iPath"), true);
//     uiPathTable->setColumnHidden(model->fieldIndex("storedinfile"), true);
//     uiPathTable->setColumnHidden(model->fieldIndex("field"), true);
//     uiPathTable->setColumnHidden(model->fieldIndex("fields_name_2"), true);
//     uiPathTable->setSelectionMode(QAbstractItemView::SingleSelection);
//     uiPathTable->setCurrentIndex(model->index(0, 0));
//     uiPathTable->resizeColumnsToContents();
//     uiPathTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
//     uiPathTable->horizontalHeader()->setVisible(false); // Hide vertical headers
//     return model;
// }

QStandardItemModel* MainWindow::setupPathTable(QTableView* uiPathTable,QString sqlTablename)
{
    if (!uiPathTable) {
        qDebug() << "ERROR: uiPathTable is null in setupPathTable!";
        return nullptr;
    }
    
    // Create the data model:
    QStandardItemModel* model = new QStandardItemModel(this);
    
    // Set the header labels - based on the original database columns
    // Original: iPath, name, xml, storedinfile, field, fields_name_2
    // We'll show: Name (and store ID as user data)
    model->setHorizontalHeaderLabels(QStringList() << "Name" << "Id" << "Field");
    
    // Set the model to the table view
    uiPathTable->setModel(model);
    
    // Hide the columns that should not be visible
    uiPathTable->setColumnHidden(1, true); // Id (hidden)
    uiPathTable->setColumnHidden(2, true); // Field (hidden)
    
    // Set table properties
    uiPathTable->setSelectionMode(QAbstractItemView::SingleSelection);
    uiPathTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    uiPathTable->resizeColumnsToContents();
    uiPathTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    uiPathTable->horizontalHeader()->setVisible(false); // Hide vertical headers
    
    // Set the member variable to the local model
    modelPath = model;
    
    return model;
}

void MainWindow::onSelectedFarm(const QModelIndex& current, const QModelIndex& previous)
{
    int row = current.row();
    
    // Get the farm ID from the first column (stored as user data)
    QStandardItem* nameItem = farmsModel->item(row, 0);
    int id = -1;
    if (nameItem) {
        QString farmId = nameItem->data(Qt::UserRole).toString();
        id = farmId.toInt();
    }

    QString lon = farmsModel->data(farmsModel->index(row, 1)).toString();
    QString lat = farmsModel->data(farmsModel->index(row, 2)).toString();
    QString usr = farmsModel->data(farmsModel->index(row, 6)).toString();
    QString pwd = farmsModel->data(farmsModel->index(row, 7)).toString();

    mPacketInterface->sendSetUserCmd(ui->mapCarBox->value(),usr);
    mPacketInterface->sendSetPwdCmd(ui->mapCarBox->value(),pwd);

//    qDebug() << "Vehicle: " << ui->mapCarBox->value();

    double llh[3];
    llh[0]=lat.toDouble();
    llh[1]=lon.toDouble();
    llh[2]=0;
    ui->mapLiveWidget->setEnuRef(llh[0],llh[1],0);
    ui->mapWidgetFields->setEnuRef(llh[0],llh[1],0);
    ui->mapWidgetAnalysis->setEnuRef(llh[0],llh[1],0);
    ui->mapWidgetAnalysisResult->setEnuRef(llh[0],llh[1],0);

    qDebug() << "lat: " << llh[0];
    qDebug() << "lon: " << llh[1];

    mPacketInterface->setEnuRef(ui->mapCarBox->value(), llh);
//    setEnuRef(quint8 id, double *llh, int retries)

    // Ett eget gårdsval gäller fram till nästa anslutning: avbryt den automatiska
    // nollpunkten om GPS-positionen inte hunnit komma, och kom ihåg platsen.
    mAutoEnuRefPending = false;
    saveMapPosition(llh[0], llh[1]);

    ui->mapWidgetFields->clearAllFields();
    ui->mapWidgetFields->clearAllPaths();
    qDebug() << "Fields (onSelectedFarm 1): " << ui->mapWidgetFields->mFields->size();
    // Get the selected value from the first table view
    // QVariant selectedValue = id; // No longer needed with webserver model

    // Fetch fields for the selected farm from webserver
    fetchAllFieldsData(id);
    /*
    //To make sure the path table view is empty until a field has been selected
    QString filter2 = QString("field = %1").arg(0);
    modelPath->setFilter(filter2);
    modelPath->select();
    */

    qDebug() << "AC";
    //            if (ui->fieldTable->model()->rowCount()>0)
    if (ui->mapWidgetFields->getFieldNum()>0)
    {
        std::array<double, 4> extremes_m=ui->mapWidgetFields->findExtremeValuesFieldBorders();
        double fieldareawidth_m=extremes_m[2]-extremes_m[0];
        double fieldareaheight_m=extremes_m[3]-extremes_m[1];
        double offsetx_m=(extremes_m[2]+extremes_m[0])/2;
        double offsety_m=(extremes_m[3]+extremes_m[1])/2;
        double scalex=0.5/(fieldareawidth_m);
        double scaley=0.5/(fieldareaheight_m);
        ui->mapWidgetFields->moveView(offsetx_m, offsety_m);
        ui->mapWidgetFields->setScaleFactor(std::min(scalex,scaley));
   //     //                ui->fieldTable->selectRow(0);
    } else
    {
        ui->mapWidgetFields->moveView(0, 0);
  //      ui->mapWidget->moveView(0, 0);
        // If no fields set a zoom matching a with of about 500 m -> scalefactor=0.5/500=0.001
        ui->mapWidgetFields->setScaleFactor(0.001);
    }
}

void MainWindow::onSelectedField(const QModelIndex& current, const QModelIndex& previous)
{
    onSelectedFieldGeneral(fieldsModel, modelPath, current, previous);
};

void MainWindow::onSelectedFieldGeneral(QStandardItemModel *model, QStandardItemModel *modelPth, const QModelIndex& current, const QModelIndex& previous)
{
    MapWidget* activeMap=ui->mapWidgetFields;
    int row = current.row();

    // Retrieve the data of the selected row if needed
    // Get the field ID from the name column (stored as user data)
    if (!model || row < 0 || row >= model->rowCount()) {
        qDebug() << "ERROR: Invalid model or row index in onSelectedFieldGeneral";
        return;
    }
    
    QStandardItem* nameItem = model->item(row, 1); // Name is column 1
    QString fieldName = model->data(model->index(row, 1)).toString();
    int id = -1;
    if (nameItem) {
        QString fieldId = nameItem->data(Qt::UserRole).toString();
        id = fieldId.toInt();
    }

    // Fetch the field XML to ensure it's loaded in the map widget
    if (id != -1) {
        fetchFieldXml(id, fieldName);
    }

    ui->mapWidgetFields->setFieldNow(row);

    QLabel *areaLabel=ui->label_area_ha;
    MapRoute border=activeMap->getField();
    double area=border.getArea();
    areaLabel->setText(QString::number(area));
    ui->mapLiveWidget->clearAllPaths(); // Drive widget
    activeMap->clearAllPaths();

    // Clear the path model and fetch paths for the selected field
    if (modelPth) {
        modelPth->removeRows(0, modelPth->rowCount());
        if (id != -1) {
            fetchAllPathsData(id);
        }
    }

    // Clear the existing items in the combobox
    QSpinBox *selectedRoute=ui->mapRouteBox;
    selectedRoute->clear();
    qDebug() << "G";
    activeMap->setBorderFocus(true);
    qDebug() << "H";

    //       mapFields->setRouteNow();   // Make sure that no route is set automatically (in order to make it easier to edit)
}

void MainWindow::onUnconnectedFieldsTableItemClicked(QTableWidgetItem *item)
{
    qDebug() << "onUnconnectedFieldsTableItemClicked: called";
    
    if (!item || !mUnconnectedFieldsTable || !mMapWidgetFileAdmin) {
        qDebug() << "ERROR: Invalid parameters in onUnconnectedFieldsTableItemClicked";
        return;
    }
    
    int row = item->row();
    QTableWidgetItem *filenameItem = mUnconnectedFieldsTable->item(row, 0);
    if (!filenameItem) {
        qDebug() << "ERROR: No filename item at row" << row;
        return;
    }
    
    QString filename = filenameItem->text();
    qDebug() << "File administration: Loading file:" << filename;
    
    // Clear the map first
    mMapWidgetFileAdmin->clearAllFields();
    mMapWidgetFileAdmin->clearAllPaths();
    mMapWidgetFileAdmin->update();
    
    // Load the file from the server via HTTP
    QUrl url(getServerBaseUrl() + QString("/field/%1").arg(filename));
    qDebug() << "Fetching field from URL:" << url.toString();
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    if (!reply) {
        qDebug() << "ERROR: Network request failed for" << url.toString();
        showStatusInfo("Network error: " + filename, false);
        return;
    }
    
    // Show loading state
    showStatusInfo("Loading border: " + filename, true);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, filename]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "HTTP Status Code (field):" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received XML data for field (size:" << xmlData.size() << ")";
            
            if (!xmlData.isEmpty()) {
                QXmlStreamReader xmlReader(xmlData);
                bool success = mMapWidgetFileAdmin->loadXMLRoute(&xmlReader, true); // true = isBorder
                
                if (success) {
                    qDebug() << "Successfully loaded border file:" << filename;
                    showStatusInfo("Loaded border: " + filename, true);
                    
                    // Center the view on the loaded data
                    if (mMapWidgetFileAdmin->getFieldNum() > 0) {
                        std::array<double, 4> extremes = mMapWidgetFileAdmin->findExtremeValuesFieldBorders();
                        double offsetx = (extremes[0] + extremes[1]) / 2.0;
                        double offsety = (extremes[2] + extremes[3]) / 2.0;
                        double scalex = 1.0 / (extremes[1] - extremes[0]);
                        double scaley = 1.0 / (extremes[3] - extremes[2]);
                        
                        mMapWidgetFileAdmin->moveView(offsetx, offsety);
                        mMapWidgetFileAdmin->setScaleFactor(std::min(scalex, scaley) * 0.9);
                        mMapWidgetFileAdmin->update();
                    }
                } else {
                    qDebug() << "Failed to load border file:" << filename << "XML error:" << xmlReader.errorString();
                    showStatusInfo("Failed to load border: " + filename, false);
                }
            } else {
                qDebug() << "Empty response for field:" << filename;
                showStatusInfo("Empty response: " + filename, false);
            }
        } else {
            QString errorMsg = reply->errorString();
            if (statusCode != 200 && statusCode > 0) {
                errorMsg = QString("HTTP %1").arg(statusCode);
            }
            qDebug() << "Error fetching field:" << filename << "-" << errorMsg;
            showStatusInfo("Error loading: " + filename + " (" + errorMsg + ")", false);
        }
        reply->deleteLater();
    });
}

void MainWindow::addCar(int id, QString name, bool pollData)
{
    CarInterface *car = new CarInterface(this);
    mCars.append(car);
    car->setID(id);
    ui->carsWidget->addTab(car, name);
    car->setMap(ui->mapLiveWidget);
    car->setPacketInterface(mPacketInterface);
    car->setPollData(pollData);
    connect(car, SIGNAL(showStatusInfo(QString,bool)), this, SLOT(showStatusInfo(QString,bool)));
    
    // Populate motor type combo boxes with data from database
    QList<QPair<int, QString>> motorTypes = getMotorTypesFromDatabase();
    car->populateMotorTypeComboBoxes(motorTypes);
    QList<QPair<int, QString>> actions = getActionsFromDatabase();
    car->populateActionsComboBoxes(actions);
    QList<QPair<int, QString>> modes = getModesFromDatabase();
    car->populateModesComboBoxes(modes);
}

void MainWindow::removeCars()
{
    int iCars=mCars.size();
    for (int i=0;i<iCars;i++)
    {
        int iCarId=mCars.at(i)->getId();
        ui->mapLiveWidget->removeCar(iCarId);
    }
    mCars.clear();
    ui->mapLiveWidget->update();
}

bool MainWindow::connectJoystick()
{
    bool connectJs = false;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    // Clean up any existing controller
    if (mController != nullptr) {
        SDL_GameControllerClose(mController);
        mController = nullptr;
    }
    
    // Initialize SDL if not already initialized
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
        if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
            qDebug() << "SDL_Init Error:" << SDL_GetError();
            return false;
        }
    }

    if (SDL_NumJoysticks() < 1) {
        qDebug() << "No joysticks connected!";
    } else {
        SDL_GameController* controller = SDL_GameControllerOpen(0);
        if (controller) {
            qDebug() << "Game controller connected:" << SDL_GameControllerName(controller);
            mController = controller;

            // Set up a timer to poll for gamepad events (make it static so it is only created once)
            static QTimer* timer = nullptr;
            if (!timer) {
                timer = new QTimer(this);
                connect(timer, &QTimer::timeout, this, &MainWindow::pollGamepad);
                timer->start(16); // Poll every 16ms
            }

            connectJs=true;
        } else {
            qDebug() << "Could not open game controller 0:" << SDL_GetError();
        }
    }
#else

#endif
    return connectJs;
}

void MainWindow::checkJoystickConnection()
{
#ifdef HAS_JOYSTICK
    #if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    int numJoysticks = SDL_NumJoysticks();

    // Dosan urkopplad och inkopplad mellan två kontroller: antalet är detsamma men
    // den gamla handtaget är dött. Öppna igen.
    if (numJoysticks > 0 && !gamepadAttached()) {
        mLastJoystickCount = -1;
    }

    // Check if joystick count changed
    if (numJoysticks != mLastJoystickCount) {
        mLastJoystickCount = numJoysticks;
        qDebug() << "Joystick count changed to:" << numJoysticks;
        
        if (numJoysticks > 0) {
            // Joysticks are available, try to connect
            qDebug() << "Joystick(s) available, attempting to connect...";
            bool connected = connectJoystick();
            if (connected) {
                qDebug() << "Joystick connected successfully!";
                on_jsConnectButton_clicked();
            }
        } else {
            // No joysticks available
            if (mController != nullptr) {
                qDebug() << "No joysticks detected, cleaning up controller.";
                SDL_GameControllerClose(mController);
                mController = nullptr;
            }
        }
    }
    #else
    // For Qt5 version, check if joystick is connected
    if (mJoystick && !mJoystick->isConnected()) {
        qDebug() << "Joystick disconnected, attempting to reconnect...";
        bool connected = connectJoystick();
        if (connected) {
            qDebug() << "Joystick reconnected successfully!";
            on_jsConnectButton_clicked();
        }
    }
    #endif
#endif
}

void MainWindow::addTcpConnection(QString ip, int port)
{
    mTcpClientMulti->addConnection(ip, port);
}

void MainWindow::setNetworkTcpEnabled(bool enabled, int port)
{
    ui->networkInterface->setTcpEnabled(enabled, port);
}

void MainWindow::setNetworkUdpEnabled(bool enabled, int port)
{
    ui->networkInterface->setUdpEnabled(enabled, port);
}

MapWidget *MainWindow::map()
{
    return ui->mapLiveWidget;
}

void MainWindow::serialDataAvailable()
{
    while (mSerialPort->bytesAvailable() > 0) {
        QByteArray data = mSerialPort->readAll();
        mPacketInterface->processData(data);
    }
}

void MainWindow::serialPortError(QSerialPort::SerialPortError error)
{
    QString message;
    switch (error) {
    case QSerialPort::NoError:
        break;

    default:
        message = "Serial port error: " + mSerialPort->errorString();
        break;
    }

    if(!message.isEmpty()) {
        showStatusInfo(message, false);

        if(mSerialPort->isOpen()) {
            mSerialPort->close();
        }
    }
}

void MainWindow::timerSlot()
{
#ifdef HAS_JOYSTICK
    // Nothing to qDebug, as the function is called frequently

        double js_mr_thr = 0.0;
        double js_mr_roll = 0.0;
        double js_mr_pitch = 0.0;
        double js_mr_yaw = 0.0;

        // Update throttle and steering from keys.
        if (JSconnected()) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
                // Read axis values using SDL_GameController
                mThrottle = -SDL_GameControllerGetAxis(mController, SDL_CONTROLLER_AXIS_LEFTY)*1.0/32768.0;
                mSteering = SDL_GameControllerGetAxis(mController, SDL_CONTROLLER_AXIS_RIGHTX)*1.0/32768.0;
                //#ifdef DEBUG_FUNCTIONS
                if (mThrottle !=0 || mSteering !=0)
                {
//sys                    qDebug() << QDateTime::currentDateTime().toString() << " - JOYSTICK (timerslot), mThrottle: " << mThrottle << ", mSteering: " << mSteering;
                }
                //  #endif

                js_mr_thr = -SDL_GameControllerGetAxis(mController, SDL_CONTROLLER_AXIS_LEFTY);
               // js_mr_roll = SDL_GameControllerGetAxis(mController, SDL_CONTROLLER_AXIS_RIGHTX);

#else
                mThrottle = -mJoystick->axisLeftY();
                mSteering = mJoystick->axisRightX();

                js_mr_thr = -mJoystick->axisLeftY();
                js_mr_roll = mJoystick->axisRightX();
#endif
                js_mr_pitch = 0; // mJoystick->getAxis(4) / 32768.0; // GL - not sure which controller this corresponds to
                js_mr_yaw = 0; //mJoystick->getAxis(0) / 32768.0;   // GL - not sure which controller this corresponds to


                deadband(mThrottle,0.1, 1.0);
                // qDebug () << "Throttle: " << mThrottle << ", Steering: " << mSteering;

                utility::truncate_number(&js_mr_thr, 0.0, 1.0);
                utility::truncate_number_abs(&js_mr_roll, 1.0);
                utility::truncate_number_abs(&js_mr_pitch, 1.0);
                utility::truncate_number_abs(&js_mr_yaw, 1.0);
            //mSteering /= 2.0;
        } else {
            if (mKeyUp) {
                stepTowards(mThrottle, 1.0, ui->throttleGainBox->value());
            } else if (mKeyDown) {
                stepTowards(mThrottle, -1.0, ui->throttleGainBox->value());
            } else {
                stepTowards(mThrottle, 0.0, ui->throttleGainBox->value());
            }

            if (mKeyRight) {
                stepTowards(mSteering, 1.0, ui->steeringGainBox->value());
            } else if (mKeyLeft) {
                stepTowards(mSteering, -1.0, ui->steeringGainBox->value());
            } else {
                stepTowards(mSteering, 0.0, ui->steeringGainBox->value());
            }
        }

        ui->throttleBar->setValue(mThrottle * 100.0);
        ui->steeringBar->setValue(mSteering * 100.0);
#endif

    // Notify about key events
    for(QList<CarInterface*>::Iterator it_car = mCars.begin();it_car < mCars.end();it_car++) {
        CarInterface *car = *it_car;
        car->setControlValues(mThrottle, mSteering, ui->throttleMaxBox->value(), ui->throttleCurrentButton->isChecked());
    }
    // Update status label
    if (mStatusInfoTime) {
        mStatusInfoTime--;
        if (!mStatusInfoTime) {
            mStatusLabel->setStyleSheet(qApp->styleSheet());
        }
    } else {
        if (mSerialPort->isOpen() || mPacketInterface->isUdpConnected() || mTcpClientMulti->isAnyConnected()) {
            mStatusLabel->setText("Connected");
        } else {
            mStatusLabel->setText("Not connected");
        }
    }

    // Poll data (one vehicle per timeslot)
    static int next_car = 0;
    int ind = 0;
    int largest = 0;
    bool polled = false;

    for(QList<CarInterface*>::Iterator it_car = mCars.begin();it_car < mCars.end();it_car++) {
        CarInterface *car = *it_car;
        if ((mSerialPort->isOpen() || mPacketInterface->isUdpConnected() || mTcpClientMulti->isAnyConnected()) &&
                car->pollData() && ind >= next_car && !polled) {
            mPacketInterface->getState(car->getId());
            next_car = ind + 1;
            polled = true;
        }

        if (car->pollData() && ind > largest) {
            largest = ind;
        }

        ind++;
    }

    if (next_car > largest) {
        next_car = 0;
    }

    // Update map settings
    if (ui->mapFollowBox->isChecked()) {
        ui->mapLiveWidget->setFollowCar(ui->mapCarBox->value());
    } else {
        ui->mapLiveWidget->setFollowCar(-1);
    }
    if (ui->mapTraceBox->isChecked()) {
        ui->mapLiveWidget->setTraceCar(ui->mapCarBox->value());
    } else {
        ui->mapLiveWidget->setTraceCar(-1);
    }
    if (ui->mapShowTextBox->isChecked()) {
        ui->mapLiveWidget->setDrawRouteText(true);
    } else {
        ui->mapLiveWidget->setDrawRouteText(false);
    }
    ui->mapLiveWidget->setSelectedCar(ui->mapCarBox->value());

    // Dosan låses av sig själv: anslutningen bruten, dosan urkopplad, 5 min utan spakrörelse.
    if (mJoystickControlEnabled) {
        if (!mTcpClientMulti->isAnyConnected()) {
            lockGamepad("Dosan låst: anslutningen till roboten bröts");
        }
#ifdef HAS_JOYSTICK
        else if (!JSconnected()) {
            lockGamepad("Dosan låst: dosan kopplades ur");
        }
#endif
        else if (mGamepadActivity.isValid() && mGamepadActivity.elapsed() > 5 * 60 * 1000) {
            lockGamepad("Dosan låst: ingen spakrörelse på 5 minuter");
        }
    }

    // Joystick connected
#ifdef HAS_JOYSTICK
    static bool jsWasconn = false;
    if (JSconnected() != jsWasconn) {
        jsWasconn = JSconnected();

        if (jsWasconn) {
            ui->jsConnectedLabel->setText("Connected");
        } else {
            ui->jsConnectedLabel->setText("Not connected");
            // STOP STOP STOP
            on_stopButton_clicked();
        }
    }
#endif

    // Update nmea stream connected label
    static bool wasNmeaStreamConnected = false;
    if (wasNmeaStreamConnected != mNmea->isClientTcpConnected()) {
        wasNmeaStreamConnected = mNmea->isClientTcpConnected();

        if (wasNmeaStreamConnected) {
            ui->mapStreamNmeaConnectedLabel->setText("Connected");
        } else {
            ui->mapStreamNmeaConnectedLabel->setText("Not connected");
        }
    }
}

#ifdef HAS_JOYSTICK_CHECK
bool MainWindow::JSconnected()
{
    #if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        return gamepadAttached();
    #else
    return mJoystick->isConnected();
    #endif
};
#endif
void MainWindow::updateStatusBox()
{
    if (!mStatusBoxLabel) {
        return;
    }

    QString gps;
    if (!mHaveGga || mGgaAge.elapsed() > 3000) {
        gps = "<b>Lösning:</b> <span style='color:#c00'>ingen GPS-data</span>";
    } else {
        QString sol;
        QString col;
        switch (mLastGga.fix_type) {
        case 4: sol = "RTK fix"; col = "#080"; break;
        case 5: sol = "RTK float"; col = "#c70"; break;
        case 2: sol = "DGPS"; col = "#c70"; break;
        case 1: sol = "SPP"; col = "#c00"; break;
        case 6: sol = "Dead reckoning"; col = "#c00"; break;
        default: sol = "Ingen fix"; col = "#c00"; break;
        }
        bool rtk = (mLastGga.fix_type == 4 || mLastGga.fix_type == 5);
        gps = QString("<b>Lösning:</b> <span style='color:%1'><b>%2</b></span><br>"
                      "<b>Satelliter:</b> %3 (RTK: %4)<br>"
                      "<b>Korr. ålder:</b> %5")
                .arg(col, sol)
                .arg(mLastGga.n_sat)
                .arg(rtk ? mLastGga.n_sat : 0)
                .arg(mLastGga.diff_age > 0.0 ? QString::number(mLastGga.diff_age, 'f', 1) + " s" : QString("–"));
    }

    QString link;
    if (!mTcpClientMulti->isAnyConnected()) {
        link = "<b>Ping (bil):</b> <span style='color:#c00'>ej ansluten</span>";
    } else if (!mStateAge.isValid() || mStateAge.elapsed() > 3000) {
        link = "<b>Ping (bil):</b> <span style='color:#c00'>inget svar</span>";
    } else {
        int rtt = mPacketInterface->lastStateRttMs();
        QString col = rtt < 0 ? "#000" : (rtt < 150 ? "#080" : (rtt < 500 ? "#c70" : "#c00"));
        link = QString("<b>Ping (bil):</b> <span style='color:%1'>%2</span>")
                .arg(col, rtt < 0 ? QString("–") : QString::number(rtt) + " ms");
    }

    // 4G/5G-raden visas bara när router_signal.py körs på Pi:n (valfritt tillägg)
    QString router;
    if (!mRouterLine.isEmpty() && mRouterAge.isValid() && mRouterAge.elapsed() < 15000) {
        router = "<br>" + mRouterLine;
    }

    // Fordonsdata (batteri, räckvidd, styrning, lutning, fel) och körlogg.
    QString vehicle;
    if (mVehicle) {
        bool connected = mTcpClientMulti->isAnyConnected();
        const QString machine = mTcpClientMulti->connectedIp();
        if (machine != mVehicleMachine) {
            mVehicleMachine = machine;
            mVehicle->setConfig(VehicleConfig::load(machine));
        }
        mVehicle->tick(connected);
        if (connected && mStateAge.isValid() && mStateAge.elapsed() < 3000) {
            if (mVehicle->wantAngleQuery()) {
                mPacketInterface->sendTerminalCmd(mVehicle->carId(), "vinkel");
            }
            if (mVehicle->wantVescQuery()) {
                QByteArray p;
                p.append((char)mVehicle->carId());
                p.append((char)140); // CMD_GET_VESC_STATUS
                mPacketInterface->sendPacket(p);
            }
        }
        QString h = mVehicle->html();
        if (!h.isEmpty()) {
            vehicle = "<hr>" + h;
        }
    }

    mStatusBoxLabel->setText(gps + "<br>" + link + router + vehicle);
}

// Hämtar routerns mottagning från router_signal.py på Pi:n (port 8310).
void MainWindow::pollRouterSignal()
{
    if (mConnectedIp.isEmpty() || !mTcpClientMulti->isAnyConnected()) {
        return;
    }

    QNetworkRequest req(QUrl(QString("http://%1:8310/").arg(mConnectedIp)));
    req.setTransferTimeout(3000);
    QNetworkReply *reply = mRouterNet->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            mRouterLine.clear(); // router_signal körs inte på Pi:n – visa ingen rad alls
            return;
        }

        QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (!o.value("ok").toBool()) {
            mRouterLine = QString("<b>4G/5G:</b> <span style='color:#c00'>%1</span>")
                    .arg(o.value("error").toString().toHtmlEscaped());
            mRouterAge.restart();
            return;
        }

        // Kvalitet efter RSRP (LTE/5G) om den finns, annars RSSI.
        QString quality, col, value;
        if (!o.value("rsrp").isNull()) {
            double rsrp = o.value("rsrp").toDouble();
            value = QString("RSRP %1 dBm").arg(rsrp, 0, 'f', 0);
            if (rsrp >= -80) { quality = "utmärkt"; col = "#080"; }
            else if (rsrp >= -90) { quality = "bra"; col = "#080"; }
            else if (rsrp >= -100) { quality = "ok"; col = "#c70"; }
            else { quality = "svag"; col = "#c00"; }
        } else {
            double rssi = o.value("rssi").toDouble();
            value = QString("RSSI %1 dBm").arg(rssi, 0, 'f', 0);
            if (rssi >= -65) { quality = "utmärkt"; col = "#080"; }
            else if (rssi >= -75) { quality = "bra"; col = "#080"; }
            else if (rssi >= -85) { quality = "ok"; col = "#c70"; }
            else { quality = "svag"; col = "#c00"; }
        }

        QString type = o.value("conntype").toString();
        QString op = o.value("operator").toString();
        QString sinr = o.value("sinr").isNull() ? QString()
                : QString(", SINR %1 dB").arg(o.value("sinr").toDouble(), 0, 'f', 0);

        mRouterLine = QString("<b>4G/5G:</b> %1%2<br>&nbsp;&nbsp;<span style='color:%3'><b>%4</b></span> (%5%6)")
                .arg(type.isEmpty() ? QString("?") : type.toHtmlEscaped(),
                     op.isEmpty() ? QString() : " " + op.toHtmlEscaped(),
                     col, quality, value, sinr);
        mRouterAge.restart();
    });
}

bool MainWindow::gamepadAttached()
{
#if defined(HAS_JOYSTICK_CHECK) && (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    return mController != nullptr && SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0 &&
           SDL_GameControllerGetAttached(mController) == SDL_TRUE;
#else
    return true;
#endif
}

void MainWindow::rcResendTick()
{
    // Hydraulik: firmware stoppar armarna efter 2 s utan kommando, så skicka om
    // medan en armknapp hålls. Tappas dosan: stopp.
    if (mArmFrontMove != 0 || mArmRearMove != 0) {
        if (!gamepadAttached()) {
            mArmL1 = mArmL2 = mArmR1 = mArmR2 = false;
            updateArms();
        } else if (!mArmResendAge.isValid() || mArmResendAge.elapsed() >= 500) {
            sendArmHydraulics(true);
        }
    }

    if (mLastActionValues.isEmpty()) {
        return;
    }

    if (!gamepadAttached()) {
        // Dosan borta mitt i körning: nolla allt direkt i stället för att vänta
        // på hot-plug-kontrollen (5 s).
        qWarning() << "Gamepad lost while active, sending 0 to all actions";
        for (auto it = mLastActionValues.constBegin(); it != mLastActionValues.constEnd(); ++it) {
            controllerAction(mActiveCarId, it.key(), 0.0f);
        }
        mLastActionValues.clear();
        mCachedControllerValues.clear();
        return;
    }

    for (auto it = mLastActionValues.constBegin(); it != mLastActionValues.constEnd(); ++it) {
        controllerAction(mActiveCarId, it.key(), it.value());
    }
}

// Armknapparna: L1 upp / L2 ned = främre armar, R1 upp / R2 ned = bakre armar.
// Båda nedtryckta på samma sida = stopp.
void MainWindow::updateArms()
{
    // Nyare vägen (dosabindningar, t.ex. VESC-armar): kontroll 1 = vänster sida,
    // kontroll 3 = höger sida; +1 upp, -1 ned, 0 släppt.
    float left = (mArmL1 == mArmL2) ? 0.0f : (mArmL1 ? 1.0f : -1.0f);
    float right = (mArmR1 == mArmR2) ? 0.0f : (mArmR1 ? 1.0f : -1.0f);
    handleControllerInput(1, left);
    handleControllerInput(3, right);

    // Hydraulik via firmware (CMD_HYDRAULIC_MOVE). Firmwarens värden skickas direkt:
    // fram = 1, bak = 0; stopp = 0, upp = 1, ned = 2. (RControlStations egen
    // HYDRAULIC_POS har fram = 0, bak = 1 — omvänt mot firmware, använd inte den.)
    mArmFrontMove = (mArmL1 == mArmL2) ? 0 : (mArmL1 ? 1 : 2);
    mArmRearMove = (mArmR1 == mArmR2) ? 0 : (mArmR1 ? 1 : 2);
    sendArmHydraulics(false);
}

void MainWindow::sendArmHydraulics(bool force)
{
    static int lastFront = -1, lastRear = -1;
    if (!mJoystickControlEnabled || !mTcpClientMulti->isAnyConnected()) {
        return;
    }
    const quint8 FW_POS_FRONT = 1, FW_POS_REAR = 0;
    if (force || mArmFrontMove != lastFront) {
        mPacketInterface->hydraulicMove(mActiveCarId, (HYDRAULIC_POS)FW_POS_FRONT, (HYDRAULIC_MOVE)mArmFrontMove);
        lastFront = mArmFrontMove;
    }
    if (force || mArmRearMove != lastRear) {
        mPacketInterface->hydraulicMove(mActiveCarId, (HYDRAULIC_POS)FW_POS_REAR, (HYDRAULIC_MOVE)mArmRearMove);
        lastRear = mArmRearMove;
    }
    mArmResendAge.restart();
}

void MainWindow::sendHeartbeat()
{
    for(QList<CarInterface*>::Iterator it_car = mCars.begin();it_car < mCars.end();it_car++)
        if ((*it_car)->getFirmwareVersion().first >= 20)
            mPacketInterface->sendHeartbeat((*it_car)->getId());
}

void MainWindow::showStatusInfo(QString info, bool isGood)
{
    if (mStatusLabel->text() == info) {
        mStatusInfoTime = 80;
        return;
    }

    if (isGood) {
        mStatusLabel->setStyleSheet("QLabel { background-color : lightgreen; color : black; }");
    } else {
        mStatusLabel->setStyleSheet("QLabel { background-color : red; color : black; }");
    }

    mStatusInfoTime = 80;
    mStatusLabel->setText(info);
}

void MainWindow::packetDataToSend(QByteArray &data)
{
    if (mSerialPort->isOpen()) {
        mSerialPort->write(data);
    }

    mTcpClientMulti->sendAll(data);
}

void MainWindow::stateReceived(quint8 id, CAR_STATE state)
{
    mStateAge.restart();
    if (mVehicle) {
        mVehicle->stateReceived(id, state);
    }

    if (!mSupportedFirmwares.contains(qMakePair(static_cast<int>(state.fw_major), static_cast<int>(state.fw_minor)))) {
        on_disconnectButton_clicked();
        QMessageBox::warning(this, "Unsupported Firmware",
                             "This version of RControlStation is not compatible with the "
                             "firmware of the connected car. Update RControlStation, the car "
                             "firmware or both.");
    } else {
        for(QList<CarInterface*>::Iterator it_car = mCars.begin();it_car < mCars.end();it_car++) {
            CarInterface *car = *it_car;
            if (car->getId() == id || car->getId() == 0) {
                car->setStateData(state);
            }
        }
    }
}

void MainWindow::mapPosSet(quint8 id, LocPoint pos)
{
    mPacketInterface->setPos(id, pos.getX(), pos.getY(), pos.getYaw() * 180.0 / M_PI);
}

void MainWindow::ackReceived(quint8 id, CMD_PACKET cmd, QString msg)
{
    (void)cmd;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QString str = QString("Vehicle %1 ack: ").arg(id);
#else
    QString str;
    str.sprintf("Vehicle %d ack: ", id);
#endif
    str += msg;
    showStatusInfo(str, true);
}

void MainWindow::rtcmReceived(QByteArray data)
{
    mPacketInterface->sendRtcmUsb(ID_ALL, data);

    if (ui->mapEnuBaseBox->isChecked()) {
        rtcm3_init_state(&mRtcmState);
        mRtcmState.decode_all = true;

        for(char b: data) {
            int res = rtcm3_input_data(b, &mRtcmState);
            if (res == 1005 || res == 1006) {
                ui->mapLiveWidget->setEnuRef(mRtcmState.pos.lat, mRtcmState.pos.lon, mRtcmState.pos.height);
                ui->mapWidgetAnalysisResult->setEnuRef(mRtcmState.pos.lat, mRtcmState.pos.lon, mRtcmState.pos.height);
            }
        }
    }
}

void MainWindow::rtcmRefPosGet()
{
    QMessageBox::warning(this, "Reference Position",
                         "Not implemented yet");
}

void MainWindow::pingRx(int time, QString msg)
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QString str = QString("ping response time: % ms").arg((double)time / 1000.0,0,'f',3);
#else
    QString str;
    ("ping response time: %.3f ms", (double)time / 1000.0);
#endif
    QMessageBox::information(this, "Ping " + msg, str);
}

void MainWindow::pingError(QString msg, QString error)
{
    QMessageBox::warning(this, "Error ping " + msg, error);
}

void MainWindow::enuRx(quint8 id, double lat, double lon, double height)
{
    double cur[3];
    ui->mapLiveWidget->getEnuRef(cur);
    if (cur[0] != lat || cur[1] != lon || cur[2] != height) {
        qDebug().noquote() << QString("ENU-ref från bil %1: %2, %3, %4 (kartan hade %5, %6, %7)")
                              .arg(id).arg(lat, 0, 'f', 8).arg(lon, 0, 'f', 8).arg(height, 0, 'f', 2)
                              .arg(cur[0], 0, 'f', 8).arg(cur[1], 0, 'f', 8).arg(cur[2], 0, 'f', 2);
    }
    ui->mapLiveWidget->setEnuRef(lat, lon, height);
    ui->mapWidgetAnalysisResult->setEnuRef(lat, lon, height);
}

void MainWindow::applyAutoEnuRef(double lat, double lon, double height)
{
    int car = ui->mapCarBox->value();
    double llh[3] = {lat, lon, height};
    qDebug().noquote() << QString("Nollpunkt satt där roboten står: %1, %2, %3 (bil %4)")
                          .arg(lat, 0, 'f', 8).arg(lon, 0, 'f', 8).arg(height, 0, 'f', 2).arg(car);

    // Samma som ett gårdsval, men med robotens egen position.
    ui->mapLiveWidget->setEnuRef(lat, lon, height);
    ui->mapWidgetFields->setEnuRef(lat, lon, height);
    ui->mapWidgetAnalysis->setEnuRef(lat, lon, height);
    ui->mapWidgetAnalysisResult->setEnuRef(lat, lon, height);
    mPacketInterface->setEnuRef(car, llh);
    mPacketInterface->getEnuRef(car); // svaret (enuRx) bekräftar att kartan och bilen är i takt

    // Roboten står i nollpunkten: centrera, ca 50 m i bild, och följ bilen.
    double w = qMax(1, ui->mapLiveWidget->width());
    ui->mapLiveWidget->setScaleFactor(w / (50.0 * 1000.0));
    ui->mapLiveWidget->moveView(0.0, 0.0);
    ui->mapStreamNmeaFollowBox->setChecked(false);
    ui->mapFollowBox->setChecked(true);
    ui->mapLiveWidget->setFollowCar(car);

    saveMapPosition(lat, lon);
}

void MainWindow::saveMapPosition(double lat, double lon)
{
    QSettings s("RControlStation", "karta");
    s.setValue("senasteLat", lat);
    s.setValue("senasteLon", lon);
}

void MainWindow::nmeaGgaRx(int fields, NmeaServer::nmea_gga_info_t gga)
{
    if (fields >= 5) {
        mLastGga = gga;
        mHaveGga = true;
        mGgaAge.restart();
    }

    // Första GPS-positionen efter anslutning (även SPP): nollpunkt där roboten står.
    if (mAutoEnuRefPending && fields >= 5 &&
            (gga.fix_type == 1 || gga.fix_type == 2 || gga.fix_type == 4 || gga.fix_type == 5)) {
        mAutoEnuRefPending = false;
        applyAutoEnuRef(gga.lat, gga.lon, gga.height);
    }

    if (fields >= 5) {
        if (gga.fix_type == 4 || gga.fix_type == 5 || gga.fix_type == 2 ||
                (gga.fix_type == 1 && !ui->mapStreamNmeaRtkOnlyBox->isChecked())) {
            double i_llh[3];

            if (ui->mapStreamNmeaZeroEnuBox->isChecked()) {
                i_llh[0] = gga.lat;
                i_llh[1] = gga.lon;
                i_llh[2] = gga.height;
                ui->mapLiveWidget->setEnuRef(i_llh[0], i_llh[1], i_llh[2]);
                ui->mapStreamNmeaZeroEnuBox->setChecked(false);
            } else {
                ui->mapLiveWidget->getEnuRef(i_llh);
            }

            double llh[3];
            double xyz[3];

            llh[0] = gga.lat;
            llh[1] = gga.lon;
            llh[2] = gga.height;
            utility::llhToEnu(i_llh, llh, xyz);

            LocPoint p;
            p.setXY(xyz[0], xyz[1]);
            QString info;

            QString fix_t = "Unknown";
            if (gga.fix_type == 4) {
                fix_t = "RTK fix";
                p.setColor(Qt::green);
            } else if (gga.fix_type == 5) {
                fix_t = "RTK float";
                p.setColor(Qt::yellow);
            } else if (gga.fix_type == 1) {
                fix_t = "Single";
                p.setColor(Qt::red);
            }


#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
/*            info = QString("Fix type: %1\n") 
                         "Sats    : %2\n") 
                         "Height  : %3\n") 
                         "Age     : %4")
                      .arg(fix_t.toLocal8Bit().data())
                      .arg(gga.n_sat,0,'d',0)
                      .arg(gga.height,0,'f',2)
                      .arg(gga.diff_age,0,'f',2);*/
            QByteArray fixBytes = fix_t.toLocal8Bit();
            info = (QString("Fix type: %1\n") +
                    QString("Sats    : %2\n") +
                    QString("Height  : %3\n") +
                    QString("Age     : %4"))
                       .arg(QString::fromLocal8Bit(fixBytes))
                       .arg(QString::number(gga.n_sat))
                       .arg(gga.height, 0, 'f', 2)
                       .arg(gga.diff_age, 0, 'f', 2);



#else
            info.sprintf("Fix type: %s\n") 
                         "Sats    : %d\n") 
                         "Height  : %.2f\n") 
                         "Age     : %.2f",
                         fix_t.toLocal8Bit().data(),
                         gga.n_sat,
                         gga.height,
                         gga.diff_age);
#endif
            p.setInfo(info);
            if (mNmeaDrawTrace) {
                ui->mapLiveWidget->addInfoPoint(p);
            }

            if (ui->mapStreamNmeaFollowBox->isChecked()) {
                ui->mapLiveWidget->moveView(p.getX(), p.getY());
            }

            // Optionally stream the data over UDP
            if (ui->mapStreamNmeaForwardUdpBox->isChecked()) {
                QString hostString = ui->mapStreamNmeaForwardUdpHostEdit->text();
                QHostAddress host;

                host.setAddress(hostString);

                // In case setting the address failed try DNS lookup. Notice
                // that the lookup is stored in a static QHostInfo as long as
                // the host line does not change. This is to avoid some delay.
                if (host.isNull()) {
                    static QString hostStringBefore;
                    static QHostInfo hostBefore;

                    QList<QHostAddress> addresses = hostBefore.addresses();

                    // Make a new lookup if the address has changed or the old one is invalid.
                    if (hostString != hostStringBefore || addresses.isEmpty()) {
                        hostBefore = QHostInfo::fromName(hostString);
                        hostStringBefore = hostString;
                    }

                    if (!addresses.isEmpty()) {
                        host.setAddress(addresses.first().toString());
                    }
                }

                if (!host.isNull()) {
                    static int seq = 0;
                    QByteArray datagram;
                    QTextStream out(&datagram);
                    QString str;

                    utility::llhToXyz(llh[0], llh[1], llh[2],
                            &xyz[0], &xyz[1], &xyz[2]);
/*
                    out << str.sprintf("%d\n", seq);          // Seq
                    out << str.sprintf("%05f\n", xyz[0]);     // X
                    out << str.sprintf("%05f\n", xyz[1]);     // Y
                    out << str.sprintf("%05f\n", xyz[2]);     // Height
                    out << str.sprintf("%05f\n", gga.t_tow);  // GPS time of week
                    out << str.sprintf("%d\n", 2);            // Vehicle ID
*/
                    out << QString("%1\n").arg(seq);                  // Seq
                    out << QString("%1\n").arg(xyz[0], 0, 'f', 5);    // X
                    out << QString("%1\n").arg(xyz[1], 0, 'f', 5);    // Y
                    out << QString("%1\n").arg(xyz[2], 0, 'f', 5);    // Height
                    out << QString("%1\n").arg(gga.t_tow, 0, 'f', 5); // GPS time of week
                    out << QString("%1\n").arg(2);                    // Vehicle ID

                    out.flush();

                    mUdpSocket->writeDatagram(datagram,
                                              host,
                                              ui->mapStreamNmeaForwardUdpPortBox->value());

                    seq++;
                } else {
                    QMessageBox::warning(this,
                                         tr("Host not found"),
                                         tr("Could not find %1").arg(hostString));
                    ui->mapStreamNmeaForwardUdpBox->setChecked(false);
                }
            }
        }
    }
}

void MainWindow::routePointAdded(LocPoint pos)
{
    (void)pos;
    QTime t = ui->mapRouteTimeEdit->time();
    t = t.addMSecs(ui->mapRouteAddTimeEdit->time().msecsSinceStartOfDay());
    ui->mapRouteTimeEdit->setTime(t);
}

void MainWindow::infoTraceChanged(int traceNow)
{
    ui->mapInfoTraceBox->setValue(traceNow);
}

void MainWindow::controllerAction(int car, int iAction,float value=0)
{
    if (mJoystickControlEnabled) {
        // qDebug() << "Sending";
        mPacketInterface->setRcControlAdvanced(car, iAction, value);
    };
/*
 *     switch (iAction)
    {
    case FRONT_UP:
        qDebug() << "Hydraulic, front up: " << value;
        mPacketInterface->hydraulicMove(car, HYDRAULIC_POS_FRONT, HYDRAULIC_MOVE_UP);
        break;
    case FRONT_DOWN:
        qDebug() << "Hydraulic, front down: " << value;
        mPacketInterface->hydraulicMove(car, HYDRAULIC_POS_FRONT, HYDRAULIC_MOVE_DOWN);
        break;
    case REAR_UP:
        qDebug() << "Hydraulic, rear up";
        mPacketInterface->hydraulicMove(car, HYDRAULIC_POS_REAR, HYDRAULIC_MOVE_UP);
        break;
    case REAR_DOWN:
        qDebug() << "Hydraulic, rear down";
        mPacketInterface->hydraulicMove(car, HYDRAULIC_POS_REAR, HYDRAULIC_MOVE_DOWN);
        break;
    }
*/
}



/*
void MainWindow::jsButtonChanged(int button, bool pressed)
{
        qDebug() << "JS BT:" << button << pressed;

    #ifdef HAS_JOYSTICK        
        if (1) {
            // 1: Extra out

            // 3: Extra in

            if (button == FRONT_UP || button == FRONT_DOWN || button == 1 ||
                    button == REAR_DOWN || button == REAR_UP || button == 6) {
                // Use the cached active car ID

                bool activeCarExists = false;
                
                // Find the car with matching ID
                for(QList<CarInterface*>::Iterator it_car = mCars.begin(); it_car < mCars.end(); it_car++) {
                    CarInterface *car = *it_car;
                    if (car->getId() == mActiveCarId) {
                        activeCarExists = true;
                        break;
                    }
                }
                activeCarExists
                // Only send commands to the active car if joystick control is enabled globally
                if (activeCarExists && mJoystickControlEnabled) {
                    qDebug() << "Active car id: " << mActiveCarId;
                        if (button == FRONT_UP || button == FRONT_DOWN) {
                            qDebug() << "Hydraulic, rear down";
                            mPacketInterface->hydraulicMove(mActiveCarId, HYDRAULIC_POS_REAR, HYDRAULIC_MOVE_DOWN);
                            if (pressed) {
                                if (button == FRONT_UP) {
                                    controllerAction(mActiveCarId,FRONT_UP);
                                } else {
                                    controllerAction(mActiveCarId,FRONT_DOWN);
                                }
                            } else {
                                mPacketInterface->hydraulicMove(mActiveCarId, HYDRAULIC_POS_FRONT, HYDRAULIC_MOVE_STOP);
                            }
                        } else if (button == REAR_UP || button == REAR_DOWN) {
                            if (pressed) {
                                if (button == REAR_UP) {
                                    controllerAction(mActiveCarId,REAR_UP);
                                } else {
                                    controllerAction(mActiveCarId,REAR_DOWN);
                                }
                            } else {
                                qDebug() << "Hydraulic, rear down";
                                mPacketInterface->hydraulicMove(mActiveCarId, HYDRAULIC_POS_REAR, HYDRAULIC_MOVE_DOWN);
                                mPacketInterface->hydraulicMove(mActiveCarId, HYDRAULIC_POS_REAR, HYDRAULIC_MOVE_STOP);
                            }
                        } else if (button == 1 || button == 3) {
                            qDebug() << "Hydraulic, rear down";
                            mPacketInterface->hydraulicMove(mActiveCarId, HYDRAULIC_POS_REAR, HYDRAULIC_MOVE_DOWN);
                            if (pressed) {
                                if (button == 1) {
                                    mPacketInterface->hydraulicMove(mActiveCarId, HYDRAULIC_POS_EXTRA, HYDRAULIC_MOVE_OUT);
                                } else {
                                    mPacketInterface->hydraulicMove(mActiveCarId, HYDRAULIC_POS_EXTRA, HYDRAULIC_MOVE_IN);
                                }
                            } else {
                                mPacketInterface->hydraulicMove(mActiveCarId, HYDRAULIC_POS_EXTRA, HYDRAULIC_MOVE_STOP);
                            }
                        }
                    }
                }
            }
//        }
    #endif
}
*/
void MainWindow::onMapCarBoxChanged(int value)
{
    mActiveCarId = value;
    qDebug() << "Active car changed to:" << mActiveCarId;

    bool activeCarExists = false;

    // Find the car with matching ID
    for(QList<CarInterface*>::Iterator it_car = mCars.begin(); it_car < mCars.end(); it_car++) {
        CarInterface *car = *it_car;
        if (car->getId() == mActiveCarId) {
            activeCarExists = true;
            break;
        }
    }
}

void MainWindow::setJoystickControlEnabled(bool enabled)
{
    mJoystickControlEnabled = enabled;
    qDebug() << "Joystick control" << (enabled ? "enabled" : "disabled");
}

void MainWindow::on_disconnectButton_clicked()
{
    if (mSerialPort->isOpen()) {
        mSerialPort->close();
    }

    if (mPacketInterface->isUdpConnected()) {
        mPacketInterface->stopUdpConnection();
    }
MainWindow::
    mTcpClientMulti->disconnectAll();
    removeCars();
    ui->carsWidget->clear();

}

void MainWindow::on_connectSelectedButton_clicked()
{
    on_tcpConnectButton_clicked();
}

void MainWindow::on_disconnectSelectedButton_clicked()
{
    // Disconnect from the selected machine
    mTcpClientMulti->disconnectAll();
    removeCars();
    ui->carsWidget->clear();
}

void MainWindow::on_refreshMachinesButton_clicked()
{
    fetchMachinesData();
}

void MainWindow::fetchMachinesData(int retryCount)
{
    const int MAX_RETRIES = 3;
    const int RETRY_DELAY_MS = 2000; // 2 seconds between retries
    
    QUrl url(getServerBaseUrl() + "/machines");
    QNetworkRequest request(url);
    
    // Set a timeout for the request (10 seconds to be safe)
    request.setTransferTimeout(10000);
    
    // Disconnect any existing connections to prevent multiple handlers
    disconnect(mNetworkManager, &QNetworkAccessManager::finished, this, nullptr);
    
    // Show loading state
    ui->machinesTable->setRowCount(0);
    ui->machinesTable->insertRow(0);
    ui->machinesTable->setItem(0, 0, new QTableWidgetItem(retryCount > 0 ? QString("Retrying... (%1/%2)").arg(retryCount).arg(MAX_RETRIES) : "Loading..."));
    ui->machinesTable->setItem(0, 1, new QTableWidgetItem(""));
    
    // Connect the finished signal to parse the response
    QNetworkReply* reply = mNetworkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, retryCount]() {
        // Clear loading message
        ui->machinesTable->setRowCount(0);
        
        // Check HTTP status code
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received XML data (size:" << xmlData.size() << "):" << xmlData;
            
            if (xmlData.isEmpty()) {
                qDebug() << "Empty response received";
                ui->machinesTable->insertRow(0);
                ui->machinesTable->setItem(0, 0, new QTableWidgetItem("No data"));
                ui->machinesTable->setItem(0, 1, new QTableWidgetItem("Empty response"));
            } else {
                // Check if response contains valid XML
                if (xmlData.contains("<machines>") && xmlData.contains("</machines>")) {
                    parseMachinesXml(xmlData);
                } else {
                    qDebug() << "Invalid XML format received";
                    ui->machinesTable->insertRow(0);
                    ui->machinesTable->setItem(0, 0, new QTableWidgetItem("Invalid data"));
                    ui->machinesTable->setItem(0, 1, new QTableWidgetItem("Bad format"));
                }
            }
        } else {
            qDebug() << "Error fetching machines data:" << reply->errorString();
            qDebug() << "Error code:" << reply->error();
            qDebug() << "HTTP Status Code:" << statusCode;
            
            // Retry if we haven't exceeded max retries and it's a timeout or temporary error
            if (retryCount < MAX_RETRIES && 
                (reply->error() == QNetworkReply::TimeoutError ||
                 reply->error() == QNetworkReply::TemporaryNetworkFailureError ||
                 reply->error() == QNetworkReply::ConnectionRefusedError)) {
                qDebug() << "Retrying in" << RETRY_DELAY_MS << "ms...";
                QTimer::singleShot(RETRY_DELAY_MS, this, [this, retryCount]() {
                    fetchMachinesData(retryCount + 1);
                });
            } else {
                // Show final error after all retries failed
                QString errorMsg = reply->errorString();
                if (statusCode != 200 && statusCode > 0) {
                    errorMsg = QString("HTTP %1").arg(statusCode);
                }
                ui->machinesTable->insertRow(0);
                ui->machinesTable->setItem(0, 0, new QTableWidgetItem("Error"));
                ui->machinesTable->setItem(0, 1, new QTableWidgetItem(errorMsg));
            }
        }
        reply->deleteLater();
    });
    
    qDebug() << "Fetching machines data from:" << url.toString() << "(attempt" << (retryCount + 1) << ")";
}

void MainWindow::parseMachinesXml(const QByteArray &xmlData)
{
    // Clear existing data
    ui->machinesTable->setRowCount(0);
    
    // Parse the XML
    QXmlStreamReader xmlReader(xmlData);
    bool foundMachines = false;
    
    qDebug() << "Starting XML parsing...";
    
    while (!xmlReader.atEnd()) {
        QXmlStreamReader::TokenType token = xmlReader.readNext();
        
        if (token == QXmlStreamReader::StartElement && xmlReader.name() == QLatin1String("machine")) {
            QString name, ip;
            foundMachines = true;
            qDebug() << "Found machine element";
            
            // Read machine element contents
            while (!xmlReader.atEnd()) {
                token = xmlReader.readNext();
                
                if (token == QXmlStreamReader::EndElement && xmlReader.name() == QLatin1String("machine")) {
                    break; // End of machine element
                }
                
                if (token == QXmlStreamReader::StartElement) {
                    if (xmlReader.name() == QLatin1String("name")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            name = xmlReader.text().toString();
                            qDebug() << "Found name:" << name;
                        }
                    } else if (xmlReader.name() == QLatin1String("ip")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            ip = xmlReader.text().toString();
                            qDebug() << "Found ip:" << ip;
                        }
                    }
                }
            }
            
            // Add the machine to the table if we have both name and IP
            if (!name.isEmpty() && !ip.isEmpty()) {
                int row = ui->machinesTable->rowCount();
                ui->machinesTable->insertRow(row);
                ui->machinesTable->setItem(row, 0, new QTableWidgetItem(name));
                ui->machinesTable->setItem(row, 1, new QTableWidgetItem(ip));
                qDebug() << "Added machine to table:" << name << "-" << ip;
            } else {
                qDebug() << "Machine missing name or IP - name:" << name << ", ip:" << ip;
            }
        }
    }
    
    if (xmlReader.hasError()) {
        qDebug() << "XML parsing error:" << xmlReader.errorString();
        qDebug() << "Error line:" << xmlReader.lineNumber() << "column:" << xmlReader.columnNumber();
        // Show the raw data for debugging
        qDebug() << "Raw XML data:" << xmlData;
        
        // If no machines were found and there was an error, show the error in the table
        if (!foundMachines) {
            ui->machinesTable->insertRow(0);
            ui->machinesTable->setItem(0, 0, new QTableWidgetItem("XML Error"));
            ui->machinesTable->setItem(0, 1, new QTableWidgetItem(xmlReader.errorString()));
        }
    }
    
    // If no machines were found but no error occurred, show a message
    if (!foundMachines && !xmlReader.hasError()) {
        qDebug() << "No machines found in XML";
        ui->machinesTable->insertRow(0);
        ui->machinesTable->setItem(0, 0, new QTableWidgetItem("No machines"));
        ui->machinesTable->setItem(0, 1, new QTableWidgetItem("found"));
    }
    
    ui->machinesTable->resizeColumnsToContents();
}

void MainWindow::fetchAllMachinesData(int retryCount)
{
    const int MAX_RETRIES = 3;
    const int RETRY_DELAY_MS = 2000; // 2 seconds between retries
    
    QUrl url(getServerBaseUrl() + "/all_machines");
    QNetworkRequest request(url);
    
    // Set a timeout for the request (10 seconds to be safe)
    request.setTransferTimeout(10000);
    
    // Disconnect any existing connections to prevent multiple handlers
    disconnect(mNetworkManager, &QNetworkAccessManager::finished, this, nullptr);
    
    // Clear the model
    machinesModel->removeRows(0, machinesModel->rowCount());
    
    // Add loading state
    machinesModel->appendRow(new QStandardItem(retryCount > 0 ? QString("Retrying... (%1/%2)").arg(retryCount).arg(MAX_RETRIES) : "Loading..."));
    machinesModel->appendRow(new QStandardItem(""));
    
    // Connect the finished signal to parse the response
    QNetworkReply* reply = mNetworkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, retryCount]() {
        // Clear loading message
        machinesModel->removeRows(0, machinesModel->rowCount());
        
        // Check HTTP status code
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "HTTP Status Code (all_machines):" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received XML data from all_machines (size:" << xmlData.size() << "):" << xmlData;
            
            if (xmlData.isEmpty()) {
                qDebug() << "Empty response received from all_machines";
                machinesModel->appendRow(new QStandardItem("No data"));
                machinesModel->appendRow(new QStandardItem("Empty response"));
            } else {
                // Check if response contains valid XML
                if (xmlData.contains("<machines>") && xmlData.contains("</machines>")) {
                    parseAllMachinesXml(xmlData);
                } else {
                    machinesModel->appendRow(new QStandardItem("Invalid data"));
                    machinesModel->appendRow(new QStandardItem("Bad format"));
                }
            }
        } else {
            qDebug() << "Error fetching all_machines data:" << reply->errorString();
            qDebug() << "Error code:" << reply->error();
            qDebug() << "HTTP Status Code:" << statusCode;
            
            // Retry if we haven't exceeded max retries and it's a timeout or temporary error
            if (retryCount < MAX_RETRIES && 
                (reply->error() == QNetworkReply::TimeoutError ||
                 reply->error() == QNetworkReply::TemporaryNetworkFailureError ||
                 reply->error() == QNetworkReply::ConnectionRefusedError)) {
                qDebug() << "Retrying all_machines in" << RETRY_DELAY_MS << "ms...";
                QTimer::singleShot(RETRY_DELAY_MS, this, [this, retryCount]() {
                    fetchAllMachinesData(retryCount + 1);
                });
            } else {
                // Show final error after all retries failed
                QString errorMsg = reply->errorString();
                if (statusCode != 200 && statusCode > 0) {
                    errorMsg = QString("HTTP %1").arg(statusCode);
                }
                machinesModel->appendRow(new QStandardItem("Error"));
                machinesModel->appendRow(new QStandardItem(errorMsg));
            }
        }
        reply->deleteLater();
    });
    
    qDebug() << "Fetching all_machines data from:" << url.toString() << "(attempt" << (retryCount + 1) << ")";
}

void MainWindow::fetchAllFarmsData(int retryCount)
{
    qDebug() << "fetchAllFarmsData: Starting, retryCount:" << retryCount;
    const int MAX_RETRIES = 3;
    const int RETRY_DELAY_MS = 2000; // 2 seconds between retries
    
    QUrl url(getServerBaseUrl() + "/all_farms");
    QNetworkRequest request(url);
    
    // Set a timeout for the request (10 seconds to be safe)
    request.setTransferTimeout(10000);
    
    // Disconnect any existing connections to prevent multiple handlers
    // Note: Removed disconnect call as it was causing crashes - same pattern as fetchAllMachinesData
    
    // Check if farmsModel is initialized
    if (!farmsModel) {
        qDebug() << "ERROR: farmsModel is null in fetchAllFarmsData!";
        return;
    }
    
    // Clear the model
    farmsModel->removeRows(0, farmsModel->rowCount());
    
    // Add loading state
    farmsModel->appendRow(new QStandardItem(retryCount > 0 ? QString("Retrying... (%1/%2)").arg(retryCount).arg(MAX_RETRIES) : "Loading..."));
    farmsModel->appendRow(new QStandardItem(""));
    
    // Connect the finished signal to parse the response
    QNetworkReply* reply = mNetworkManager->get(request);
    if (!reply) {
        qDebug() << "ERROR: reply is null in fetchAllFarmsData!";
        return;
    }
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, retryCount]() {
        // Clear loading message
        farmsModel->removeRows(0, farmsModel->rowCount());
        
        // Check HTTP status code
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "HTTP Status Code (all_farms):" << statusCode;
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Network error:" << reply->error() << "-" << reply->errorString();
        }
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received XML data from all_farms (size:" << xmlData.size() << "):";
            qDebug() << "XML content:" << xmlData;
            
            if (xmlData.isEmpty()) {
                qDebug() << "Empty response received from all_farms";
                farmsModel->appendRow(new QStandardItem("No data"));
                farmsModel->appendRow(new QStandardItem("Empty response"));
            } else {
                // Check if response contains valid XML
                bool hasLocations = xmlData.contains("<locations>");
                bool hasEndLocations = xmlData.contains("</locations>");
                qDebug() << "XML validation - has <locations>:" << hasLocations << ", has </locations>:" << hasEndLocations;
                
                if (hasLocations && hasEndLocations) {
                    parseAllFarmsXml(xmlData);
                } else {
                    qDebug() << "XML validation failed - missing locations tags";
                    farmsModel->appendRow(new QStandardItem("Invalid data"));
                    farmsModel->appendRow(new QStandardItem("Bad format"));
                }
            }
        } else {
            qDebug() << "Error fetching all_farms data:" << reply->errorString();
            qDebug() << "Error code:" << reply->error();
            qDebug() << "HTTP Status Code:" << statusCode;
            
            // Retry if we haven't exceeded max retries and it's a timeout or temporary error
            if (retryCount < MAX_RETRIES && 
                (reply->error() == QNetworkReply::TimeoutError ||
                 reply->error() == QNetworkReply::TemporaryNetworkFailureError ||
                 reply->error() == QNetworkReply::ConnectionRefusedError)) {
                qDebug() << "Retrying all_farms in" << RETRY_DELAY_MS << "ms...";
                QTimer::singleShot(RETRY_DELAY_MS, this, [this, retryCount]() {
                    fetchAllFarmsData(retryCount + 1);
                });
            } else {
                // Show final error after all retries failed
                QString errorMsg = reply->errorString();
                if (statusCode != 200 && statusCode > 0) {
                    errorMsg = QString("HTTP %1").arg(statusCode);
                }
                farmsModel->appendRow(new QStandardItem("Error"));
                farmsModel->appendRow(new QStandardItem(errorMsg));
            }
        }
        reply->deleteLater();
    });
    
    qDebug() << "Fetching all_farms data from:" << url.toString() << "(attempt" << (retryCount + 1) << ")";
}

// Log tab functions
void MainWindow::fetchAllFarmsForLog()
{
    qDebug() << "fetchAllFarmsForLog: Starting";
    
    if (!logFarmsModel) {
        qDebug() << "ERROR: logFarmsModel is null!";
        return;
    }
    
    // Clear existing data
    logFarmsModel->removeRows(0, logFarmsModel->rowCount());
    
    // Add loading indicator
    logFarmsModel->appendRow(new QStandardItem("Loading farms..."));
    
    QUrl url(getServerBaseUrl() + "/all_farms");
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    if (!reply) {
        qDebug() << "ERROR: reply is null in fetchAllFarmsForLog!";
        logFarmsModel->removeRows(0, logFarmsModel->rowCount());
        logFarmsModel->appendRow(new QStandardItem("Error: Network request failed"));
        return;
    }
    
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        logFarmsModel->removeRows(0, logFarmsModel->rowCount());
        
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "fetchAllFarmsForLog HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received farms data for log tab (size:" << xmlData.size() << ")";
            
            if (!xmlData.isEmpty()) {
                parseAllFarmsXmlForLog(xmlData);
            } else {
                qDebug() << "Empty response for farms";
                logFarmsModel->appendRow(new QStandardItem("No farms data"));
            }
        } else {
            qDebug() << "Error fetching farms for log:" << reply->errorString();
            logFarmsModel->appendRow(new QStandardItem("Error loading farms"));
        }
        reply->deleteLater();
    });
}

void MainWindow::parseAllFarmsXmlForLog(const QByteArray &xmlData)
{
    qDebug() << "parseAllFarmsXmlForLog: Starting";
    
    if (!logFarmsModel) {
        qDebug() << "ERROR: logFarmsModel is null!";
        return;
    }
    
    QXmlStreamReader xmlReader(xmlData);
    
    while (!xmlReader.atEnd()) {
        QXmlStreamReader::TokenType token = xmlReader.readNext();
        
        if (token == QXmlStreamReader::StartElement && xmlReader.name() == QLatin1String("location")) {
            QString id, name;
            
            while (!xmlReader.atEnd()) {
                token = xmlReader.readNext();
                
                if (token == QXmlStreamReader::EndElement && xmlReader.name() == QLatin1String("location")) {
                    break;
                }
                
                if (token == QXmlStreamReader::StartElement) {
                    if (xmlReader.name() == QLatin1String("id")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            id = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("name")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            name = xmlReader.text().toString();
                        }
                    }
                }
            }
            
            if (!name.isEmpty()) {
                QStandardItem* farmItem = new QStandardItem(name);
                if (!id.isEmpty()) {
                    farmItem->setData(id, Qt::UserRole); // Store ID as user data
                }
                logFarmsModel->appendRow(farmItem);
                qDebug() << "Added farm to log tab:" << name << "(ID:" << id << ")";
            }
        }
    }
    
    if (xmlReader.hasError()) {
        qDebug() << "XML parsing error in parseAllFarmsXmlForLog:" << xmlReader.errorString();
    }
    
    qDebug() << "parseAllFarmsXmlForLog: Farms loaded:" << logFarmsModel->rowCount();
}

void MainWindow::fetchFieldsForLogFarm(int farmId)
{
    qDebug() << "fetchFieldsForLogFarm: Starting for farmId:" << farmId;
    
    if (!logFieldsModel) {
        qDebug() << "ERROR: logFieldsModel is null!";
        return;
    }
    
    // Clear existing data
    logFieldsModel->removeRows(0, logFieldsModel->rowCount());
    logPathsModel->removeRows(0, logPathsModel->rowCount()); // Also clear paths
    
    // Add loading indicator
    logFieldsModel->appendRow(new QStandardItem("Loading fields..."));
    
    QUrl url(getServerBaseUrl() + "/all_fields");
    QUrlQuery query;
    query.addQueryItem("farm", QString::number(farmId));
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    if (!reply) {
        qDebug() << "ERROR: reply is null in fetchFieldsForLogFarm!";
        logFieldsModel->removeRows(0, logFieldsModel->rowCount());
        logFieldsModel->appendRow(new QStandardItem("Error: Network request failed"));
        return;
    }
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, farmId]() {
        logFieldsModel->removeRows(0, logFieldsModel->rowCount());
        
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "fetchFieldsForLogFarm HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received fields data for farm" << farmId << "(size:" << xmlData.size() << ")";
            
            if (!xmlData.isEmpty()) {
                parseAllFieldsXmlForLog(xmlData);
            } else {
                qDebug() << "Empty response for fields";
                logFieldsModel->appendRow(new QStandardItem("No fields data"));
            }
        } else {
            qDebug() << "Error fetching fields for log:" << reply->errorString();
            logFieldsModel->appendRow(new QStandardItem("Error loading fields"));
        }
        reply->deleteLater();
    });
}

void MainWindow::parseAllFieldsXmlForLog(const QByteArray &xmlData)
{
    qDebug() << "parseAllFieldsXmlForLog: Starting";
    
    if (!logFieldsModel) {
        qDebug() << "ERROR: logFieldsModel is null!";
        return;
    }
    
    QXmlStreamReader xmlReader(xmlData);
    
    while (!xmlReader.atEnd()) {
        QXmlStreamReader::TokenType token = xmlReader.readNext();
        
        if (token == QXmlStreamReader::StartElement && xmlReader.name() == QLatin1String("field")) {
            QString id, name;
            
            while (!xmlReader.atEnd()) {
                token = xmlReader.readNext();
                
                if (token == QXmlStreamReader::EndElement && xmlReader.name() == QLatin1String("field")) {
                    break;
                }
                
                if (token == QXmlStreamReader::StartElement) {
                    if (xmlReader.name() == QLatin1String("id")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            id = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("name")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            name = xmlReader.text().toString();
                        }
                    }
                }
            }
            
            if (!name.isEmpty()) {
                QStandardItem* fieldItem = new QStandardItem(name);
                if (!id.isEmpty()) {
                    fieldItem->setData(id, Qt::UserRole); // Store ID as user data
                }
                logFieldsModel->appendRow(fieldItem);
                qDebug() << "Added field to log tab:" << name << "(ID:" << id << ")";
            }
        }
    }
    
    if (xmlReader.hasError()) {
        qDebug() << "XML parsing error in parseAllFieldsXmlForLog:" << xmlReader.errorString();
    }
    
    qDebug() << "parseAllFieldsXmlForLog: Fields loaded:" << logFieldsModel->rowCount();
}

void MainWindow::fetchPathsForLogField(int fieldId)
{
    qDebug() << "fetchPathsForLogField: Starting for fieldId:" << fieldId;
    
    if (!logPathsModel) {
        qDebug() << "ERROR: logPathsModel is null!";
        return;
    }
    
    // Clear existing data
    logPathsModel->removeRows(0, logPathsModel->rowCount());
    
    // Add loading indicator
    logPathsModel->appendRow(new QStandardItem("Loading paths..."));
    
    QUrl url(getServerBaseUrl() + "/all_paths");
    QUrlQuery query;
    query.addQueryItem("field", QString::number(fieldId));
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    if (!reply) {
        qDebug() << "ERROR: reply is null in fetchPathsForLogField!";
        logPathsModel->removeRows(0, logPathsModel->rowCount());
        logPathsModel->appendRow(new QStandardItem("Error: Network request failed"));
        return;
    }
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, fieldId]() {
        logPathsModel->removeRows(0, logPathsModel->rowCount());
        
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "fetchPathsForLogField HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received paths data for field" << fieldId << "(size:" << xmlData.size() << ")";
            
            if (!xmlData.isEmpty()) {
                parseAllPathsXmlForLog(xmlData);
            } else {
                qDebug() << "Empty response for paths";
                logPathsModel->appendRow(new QStandardItem("No paths data"));
            }
        } else {
            qDebug() << "Error fetching paths for log:" << reply->errorString();
            logPathsModel->appendRow(new QStandardItem("Error loading paths"));
        }
        reply->deleteLater();
    });
}

void MainWindow::parseAllPathsXmlForLog(const QByteArray &xmlData)
{
    qDebug() << "parseAllPathsXmlForLog: Starting";
    
    if (!logPathsModel) {
        qDebug() << "ERROR: logPathsModel is null!";
        return;
    }
    
    QXmlStreamReader xmlReader(xmlData);
    
    while (!xmlReader.atEnd()) {
        QXmlStreamReader::TokenType token = xmlReader.readNext();
        
        if (token == QXmlStreamReader::StartElement && xmlReader.name() == QLatin1String("path")) {
            QString id, name;
            
            while (!xmlReader.atEnd()) {
                token = xmlReader.readNext();
                
                if (token == QXmlStreamReader::EndElement && xmlReader.name() == QLatin1String("path")) {
                    break;
                }
                
                if (token == QXmlStreamReader::StartElement) {
                    if (xmlReader.name() == QLatin1String("id")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            id = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("name")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            name = xmlReader.text().toString();
                        }
                    }
                }
            }
            
            if (!name.isEmpty()) {
                QStandardItem* pathItem = new QStandardItem(name);
                if (!id.isEmpty()) {
                    pathItem->setData(id, Qt::UserRole); // Store ID as user data
                }
                logPathsModel->appendRow(pathItem);
                qDebug() << "Added path to log tab:" << name << "(ID:" << id << ")";
            }
        }
    }
    
    if (xmlReader.hasError()) {
        qDebug() << "XML parsing error in parseAllPathsXmlForLog:" << xmlReader.errorString();
    }
    
    qDebug() << "parseAllPathsXmlForLog: Paths loaded:" << logPathsModel->rowCount();
}

void MainWindow::loadPathAsLog(int pathId)
{
    qDebug() << "loadPathAsLog: Starting for pathId:" << pathId;
    
    QUrl url(getServerBaseUrl() + "/log");
    QUrlQuery query;
    query.addQueryItem("path", QString::number(pathId));
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    if (!reply) {
        qDebug() << "ERROR: reply is null in loadPathAsLog!";
        return;
    }
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, pathId]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "loadPathAsLog HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received log data for path" << pathId << "(size:" << xmlData.size() << ")";
            
            if (!xmlData.isEmpty()) {
                // Load the log data into mapWidgetAnalysis
                QXmlStreamReader xmlReader(xmlData);
                bool success = ui->mapWidgetAnalysis->loadXMLRoute(&xmlReader, false); // false = not a border
                if (success) {
                    qDebug() << "Successfully loaded log for path:" << pathId;
                    ui->mapWidgetAnalysis->update();
                } else {
                    qDebug() << "Failed to load log XML for path:" << pathId;
                }
            } else {
                qDebug() << "Empty log data received for path:" << pathId;
            }
        } else {
            qDebug() << "Error fetching log for path:" << pathId << "- Error:" << reply->errorString();
        }
        reply->deleteLater();
    });
}

void MainWindow::onFarmSelectedForLog(int index)
{
    qDebug() << "onFarmSelectedForLog: Farm index selected:" << index;
    
    if (index < 0 || !logFarmsModel || index >= logFarmsModel->rowCount()) {
        qDebug() << "Invalid farm index or model not ready";
        logFieldsModel->removeRows(0, logFieldsModel->rowCount());
        logPathsModel->removeRows(0, logPathsModel->rowCount());
        return;
    }
    
    QStandardItem* farmItem = logFarmsModel->item(index);
    if (farmItem) {
        QString farmId = farmItem->data(Qt::UserRole).toString();
        qDebug() << "Selected farm ID:" << farmId;
        
        if (!farmId.isEmpty()) {
            fetchFieldsForLogFarm(farmId.toInt());
        } else {
            qDebug() << "No farm ID found for selected farm";
        }
    }
}

void MainWindow::onFieldSelectedForLog(int index)
{
    qDebug() << "onFieldSelectedForLog: Field index selected:" << index;
    
    if (index < 0 || !logFieldsModel || index >= logFieldsModel->rowCount()) {
        qDebug() << "Invalid field index or model not ready";
        logPathsModel->removeRows(0, logPathsModel->rowCount());
        return;
    }
    
    QStandardItem* fieldItem = logFieldsModel->item(index);
    if (fieldItem) {
        QString fieldId = fieldItem->data(Qt::UserRole).toString();
        qDebug() << "Selected field ID:" << fieldId;
        
        if (!fieldId.isEmpty()) {
            fetchPathsForLogField(fieldId.toInt());
        } else {
            qDebug() << "No field ID found for selected field";
        }
    }
}

void MainWindow::onPathSelectedForLog(int index)
{
    qDebug() << "onPathSelectedForLog: Path index selected:" << index;
    
    if (index < 0 || !logPathsModel || index >= logPathsModel->rowCount()) {
        qDebug() << "Invalid path index or model not ready";
        logLogsModel->removeRows(0, logLogsModel->rowCount());
        return;
    }
    
    QStandardItem* pathItem = logPathsModel->item(index);
    if (pathItem) {
        QString pathId = pathItem->data(Qt::UserRole).toString();
        qDebug() << "Selected path ID:" << pathId;
        
        if (!pathId.isEmpty()) {
            fetchLogsForPath(pathId.toInt());
        } else {
            qDebug() << "No path ID found for selected path";
        }
    }
}

void MainWindow::onLoadLogButtonClicked()
{
    qDebug() << "onLoadLogButtonClicked: Load log button clicked";
    
    int pathIndex = ui->comboBoxLogPath->currentIndex();
    if (pathIndex < 0 || !logPathsModel || pathIndex >= logPathsModel->rowCount()) {
        qDebug() << "Invalid path selection";
        return;
    }
    
    QStandardItem* pathItem = logPathsModel->item(pathIndex);
    if (pathItem) {
        QString pathId = pathItem->data(Qt::UserRole).toString();
        qDebug() << "Loading log for path ID:" << pathId;
        
        if (!pathId.isEmpty()) {
            loadPathAsLog(pathId.toInt());
        } else {
            qDebug() << "No path ID found for selected path";
        }
    }
}

void MainWindow::fetchLogsForPath(int pathId)
{
    qDebug() << "fetchLogsForPath: Starting for pathId:" << pathId;
    
    if (!logLogsModel) {
        qDebug() << "ERROR: logLogsModel is null!";
        return;
    }
    
    // Clear existing data
    logLogsModel->removeRows(0, logLogsModel->rowCount());
    
    // Add loading indicator
    logLogsModel->appendRow(new QStandardItem("Loading logs..."));
    
    QUrl url(getServerBaseUrl() + "/all_logs");
    QUrlQuery query;
    query.addQueryItem("path", QString::number(pathId));
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    if (!reply) {
        qDebug() << "ERROR: reply is null in fetchLogsForPath!";
        logLogsModel->removeRows(0, logLogsModel->rowCount());
        logLogsModel->appendRow(new QStandardItem("Error: Network request failed"));
        return;
    }
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, pathId]() {
        logLogsModel->removeRows(0, logLogsModel->rowCount());
        
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "fetchLogsForPath HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received logs data for path" << pathId << "(size:" << xmlData.size() << ")";
            
            if (!xmlData.isEmpty()) {
                parseAllLogsXmlForLog(xmlData);
            } else {
                qDebug() << "Empty response for logs";
                logLogsModel->appendRow(new QStandardItem("No logs data"));
            }
        } else {
            qDebug() << "Error fetching logs for path:" << pathId << "- Error:" << reply->errorString();
            logLogsModel->appendRow(new QStandardItem("Error loading logs"));
        }
        reply->deleteLater();
    });
}

void MainWindow::parseAllLogsXmlForLog(const QByteArray &xmlData)
{
    qDebug() << "parseAllLogsXmlForLog: Starting";
    
    if (!logLogsModel) {
        qDebug() << "ERROR: logLogsModel is null!";
        return;
    }
    
    QXmlStreamReader xmlReader(xmlData);
    
    while (!xmlReader.atEnd()) {
        QXmlStreamReader::TokenType token = xmlReader.readNext();
        
        if (token == QXmlStreamReader::StartElement && xmlReader.name() == QLatin1String("log")) {
            QString id, name;
            
            while (!xmlReader.atEnd()) {
                token = xmlReader.readNext();
                
                if (token == QXmlStreamReader::EndElement && xmlReader.name() == QLatin1String("log")) {
                    break;
                }
                
                if (token == QXmlStreamReader::StartElement) {
                    if (xmlReader.name() == QLatin1String("id")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            id = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("name")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            name = xmlReader.text().toString();
                        }
                    }
                }
            }
            
            if (!name.isEmpty()) {
                QStandardItem* logItem = new QStandardItem(name);
                if (!id.isEmpty()) {
                    logItem->setData(id, Qt::UserRole); // Store ID as user data
                }
                logLogsModel->appendRow(logItem);
                qDebug() << "Added log to log tab:" << name << "(ID:" << id << ")";
            }
        }
    }
    
    if (xmlReader.hasError()) {
        qDebug() << "XML parsing error in parseAllLogsXmlForLog:" << xmlReader.errorString();
    }
    
    qDebug() << "parseAllLogsXmlForLog: Logs loaded:" << logLogsModel->rowCount();
}

void MainWindow::fetchLogForLog(int logId)
{
    qDebug() << "fetchLogForLog: Starting for logId:" << logId;
    
    QUrl url(getServerBaseUrl() + "/log");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(logId));
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    if (!reply) {
        qDebug() << "ERROR: reply is null in fetchLogForLog!";
        return;
    }
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, logId]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "fetchLogForLog HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received log data for log" << logId << "(size:" << xmlData.size() << ")";
            
            if (!xmlData.isEmpty()) {
                // Load the log data into mapWidgetAnalysis
                QXmlStreamReader xmlReader(xmlData);
                bool success = ui->mapWidgetAnalysis->loadXMLRoute(&xmlReader, false); // false = not a border
                if (success) {
                    qDebug() << "Successfully loaded log for log:" << logId;
                    ui->mapWidgetAnalysis->update();
                    
                    // Store the original log for non-destructive filtering
                    if (ui->mapWidgetAnalysis->mPaths->size() > 0) {
                        MapRoute originalRoute = ui->mapWidgetAnalysis->getCurrentPath();
                        mOriginalLogs.append(originalRoute);
                    }
                    
                    // Initialize RangeSlider based on the loaded route
                    if (ui->mapWidgetAnalysis->mPaths->size() > 0) {
                        MapRoute& currentRoute = ui->mapWidgetAnalysis->getCurrentPath();
                        int totalPoints = currentRoute.size();
                        if (totalPoints > 0) {
                            // Set default range to 20-80% of the route
                            ui->rangeSlider->setRange(0, 100);
                            ui->rangeSlider->setLowerValue(20);
                            ui->rangeSlider->setUpperValue(80);
                            
                            // Update our stored values
                            mRangeSliderLowerValue = 20;
                            mRangeSliderUpperValue = 80;
                        }
                    }
                } else {
                    qDebug() << "Failed to load log XML for log:" << logId;
                }
            } else {
                qDebug() << "Empty log data received for log:" << logId;
            }
        } else {
            qDebug() << "Error fetching log for log:" << logId << "- Error:" << reply->errorString();
        }
        reply->deleteLater();
    });
}

void MainWindow::onLogSelectedForLog(int index)
{
    qDebug() << "onLogSelectedForLog: Log index selected:" << index;
    
    if (index < 0 || !logLogsModel || index >= logLogsModel->rowCount()) {
        qDebug() << "Invalid log index or model not ready";
        return;
    }
    
    QStandardItem* logItem = logLogsModel->item(index);
    if (logItem) {
        QString logId = logItem->data(Qt::UserRole).toString();
        qDebug() << "Selected log ID:" << logId;
        
        if (!logId.isEmpty()) {
            fetchLogForLog(logId.toInt());
        } else {
            qDebug() << "No log ID found for selected log";
        }
    }
}

void MainWindow::fetchAllFieldsData(int farmId, int retryCount)
{
    qDebug() << "fetchAllFieldsData: Starting for farmId:" << farmId << "retryCount:" << retryCount;
    const int MAX_RETRIES = 3;
    const int RETRY_DELAY_MS = 2000; // 2 seconds between retries
    
    QUrl url(getServerBaseUrl() + "/all_fields");
    QUrlQuery query;
    query.addQueryItem("farm", QString::number(farmId));
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    // Check if fieldsModel is initialized
    if (!fieldsModel) {
        qDebug() << "ERROR: fieldsModel is null in fetchAllFieldsData!";
        return;
    }
    
    // Clear the model
    fieldsModel->removeRows(0, fieldsModel->rowCount());
    
    // Add loading state
    fieldsModel->appendRow(new QStandardItem(retryCount > 0 ? QString("Retrying... (%1/%2)").arg(retryCount).arg(MAX_RETRIES) : "Loading..."));
    fieldsModel->appendRow(new QStandardItem(""));
    
    // Connect the finished signal to parse the response
    QNetworkReply* reply = mNetworkManager->get(request);
    if (!reply) {
        qDebug() << "ERROR: reply is null in fetchAllFieldsData!";
        return;
    }
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, farmId, retryCount]() {
        // Clear loading message
        fieldsModel->removeRows(0, fieldsModel->rowCount());
        
        // Check HTTP status code
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "HTTP Status Code (all_fields):" << statusCode;
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Network error:" << reply->error() << "-" << reply->errorString();
        }
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received XML data from all_fields (size:" << xmlData.size() << "):";
            qDebug() << "XML content:" << xmlData;
            
            if (xmlData.isEmpty()) {
                qDebug() << "Empty response received from all_fields";
                fieldsModel->appendRow(new QStandardItem("No data"));
                fieldsModel->appendRow(new QStandardItem("Empty response"));
            } else {
                // Check if response contains valid XML
                bool hasFields = xmlData.contains("<fields>") && xmlData.contains("</fields>");
                qDebug() << "XML validation - has <fields>:" << hasFields;
                
                if (hasFields) {
                    parseAllFieldsXml(xmlData);
                } else {
                    qDebug() << "XML validation failed - missing fields tags";
                    fieldsModel->appendRow(new QStandardItem("Invalid data"));
                    fieldsModel->appendRow(new QStandardItem("Bad format"));
                }
            }
        } else {
            qDebug() << "Error fetching all_fields data:" << reply->errorString();
            qDebug() << "Error code:" << reply->error();
            qDebug() << "HTTP Status Code:" << statusCode;
            
            // Retry if we haven't exceeded max retries and it's a timeout or temporary error
            if (retryCount < MAX_RETRIES && 
                (reply->error() == QNetworkReply::TimeoutError ||
                 reply->error() == QNetworkReply::TemporaryNetworkFailureError ||
                 reply->error() == QNetworkReply::ConnectionRefusedError)) {
                qDebug() << "Retrying all_fields in" << RETRY_DELAY_MS << "ms...";
                QTimer::singleShot(RETRY_DELAY_MS, this, [this, farmId, retryCount]() {
                    fetchAllFieldsData(farmId, retryCount + 1);
                });
            } else {
                // Show final error after all retries failed
                QString errorMsg = reply->errorString();
                if (statusCode != 200 && statusCode > 0) {
                    errorMsg = QString("HTTP %1").arg(statusCode);
                }
                fieldsModel->appendRow(new QStandardItem("Error"));
                fieldsModel->appendRow(new QStandardItem(errorMsg));
            }
        }
        reply->deleteLater();
    });
    
    qDebug() << "Fetching all_fields data from:" << url.toString() << "(attempt" << (retryCount + 1) << ")";
}

void MainWindow::fetchUnconnectedFieldsData(int retryCount)
{
    qDebug() << "fetchUnconnectedFieldsData: Starting, retryCount:" << retryCount;
    const int MAX_RETRIES = 3;
    const int RETRY_DELAY_MS = 2000; // 2 seconds between retries

    if (!mUnconnectedFieldsTable) {
        qDebug() << "ERROR: mUnconnectedFieldsTable is null in fetchUnconnectedFieldsData!";
        return;
    }

    // Clear the table
    mUnconnectedFieldsTable->setRowCount(0);
    
    // Add loading indicator
    mUnconnectedFieldsTable->insertRow(0);
    mUnconnectedFieldsTable->setItem(0, 0, new QTableWidgetItem(retryCount > 0 ? QString("Retrying... (%1/%2)").arg(retryCount).arg(MAX_RETRIES) : "Loading..."));

    QUrl url(getServerBaseUrl() + "/unconnected_fields");
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);

    QNetworkReply* reply = mNetworkManager->get(request);
    if (!reply) {
        qDebug() << "ERROR: reply is null in fetchUnconnectedFieldsData!";
        mUnconnectedFieldsTable->setRowCount(0);
        mUnconnectedFieldsTable->insertRow(0);
        mUnconnectedFieldsTable->setItem(0, 0, new QTableWidgetItem("Error: Network request failed"));
        return;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, retryCount]() {
        // Clear loading message
        mUnconnectedFieldsTable->setRowCount(0);

        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "HTTP Status Code (unconnected_fields):" << statusCode;
        
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Network error:" << reply->error() << "-" << reply->errorString();
        }

        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received XML data from unconnected_fields (size:" << xmlData.size() << ")";
            
            if (!xmlData.isEmpty()) {
                parseUnconnectedFieldsXml(xmlData);
            } else {
                qDebug() << "Empty response for unconnected_fields";
                mUnconnectedFieldsTable->insertRow(0);
                mUnconnectedFieldsTable->setItem(0, 0, new QTableWidgetItem("No unconnected fields data"));
            }
        } else {
            QString errorMsg = reply->errorString();
            if (statusCode != 200 && statusCode > 0) {
                errorMsg = QString("HTTP %1").arg(statusCode);
            }
            qDebug() << "Error fetching unconnected_fields:" << errorMsg;
            
            // Retry logic
            if (retryCount < MAX_RETRIES) {
                QTimer::singleShot(RETRY_DELAY_MS, this, [this, retryCount]() {
                    fetchUnconnectedFieldsData(retryCount + 1);
                });
            } else {
                mUnconnectedFieldsTable->insertRow(0);
                mUnconnectedFieldsTable->setItem(0, 0, new QTableWidgetItem("Error"));
                mUnconnectedFieldsTable->insertRow(1);
                mUnconnectedFieldsTable->setItem(1, 0, new QTableWidgetItem(errorMsg));
            }
        }
        reply->deleteLater();
    });

    qDebug() << "Fetching unconnected_fields data from:" << url.toString() << "(attempt" << (retryCount + 1) << ")";
}

void MainWindow::fetchAllPathsData(int fieldId, int retryCount)
{
    qDebug() << "fetchAllPathsData: Starting for fieldId:" << fieldId << "retryCount:" << retryCount;
    const int MAX_RETRIES = 3;
    const int RETRY_DELAY_MS = 2000; // 2 seconds between retries
    
    QUrl url(getServerBaseUrl() + "/all_paths");
    QUrlQuery query;
    query.addQueryItem("field", QString::number(fieldId));
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    // Check if modelPath is initialized
    if (!modelPath) {
        qDebug() << "ERROR: modelPath is null in fetchAllPathsData!";
        return;
    }
    
    // Clear the model
    modelPath->removeRows(0, modelPath->rowCount());
    
    // Add loading state
    modelPath->appendRow(new QStandardItem(retryCount > 0 ? QString("Retrying... (%1/%2)").arg(retryCount).arg(MAX_RETRIES) : "Loading..."));
    modelPath->appendRow(new QStandardItem(""));
    
    // Connect the finished signal to parse the response
    QNetworkReply* reply = mNetworkManager->get(request);
    if (!reply) {
        qDebug() << "ERROR: reply is null in fetchAllPathsData!";
        return;
    }
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, fieldId, retryCount]() {
        // Clear loading message
        modelPath->removeRows(0, modelPath->rowCount());
        
        // Check HTTP status code
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "HTTP Status Code (all_paths):" << statusCode;
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Network error:" << reply->error() << "-" << reply->errorString();
        }
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received XML data from all_paths (size:" << xmlData.size() << "):";
            qDebug() << "XML content:" << xmlData;
            
            if (xmlData.isEmpty()) {
                qDebug() << "Empty response received from all_paths";
                modelPath->appendRow(new QStandardItem("No data"));
                modelPath->appendRow(new QStandardItem("Empty response"));
            } else {
                // Check if response contains valid XML
                bool hasPaths = xmlData.contains("<paths>") && xmlData.contains("</paths>");
                qDebug() << "XML validation - has <paths>:" << hasPaths;
                
                if (hasPaths) {
                    parseAllPathsXml(xmlData);
                } else {
                    qDebug() << "XML validation failed - missing paths tags";
                    modelPath->appendRow(new QStandardItem("Invalid data"));
                    modelPath->appendRow(new QStandardItem("Bad format"));
                }
            }
        } else {
            qDebug() << "Error fetching all_paths data:" << reply->errorString();
            qDebug() << "Error code:" << reply->error();
            qDebug() << "HTTP Status Code:" << statusCode;
            
            // Retry if we haven't exceeded max retries and it's a timeout or temporary error
            if (retryCount < MAX_RETRIES && 
                (reply->error() == QNetworkReply::TimeoutError ||
                 reply->error() == QNetworkReply::TemporaryNetworkFailureError ||
                 reply->error() == QNetworkReply::ConnectionRefusedError)) {
                qDebug() << "Retrying all_paths in" << RETRY_DELAY_MS << "ms...";
                QTimer::singleShot(RETRY_DELAY_MS, this, [this, fieldId, retryCount]() {
                    fetchAllPathsData(fieldId, retryCount + 1);
                });
            } else {
                // Show final error after all retries failed
                QString errorMsg = reply->errorString();
                if (statusCode != 200 && statusCode > 0) {
                    errorMsg = QString("HTTP %1").arg(statusCode);
                }
                modelPath->appendRow(new QStandardItem("Error"));
                modelPath->appendRow(new QStandardItem(errorMsg));
            }
        }
        reply->deleteLater();
    });
    
    qDebug() << "Fetching all_paths data from:" << url.toString() << "(attempt" << (retryCount + 1) << ")";
}

void MainWindow::fetchVehicleTypes(int retryCount)
{
    const int MAX_RETRIES = 3;
    const int RETRY_DELAY_MS = 2000; // 2 seconds between retries
    
    QUrl url(getServerBaseUrl() + "/vehicle_types");
    QNetworkRequest request(url);
    
    // Set a timeout for the request (10 seconds to be safe)
    request.setTransferTimeout(10000);
    
    // Disconnect any existing connections to prevent multiple handlers
    disconnect(mNetworkManager, &QNetworkAccessManager::finished, this, nullptr);
    
    // Clear the model
    vehicleTypesModel->removeRows(0, vehicleTypesModel->rowCount());
    
    // Add loading state
    vehicleTypesModel->appendRow(new QStandardItem(retryCount > 0 ? QString("Retrying... (%1/%2)").arg(retryCount).arg(MAX_RETRIES) : "Loading..."));
    
    // Connect the finished signal to parse the response
    QNetworkReply* reply = mNetworkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, retryCount]() {
        // Clear loading message
        vehicleTypesModel->removeRows(0, vehicleTypesModel->rowCount());
        
        // Check HTTP status code
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "HTTP Status Code (vehicle_types):" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received XML data from vehicle_types (size:" << xmlData.size() << "):" << xmlData;
            
            if (xmlData.isEmpty()) {
                qDebug() << "Empty response received from vehicle_types";
            } else {
                // Always try to parse the XML, let the parser handle any errors
                parseVehicleTypesXml(xmlData);
            }
            // After loading vehicle types (or failing), refresh machines data to ensure table has content
            fetchAllMachinesData();
        } else {
            // Retry if we haven't exceeded max retries and it's a timeout or temporary error
            if (retryCount < MAX_RETRIES && 
                (reply->error() == QNetworkReply::TimeoutError ||
                 reply->error() == QNetworkReply::TemporaryNetworkFailureError ||
                 reply->error() == QNetworkReply::ConnectionRefusedError)) {
                QTimer::singleShot(RETRY_DELAY_MS, this, [this, retryCount]() {
                    fetchVehicleTypes(retryCount + 1);
                });
            } else {
                // If all retries failed, still try to fetch machines data so table shows error state
                fetchAllMachinesData();
            }
        }
        reply->deleteLater();
    });
    

}

void MainWindow::parseVehicleTypesXml(const QByteArray &xmlData)
{
    // Clear existing data
    vehicleTypesModel->removeRows(0, vehicleTypesModel->rowCount());
    
    // Parse the XML
    QXmlStreamReader xmlReader(xmlData);
    

    
    while (!xmlReader.atEnd()) {
        QXmlStreamReader::TokenType token = xmlReader.readNext();
        
        if (token == QXmlStreamReader::StartElement && xmlReader.name() == QLatin1String("vehicle_type")) {
            QString id, name;


            
            // Read vehicle_type element contents
            while (!xmlReader.atEnd()) {
                token = xmlReader.readNext();
                
                if (token == QXmlStreamReader::EndElement && xmlReader.name() == QLatin1String("vehicle_type")) {
                    break; // End of vehicle_type element
                }
                
                if (token == QXmlStreamReader::StartElement) {
                    if (xmlReader.name() == QLatin1String("id")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            id = xmlReader.text().toString();

                        }
                    } else if (xmlReader.name() == QLatin1String("name")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            name = xmlReader.text().toString();
                        }
                    }
                }
            }
            
            // Add the vehicle type to the model if we have both id and name
            if (!id.isEmpty() && !name.isEmpty()) {
                QStandardItem* item = new QStandardItem(name);
                item->setData(id, Qt::UserRole); // Store ID as user data
                vehicleTypesModel->appendRow(item);

            } else {
    
            }
        }
    }
    
    if (xmlReader.hasError()) {
        qDebug() << "XML parsing error for vehicle_types:" << xmlReader.errorString();
    }
    

}

void MainWindow::parseAllMachinesXml(const QByteArray &xmlData)
{
    // Clear existing data
    machinesModel->removeRows(0, machinesModel->rowCount());
    
    // Parse the XML
    QXmlStreamReader xmlReader(xmlData);
    bool foundMachines = false;
    
    qDebug() << "Starting XML parsing for all_machines...";
    
    while (!xmlReader.atEnd()) {
        QXmlStreamReader::TokenType token = xmlReader.readNext();
        
        if (token == QXmlStreamReader::StartElement && xmlReader.name() == QLatin1String("machine")) {
            QString id, name, ip, vehicleTypeId;
            foundMachines = true;
            qDebug() << "Found machine element in all_machines";
            
            // Read machine element contents
            while (!xmlReader.atEnd()) {
                token = xmlReader.readNext();
                
                if (token == QXmlStreamReader::EndElement && xmlReader.name() == QLatin1String("machine")) {
                    break; // End of machine element
                }
                
                if (token == QXmlStreamReader::StartElement) {
                    if (xmlReader.name() == QLatin1String("id")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            id = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("name")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            name = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("ip")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            ip = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("iVehicletype") || 
                               xmlReader.name() == QLatin1String("vehicle_type_id") || 
                               xmlReader.name() == QLatin1String("vehicle_type") ||
                               xmlReader.name() == QLatin1String("vehicletype")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            vehicleTypeId = xmlReader.text().toString();
                        }
                    }
                }
            }
            
            // Add the machine to the model if we have both name and IP
            if (!name.isEmpty() && !ip.isEmpty()) {
                QList<QStandardItem*> rowItems;
                QStandardItem* nameItem = new QStandardItem(name);
                if (!id.isEmpty()) {
                    nameItem->setData(id, Qt::UserRole); // Store ID as user data
                }
                rowItems.append(nameItem);
                rowItems.append(new QStandardItem(ip));
                
                // Look up vehicle type name from vehicleTypesModel using the vehicleTypeId
                QString vehicleTypeName = "";
                if (!vehicleTypeId.isEmpty()) {
                    for (int row = 0; row < vehicleTypesModel->rowCount(); ++row) {
                        QStandardItem* item = vehicleTypesModel->item(row);
                        if (item && item->data(Qt::UserRole).toString() == vehicleTypeId) {
                            vehicleTypeName = item->text();
                            break;
                        }
                    }
                }
                
                rowItems.append(new QStandardItem(vehicleTypeName));
                machinesModel->appendRow(rowItems);
            } else {
                qDebug() << "Machine missing name or IP in all_machines - name:" << name << ", ip:" << ip;
            }
        }
    }
    
    if (xmlReader.hasError()) {
        qDebug() << "XML parsing error for all_machines:" << xmlReader.errorString();
        qDebug() << "Error line:" << xmlReader.lineNumber() << "column:" << xmlReader.columnNumber();
        
        // If no machines were found and there was an error, show the error in the model
        if (!foundMachines) {
            machinesModel->appendRow(new QStandardItem("XML Error"));
            machinesModel->appendRow(new QStandardItem(xmlReader.errorString()));
        }
    }
    
    // If no machines were found but no error occurred, show a message
    if (!foundMachines && !xmlReader.hasError()) {
        qDebug() << "No machines found in all_machines XML";
        machinesModel->appendRow(new QStandardItem("No machines"));
        machinesModel->appendRow(new QStandardItem("found"));
    }
}

void MainWindow::parseAllFarmsXml(const QByteArray &xmlData)
{
    // Clear existing data
    farmsModel->removeRows(0, farmsModel->rowCount());
    
    // Parse the XML
    QXmlStreamReader xmlReader(xmlData);
    bool foundFarms = false;
    
    qDebug() << "Starting XML parsing for all_farms...";
    qDebug() << "XML data size:" << xmlData.size();
    qDebug() << "XML data preview:" << xmlData.left(200) << "...";
    
    while (!xmlReader.atEnd()) {
        QXmlStreamReader::TokenType token = xmlReader.readNext();
        
        if (token == QXmlStreamReader::StartElement && xmlReader.name() == QLatin1String("location")) {
            qDebug() << "Found location element in all_farms";
            QString id, name, ip, port, ntrip, user, password, stream, autoconnect;
            double longitude = 0.0, latitude = 0.0;
            foundFarms = true;
            
            // Read location element contents
            while (!xmlReader.atEnd()) {
                token = xmlReader.readNext();
                
                if (token == QXmlStreamReader::EndElement && xmlReader.name() == QLatin1String("location")) {
                    break; // End of location element
                }
                
                if (token == QXmlStreamReader::StartElement) {
                    if (xmlReader.name() == QLatin1String("id")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            id = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("name")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            name = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("longitude")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            longitude = xmlReader.text().toString().toDouble();
                        }
                    } else if (xmlReader.name() == QLatin1String("latitude")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            latitude = xmlReader.text().toString().toDouble();
                        }
                    } else if (xmlReader.name() == QLatin1String("ip")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            ip = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("port")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            port = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("NTRIP")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            ntrip = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("user")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            user = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("password")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            password = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("stream")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            stream = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("autoconnect")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            autoconnect = xmlReader.text().toString();
                        }
                    }
                }
            }
            
            // Add the location to the model if we have at least a name
            if (!name.isEmpty()) {
                qDebug() << "Adding location:" << name << "with ID:" << id << "Long:" << longitude << "Lat:" << latitude;
                QList<QStandardItem*> rowItems;
                QStandardItem* nameItem = new QStandardItem(name);
                if (!id.isEmpty()) {
                    nameItem->setData(id, Qt::UserRole); // Store ID as user data
                }
                rowItems.append(nameItem);
                rowItems.append(new QStandardItem(QString::number(longitude, 'f', 14))); // Longitude
                rowItems.append(new QStandardItem(QString::number(latitude, 'f', 14))); // Latitude
                rowItems.append(new QStandardItem(ip));
                rowItems.append(new QStandardItem(port));
                rowItems.append(new QStandardItem(ntrip));
                rowItems.append(new QStandardItem(user));
                rowItems.append(new QStandardItem(password));
                rowItems.append(new QStandardItem(stream));
                rowItems.append(new QStandardItem(autoconnect));
                farmsModel->appendRow(rowItems);
            } else {
                qDebug() << "Location missing name in all_farms - name:" << name;
            }
        }
    }
    
    if (xmlReader.hasError()) {
        qDebug() << "XML parsing error for all_farms:" << xmlReader.errorString();
        qDebug() << "Error line:" << xmlReader.lineNumber() << "column:" << xmlReader.columnNumber();
        
        // If no locations were found and there was an error, show the error in the model
        if (!foundFarms) {
            farmsModel->appendRow(new QStandardItem("XML Error"));
            farmsModel->appendRow(new QStandardItem(xmlReader.errorString()));
        }
    }
    
    // If no locations were found but no error occurred, show a message
    if (!foundFarms && !xmlReader.hasError()) {
        qDebug() << "No locations found in all_farms XML";
        farmsModel->appendRow(new QStandardItem("No locations"));
        farmsModel->appendRow(new QStandardItem("found"));
    }
}

void MainWindow::parseAllFieldsXml(const QByteArray &xmlData)
{
    // Clear existing data
    fieldsModel->removeRows(0, fieldsModel->rowCount());
    
    // Parse the XML
    QXmlStreamReader xmlReader(xmlData);
    bool foundFields = false;
    
    qDebug() << "Starting XML parsing for all_fields...";
    qDebug() << "XML data size:" << xmlData.size();
    qDebug() << "XML data preview:" << xmlData.left(200) << "...";
    
    while (!xmlReader.atEnd()) {
        QXmlStreamReader::TokenType token = xmlReader.readNext();
        
        if (token == QXmlStreamReader::StartElement && xmlReader.name() == QLatin1String("field")) {
            QString id, name, storedinfile, location;
            bool fenced = false;
            foundFields = true;
            qDebug() << "Found field element in all_fields";
            
            // Read field element contents
            while (!xmlReader.atEnd()) {
                token = xmlReader.readNext();
                
                if (token == QXmlStreamReader::EndElement && xmlReader.name() == QLatin1String("field")) {
                    break; // End of field element
                }
                
                if (token == QXmlStreamReader::StartElement) {
                    if (xmlReader.name() == QLatin1String("id")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            id = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("name")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            name = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("fenced")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            QString fencedStr = xmlReader.text().toString();
                            fenced = (fencedStr == "1" || fencedStr.toLower() == "true");
                        }
                    } else if (xmlReader.name() == QLatin1String("storedinfile")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            storedinfile = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("location")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            location = xmlReader.text().toString();
                        }
                    }
                }
            }
            
            // Add the field to the model if we have at least a name
            if (!name.isEmpty()) {
                qDebug() << "Adding field:" << name << "with ID:" << id << "Fenced:" << fenced << "File:" << storedinfile;
                QList<QStandardItem*> rowItems;
                QStandardItem* locationItem = new QStandardItem(location);
                QStandardItem* nameItem = new QStandardItem(name);
                if (!id.isEmpty()) {
                    nameItem->setData(id, Qt::UserRole); // Store ID as user data
                }
                QStandardItem* fencedItem = new QStandardItem("");
                fencedItem->setCheckable(true);
                fencedItem->setCheckState(fenced ? Qt::Checked : Qt::Unchecked);
                
                rowItems.append(locationItem);
                rowItems.append(nameItem);
                rowItems.append(fencedItem);
                rowItems.append(new QStandardItem(storedinfile));
                rowItems.append(new QStandardItem(id));
                fieldsModel->appendRow(rowItems);
                
                // Fetch the field XML from webserver and load it into the map widget
                if (!id.isEmpty()) {
                    fetchFieldXml(id.toInt(), name);
                }
            } else {
                qDebug() << "Field missing name in all_fields - name:" << name;
            }
        }
    }
    
    if (xmlReader.hasError()) {
        qDebug() << "XML parsing error for all_fields:" << xmlReader.errorString();
        qDebug() << "Error line:" << xmlReader.lineNumber() << "column:" << xmlReader.columnNumber();
        
        // If no fields were found and there was an error, show the error in the model
        if (!foundFields) {
            fieldsModel->appendRow(new QStandardItem("XML Error"));
            fieldsModel->appendRow(new QStandardItem(xmlReader.errorString()));
        }
    }
    
    // If no fields were found but no error occurred, show a message
    if (!foundFields && !xmlReader.hasError()) {
        qDebug() << "No fields found in all_fields XML";
        fieldsModel->appendRow(new QStandardItem("No fields"));
        fieldsModel->appendRow(new QStandardItem("found"));
    }
}

void MainWindow::parseUnconnectedFieldsXml(const QByteArray &xmlData)
{
    qDebug() << "parseUnconnectedFieldsXml: Starting";
    
    if (!mUnconnectedFieldsTable) {
        qDebug() << "ERROR: mUnconnectedFieldsTable is null in parseUnconnectedFieldsXml!";
        return;
    }
    
    // Clear existing data
    mUnconnectedFieldsTable->setRowCount(0);

    // Parse the XML
    QXmlStreamReader xmlReader(xmlData);
    bool foundFiles = false;

    while (!xmlReader.atEnd()) {
        QXmlStreamReader::TokenType token = xmlReader.readNext();

        if (token == QXmlStreamReader::StartElement && xmlReader.name() == QLatin1String("path")) {
            QString filename;
            foundFiles = true;
            qDebug() << "Found path element in unconnected_fields";

            // Read path element contents
            while (!xmlReader.atEnd()) {
                token = xmlReader.readNext();

                if (token == QXmlStreamReader::EndElement && xmlReader.name() == QLatin1String("path")) {
                    break; // End of path element
                }

                if (token == QXmlStreamReader::StartElement) {
                    if (xmlReader.name() == QLatin1String("filename")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            filename = xmlReader.text().toString();
                        }
                    }
                }
            }
            
            // Add the filename to the table if we got one
            if (!filename.isEmpty()) {
                int row = mUnconnectedFieldsTable->rowCount();
                mUnconnectedFieldsTable->insertRow(row);
                mUnconnectedFieldsTable->setItem(row, 0, new QTableWidgetItem(filename));
                qDebug() << "Added file to table:" << filename;
            }
        }
    }

    if (!foundFiles) {
        qDebug() << "No files found in unconnected_fields XML";
        mUnconnectedFieldsTable->insertRow(0);
        mUnconnectedFieldsTable->setItem(0, 0, new QTableWidgetItem("No files found"));
    }

    if (xmlReader.hasError()) {
        qDebug() << "XML parsing error:" << xmlReader.errorString();
        mUnconnectedFieldsTable->insertRow(0);
        mUnconnectedFieldsTable->setItem(0, 0, new QTableWidgetItem("XML Error"));
        mUnconnectedFieldsTable->insertRow(1);
        mUnconnectedFieldsTable->setItem(1, 0, new QTableWidgetItem(xmlReader.errorString()));
    }
}

void MainWindow::parseAllPathsXml(const QByteArray &xmlData)
{
    // Clear existing data
    modelPath->removeRows(0, modelPath->rowCount());
    
    // Parse the XML
    QXmlStreamReader xmlReader(xmlData);
    bool foundPaths = false;
    
    qDebug() << "Starting XML parsing for all_paths...";
    qDebug() << "XML data size:" << xmlData.size();
    qDebug() << "XML data preview:" << xmlData.left(200) << "...";
    
    while (!xmlReader.atEnd()) {
        QXmlStreamReader::TokenType token = xmlReader.readNext();
        
        if (token == QXmlStreamReader::StartElement && xmlReader.name() == QLatin1String("path")) {
            QString id, name, field;
            foundPaths = true;
            qDebug() << "Found path element in all_paths";
            
            // Read path element contents
            while (!xmlReader.atEnd()) {
                token = xmlReader.readNext();
                
                if (token == QXmlStreamReader::EndElement && xmlReader.name() == QLatin1String("path")) {
                    break; // End of path element
                }
                
                if (token == QXmlStreamReader::StartElement) {
                    if (xmlReader.name() == QLatin1String("id")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            id = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("name")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            name = xmlReader.text().toString();
                        }
                    } else if (xmlReader.name() == QLatin1String("field")) {
                        token = xmlReader.readNext();
                        if (token == QXmlStreamReader::Characters) {
                            field = xmlReader.text().toString();
                        }
                    }
                }
            }
            
            // Add the path to the model if we have at least a name
            if (!name.isEmpty()) {
                qDebug() << "Adding path:" << name << "with ID:" << id << "Field:" << field;
                QList<QStandardItem*> rowItems;
                QStandardItem* nameItem = new QStandardItem(name);
                if (!id.isEmpty()) {
                    nameItem->setData(id, Qt::UserRole); // Store ID as user data
                }
                rowItems.append(nameItem);
                rowItems.append(new QStandardItem(id)); // Id column
                rowItems.append(new QStandardItem(field)); // Field column
                modelPath->appendRow(rowItems);
            } else {
                qDebug() << "Path missing name in all_paths - name:" << name;
            }
        }
    }
    
    if (xmlReader.hasError()) {
        qDebug() << "XML parsing error for all_paths:" << xmlReader.errorString();
        qDebug() << "Error line:" << xmlReader.lineNumber() << "column:" << xmlReader.columnNumber();
        
        // If no paths were found and there was an error, show the error in the model
        if (!foundPaths) {
            modelPath->appendRow(new QStandardItem("XML Error"));
            modelPath->appendRow(new QStandardItem(xmlReader.errorString()));
        }
    }
    
    // If no paths were found but no error occurred, show a message
    if (!foundPaths && !xmlReader.hasError()) {
        qDebug() << "No paths found in all_paths XML";
        modelPath->appendRow(new QStandardItem("No paths"));
        modelPath->appendRow(new QStandardItem("found"));
    }
}

void MainWindow::addFarmToServer(const QString &name)
{
    QUrl url(getServerBaseUrl() + "/add_farm");
    QUrlQuery query;
    query.addQueryItem("name", name);
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, name]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "Add farm HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            qDebug() << "Farm added successfully";
            // Refresh the farms list
            fetchAllFarmsData();
        } else {
            qDebug() << "Error adding farm:" << reply->errorString();
            qDebug() << "HTTP Status Code:" << statusCode;
        }
        reply->deleteLater();
    });
    
    qDebug() << "Adding farm:" << name;
}

void MainWindow::updateFarmOnServer(int farmId, const QString &name, double latitude, double longitude)
{
    QUrl url(getServerBaseUrl() + "/edit_farm");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(farmId));
    query.addQueryItem("name", name);
    query.addQueryItem("latitude", QString::number(latitude, 'f', 14));
    query.addQueryItem("longitude", QString::number(longitude, 'f', 14));
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, farmId, name]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "Update farm HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            qDebug() << "Farm updated successfully";
            // Refresh the farms list
            fetchAllFarmsData();
        } else {
            qDebug() << "Error updating farm:" << reply->errorString();
            qDebug() << "HTTP Status Code:" << statusCode;
        }
        reply->deleteLater();
    });
    
    qDebug() << "Updating farm:" << farmId << "with name:" << name;
}

void MainWindow::deleteFarmFromServer(int farmId)
{
    QUrl url(getServerBaseUrl() + "/remove_farm");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(farmId));
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, farmId]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "Delete farm HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            qDebug() << "Farm deleted successfully";
            // Refresh the farms list
            fetchAllFarmsData();
        } else {
            qDebug() << "Error deleting farm:" << reply->errorString();
            qDebug() << "HTTP Status Code:" << statusCode;
        }
        reply->deleteLater();
    });
    
    qDebug() << "Deleting farm:" << farmId;
}

void MainWindow::fetchFieldXml(int fieldId, const QString &fieldName)
{
    QUrl url(getServerBaseUrl() + "/field");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(fieldId));
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000); // 10 second timeout to allow for larger files
    
    QNetworkReply* reply = mNetworkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, fieldId, fieldName]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "Field XML HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            QByteArray xmlData = reply->readAll();
            qDebug() << "Received field XML data (size:" << xmlData.size() << ") for field:" << fieldName;
            
            if (!xmlData.isEmpty()) {
                QXmlStreamReader xmlReader(xmlData);
                if (!xmlReader.hasError()) {
                    ui->mapWidgetFields->loadXMLRoute(&xmlReader, true);
                    qDebug() << "Successfully loaded field XML for field:" << fieldName;
                } else {
                    qDebug() << "Error parsing field XML:" << xmlReader.errorString();
                }
            } else {
                qDebug() << "Empty field XML received for field:" << fieldName;
            }
        } else {
            qDebug() << "Error fetching field XML for field" << fieldName << "(ID:" << fieldId << "):" << reply->errorString();
            qDebug() << "HTTP Status Code:" << statusCode;
        }
        reply->deleteLater();
    });
    
    qDebug() << "Fetching field XML for field:" << fieldName << "(ID:" << fieldId << ")";
}

void MainWindow::onFieldDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles)
{
    qDebug() << "onFieldDataChanged called - topLeft:" << topLeft.row() << "," << topLeft.column() << "bottomRight:" << bottomRight.row() << "," << bottomRight.column();
    qDebug() << "Roles:" << roles;
    
    // Check if this is a change to the name column (column 1)
    if (topLeft.column() == 1 && (roles.contains(Qt::EditRole) || roles.contains(Qt::DisplayRole))) {
        int row = topLeft.row();
        qDebug() << "Name column change detected at row:" << row;
        
        if (!fieldsModel) {
            qDebug() << "ERROR: fieldsModel is null!";
            return;
        }
        
        if (row < 0 || row >= fieldsModel->rowCount()) {
            qDebug() << "ERROR: Invalid row index:" << row << "model row count:" << fieldsModel->rowCount();
            return;
        }
        
        QStandardItem* nameItem = fieldsModel->item(row, 1); // Name is column 1
        qDebug() << "nameItem:" << (nameItem ? "valid" : "null");
        
        if (nameItem) {
            QString newName = nameItem->text();
            QString fieldId = nameItem->data(Qt::UserRole).toString();
            
            qDebug() << "New name:" << newName << "Field ID:" << fieldId;
            
            if (!fieldId.isEmpty() && !newName.isEmpty()) {
                qDebug() << "Field name changed to:" << newName << "for ID:" << fieldId;
                updateFieldOnServer(fieldId.toInt(), newName, ""); // filename is not changed
            } else {
                qDebug() << "WARNING: Empty fieldId or newName - fieldId:" << fieldId << "newName:" << newName;
            }
        } else {
            qDebug() << "ERROR: nameItem is null at row:" << row << "column: 1";
        }
    } else {
        qDebug() << "Not a name column change or roles don't match - column:" << topLeft.column() << "roles:" << roles;
    }
}

void MainWindow::addFieldToServer(const QString &name, int farmId, const QString &filename)
{
    QUrl url(getServerBaseUrl() + "/add_field");
    QUrlQuery query;
    query.addQueryItem("name", name);
    query.addQueryItem("farm_id", QString::number(farmId));
    query.addQueryItem("filename", filename);
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, name, farmId]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "Add field HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            qDebug() << "Field added successfully";
            // Refresh the fields list for the current farm
            int currentFarmId = currentFarm();
            if (currentFarmId != -1) {
                fetchAllFieldsData(currentFarmId);
            }
        } else {
            qDebug() << "Error adding field:" << reply->errorString();
            qDebug() << "HTTP Status Code:" << statusCode;
        }
        reply->deleteLater();
    });
    
    qDebug() << "Adding field:" << name << "for farm:" << farmId;
}

void MainWindow::updateFieldOnServer(int fieldId, const QString &name, const QString &filename)
{
    qDebug() << "updateFieldOnServer called with fieldId:" << fieldId << "name:" << name << "filename:" << filename;
    
    QUrl url(getServerBaseUrl() + "/edit_field");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(fieldId));
    query.addQueryItem("name", name);
    query.addQueryItem("filename", filename);
    url.setQuery(query);
    
    qDebug() << "Update field URL:" << url.toString();
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    if (!mNetworkManager) {
        qDebug() << "ERROR: mNetworkManager is null!";
        return;
    }
    
    QNetworkReply* reply = mNetworkManager->get(request);
    qDebug() << "Network request sent, reply:" << (reply ? "valid" : "null");
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, fieldId, name]() {
        qDebug() << "Update field request finished for fieldId:" << fieldId << "name:" << name;
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "Update field HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            qDebug() << "Field updated successfully";
            // Refresh the fields list for the current farm
            int currentFarmId = currentFarm();
            qDebug() << "Current farm ID:" << currentFarmId;
            if (currentFarmId != -1) {
                qDebug() << "Refreshing fields data for farm:" << currentFarmId;
                fetchAllFieldsData(currentFarmId);
            }
        } else {
            qDebug() << "Error updating field:" << reply->errorString();
            qDebug() << "HTTP Status Code:" << statusCode;
        }
        reply->deleteLater();
    });
    
    qDebug() << "Updating field:" << fieldId << "with name:" << name;
}

void MainWindow::deleteFieldFromServer(int fieldId)
{
    QUrl url(getServerBaseUrl() + "/remove_field");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(fieldId));
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, fieldId]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "Delete field HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            qDebug() << "Field deleted successfully";
            // Refresh the fields list for the current farm
            int currentFarmId = currentFarm();
            if (currentFarmId != -1) {
                fetchAllFieldsData(currentFarmId);
            }
        } else {
            qDebug() << "Error deleting field:" << reply->errorString();
            qDebug() << "HTTP Status Code:" << statusCode;
        }
        reply->deleteLater();
    });
    
    qDebug() << "Deleting field:" << fieldId;
}

void MainWindow::addPathToServer(const QString &name, int fieldId)
{
    QUrl url(getServerBaseUrl() + "/add_path");
    QUrlQuery query;
    query.addQueryItem("name", name);
    query.addQueryItem("field", QString::number(fieldId));
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, name, fieldId]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "Add path HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            qDebug() << "Path added successfully";
            // Refresh the paths list for the current field
            fetchAllPathsData(fieldId);
        } else {
            qDebug() << "Error adding path:" << reply->errorString();
            qDebug() << "HTTP Status Code:" << statusCode;
        }
        reply->deleteLater();
    });
    
    qDebug() << "Adding path:" << name << "for field:" << fieldId;
}

void MainWindow::updatePathOnServer(int pathId, const QString &name)
{
    QUrl url(getServerBaseUrl() + "/edit_path");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(pathId));
    query.addQueryItem("name", name);
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, pathId, name]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "Update path HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            qDebug() << "Path updated successfully";
            // Refresh the paths list for the current field
            // We need to get the current field ID from the selection
            QModelIndex currentFieldIndex = ui->fieldTable->selectionModel()->currentIndex();
            if (currentFieldIndex.isValid()) {
                QStandardItem* nameItem = fieldsModel->item(currentFieldIndex.row(), 1);
                if (nameItem) {
                    QString fieldId = nameItem->data(Qt::UserRole).toString();
                    fetchAllPathsData(fieldId.toInt());
                }
            }
        } else {
            qDebug() << "Error updating path:" << reply->errorString();
            qDebug() << "HTTP Status Code:" << statusCode;
        }
        reply->deleteLater();
    });
    
    qDebug() << "Updating path:" << pathId << "with name:" << name;
}

void MainWindow::deletePathFromServer(int pathId)
{
    QUrl url(getServerBaseUrl() + "/remove_path");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(pathId));
    url.setQuery(query);
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    QNetworkReply* reply = mNetworkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, pathId]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "Delete path HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            qDebug() << "Path deleted successfully";
            // Refresh the paths list for the current field
            QModelIndex currentFieldIndex = ui->fieldTable->selectionModel()->currentIndex();
            if (currentFieldIndex.isValid()) {
                QStandardItem* nameItem = fieldsModel->item(currentFieldIndex.row(), 1);
                if (nameItem) {
                    QString fieldId = nameItem->data(Qt::UserRole).toString();
                    fetchAllPathsData(fieldId.toInt());
                }
            }
        } else {
            qDebug() << "Error deleting path:" << reply->errorString();
            qDebug() << "HTTP Status Code:" << statusCode;
        }
        reply->deleteLater();
    });
    
    qDebug() << "Deleting path:" << pathId;
}

void MainWindow::onAddMachineButtonClicked()
{
    // Get the machine name and IP from the line edits
    QString name = ui->lineEditMachineName->text();
    QString ip = ui->lineEditMachineIp->text();
    
    // Get the selected vehicle type ID from the combobox
    int selectedVehicleTypeIndex = ui->comboBoxVehicleType->currentIndex();
    QString vehicleTypeId = "";
    if (selectedVehicleTypeIndex >= 0) {
        // Get the ID from user data
        QStandardItem* item = vehicleTypesModel->item(selectedVehicleTypeIndex);
        if (item) {
            vehicleTypeId = item->data(Qt::UserRole).toString();
        }
    }
    
    // Validate inputs
    if (name.isEmpty() || ip.isEmpty() || vehicleTypeId.isEmpty()) {
        qDebug() << "Name, IP, or Vehicle Type is empty - cannot add machine";
        return;
    }
    
    // Create the URL with query parameters
    QUrl url(getServerBaseUrl() + "/add_machine");
    QUrlQuery query;
    query.addQueryItem("name", name);
    query.addQueryItem("ip", ip);
    query.addQueryItem("vehicle_type_id", vehicleTypeId);
    url.setQuery(query);
    
    qDebug() << "Adding machine:" << name << "with IP:" << ip << "and Vehicle Type ID:" << vehicleTypeId;
    qDebug() << "URL:" << url.toString();
    
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    
    // Send the POST request
    QNetworkReply* reply = mNetworkManager->post(request, QByteArray());
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, name, ip]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "Add machine HTTP Status Code:" << statusCode;
        
        if (reply->error() == QNetworkReply::NoError && statusCode == 200) {
            qDebug() << "Machine added successfully:" << name << "-" << ip;
            // Clear the input fields after successful addition
            ui->lineEditMachineName->clear();
            ui->lineEditMachineIp->clear();
            // Refresh the machines list
            fetchAllMachinesData();
            // Ensure tableViewMachines is refreshed
            ui->tableViewMachines->update();
        } else {
            qDebug() << "Error adding machine:" << reply->errorString();
            qDebug() << "HTTP Status Code:" << statusCode;
            // Still refresh the table even on error
            fetchAllMachinesData();
        }
        reply->deleteLater();
    });
}

void MainWindow::on_mapRemoveTraceButton_clicked()
{
    ui->mapLiveWidget->clearTrace();
}

void MainWindow::on_MapRemovePixmapsButton_clicked()
{
    ui->mapLiveWidget->clearPerspectivePixmaps();
}

void MainWindow::on_tcpConnectButton_clicked()
{
    mTcpClientMulti->disconnectAll();
    removeCars();
    ui->carsWidget->clear();

    QString connectIpText = ui->tcpConnEdit->toPlainText().trimmed();

    // Prioritize selected machine in the sidebar machinesTable or machines tab table view
    QModelIndexList selectedRows = ui->machinesTable->selectionModel()->selectedRows();
    if (selectedRows.isEmpty()) {
        selectedRows = ui->tableViewMachines->selectionModel()->selectedRows();
    }
    
    if (!selectedRows.isEmpty()) {
        int row = selectedRows.first().row();
        QString selectedIp;
        
        // Find which table is selected and get the IP
        if (ui->machinesTable->selectionModel()->hasSelection()) {
            QTableWidgetItem* ipItem = ui->machinesTable->item(row, 1);
            if (ipItem) selectedIp = ipItem->text().trimmed();
        } else {
            QStandardItem* ipItem = machinesModel->item(row, 1);
            if (ipItem) selectedIp = ipItem->text().trimmed();
        }
        
        if (!selectedIp.isEmpty() && selectedIp != "None" && selectedIp != "Loading..." && selectedIp != "XML Error" && selectedIp != "No machines") {
            connectIpText = selectedIp;
            ui->tcpConnEdit->setPlainText(selectedIp); // Sync the edit box
        }
    }

    QStringList conns = connectIpText.split("\n");

    for (QString c: conns) {
        QStringList ipPort = c.split(":");

        if (ipPort.size() == 1) {
            mTcpClientMulti->addConnection(ipPort.at(0),
                                           8300);
        } else if (ipPort.size() == 2) {
            mTcpClientMulti->addConnection(ipPort.at(0),
                                           ipPort.at(1).toInt());
        }
        
        // Lookup correct machine ID from machinesModel based on IP address
        int car_id = mCars.size();
        for (int row = 0; row < machinesModel->rowCount(); ++row) {
            QStandardItem* ipItem = machinesModel->item(row, 1);
            if (ipItem && ipItem->text() == ipPort.at(0)) {
                QStandardItem* nameItem = machinesModel->item(row, 0);
                if (nameItem) {
                    bool ok;
                    int id_val = nameItem->data(Qt::UserRole).toInt(&ok);
                    if (ok) {
                        car_id = id_val;
                        break;
                    }
                }
            }
        }
        addCar(car_id, ipPort.at(0), true); // Automatically enable "Poll data" on connection!
        ui->mapCarBox->setValue(car_id); // Automatically select and highlight the connected car on the map!
        ui->mapFollowBox->setChecked(false); // Do not follow ENU car (which can be uninitialized at 0,0) by default
        ui->mapStreamNmeaFollowBox->setChecked(false); // Let the user toggle high-precision GPS tracking manually to prevent GUI thread congestion
        // Kartan ska använda styrkortets ENU-referens, inte den första RTK-punkten.
        // Annars räknar kartan och styrkortet från olika nollpunkter och bilen
        // (blå/rosa) hamnar förskjuten mot RTK-punkterna (grön/gul).
        ui->mapStreamNmeaZeroEnuBox->setChecked(false);
        mPacketInterface->getEnuRef(car_id);

        // Automatically connect to port 2948 NMEA stream (rtkrcv) for real-time RTK satellites and age updates!
        ui->mapStreamNmeaServerEdit->setText(ipPort.at(0));
        ui->mapStreamNmeaPortBox->setValue(2948);
        // Bara för status (satelliter, RTK, ålder) och nollpunkten: rita inte ut
        // var roboten kört. Connect-knappen i NMEA-rutan slår på spåret igen.
        mNmeaDrawTrace = false;
        mNmea->connectClientTcp(ipPort.at(0), 2948);
        mConnectedIp = ipPort.at(0);
    }

    // Nollpunkten sätts där roboten står så fort första GPS-positionen kommer
    // från rtkrcv (se nmeaGgaRx), vid varje anslutning.
    mAutoEnuRefPending = !connectIpText.isEmpty();
    if (mAutoEnuRefPending) {
        qDebug() << "Väntar på robotens GPS-position för att sätta kartans nollpunkt...";
    }
}

/*
void MainWindow::on_tcpPingButton_clicked()
{
    QStringList conns = ui->tcpConnEdit->toPlainText().split("\n");

    for (QString c: conns) {
        QStringList ipPort = c.split(":");

        if (ipPort.size() == 2) {
            mPing->pingHost(ipPort.at(0), 64, "TCP Host");
            break;
        }
    }
}
*/

void MainWindow::on_mapZeroButton_clicked()
{
    ui->mapLiveWidget->setXOffset(0);
    ui->mapLiveWidget->setYOffset(0);
}

void MainWindow::on_mapRemoveRouteButton_clicked()
{
    ui->mapLiveWidget->clearPath();
}

void MainWindow::on_mapRouteSpeedBox_valueChanged(double arg1)
{
    ui->mapLiveWidget->setRoutePointSpeed(arg1 / 3.6);
}

void MainWindow::on_jsConnectButton_clicked()
{
    connectJoystick();
}

void MainWindow::on_jsDisconnectButton_clicked()
{
}

void MainWindow::on_mapAntialiasBox_toggled(bool checked)
{
    ui->mapLiveWidget->setAntialiasDrawings(checked);
}

void MainWindow::on_carsWidget_tabCloseRequested(int index)
{
    QWidget *w = ui->carsWidget->widget(index);
    ui->carsWidget->removeTab(index);

    if (dynamic_cast<CarInterface*>(w) != NULL) {
        CarInterface *car = (CarInterface*)w;
        mCars.removeOne(car);
        delete car;
    }
}

void MainWindow::on_mapSetAbsYawButton_clicked()
{
    CarInfo *car = ui->mapLiveWidget->getCarInfo(ui->mapCarBox->value());
    if (car) {
        if (mSerialPort->isOpen() || mPacketInterface->isUdpConnected() || mTcpClientMulti->isAnyConnected()) {
            ui->mapSetAbsYawButton->setEnabled(false);
            ui->mapAbsYawSlider->setEnabled(false);
            bool ok = mPacketInterface->setYawOffsetAck(car->getId(), (double)ui->mapAbsYawSlider->value());
            ui->mapSetAbsYawButton->setEnabled(true);
            ui->mapAbsYawSlider->setEnabled(true);

            if (!ok) {
                qDebug() << "No pos ack received";
            }
        }
    }
}

void MainWindow::on_mapAbsYawSlider_valueChanged(int value)
{
    (void)value;
    CarInfo *car = ui->mapLiveWidget->getCarInfo(ui->mapCarBox->value());
    if (car) {
        mPacketInterface->setYawOffset(car->getId(), (double)ui->mapAbsYawSlider->value());
    }
}

void MainWindow::on_mapAbsYawSlider_sliderReleased()
{
    on_mapSetAbsYawButton_clicked();
}

void MainWindow::nodstoppVisa(int lage)
{
    if (lage == mNodstoppLage) {
        return;
    }
    mNodstoppLage = lage;

    if (lage >= 1 && lage <= 3) {
        ui->stopButton->setIcon(QIcon());
        ui->stopButton->setText("ÅTERSTÄLL");
        ui->stopButton->setStyleSheet(lage == 2
            ? "QPushButton { background-color: #e08a1e; color: white; }"
            : "QPushButton { background-color: #c62828; color: white; }");
        ui->stopButton->setToolTip(lage == 2
            ? "Nödstoppet är uppdraget. Tryck för att återställa och köra igen."
            : "NÖDSTOPP intryckt. Dra upp nödstoppknappen, tryck sedan här.");
    } else {
        ui->stopButton->setIcon(mStopIcon);
        ui->stopButton->setText("STOPP");
        ui->stopButton->setStyleSheet("");
        ui->stopButton->setToolTip("Stop all cars");
    }
}

void MainWindow::on_stopButton_clicked()
{
    if (mNodstoppLage == 1 || mNodstoppLage == 3) {
        showStatusInfo("Dra upp nödstoppknappen först, tryck sedan ÅTERSTÄLL.", false);
    }

    for (int i = 0;i < mCars.size();i++) {
        mCars[i]->emergencyStop();
    }

    mPacketInterface->setRcControlCurrentBrake(255, 40.0, 0.0);
    mPacketInterface->setRcControlCurrentBrake(255, 40.0, 0.0);
    mPacketInterface->setRcControlCurrentBrake(255, 40.0, 0.0);
    mPacketInterface->setRcControlCurrentBrake(255, 40.0, 0.0);
}

void MainWindow::on_mapUploadRouteButton_clicked()
{
    if (!mSerialPort->isOpen() && !mPacketInterface->isUdpConnected() && !mTcpClientMulti->isAnyConnected()) {
        QMessageBox::warning(this, "Upload route",
                             "Serial port not connected.");
        return;
    }

    try {
        MapRoute route = ui->mapLiveWidget->getPath();
        int len = route.size();
        int car = ui->mapCarBox->value();
        bool ok = true;

        if (len <= 0) {
            QMessageBox::warning(this, "Upload route",
                                 "No route on map.");
            return;
        }

        ui->mapUploadRouteButton->setEnabled(false);

        // Stop car
        for (int i = 0;i < mCars.size();i++) {
            if (mCars[i]->getId() == car) {
                ok = mCars[i]->setAp(false);
                break;
            }
        }

        // Clear previous route
        if (ok) {
            ok = mPacketInterface->clearRoute(car);
        }

        QElapsedTimer timer;
        timer.start();

        if (ok) {
            int ind = 0;
            for (ind = 0;ind < len;ind += 5) {
                QList<LocPoint> tmpList;
                for (int j = ind;j < (ind + 5);j++) {
                    if (j < len) {
                        tmpList.append(route[j]);
                    }
                }

                ok = mPacketInterface->setRoutePoints(car, tmpList);

                if (!ok) {
                    break;
                }

                if (timer.elapsed() >= 20) {
                    timer.restart();
                    ui->mapUploadRouteProgressBar->setValue((100 * (ind + 5)) / len);
                }
            }
        }

        if (!ok) {
            QMessageBox::warning(this, "Upload route",
                                 "No response when uploading route.");
        } else {
            ui->mapUploadRouteProgressBar->setValue(100);
        }

        ui->mapUploadRouteButton->setEnabled(true);


    } catch (...) {
        qDebug() << "Could not get route!";
    }

}

void MainWindow::on_mapGetRouteButton_clicked()
{
    if (!mSerialPort->isOpen() && !mPacketInterface->isUdpConnected() && !mTcpClientMulti->isAnyConnected()) {
        QMessageBox::warning(this, "Get route",
                             "Car not connected.");
        return;
    }

    ui->mapGetRouteButton->setEnabled(false);

    MapRoute route;
    int routeLen = 0;
    bool ok = mPacketInterface->getRoutePart(ui->mapCarBox->value(), route.size(), 10, route.mRoute, routeLen);

    QElapsedTimer timer;
    timer.start();

    while (route.size() < routeLen && ok) {
        ok = mPacketInterface->getRoutePart(ui->mapCarBox->value(), route.size(), 10, route.mRoute, routeLen);
        if (timer.elapsed() >= 20) {
            timer.restart();
            ui->mapUploadRouteProgressBar->setValue((100 * route.size()) / routeLen);
        }
    }

    while (route.size() > routeLen) {
        route.removeLast();
    }

    ui->mapGetRouteButton->setEnabled(true);

    if (ok) {
        if (route.size() > 0) {
            ui->mapLiveWidget->setPath(route);
            ui->mapUploadRouteProgressBar->setValue(100);
            showStatusInfo("GetRoute OK", true);
        } else {
            showStatusInfo("GetRoute OK, but route empty", true);
        }
    } else {
        showStatusInfo("GetRoute failed", false);
        QMessageBox::warning(this, "Get route",
                             "Could not get route from car.");
    }
}

void MainWindow::on_mapApButton_clicked()
{
    for (int i = 0;i < mCars.size();i++) {
        if (mCars[i]->getId() == ui->mapCarBox->value()) {
            mCars[i]->setCtrlAp();
        }
    }
    ui->throttleOffButton->setChecked(true);
}

void MainWindow::on_mapKbButton_clicked()
{
    for (int i = 0;i < mCars.size();i++) {
        if (mCars[i]->getId() == ui->mapCarBox->value()) {
            mCars[i]->setCtrlKb();
        }
    }
    ui->throttleDutyButton->setChecked(true);
}

void MainWindow::on_mapOffButton_clicked()
{
    for (int i = 0;i < mCars.size();i++) {
        if (mCars[i]->getId() == ui->mapCarBox->value()) {
            mCars[i]->emergencyStop();
        }
    }
}

void MainWindow::on_mapUpdateSpeedButton_clicked()
{
    MapRoute route = ui->mapLiveWidget->getPath();
    qint32 timeAcc = 0;

    for (int i = 0;i < route.size();i++) {
        double speed = ui->mapRouteSpeedBox->value() / 3.6;
        qDebug() << "Speed:" << speed;

        route[i].setSpeed(speed);

        if (i == 0) {
            route[i].setTime(0);
        } else {
            double dist = route[i].getDistanceTo(route[i - 1]);
            timeAcc += (dist / speed) * 1000.0;
            route[i].setTime(timeAcc);
        }
    }

    ui->mapLiveWidget->setPath(route);
}

void MainWindow::on_mapOpenStreetMapBox_toggled(bool checked)
{
    ui->mapLiveWidget->setDrawOpenStreetmap(checked);
    ui->mapLiveWidget->update();
}

void MainWindow::on_mapAntialiasOsmBox_toggled(bool checked)
{
    ui->mapLiveWidget->setAntialiasOsm(checked);
}

void MainWindow::on_mapOsmResSlider_valueChanged(int value)
{
    ui->mapLiveWidget->setOsmRes((double)value / 100.0);
}

void MainWindow::onGenerateLineButtonClicked()
{
    int p1=ui->point1LineEdit->text().toInt();
    int p2=ui->point2LineEdit->text().toInt();

    qDebug() << "p1: " << p1;
    qDebug() << "p2: " << p2;

    int iField=ui->mapWidgetFields->getFieldNow();
    qDebug() << "field: " << iField;
    MapRoute CurField=ui->mapWidgetFields->getField(iField);

    qDebug() << "Size: " << CurField.size();
    LocPoint first=CurField.at(p1);
    LocPoint second=CurField.at(p2);

    qDebug() << "first, x: " << first.getX() << ", y: " << first.getY();
    qDebug() << "second, x: " << second.getX() << ", y: " << second.getY();

    MapRoute *currentRoute;
    bool bHasCurrentRoute=false;

    if (ui->mapLiveWidget->mPaths->size()>0)
    {
        currentRoute=&(ui->mapLiveWidget->mPaths->getCurrent());
        bHasCurrentRoute=true;
    } else
    {
        currentRoute=new MapRoute();
    }
    currentRoute->append(first);
    emit routePointAdded(first);
    currentRoute->prepend(second);
    emit routePointAdded(second);
    if (!bHasCurrentRoute)
    {
        ui->mapLiveWidget->mPaths->append(*currentRoute);
        ui->mapLiveWidget->mPaths->mRouteNow=ui->mapLiveWidget->mPaths->size()-1;
    }

//    ui->mapLiveWidget->addRoutePoint(first.getX(), first.getY());
//    ui->mapLiveWidget->addRoutePoint(second.getX(), second.getY());
}
void MainWindow::onGeneratePathButtonClicked()
{
    double fieldLength_m = ui->fieldLengthMLineEdit->text().toDouble(); // Assuming input1 is the object name of a QLineEdit
    double fieldWidth_m = ui->fieldWidthMLineEdit->text().toDouble(); // Assuming input1 is the object name of a QLineEdit

    double implementLength_m = ui->implementLengthLineEdit->text().toDouble(); // Assuming input1 is the object name of a QLineEdit
    double implementWidth_m = ui->implementWidthLineEdit->text().toDouble(); // Assuming input1 is the object name of a QLineEdit

    int plots_DrivingDirection = ui->plotsInDrivingDirectionLineEdit->text().toInt();
    int plots_NonDrivingDirection = ui->plotsInNonDrivingDirectionLineEdit->text().toInt();

    double distancebetweenplots_drivingdirection_m = ui->distanceBetweenPlotsMDdLineEdit->text().toDouble();
    double distancebetweenplots_nondrivingdirection_m = ui->distanceBetweenPlotsMNddLineEdit->text().toDouble();

    double speed_m__s = ui->hastighetkmhLineEdit->text().toDouble();

    double turnDiameterX_m = ui->turnRadiusMDdLineEdit->text().toDouble();
    int turnSteps = ui->turnStepsLineEdit->text().toInt();
    bool isResearchPlots = ui->researchplotsCheckBox->isChecked();
    bool isFieldTrial = ui->randomizedCheckBox->isChecked();
    int iTask = ui->agriculttaskBox->currentIndex();
    bool pieces=ui->piecesCheckBox->isChecked();
    if (ui->mapLiveWidget->getPathNum()>0)
    {
        MapRoute activeRoute=ui->mapLiveWidget->getCurrentPath();
        LocPoint p1,p2;
        if (ui->switchStartpointCheckBox->isChecked())
        {
            p1=activeRoute[1];
            p2=activeRoute[0];
        } else
        {
            p1=activeRoute[0];
            p2=activeRoute[1];
        }

        RouteGenerator rg(fieldLength_m,fieldWidth_m,implementLength_m,implementWidth_m,plots_DrivingDirection,plots_NonDrivingDirection,distancebetweenplots_drivingdirection_m,distancebetweenplots_nondrivingdirection_m,p1,p2,speed_m__s,turnDiameterX_m, turnSteps,ui->flipSideCheckBox->isChecked(),isResearchPlots,isFieldTrial,iTask,pieces);
        rg.generateXmlFile();

        QString filePath="output.xml";
        QFile file(filePath);
        if (!file.exists()) {
            qDebug() << "File does not exist:" << filePath;
            return;
        }

        if (!file.open(QIODevice::ReadOnly)) {
            qDebug() << "Could not open file for reading:" << filePath;
            return;
        }

        QXmlStreamReader xmlData(&file);
        ui->mapLiveWidget->loadXMLRoute(&xmlData,false);
        int newPath=ui->mapLiveWidget->getPathNow()+1;
        ui->mapRouteBox->setValue(newPath);
        ui->removeAfterSpinBox->setValue(ui->mapLiveWidget->getPath().size());  // Later change so this line instead is execute when ui->mapRouteBox changes value (i.e. so it change whenever the size of the path changes - oh, and also need to change it when adding/removing points)
    } else
    {
        QMessageBox msgBox;
        msgBox.setText("You need to add a main line");
        msgBox.exec();
    }
}

void MainWindow::onCutButtonClicked()
{
    qDebug() << "CUT";
    int before = ui->removeBeforeSpinBox->value();
    qDebug() << "before: " << before;
    int after = ui->removeAfterSpinBox->value();
    qDebug() << "after: " << after;
    if (ui->createNewPathCheckBox->isChecked())
    {
    qDebug() << "CHECKED";
    MapRoute newRoute=ui->mapLiveWidget->getCurrentPath();
    newRoute.cut(before,after);
    ui->mapLiveWidget->addPath(newRoute);
    } else
    {
        qDebug() << "UNCHECKED";
        ui->mapLiveWidget->getCurrentPath().cut(before,after);
    }
    ui->mapLiveWidget->update();
}

void MainWindow::onTransformButtonClicked()
{
    double moveX_m = ui->moveXMLineEdit->text().toDouble();
    double moveY_m = ui->moveYMLineEdit->text().toDouble();
    double rotate = ui->rotateLineEdit->text().toDouble();
    ui->mapLiveWidget->getCurrentPath().transform(moveX_m,moveY_m,rotate);
    ui->mapLiveWidget->update();
}

void MainWindow::onPrependButtonClicked()
{
    int iPrependRouteId=ui->prependRouteSpinBox->value();
    MapRoute prependRoute=ui->mapLiveWidget->getPath(iPrependRouteId);
    MapRoute* currentRoute=ui->mapLiveWidget->mPaths->getCurrentP();
    int pr_size=prependRoute.size();
    int cr_size=currentRoute->size();
    qDebug() << "pr_size: " << pr_size;
    qDebug() << "cr_size: " << cr_size;
    for (int i=0;i<pr_size;i++)
    {
        qDebug() << i;
        LocPoint lp=prependRoute.at(i);
        lp.setAttributes(8);            // implement up
        currentRoute->prepend(lp);
    }
    cr_size=currentRoute->size();
    qDebug() << "cr_size: " << cr_size;
    ui->mapLiveWidget->repaint();
}

void MainWindow::onAppendButtonClicked()
{
    int iAppendRouteId=ui->prependRouteSpinBox->value();
    MapRoute appendRoute=ui->mapLiveWidget->getPath(iAppendRouteId);
    MapRoute* currentRoute=ui->mapLiveWidget->mPaths->getCurrentP();
    int ap_size=appendRoute.size();
    int cr_size=currentRoute->size();
    qDebug() << "ap_size: " << ap_size;
    qDebug() << "cr_size: " << cr_size;
    for (int i=0;i<ap_size;i++)
    {
        qDebug() << i;
        LocPoint lp=appendRoute.at(i);
        currentRoute->append(lp);
    }
    ui->mapLiveWidget->repaint();
}

void MainWindow::onRangeSliderLowerChanged(int value)
{
    qDebug() << "RangeSlider lower value changed:" << value;
    // Store the lower value for later use when filtering
    mRangeSliderLowerValue = value;
    // If both values are set, perform the filtering
    if (mRangeSliderUpperValue > mRangeSliderLowerValue) {
        filterLogBasedOnRangeSlider();
    }
}

void MainWindow::onRangeSliderUpperChanged(int value)
{
    qDebug() << "RangeSlider upper value changed:" << value;
    // Store the upper value for later use when filtering
    mRangeSliderUpperValue = value;
    // If both values are set, perform the filtering
    if (mRangeSliderUpperValue > mRangeSliderLowerValue) {
        filterLogBasedOnRangeSlider();
    }
}

void MainWindow::filterLogBasedOnRangeSlider()
{
    qDebug() << "Filtering log based on range slider values:" << mRangeSliderLowerValue << "-" << mRangeSliderUpperValue;
    
    // Get the current path index
    int currentPathIndex = ui->mapWidgetAnalysis->mPaths->mRouteNow;
    
    // Check if we have the original log stored and it's valid
    if (currentPathIndex >= 0 && currentPathIndex < mOriginalLogs.size()) {
        MapRoute& originalRoute = mOriginalLogs[currentPathIndex];
        
        // Calculate the filter points based on the slider range
        // The slider range is 0-100, so we need to convert to actual indices
        int totalPoints = originalRoute.size();
        if (totalPoints == 0) return;
        
        int start = static_cast<int>((mRangeSliderLowerValue / 100.0) * totalPoints);
        int end = static_cast<int>((mRangeSliderUpperValue / 100.0) * totalPoints);
        
        qDebug() << "Showing route from point" << start << "to" << end;
        qDebug() << "Total points in original:" << totalPoints;
        
        // Create a filtered route with only the selected section
        MapRoute filteredRoute;
        for (int i = start; i <= end && i < totalPoints; i++) {
            filteredRoute.append(originalRoute.at(i));
        }
        
        // Replace the current route with the filtered version
        // Since we can't directly replace, we'll clear the current route and add the new points
        MapRoute& currentRoute = ui->mapWidgetAnalysis->getCurrentPath();
        currentRoute.clear();
        currentRoute.append(filteredRoute);
        
        // If area filtering is loaded, apply it automatically
        if (mAreaLoaded) {
            applyAreaFiltering();
        } else {
            ui->mapWidgetAnalysis->update();
        }
    }
}

void MainWindow::cutCurrentLogByArea(double minX, double minY, double maxX, double maxY)
{
    qDebug() << "Cutting current log by area:" << minX << "," << minY << "to" << maxX << "," << maxY;
    
    // Get the current path index
    int currentPathIndex = ui->mapWidgetAnalysis->mPaths->mRouteNow;
    
    // Check if we have a valid path
    if (currentPathIndex >= 0 && currentPathIndex < ui->mapWidgetAnalysis->mPaths->size()) {
        MapRoute& currentRoute = ui->mapWidgetAnalysis->getCurrentPath();
        
        // Cut the route by area
        QList<MapRoute> routesWithinArea = currentRoute.cutByArea(minX, minY, maxX, maxY);
        
        if (routesWithinArea.isEmpty()) {
            qDebug() << "No route sections found within the specified area";
            return;
        }
        
        // Clear the current routes and add the area-cut routes
        ui->mapWidgetAnalysis->mPaths->clearAllRoutes();
        
        for (const MapRoute& route : routesWithinArea) {
            if (!route.isEmpty()) {
                ui->mapWidgetAnalysis->mPaths->addRoute(route);
            }
        }
        
        // Update the display
        ui->mapWidgetAnalysis->update();
        
        qDebug() << "Added" << routesWithinArea.size() << "route sections that fall within the area";
    } else {
        qDebug() << "No valid path selected for area cutting";
    }
}

void MainWindow::loadAreaFromXML()
{
    qDebug() << "Loading area definition from XML file";
    
    // Create a file dialog to select the XML file
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Open Area Definition File",
        "",
        "XML Files (*.xml)");
    
    if (fileName.isEmpty()) {
        qDebug() << "No file selected";
        return;
    }
    
    // Open the file
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file:" << fileName;
        QMessageBox::warning(this, "Error", "Failed to open the area definition file.");
        return;
    }
    
    // Use the existing XML loading infrastructure
    QXmlStreamReader xmlReader(&file);
    
    // Load the area as a border using the existing function
    bool success = ui->mapWidgetAnalysis->loadXMLRoute(&xmlReader, true); // true = isBorder
    
    file.close();
    
    if (success) {
        // Get the index of the newly loaded border
        mAreaBorderIndex = ui->mapWidgetAnalysis->getFieldNum() - 1;
        mAreaLoaded = true;
        
        qDebug() << "Area loaded as border index:" << mAreaBorderIndex;
        
        // Update UI status
        ui->labelAreaStatus->setText("Area loaded successfully");
        
        // Apply the area filtering immediately if there's a log loaded
        if (ui->mapWidgetAnalysis->mPaths->size() > 0) {
            applyAreaFiltering();
        }
    } else {
        qDebug() << "Failed to load area definition";
        
        // Update UI status
        ui->labelAreaStatus->setText("Failed to load area");
        QMessageBox::warning(this, "Error", "Failed to load the area definition file.");
    }
}

// Point-in-polygon algorithm using ray casting
bool MainWindow::isPointInsideBorder(const LocPoint& point, const MapRoute& border)
{
    if (border.size() < 3) {
        return false; // Need at least 3 points to form a polygon
    }
    
    double x = point.getX();
    double y = point.getY();
    
    bool inside = false;
    int n = border.size();
    
    for (int i = 0, j = n - 1; i < n; j = i++) {
        double xi = border.at(i).getX();
        double yi = border.at(i).getY();
        double xj = border.at(j).getX();
        double yj = border.at(j).getY();
        
        // Check if the ray from point to infinity crosses this edge
        bool intersect = ((yi > y) != (yj > y)) && 
                        (x < (xj - xi) * (y - yi) / (yj - yi) + xi);
        
        if (intersect) {
            inside = !inside;
        }
    }
    
    return inside;
}

void MainWindow::applyAreaFiltering()
{
    if (!mAreaLoaded || mAreaBorderIndex < 0) {
        qDebug() << "No area definition loaded";
        return;
    }

    qDebug() << "Applying area filtering using border index:" << mAreaBorderIndex;

    // Get the current path index
    int currentPathIndex = ui->mapWidgetAnalysis->mPaths->mRouteNow;

    if (currentPathIndex < 0 || currentPathIndex >= ui->mapWidgetAnalysis->mPaths->size()) {
        qDebug() << "No valid path selected";
        return;
    }

    // Get the border that defines our area
    MapRoute& border = ui->mapWidgetAnalysis->getField(mAreaBorderIndex);

    if (border.size() < 3) {
        qDebug() << "Border has insufficient points to form a valid area";
        return;
    }

    // Work with the original log, not the potentially filtered current route
    if (currentPathIndex >= 0 && currentPathIndex < mOriginalLogs.size()) {
        MapRoute& originalRoute = mOriginalLogs[currentPathIndex];

        // Apply area filtering to the original route
        QList<MapRoute> routesWithinArea;
        MapRoute currentSection;
        bool insideArea = false;

        for (const LocPoint& point : originalRoute) {
            bool pointInside = isPointInsideBorder(point, border);

            if (pointInside) {
                // Point is inside the area
                if (!insideArea) {
                    // We just entered the area, start a new route section
                    currentSection = MapRoute();
                    insideArea = true;
                }
                // Add the point to the current section
                currentSection.append(point);
            } else {
                // Point is outside the area
                if (insideArea) {
                    // We just exited the area, finalize the current section
                    if (!currentSection.isEmpty()) {
                        routesWithinArea.append(currentSection);
                    }
                    insideArea = false;
                }
                // Don't add points outside the area
            }
        }

        // Add the last section if we're still inside the area
        if (insideArea && !currentSection.isEmpty()) {
            routesWithinArea.append(currentSection);
        }

        if (routesWithinArea.isEmpty()) {
            qDebug() << "No route sections found within the specified area";
            return;
        }

        // Clear the current routes and show both original and filtered results
        ui->mapWidgetAnalysisResult->mPaths->clearAllRoutes();

        // Add the original route first
        //ui->mapWidgetAnalysis->mPaths->addRoute(originalRoute);

        // Add the filtered sections as additional routes
        for (const MapRoute& route : routesWithinArea) {
            if (!route.isEmpty()) {
                ui->mapWidgetAnalysisResult->mPaths->addRoute(route);
            }
        }

        // Keep the original route as the current route (index 0)
        //ui->mapWidgetAnalysis->setPathNow(0);

        // Update the display to show both original and filtered results
        ui->mapWidgetAnalysisResult->update();
        
        // Update the result path spinbox
        int resultPathCount = ui->mapWidgetAnalysisResult->mPaths->size();
        ui->spinBoxResultPath->setMaximum(qMax(0, resultPathCount - 1));
        if (resultPathCount > 0) {
            ui->spinBoxResultPath->setValue(0);
        }

        qDebug() << "Applied area filtering. Found" << routesWithinArea.size() << "route sections within the area.";
    }
}

void MainWindow::cutPathByArea()
{
    qDebug() << "=== DEBUG: Starting cutPathByArea ===";
    qDebug() << "Main analysis widget:" << ui->mapWidgetAnalysis;
    qDebug() << "Result analysis widget:" << ui->mapWidgetAnalysisResult;
    
    if (!mAreaLoaded || mAreaBorderIndex < 0) {
        qDebug() << "DEBUG: No area definition loaded (mAreaLoaded:" << mAreaLoaded << ", mAreaBorderIndex:" << mAreaBorderIndex << ")";
        QMessageBox::warning(this, "Error", "No area definition loaded. Please load an area first.");
        return;
    }
    
    qDebug() << "DEBUG: Area loaded, border index:" << mAreaBorderIndex;
    
    // Get the border that defines our area
    MapRoute& border = ui->mapWidgetAnalysis->getField(mAreaBorderIndex);
    qDebug() << "DEBUG: Border points:" << border.size();
    
    if (border.size() < 3) {
        qDebug() << "DEBUG: Border has insufficient points:" << border.size();
        QMessageBox::warning(this, "Error", "Border has insufficient points to form a valid area.");
        return;
    }
    
    // Get the current path index
    int currentPathIndex = ui->mapWidgetAnalysis->mPaths->mRouteNow;
    qDebug() << "DEBUG: Current path index:" << currentPathIndex;
    qDebug() << "DEBUG: Total paths in main widget:" << ui->mapWidgetAnalysis->mPaths->size();
    
    if (currentPathIndex < 0 || currentPathIndex >= ui->mapWidgetAnalysis->mPaths->size()) {
        qDebug() << "DEBUG: Invalid path index (current:" << currentPathIndex << ", total:" << ui->mapWidgetAnalysis->mPaths->size() << ")";
        QMessageBox::warning(this, "Error", "No valid path selected.");
        return;
    }
    
    // Work with the currently displayed route (which may already be RangeSlider-filtered)
    MapRoute& currentRoute = ui->mapWidgetAnalysis->getCurrentPath();
    qDebug() << "DEBUG: Current route points:" << currentRoute.size();
    
    if (currentRoute.isEmpty()) {
        qDebug() << "DEBUG: Current route is empty";
        QMessageBox::warning(this, "Error", "Current route is empty.");
        return;
    }
        
    // Apply area cutting to create separate route sections
    QList<MapRoute> routesWithinArea;
    MapRoute currentSection;
    bool insideArea = false;

    for (const LocPoint& point : currentRoute) {
        bool pointInside = isPointInsideBorder(point, border);

        if (pointInside) {
            // Point is inside the area
            if (!insideArea) {
                // We just entered the area, start a new route section
                currentSection = MapRoute();
                insideArea = true;
            }
            // Add the point to the current section
            currentSection.append(point);
        } else {
            // Point is outside the area
            if (insideArea) {
                // We just exited the area, finalize the current section
                if (!currentSection.isEmpty()) {
                    routesWithinArea.append(currentSection);
                }
                insideArea = false;
            }
            // Don't add points outside the area
        }
    }

    // Add the last section if we're still inside the area
    if (insideArea && !currentSection.isEmpty()) {
        routesWithinArea.append(currentSection);
    }

    qDebug() << "DEBUG: Found" << routesWithinArea.size() << "route sections after filtering";

    if (routesWithinArea.isEmpty()) {
        qDebug() << "DEBUG: No route sections found within the specified area";
        QMessageBox::warning(this, "Info", "No route sections found within the specified area.");
        return;
    }

    // Debug: Check result widget state before clearing
    qDebug() << "DEBUG: Result widget paths before clear:" << ui->mapWidgetAnalysisResult->mPaths->size();
    qDebug() << "DEBUG: Result widget fields before clear:" << ui->mapWidgetAnalysisResult->getFieldNum();

    // Display the filtered results in the second map widget instead of replacing the first one
    ui->mapWidgetAnalysisResult->mPaths->clearAllRoutes();
    qDebug() << "DEBUG: Cleared result widget routes";

    // Add only the filtered sections to the result map widget
    int addedRoutes = 0;
    for (const MapRoute& route : routesWithinArea) {
        if (!route.isEmpty()) {
            ui->mapWidgetAnalysisResult->mPaths->addRoute(route);
            addedRoutes++;
            qDebug() << "DEBUG: Added route with" << route.size() << "points to result widget";
        }
    }
    qDebug() << "DEBUG: Added" << addedRoutes << "routes to result widget";

    // Set the current route to the first filtered section in the result widget (if any exist)
    if (routesWithinArea.size() > 0) {
        ui->mapWidgetAnalysisResult->setPathNow(0); // Set to first filtered section
        qDebug() << "DEBUG: Set result path to index 0";
        qDebug() << "DEBUG: Result widget total paths:" << ui->mapWidgetAnalysisResult->mPaths->size();
        qDebug() << "DEBUG: Result widget current path size:" << ui->mapWidgetAnalysisResult->getCurrentPath().size();
        
        // Update the result path spinbox
        int resultPathCount = ui->mapWidgetAnalysisResult->mPaths->size();
        ui->spinBoxResultPath->setMaximum(qMax(0, resultPathCount - 1));
        ui->spinBoxResultPath->setValue(0);

        // Debug: Check if the result widget has the expected content
        if (ui->mapWidgetAnalysisResult->mPaths->size() == 0) {
            qDebug() << "DEBUG: WARNING: Result widget has 0 paths after adding!";
        }
    } else {
        qDebug() << "DEBUG: No filtered sections found!";
    }

    // Copy the ENU reference from the main analysis widget to the result widget
    double refs[3];
    ui->mapWidgetAnalysis->getEnuRef(refs);
    qDebug() << "DEBUG: Copying ENU reference:" << refs[0] << "," << refs[1] << "," << refs[2];
    ui->mapWidgetAnalysisResult->setEnuRef(refs[0], refs[1], refs[2]);

    // Copy the area border to the result widget for visualization
    if (mAreaBorderIndex >= 0) {
        MapRoute borderCopy = ui->mapWidgetAnalysis->getField(mAreaBorderIndex);
        ui->mapWidgetAnalysisResult->addField(borderCopy);
        qDebug() << "DEBUG: Added border with" << borderCopy.size() << "points to result widget";
    }

    // Debug: Check final state
    qDebug() << "DEBUG: Final result widget paths:" << ui->mapWidgetAnalysisResult->mPaths->size();
    qDebug() << "DEBUG: Final result widget fields:" << ui->mapWidgetAnalysisResult->getFieldNum();
    qDebug() << "DEBUG: Result widget current path:" << ui->mapWidgetAnalysisResult->getCurrentPath().size() << "points";

    // Update UI status
    ui->labelAreaStatus->setText("Path cut to " + QString::number(routesWithinArea.size()) + " sections (shown in result map)");

    // Force a repaint to ensure visibility in both widgets
    qDebug() << "DEBUG: Calling repaint on both widgets";
    ui->mapWidgetAnalysis->repaint();
    ui->mapWidgetAnalysisResult->repaint();

    // Update the display for both widgets
    qDebug() << "DEBUG: Calling update on both widgets";
    ui->mapWidgetAnalysis->update();
    ui->mapWidgetAnalysisResult->update();

    qDebug() << "DEBUG: Completed cutPathByArea with" << routesWithinArea.size() << "route sections";
    QMessageBox::information(this, "Success", "Path cut to " + QString::number(routesWithinArea.size()) + " sections within the area.");
}



void MainWindow::onResultPathChanged(int pathIndex)
{
    qDebug() << "DEBUG: Result path changed to" << pathIndex;
    
    // Set the current path in the result map widget
    if (ui->mapWidgetAnalysisResult->mPaths->size() > 0) {
        // Ensure the path index is within valid range
        int validIndex = qBound(0, pathIndex, ui->mapWidgetAnalysisResult->mPaths->size() - 1);
        ui->mapWidgetAnalysisResult->setPathNow(validIndex);
        ui->mapWidgetAnalysisResult->update();
        
        // Update the spinbox in case the index was clamped
        if (pathIndex != validIndex) {
            ui->spinBoxResultPath->setValue(validIndex);
        }
    } else {
        // No paths available, reset to 0
        ui->spinBoxResultPath->setValue(0);
    }
}

void MainWindow::onAnalysisSelectionChanged()
{
    qDebug() << "DEBUG: Analysis selection changed";
    
    // Debug: Check table state
    qDebug() << "DEBUG: Table row count:" << ui->tableAnalysis->rowCount();
    qDebug() << "DEBUG: Table column count:" << ui->tableAnalysis->columnCount();
    
    // Check if items exist
    for (int i = 0; i < ui->tableAnalysis->rowCount(); i++) {
        QTableWidgetItem* item = ui->tableAnalysis->item(i, 0);
        if (item) {
            qDebug() << "DEBUG: Row" << i << "has item:" << item->text();
        } else {
            qDebug() << "DEBUG: Row" << i << "has NO item";
        }
    }
    
    // Get the selected items
    QList<QTableWidgetItem*> selectedItems = ui->tableAnalysis->selectedItems();
    
    if (selectedItems.isEmpty()) {
        qDebug() << "DEBUG: No item selected";
        
        // Try to get current item
        QTableWidgetItem* currentItem = ui->tableAnalysis->currentItem();
        if (currentItem) {
            qDebug() << "DEBUG: Current item:" << currentItem->text();
        } else {
            qDebug() << "DEBUG: No current item either";
        }
        return;
    }
    
    // Get the first selected item (since we're using single selection)
    QTableWidgetItem* selectedItem = selectedItems.first();
    int selectedRow = selectedItem->row();
    
    // For single-column table, get the text directly from the item
    QString analysisType = selectedItem->text();
    
    qDebug() << "DEBUG: Selected analysis type:" << analysisType << "(row" << selectedRow << ")";
    
    // Handle different analysis types
    if (analysisType == "Length") {
        calculateAndDisplayPathLengths();
    } else if (analysisType == "Angle") {
        qDebug() << "DEBUG: Angle analysis selected";
        calculateAndDisplayPathAngles();
    } else if (analysisType == "Root-Mean-Square") {
        qDebug() << "DEBUG: RMS analysis selected";
        calculateAndDisplayPathRMS();
    }
}

void MainWindow::updateStatisticsDisplay(const QList<double>& values, const QString& unit)
{
    qDebug() << "DEBUG: Updating statistics display with" << values.size() << "values";
    if (values.isEmpty()) {
        ui->textAnalysis->setText("No data available for statistical analysis.");
        return;
    }
    
    // Calculate mean
    double sum = 0.0;
    foreach (double value, values) {
        sum += value;
    }
    double mean = sum / values.size();
    
    // Calculate standard deviation
    double sumSquaredDiffs = 0.0;
    foreach (double value, values) {
        double diff = value - mean;
        sumSquaredDiffs += diff * diff;
    }
    double stdDev = std::sqrt(sumSquaredDiffs / values.size());
    
    // Calculate min and max
    double min = *std::min_element(values.begin(), values.end());
    double max = *std::max_element(values.begin(), values.end());
    
    // Format the statistics display with styling
    QString statsText;
    statsText += QString("<style>");
    statsText += QString("h3 { color: #2c3e50; font-family: Arial, sans-serif; }");
    statsText += QString("p { margin: 5px 0; font-family: Arial, sans-serif; }");
    statsText += QString(".stat-value { color: #e74c3c; font-weight: bold; }");
    statsText += QString(".stat-unit { color: #3498db; font-style: italic; }");
    statsText += QString("</style>");
    statsText += QString("<h3>📊 Statistical Analysis</h3>");
    statsText += QString("<p><b>Sample Size:</b> <span class=\"stat-value\">%1</span></p>").arg(values.size());
    statsText += QString("<p><b>Mean:</b> <span class=\"stat-value\">%1</span> <span class=\"stat-unit\">%2</span></p>").arg(mean, 0, 'f', 2).arg(unit);
    statsText += QString("<p><b>Standard Deviation:</b> <span class=\"stat-value\">%1</span> <span class=\"stat-unit\">%2</span></p>").arg(stdDev, 0, 'f', 2).arg(unit);
    statsText += QString("<p><b>Minimum:</b> <span class=\"stat-value\">%1</span> <span class=\"stat-unit\">%2</span></p>").arg(min, 0, 'f', 2).arg(unit);
    statsText += QString("<p><b>Maximum:</b> <span class=\"stat-value\">%1</span> <span class=\"stat-unit\">%2</span></p>").arg(max, 0, 'f', 2).arg(unit);
    
    // Calculate coefficient of variation if mean is not zero
    if (qFuzzyCompare(mean, 0.0)) {
        statsText += QString("<p><b>Coefficient of Variation:</b> <span class=\"stat-value\">N/A</span> (mean is zero)</p>");
    } else {
        double cv = (stdDev / mean) * 100.0;
        statsText += QString("<p><b>Coefficient of Variation:</b> <span class=\"stat-value\">%1%</span></p>").arg(cv, 0, 'f', 1);
    }
    
    // Add interpretation
    statsText += QString("<p style=\"margin-top: 10px; color: #7f8c8d; font-size: 90%;\">");
    statsText += QString("Note: Statistics are calculated from the path lengths shown in the graph.");
    statsText += QString("</p>");
    
    ui->textAnalysis->setText(statsText);
    qDebug() << "DEBUG: Updated statistics display with" << values.size() << "data points";
}

void MainWindow::updateCurrentAnalysis()
{
    qDebug() << "DEBUG: Updating current analysis";
    
    // Get the currently selected analysis type
    QList<QTableWidgetItem*> selectedItems = ui->tableAnalysis->selectedItems();
    
    if (selectedItems.isEmpty()) {
        qDebug() << "DEBUG: No analysis selected, defaulting to Length analysis";
        // If no analysis is selected, default to Length analysis
        calculateAndDisplayPathLengths();
        return;
    }
    
    // Get the selected analysis type
    QTableWidgetItem* selectedItem = selectedItems.first();
    QString analysisType = selectedItem->text();
    
    qDebug() << "DEBUG: Updating analysis type:" << analysisType;
    
    // Call the appropriate analysis function based on the current selection
    if (analysisType == "Length") {
        calculateAndDisplayPathLengths();
    } else if (analysisType == "Angle") {
        qDebug() << "DEBUG: Angle analysis update requested";
        calculateAndDisplayPathAngles();
    } else if (analysisType == "Root-Mean-Square") {
        qDebug() << "DEBUG: RMS analysis update requested";
        calculateAndDisplayPathRMS();
    } else {
        qDebug() << "DEBUG: Unknown analysis type:" << analysisType << ", defaulting to Length";
        calculateAndDisplayPathLengths();
    }
}

void MainWindow::calculateAndDisplayPathLengths()
{
    qDebug() << "DEBUG: Calculating path lengths...";
    
    // Check if we have any paths to analyze
    if (ui->mapWidgetAnalysisResult->mPaths->size() == 0) {
        qDebug() << "DEBUG: No paths available for length analysis";
        QMessageBox::information(this, "Analysis", "No paths available for length analysis.");
        ui->textAnalysis->setText("No paths available for statistical analysis.");
        return;
    }
    
    // Create a bar chart to display the lengths
    QChart* chart = new QChart();
    chart->setTitle("Path Lengths Analysis");
    chart->setAnimationOptions(QChart::SeriesAnimations);
    
    QBarSeries* series = new QBarSeries();
    QList<double> lengthValues; // Store values for statistics
    
    // Calculate length for each path and add to the chart
    for (int i = 0; i < ui->mapWidgetAnalysisResult->mPaths->size(); i++) {
        MapRoute& route = ui->mapWidgetAnalysisResult->getPath(i);
        
        if (route.isEmpty()) {
            qDebug() << "DEBUG: Path" << i << "is empty, skipping";
            continue;
        }
        
        // Calculate the length of this path
        double length = 0.0;
        LocPoint prevPoint = route.first();
        
        for (int j = 1; j < route.size(); j++) {
            LocPoint currentPoint = route.at(j);
            // Calculate distance between points using Euclidean distance
            double dx = currentPoint.getX() - prevPoint.getX();
            double dy = currentPoint.getY() - prevPoint.getY();
            length += std::sqrt(dx*dx + dy*dy);
            prevPoint = currentPoint;
        }
        
        qDebug() << "DEBUG: Path" << i << "length:" << length << "units";
        
        // Add this path's length to the chart
        QBarSet* set = new QBarSet(QString("%1").arg(i));
        set->append(length);
        series->append(set);
        
        // Store the value for statistics calculation
        lengthValues.append(length);
        
        // For single-column table, we could store the value in the item's data
        // Or display it in the chart title or as a tooltip
        // ui->tableAnalysis->item(i, 0)->setToolTip(QString::number(length, 'f', 2));
    }
    
    if (series->count() == 0) {
        qDebug() << "DEBUG: No valid paths found for length analysis";
        QMessageBox::information(this, "Analysis", "No valid paths found for length analysis.");
        ui->textAnalysis->setText("No valid paths found for statistical analysis.");
        delete series;
        delete chart;
        return;
    }
    
    // Add the series to the chart
    chart->addSeries(series);
    
    // Customize the chart
    chart->setTitle("Path Lengths");
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);
    
    // Create axis
    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("Length (units)");
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);
    
    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);
    
    // Note: The chart is now owned by the chartView and will be deleted when
    // the chartView is destroyed or when a new chart is set
    
    // Display the chart in the existing Graph widget
    QChartView* chartView = ui->Graph;
    qDebug() << "DEBUG: Chart view pointer:" << chartView;
    if (chartView) {
        qDebug() << "DEBUG: Chart view is valid";
        // Clear any existing chart
        QChart* existingChart = chartView->chart();
        if (existingChart) {
            delete existingChart;
        }

        // Set the new chart
        chartView->setChart(chart);
        chartView->setRenderHint(QPainter::Antialiasing);

        // Connect to bar series clicked signal for proper bar click handling
        QBarSeries* barSeries = static_cast<QBarSeries*>(chart->series().first());
        if (barSeries) {
            // Debug: Print the number of bar sets and bars in the first set
            qDebug() << "DEBUG: Number of bar sets in series:" << barSeries->barSets().size();
            QBarSet* firstBarSet = barSeries->barSets().first();
            if (firstBarSet) {
                qDebug() << "DEBUG: Number of bars in the first set:" << firstBarSet->count();
            }

            // Connect the clicked signal
            connect(barSeries, &QBarSeries::clicked, [this, barSeries](int index, QBarSet* barSet) {
                qDebug() << "DEBUG: Bar series clicked, index:" << index;
                qDebug() << "DEBUG: Bar set pointer:" << barSet;
                qDebug() << "DEBUG: Bar set label:" << barSet->label();
                index = barSet->label().toInt();
                qDebug() << "DEBUG: New index:" << index;

                // Debug: Print the value of the clicked bar
                if (index >= 0 && index < barSet->count()) {
                    qDebug() << "DEBUG: Value of clicked bar:" << barSet->at(index);
                }

                // Your logic to handle the click
                if (index >= 0 && index < ui->mapWidgetAnalysisResult->mPaths->size()) {
                    qDebug() << "DEBUG: Setting path index to:" << index;
                    ui->mapWidgetAnalysisResult->setPathNow(index);
                    ui->mapWidgetAnalysisResult->update();
                    ui->spinBoxResultPath->setValue(index);
                } else {
                    qDebug() << "DEBUG: Index out of bounds:" << index;
                }
            });
        } else {
            qDebug() << "DEBUG: No bar series found!";
        }
    } else {
        qDebug() << "DEBUG: Chart view is invalid!";
    }
    
    // Update statistics display with the calculated values
    if (!lengthValues.isEmpty()) {
        updateStatisticsDisplay(lengthValues, "units");
    } else {
        ui->textAnalysis->setText("No valid path data available for statistical analysis.");
    }
}

void MainWindow::calculateAndDisplayPathAngles()
{
    qDebug() << "DEBUG: Calculating path closing angles...";
    
    // Check if we have any paths to analyze
    if (ui->mapWidgetAnalysisResult->mPaths->size() == 0) {
        qDebug() << "DEBUG: No paths available for angle analysis";
        QMessageBox::information(this, "Analysis", "No paths available for angle analysis.");
        ui->textAnalysis->setText("No paths available for statistical analysis.");
        return;
    }
    
    // Create a bar chart to display the angles
    QChart* chart = new QChart();
    chart->setTitle("Path Closing Angles");
    chart->setAnimationOptions(QChart::SeriesAnimations);
    
    QBarSeries* series = new QBarSeries();
    QList<double> angleValues; // Store values for statistics
    
    // Calculate closing angle for each path and add to the chart
    for (int i = 0; i < ui->mapWidgetAnalysisResult->mPaths->size(); i++) {
        MapRoute& route = ui->mapWidgetAnalysisResult->getPath(i);
        
        if (route.isEmpty()) {
            qDebug() << "DEBUG: Path" << i << "is empty, skipping";
            continue;
        }
        
        // Calculate the closing angle of this path (angle between first and last point)
        LocPoint firstPoint = route.first();
        LocPoint lastPoint = route.last();
        double angleRadians = LocPoint::calculateAngle(firstPoint, lastPoint);
        double angleDegrees = angleRadians * (180.0 / M_PI);
        if (angleDegrees<0)  angleDegrees+=180.0;
        qDebug() << "DEBUG: Path" << i << "closing angle:" << angleDegrees << "degrees";
        
        // Add this path's angle to the chart
        QBarSet* set = new QBarSet(QString("%1").arg(i));
        set->append(angleDegrees);
        series->append(set);
        
        // Store the value for statistics calculation
        angleValues.append(angleDegrees);
    }
    
    if (series->count() == 0) {
        qDebug() << "DEBUG: No valid paths found for angle analysis";
        QMessageBox::information(this, "Analysis", "No valid paths found for angle analysis.");
        ui->textAnalysis->setText("No valid paths found for statistical analysis.");
        delete series;
        delete chart;
        return;
    }
    
    // Add the series to the chart
    chart->addSeries(series);
    
    // Customize the chart
    chart->setTitle("Path Closing Angles");
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);
    
    // Create axis
    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("Angle (degrees)");
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);
    
    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);
    
    // Display the chart in the existing Graph widget
    QChartView* chartView = ui->Graph;
    qDebug() << "DEBUG: Chart view pointer:" << chartView;
    if (chartView) {
        qDebug() << "DEBUG: Chart view is valid";
        // Clear any existing chart
        QChart* existingChart = chartView->chart();
        if (existingChart) {
            delete existingChart;
        }
        
        // Set the new chart
        chartView->setChart(chart);
        chartView->setRenderHint(QPainter::Antialiasing);
        
        // Connect to bar series clicked signal for proper bar click handling
        QBarSeries* barSeries = static_cast<QBarSeries*>(chart->series().first());
        if (barSeries) {
            connect(barSeries, &QBarSeries::clicked, [this, barSeries](int index, QBarSet* barSet) {
                qDebug() << "DEBUG: Bar series clicked, index:" << index;
                index = barSet->label().toInt();
                qDebug() << "DEBUG: New index:" << index;
                
                // Handle the click
                if (index >= 0 && index < ui->mapWidgetAnalysisResult->mPaths->size()) {
                    qDebug() << "DEBUG: Setting path index to:" << index;
                    ui->mapWidgetAnalysisResult->setPathNow(index);
                    ui->mapWidgetAnalysisResult->update();
                    ui->spinBoxResultPath->setValue(index);
                } else {
                    qDebug() << "DEBUG: Index out of bounds:" << index;
                }
            });
        }
    } else {
        qDebug() << "DEBUG: Chart view is invalid!";
    }
    
    // Update statistics display with the calculated values
    if (!angleValues.isEmpty()) {
        updateStatisticsDisplay(angleValues, "degrees");
    } else {
        ui->textAnalysis->setText("No valid path data available for statistical analysis.");
    }
}

void MainWindow::calculateAndDisplayPathRMS()
{
    qDebug() << "DEBUG: Calculating path angle RMS...";
    
    // Check if we have any paths to analyze
    if (ui->mapWidgetAnalysisResult->mPaths->size() == 0) {
        qDebug() << "DEBUG: No paths available for RMS analysis";
        QMessageBox::information(this, "Analysis", "No paths available for RMS analysis.");
        ui->textAnalysis->setText("No paths available for statistical analysis.");
        return;
    }
    
    // Create a bar chart to display the RMS values
    QChart* chart = new QChart();
    chart->setTitle("Path Angle RMS Analysis");
    chart->setAnimationOptions(QChart::SeriesAnimations);
    
    QBarSeries* series = new QBarSeries();
    QList<double> rmsValues; // Store values for statistics
    
    // Calculate RMS for each path and add to the chart
    for (int i = 0; i < ui->mapWidgetAnalysisResult->mPaths->size(); i++) {
        MapRoute& route = ui->mapWidgetAnalysisResult->getPath(i);
        
        if (route.isEmpty()) {
            qDebug() << "DEBUG: Path" << i << "is empty, skipping";
            continue;
        }
        
        // Calculate the average closing angle (first to last point)
        LocPoint firstPoint = route.first();
        LocPoint lastPoint = route.last();
        double averageAngleRadians = LocPoint::calculateAngle(firstPoint, lastPoint);
        double averageAngleDegrees = averageAngleRadians * (180.0 / M_PI);
        
        // Calculate angles for all segments and their deviations from the average
        QList<double> segmentAngles;
        for (int j = 1; j < route.size(); j++) {
            LocPoint prevPoint = route.at(j-1);
            LocPoint currentPoint = route.at(j);
            double segmentAngleRadians = LocPoint::calculateAngle(prevPoint, currentPoint);
            double segmentAngleDegrees = segmentAngleRadians * (180.0 / M_PI);
            segmentAngles.append(segmentAngleDegrees);
        }
        
        // Calculate RMS of deviations from the average angle
        double sumSquaredDeviations = 0.0;
        foreach (double angle, segmentAngles) {
            double deviation = angle - averageAngleDegrees;
            sumSquaredDeviations += deviation * deviation;
        }
        
        double rms = 0.0;
        if (!segmentAngles.isEmpty()) {
            rms = std::sqrt(sumSquaredDeviations / segmentAngles.size());
        }
        
        qDebug() << "DEBUG: Path" << i << "RMS angle:" << rms << "degrees";
        qDebug() << "DEBUG: Path" << i << "average angle:" << averageAngleDegrees << "degrees";
        qDebug() << "DEBUG: Path" << i << "number of segments:" << segmentAngles.size();
        
        // Add this path's RMS to the chart
        QBarSet* set = new QBarSet(QString("%1").arg(i));
        set->append(rms);
        series->append(set);
        
        // Store the value for statistics calculation
        rmsValues.append(rms);
    }
    
    if (series->count() == 0) {
        qDebug() << "DEBUG: No valid paths found for RMS analysis";
        QMessageBox::information(this, "Analysis", "No valid paths found for RMS analysis.");
        ui->textAnalysis->setText("No valid paths found for statistical analysis.");
        delete series;
        delete chart;
        return;
    }
    
    // Add the series to the chart
    chart->addSeries(series);
    
    // Customize the chart
    chart->setTitle("Path Angle RMS Analysis");
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);
    
    // Create axis
    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("RMS Angle (degrees)");
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);
    
    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);
    
    // Display the chart in the existing Graph widget
    QChartView* chartView = ui->Graph;
    qDebug() << "DEBUG: Chart view pointer:" << chartView;
    if (chartView) {
        qDebug() << "DEBUG: Chart view is valid";
        // Clear any existing chart
        QChart* existingChart = chartView->chart();
        if (existingChart) {
            delete existingChart;
        }
        
        // Set the new chart
        chartView->setChart(chart);
        chartView->setRenderHint(QPainter::Antialiasing);
        
        // Connect to bar series clicked signal for proper bar click handling
        QBarSeries* barSeries = static_cast<QBarSeries*>(chart->series().first());
        if (barSeries) {
            connect(barSeries, &QBarSeries::clicked, [this, barSeries](int index, QBarSet* barSet) {
                qDebug() << "DEBUG: Bar series clicked, index:" << index;
                index = barSet->label().toInt();
                qDebug() << "DEBUG: New index:" << index;
                
                // Handle the click
                if (index >= 0 && index < ui->mapWidgetAnalysisResult->mPaths->size()) {
                    qDebug() << "DEBUG: Setting path index to:" << index;
                    ui->mapWidgetAnalysisResult->setPathNow(index);
                    ui->mapWidgetAnalysisResult->update();
                    ui->spinBoxResultPath->setValue(index);
                } else {
                    qDebug() << "DEBUG: Index out of bounds:" << index;
                }
            });
        }
    } else {
        qDebug() << "DEBUG: Chart view is invalid!";
    }
    
    // Update statistics display with the calculated values
    if (!rmsValues.isEmpty()) {
        updateStatisticsDisplay(rmsValues, "degrees");
    } else {
        ui->textAnalysis->setText("No valid path data available for statistical analysis.");
    }
}

void MainWindow::testAreaCutting()
{
    qDebug() << "Testing area cutting with predefined area";
    
    // Create a simple rectangular border for testing
    MapRoute testBorder;
    
    // Create a rectangle from (-100,-100) to (100,100)
    LocPoint p1, p2, p3, p4;
    p1.setXY(-100.0, -100.0);
    p2.setXY(100.0, -100.0);
    p3.setXY(100.0, 100.0);
    p4.setXY(-100.0, 100.0);
    
    testBorder.append(p1);
    testBorder.append(p2);
    testBorder.append(p3);
    testBorder.append(p4);
    testBorder.append(p1); // Close the polygon
    
    // Add this border to the analysis widget
    ui->mapWidgetAnalysis->addField(testBorder);
    mAreaBorderIndex = ui->mapWidgetAnalysis->getFieldNum() - 1;
    mAreaLoaded = true;
    
    qDebug() << "Created test border with" << testBorder.size() << "points";
    
    // Update UI status
    ui->labelAreaStatus->setText("Test area loaded");
    
    // Apply the area filtering
    applyAreaFiltering();
}

bool MainWindow::onLoadShapefile()
{
    ShapeFile sf;
    // Create a file dialog
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    "Open File", "", "Shape Files [*.shp](*.shp)");

    double illh[3];
    ui->mapWidgetFields->getEnuRef(illh);

    // Check if a file was selected
    if (!fileName.isEmpty()) {
        qDebug() << "Selected file:" << fileName;
        // Use the selected file path as needed
        sf.load(fileName,illh,ui->mapWidgetFields);

        if (ui->mapWidgetFields->saveRoutes(ui->filenameEdit->text()))
        {
            showStatusInfo("Saved routes", true);
        } else
        {
            showStatusInfo("Could not save routes", false);
        };
        qDebug() << "stored file";

        addField();

        return true;
    } else return false;
}

bool MainWindow::onShowShapefile()
{
    ShapeFile sf;
    // Create a file dialog
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    "Open File", "", "Shape Files [*.shp](*.shp)");

    double illh[3];
    ui->mapLiveWidget->getEnuRef(illh);

    // Check if a file was selected
    if (!fileName.isEmpty()) {
        qDebug() << "Selected file:" << fileName;
        // Use the selected file path as needed
        sf.load(fileName,illh,ui->mapLiveWidget);
        return true;
    }
    return false;
};

bool MainWindow::onLoadLogfile()
{
    // Create a file dialog
//    QString fileName = QFileDialog::getOpenFileName(this,
//                                                    "Open File", "", "CSV/text files [*.csv,*.txt](*.csv,*.txt)");

    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Open File",
        "",
        "CSV/text files [*.csv,*.txt] (*.csv *.txt)");

    // Check if a file was selected
    if (!fileName.isEmpty()) {
        qDebug() << "Selected file:" << fileName;
        // Use the selected file path as needed
    }

    QByteArray utf8Path = fileName.toUtf8();     // store the QByteArray
    const string &nmeafilename = utf8Path.constData(); // safe pointer

    // Extract farm name from filename if possible
    QString fileNameOnly = QFileInfo(fileName).fileName();
    int lastUnderscore = fileNameOnly.lastIndexOf('_');
    int lastDot = fileNameOnly.lastIndexOf('.');
    
    if (lastUnderscore != -1 && lastDot != -1 && lastUnderscore < lastDot) {
        QString potentialFarmName = fileNameOnly.mid(lastUnderscore + 1, lastDot - lastUnderscore - 1);
        qDebug() << "Extracted potential farm name:" << potentialFarmName;
        
        // Check if this farm name exists in the database
        QSqlQuery query;
        query.prepare("SELECT id FROM locations WHERE name = :farmname");
        query.bindValue(":farmname", potentialFarmName);
        
        if (!query.exec()) {
            qDebug() << "Failed to execute farm name query:" << query.lastError().text();
        } else if (query.next()) {
            int farmId = query.value(0).toInt();
            qDebug() << "Found matching farm ID:" << farmId;
            
            // Select this farm in the farm table
            setCurrentFarm(farmId);
        }
    }

    double refs[3]={0.0,0.0,0.0};
    ui->mapWidgetAnalysis->getEnuRef(refs);
    QByteArray xmlData;
    if (NmeaServer::toXML(refs[0],refs[1],nmeafilename, &xmlData)) {
        QXmlStreamReader xmlReader(xmlData);
        ui->mapWidgetAnalysis->loadXMLRoute(&xmlReader, false);
        
        // Store the original log for non-destructive filtering
        if (ui->mapWidgetAnalysis->mPaths->size() > 0) {
            MapRoute originalRoute = ui->mapWidgetAnalysis->getCurrentPath();
            mOriginalLogs.append(originalRoute);
        }
        
        // Initialize RangeSlider based on the loaded route
        if (ui->mapWidgetAnalysis->mPaths->size() > 0) {
            MapRoute& currentRoute = ui->mapWidgetAnalysis->getCurrentPath();
            int totalPoints = currentRoute.size();
            if (totalPoints > 0) {
                // Set default range to 20-80% of the route
                ui->rangeSlider->setRange(0, 100);
                ui->rangeSlider->setLowerValue(20);
                ui->rangeSlider->setUpperValue(80);
                
                // Update our stored values
                mRangeSliderLowerValue = 20;
                mRangeSliderUpperValue = 80;
            }
        }
        
        // Note: fileList and fileModel are no longer used - replaced with combo boxes
        // fileList.append(QString::fromStdString(nmeafilename));
        // fileModel->setStringList(fileList);  // Update the model
    } else
    {
        QMessageBox msgBox;
        msgBox.setText("The file does not store nmea data in the right format!");
        msgBox.exec();
    }

    return true;
};

void MainWindow::onMachinesTableClicked(const QModelIndex &index)
{
    qDebug() << "onMachinesTableClicked triggered! Row:" << index.row() << "Column:" << index.column() << "Valid:" << index.isValid();
    if (index.isValid()) {
        int row = index.row();
        QString ip;
        QString name;
        
        if (sender() == ui->machinesTable) {
            QTableWidgetItem* ipItem = ui->machinesTable->item(row, 1);
            QTableWidgetItem* nameItem = ui->machinesTable->item(row, 0);
            if (ipItem) ip = ipItem->text().trimmed();
            if (nameItem) name = nameItem->text().trimmed();
        } else {
            QStandardItem* ipItem = machinesModel->item(row, 1);
            QStandardItem* nameItem = machinesModel->item(row, 0);
            if (ipItem) ip = ipItem->text().trimmed();
            if (nameItem) name = nameItem->text().trimmed();
        }
        
        qDebug() << "onMachinesTableClicked: Found machine Name:" << name << "IP:" << ip;
        if (!ip.isEmpty() && ip != "None" && ip != "Loading..." && ip != "XML Error" && ip != "No machines") {
            ui->tcpConnEdit->setPlainText(ip);
            qDebug() << "onMachinesTableClicked: Successfully updated Connection Edit to" << ip;
        } else {
            qDebug() << "onMachinesTableClicked: IP text is invalid or placeholder:" << ip;
        }
    }
}

void MainWindow::onMachinesTableDoubleClicked(const QModelIndex &index)
{
    qDebug() << "onMachinesTableDoubleClicked triggered! Row:" << index.row() << "Column:" << index.column() << "Valid:" << index.isValid();
    if (index.isValid()) {
        int row = index.row();
        QString ip;
        QString name;
        
        if (sender() == ui->machinesTable) {
            QTableWidgetItem* ipItem = ui->machinesTable->item(row, 1);
            QTableWidgetItem* nameItem = ui->machinesTable->item(row, 0);
            if (ipItem) ip = ipItem->text().trimmed();
            if (nameItem) name = nameItem->text().trimmed();
        } else {
            QStandardItem* ipItem = machinesModel->item(row, 1);
            QStandardItem* nameItem = machinesModel->item(row, 0);
            if (ipItem) ip = ipItem->text().trimmed();
            if (nameItem) name = nameItem->text().trimmed();
        }
        
        qDebug() << "onMachinesTableDoubleClicked: Found machine Name:" << name << "IP:" << ip;
        if (!ip.isEmpty() && ip != "None" && ip != "Loading..." && ip != "XML Error" && ip != "No machines") {
            ui->tcpConnEdit->setPlainText(ip);
            qDebug() << "onMachinesTableDoubleClicked: Triggering immediate connection to" << ip;
            on_tcpConnectButton_clicked();
        } else {
            qDebug() << "onMachinesTableDoubleClicked: IP text is invalid or placeholder:" << ip;
        }
    }
}

void MainWindow::onMachinesSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(deselected);
    QModelIndexList indexes = selected.indexes();
    qDebug() << "onMachinesSelectionChanged triggered! Selected indexes count:" << indexes.size();
    if (!indexes.isEmpty()) {
        int row = indexes.first().row();
        QString ip;
        QString name;
        
        if (sender() == ui->machinesTable->selectionModel()) {
            QTableWidgetItem* ipItem = ui->machinesTable->item(row, 1);
            QTableWidgetItem* nameItem = ui->machinesTable->item(row, 0);
            if (ipItem) ip = ipItem->text().trimmed();
            if (nameItem) name = nameItem->text().trimmed();
        } else {
            QStandardItem* ipItem = machinesModel->item(row, 1);
            QStandardItem* nameItem = machinesModel->item(row, 0);
            if (ipItem) ip = ipItem->text().trimmed();
            if (nameItem) name = nameItem->text().trimmed();
        }
        
        qDebug() << "onMachinesSelectionChanged: Found machine Name:" << name << "IP:" << ip;
        if (!ip.isEmpty() && ip != "None" && ip != "Loading..." && ip != "XML Error" && ip != "No machines") {
            ui->tcpConnEdit->setPlainText(ip);
            qDebug() << "onMachinesSelectionChanged: Selection changed, updated Connection Edit to" << ip;
        } else {
            qDebug() << "onMachinesSelectionChanged: IP text is invalid or placeholder:" << ip;
        }
    }
}


void MainWindow::on_listLogFilesView_clicked(const QModelIndex& index) {
    // Note: This function is no longer used - replaced with combo box dropdowns
    // if (index.isValid()) {
    //     QString filename = fileModel->data(index, Qt::DisplayRole).toString();
    //     qDebug() << "Selected file:" << filename;
    //     int row = index.row();  // 0-based row index
    //     qDebug() << "Clicked row:" << row;
    //     ui->mapWidgetAnalysis->setPathNow(row);
    //     
    //     // Store the original log for non-destructive filtering if not already stored
    //     if (row >= 0 && row >= mOriginalLogs.size()) {
    //         MapRoute originalRoute = ui->mapWidgetAnalysis->getCurrentPath();
    //         mOriginalLogs.append(originalRoute);
    //     }
    //     
    //     // Initialize RangeSlider based on the selected route
    //     if (ui->mapWidgetAnalysis->mPaths->size() > 0) {
    //         MapRoute& currentRoute = ui->mapWidgetAnalysis->getCurrentPath();
    //         int totalPoints = currentRoute.size();
    //         if (totalPoints > 0) {
    //             // Set default range to 20-80% of the route
    //             ui->rangeSlider->setRange(0, 100);
    //             ui->rangeSlider->setLowerValue(20);
    //             ui->rangeSlider->setUpperValue(80);
    //             
    //             // Update our stored values
    //             mRangeSliderLowerValue = 20;
    //             mRangeSliderUpperValue = 80;
    //         }
    //     }
    // }
}

void MainWindow::on_mapChooseNmeaButton_clicked()
{
    QString path;
    path = QFileDialog::getOpenFileName(this, tr("Choose log file to open"));
    if (path.isNull()) {
        return;
    }

    ui->mapImportNmeaEdit->setText(path);
}

void MainWindow::on_mapImportNmeaButton_clicked()
{
    QFile file;
    file.setFileName(ui->mapImportNmeaEdit->text());
    bool mapUpdated = false;

    if (file.exists()) {
        bool ok = file.open(QIODevice::ReadOnly | QIODevice::Text);

        if (ok) {
            QTextStream in(&file);

            double i_llh[3];
            bool i_llh_set = false;

            while(!in.atEnd()) {
                QString line = in.readLine();

                NmeaServer::nmea_gga_info_t gga;
                int res = NmeaServer::decodeNmeaGGA(line.toLocal8Bit(), gga);

                if (res > 5) {
                    if (!i_llh_set) {
                        if (ui->mapImportNmeaZeroEnuBox->isChecked()) {
                            i_llh[0] = gga.lat;
                            i_llh[1] = gga.lon;
                            i_llh[2] = gga.height;
                            ui->mapLiveWidget->setEnuRef(i_llh[0], i_llh[1], i_llh[2]);
                        } else {
                            ui->mapLiveWidget->getEnuRef(i_llh);
                        }

                        i_llh_set = true;
                    }

                    double llh[3];
                    double xyz[3];

                    llh[0] = gga.lat;
                    llh[1] = gga.lon;
                    llh[2] = gga.height;
                    utility::llhToEnu(i_llh, llh, xyz);

                    LocPoint p;
                    p.setXY(xyz[0], xyz[1]);
                    QString info;

                    QString fix_t = "Unknown";
                    if (gga.fix_type == 4) {
                        fix_t = "RTK fix";
                        p.setColor(Qt::green);
                    } else if (gga.fix_type == 5) {
                        fix_t = "RTK float";
                        p.setColor(Qt::yellow);
                    } else if (gga.fix_type == 1) {
                        fix_t = "Single";
                        p.setColor(Qt::red);
                    }
                    int rtk_sats = 0;
                    if (gga.fix_type == 4 || gga.fix_type == 5) {
                        rtk_sats = gga.n_sat;
                    }
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
                    info = QString("Fix type: %1\n Sats    : %2 (RTK: %3)\n Height  : %4")
                               .arg(fix_t)
                               .arg(gga.n_sat)
                               .arg(rtk_sats)
                               .arg(gga.height, 0, 'f', 2);
#else
                    info.sprintf("Fix type: %s\nSats    : %d (RTK: %d)\nHeight  : %.2f",
                                 fix_t.toLocal8Bit().data(),
                                 gga.n_sat,
                                 rtk_sats,
                                 gga.height);
#endif
                    p.setInfo(info);

                    if (!mapUpdated) {
                        mapUpdated = true;
                        ui->mapLiveWidget->setNextEmptyOrCreateNewInfoTrace();
                    }

                    ui->mapLiveWidget->addInfoPoint(p);
                }
            }
        } else {
            QMessageBox::warning(this, "Open Error", "Could not open " + file.fileName());
        }

    } else {
        QMessageBox::warning(this, "Open Error", "Please select a valid log file");
    }
}

void MainWindow::on_mapRemoveInfoAllButton_clicked()
{
    ui->mapLiveWidget->clearAllInfoTraces();
}

void MainWindow::on_traceInfoMinZoomBox_valueChanged(double arg1)
{
    ui->mapLiveWidget->setInfoTraceTextZoom(arg1);
}

void MainWindow::on_removeRouteExtraButton_clicked()
{
    on_mapRemoveRouteButton_clicked();
}

void MainWindow::on_mapOsmClearCacheButton_clicked()
{
    ui->mapLiveWidget->osmClient()->clearCache();
    ui->mapLiveWidget->update();
}

void MainWindow::on_mapOsmServerOsmButton_toggled(bool checked)
{
    if (checked) {
                ui->mapLiveWidget->osmClient()->setTileServerUrl("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile");
    	//        ui->mapWidget->osmClient()->setTileServerUrl("http://tile.openstreetmap.org");
    }
}

void MainWindow::on_mapOsmServerHiResButton_toggled(bool checked)
{
    if (checked) {
                ui->mapLiveWidget->osmClient()->setTileServerUrl("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile");
    }
}

void MainWindow::on_mapOsmMaxZoomBox_valueChanged(int arg1)
{
    ui->mapLiveWidget->setOsmMaxZoomLevel(arg1);
}

void MainWindow::on_mapDrawGridBox_toggled(bool checked)
{
    ui->mapLiveWidget->setDrawGrid(checked);
}

void MainWindow::on_mapGetEnuButton_clicked()
{
    mPacketInterface->getEnuRef(ui->mapCarBox->value());
}

void MainWindow::on_mapSetEnuButton_clicked()
{
    double llh[3];
    ui->mapLiveWidget->getEnuRef(llh);
    llh[0]=llh[0]-346;
    llh[1]=llh[1]-75;
    mPacketInterface->setEnuRef(ui->mapCarBox->value(), llh);
}

void MainWindow::on_mapOsmStatsBox_toggled(bool checked)
{
    ui->mapLiveWidget->setDrawOsmStats(checked);
}

void MainWindow::on_removeTraceExtraButton_clicked()
{
    ui->mapLiveWidget->clearTrace();
}

void MainWindow::on_mapEditHelpButton_clicked()
{
    QMessageBox::information(this, tr("Keyboard shortcuts"),
                             tr("<b>CTRL + Left click:</b> Move selected car<br>") +
                                tr("<b>CTRL + Right click:</b> Update route point or anchor settings<br>") +
                                tr("<b>Shift + Left click:</b> Add route point or anchor<br>") +
                                tr("<b>Shift + Left drag:</b> Move route point or anchor<br>") +
                                tr("<b>Shift + right click:</b> Delete route point or anchor<br>") +
                                tr("<b>CTRL + SHIFT + Left click:</b> Zero map ENU coordinates<br>"));
}

void MainWindow::on_mapStreamNmeaConnectButton_clicked()
{
    mNmeaDrawTrace = true;
    mNmea->connectClientTcp(ui->mapStreamNmeaServerEdit->text(),
                            ui->mapStreamNmeaPortBox->value());
}

void MainWindow::on_mapStreamNmeaDisconnectButton_clicked()
{
    mNmea->disconnectClientTcp();
}

void MainWindow::on_mapStreamNmeaClearTraceButton_clicked()
{
    ui->mapLiveWidget->clearInfoTrace();
}

void MainWindow::on_mapRouteBox_valueChanged(int arg1)
{
    ui->mapLiveWidget->setPathNow(arg1);
}

void MainWindow::on_mapRemoveRouteAllButton_clicked()
{
    ui->mapLiveWidget->clearAllPaths();
}

void MainWindow::on_mapUpdateTimeButton_clicked()
{
    bool ok;
    int res = QInputDialog::getInt(this,
                                   tr("Set new route start time"),
                                   tr("Seconds from now"), 30, 0, 60000, 1, &ok);

    if (ok) {
        MapRoute route = ui->mapLiveWidget->getPath();
        QDateTime date = QDateTime::currentDateTime();
        QTime current = QTime::currentTime().addSecs(-date.offsetFromUtc());
        qint32 now = current.msecsSinceStartOfDay() + res * 1000;
        qint32 start_diff = 0;

        for (int i = 0;i < route.size();i++) {
            if (i == 0) {
                start_diff = now - route[i].getTime();
            }

            route[i].setTime(route[i].getTime() + start_diff);
        }

        ui->mapLiveWidget->setPath(route);
    }
}
void MainWindow::on_mapRouteTimeEdit_timeChanged(const QTime &time)
{
    ui->mapLiveWidget->setRoutePointTime(time.msecsSinceStartOfDay());
}

void MainWindow::on_mapTraceMinSpaceCarBox_valueChanged(double arg1)
{
    ui->mapLiveWidget->setTraceMinSpaceCar(arg1 / 1000.0);
}

void MainWindow::on_mapTraceMinSpaceGpsBox_valueChanged(double arg1)
{
    ui->mapLiveWidget->setTraceMinSpaceGps(arg1 / 1000.0);
}

void MainWindow::on_mapInfoTraceBox_valueChanged(int arg1)
{
    ui->mapLiveWidget->setInfoTraceNow(arg1);
}

void MainWindow::on_removeInfoTraceExtraButton_clicked()
{
    ui->mapLiveWidget->clearInfoTrace();
}

void MainWindow::on_pollIntervalBox_valueChanged(int arg1)
{
    mTimer->setInterval(arg1);
}

void MainWindow::on_actionAbout_triggered()
{
    QMessageBox::about(this, "RControlStation",
                       tr("<b>RControlStation %1</b><br>"
                          "&copy; Benjamin Vedder 2016 - 2017<br>"
                          "<a href=\"mailto:benjamin@vedder.se\">benjamin@vedder.se</a><br>").
                       arg(mVersion));
}

void MainWindow::on_actionAboutLibrariesUsed_triggered()
{

    QMessageBox::about(this, "Libraries Used",
                       tr("<b>Icons<br>") +
                          "<a href=\"https://icons8.com/\">https://icons8.com/</a><br><br>" +
                          "<b>Plotting<br>" +
                          "<a href=\"http://qcustomplot.com/\">http://qcustomplot.com/</a><br><br>" +
                          "<b>Linear Algebra<br>" +
                          "<a href=\"http://eigen.tuxfamily.org\">http://eigen.tuxfamily.org</a>");
}

void MainWindow::on_actionExit_triggered()
{
    qApp->exit();
}

void MainWindow::on_actionSaveRoutes_triggered()
{
    saveRoutes(false);
}

void MainWindow::on_actionSaveRouteswithIDs_triggered()
{
    saveRoutes(true);
}

void MainWindow::on_actionLoadRoutes_triggered()
{
    QString filename = QFileDialog::getOpenFileName(this,
                                                    tr("Load Routes"), "",
                                                    tr("Xml files (*.xml)"));

    if (!filename.isEmpty()) {
        int res = utility::loadRoutes(filename, ui->mapLiveWidget);

        if (res >= 0) {
            showStatusInfo("Loaded routes", true);
        } else if (res == -1) {
            QMessageBox::critical(this, "Load Routes",
                                  "Could not open\n" + filename + "\nfor reading");
        } else if (res == -2) {
            QMessageBox::critical(this, "Load Routes",
                                  "routes tag not found in " + filename);
        } else {
            QMessageBox::critical(this, "Load Routes", "unknown error");
        }
    }
}

void MainWindow::on_actionTestIntersection_triggered()
{
    mIntersectionTest->show();
}

void MainWindow::on_actionSaveSelectedRouteAsDriveFile_triggered()
{
    QString filename = QFileDialog::getSaveFileName(this,
                                                    tr("Save Drive File"), "",
                                                    tr("Csv files (*.csv)"));

    // Cancel pressed
    if (filename.isEmpty()) {
        return;
    }

    if (!filename.toLower().endsWith(".csv")) {
        filename.append(".csv");
    }

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, "Save Drive File",
                              "Could not open\n" + filename + "\nfor writing");
        showStatusInfo("Could not save drive file", false);
        return;
    }

    QFileInfo fileInfo(file);


    QTextStream stream(&file);
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    stream.setCodec("UTF-8");
#endif
    MapRoute route = ui->mapLiveWidget->getPath();

    QString trajName = fileInfo.fileName();
    trajName.chop(4);
    stream << "TRAJECTORY;" << trajName <<
              ";0.1;" << route.size() << ";\n";

    for (LocPoint p: route) {
        // LINE;TIME(s);X(m);Y(m);Z(m);HEAD(rad, 0=right, ccw);VEL(m/s);ACCEL(m/s/s);CURVATURE(rad/m);MODE(0);ENDLINE;

        stream << "LINE;";
        stream << (double)p.getTime() / 1000.0 << ";";
        stream << p.getX() << ";" << p.getY() << ";" << "0.0;";
        stream << "0.0;";
        stream << p.getSpeed() << ";";
        stream << "0.0;";
        stream << "0.0;";
        stream << "0;";
        stream << "ENDLINE;\n";
    }

    stream << "ENDTRAJECTORY;";
    file.close();
    showStatusInfo("Saved drive file", true);
}

void MainWindow::on_actionLoadDriveFile_triggered()
{
    QString filename = QFileDialog::getOpenFileName(this,
                                                    tr("Load Drive File"), "",
                                                    tr("Csv files (*.csv)"));

    if (!filename.isEmpty()) {
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, "Load Drive File",
                                  "Could not open\n" + filename + "\nfor reading");
            return;
        }

        QTextStream stream(&file);

        QList<LocPoint> route;

        while (!stream.atEnd()) {
            QString line = stream.readLine();
            if (line.toUpper().startsWith("LINE;")) {
                QStringList tokens = line.split(";");

                // LINE;TIME(s);X(m);Y(m);Z(m);HEAD(rad, 0=right, ccw);VEL(m/s);ACCEL(m/s/s);CURVATURE(rad/m);MODE(0);ENDLINE;

                LocPoint p;
                p.setTime(tokens.at(1).toDouble() * 1000.0);
                p.setX(tokens.at(2).toDouble());
                p.setY(tokens.at(3).toDouble());
                p.setSpeed(tokens.at(6).toDouble());

                route.append(p);
            }
        }

        if (route.size() == 0) {
            file.close();
            QMessageBox::critical(this, "Load Drive File",
                                  "Drive file empty or could not be parsed.");
            return;
        }

        // Reduce route density
        MapRoute routeReduced;
        LocPoint pointLast = route.first();
        routeReduced.append(route.first());

        for (LocPoint p: route) {
            if (p.getDistanceTo(pointLast) >= 1.0) {
                routeReduced.append(p);
                pointLast = p;
            }
        }

        if (route.last().getDistanceTo(pointLast) > 0.01) {
            routeReduced.append(route.last());
        }

        ui->mapLiveWidget->addPath(routeReduced);

        file.close();
        showStatusInfo("Loaded drive file", true);
    }
}

/*
void MainWindow::on_modeRouteButton_toggled(bool checked)
{
    ui->mapLiveWidget->setAnchorMode(!checked);
}

void MainWindow::on_uploadAnchorButton_clicked()
{
    QVector<UWB_ANCHOR> anchors;
    for (LocPoint p: ui->mapLiveWidget->getAnchors()) {
        UWB_ANCHOR a;
        a.dist_last = 0.0;
        a.height = p.getHeight();
        a.id = p.getId();
        a.px = p.getX();
        a.py = p.getY();
        anchors.append(a);
    }

    if (anchors.size() == 0) {
        return;
    }

    ui->uploadAnchorButton->setEnabled(false);

    bool ok = true;
    int car = ui->mapCarBox->value();

    ok = mPacketInterface->clearUwbAnchors(car);

    if (ok) {
        ui->anchorUploadProgressBar->setValue(0);
        for (int i = 0;i < anchors.size();i++) {
            ok = mPacketInterface->addUwbAnchor(car, anchors.at(i));
            if (!ok) {
                break;
            }
        }
    }

    if (!ok) {
        QMessageBox::warning(this, "Upload anchors",
                             "No response when uploading anchors.");
    } else {
        ui->anchorUploadProgressBar->setValue(100);
    }

    ui->uploadAnchorButton->setEnabled(true);
}

void MainWindow::on_anchorIdBox_valueChanged(int arg1)
{
    ui->mapLiveWidget->setAnchorId(arg1);
}

void MainWindow::on_anchorHeightBox_valueChanged(double arg1)
{
    ui->mapLiveWidget->setAnchorHeight(arg1);
}

void MainWindow::on_removeAnchorsButton_clicked()
{
    ui->mapLiveWidget->clearAnchors();
}
*/
void MainWindow::saveRoutes(bool withId)
{
    QString filename = QFileDialog::getSaveFileName(this,
                                                    tr("Save Routes"), "",
                                                    tr("Xml files (*.xml)"));

    // Cancel pressed
    if (filename.isEmpty()) {
        return;
    }

    if (!filename.toLower().endsWith(".xml")) {
        filename.append(".xml");
    }

    if (ui->mapLiveWidget->saveRoutes(filename))
    {
        showStatusInfo("Saved routes", true);
    } else
    {
        showStatusInfo("Could not save routes", false);
    };
}

void MainWindow::on_mapDrawRouteTextBox_toggled(bool checked)
{
    ui->mapLiveWidget->setDrawRouteText(checked);
}

void MainWindow::on_actionGPSSimulator_triggered()
{
#ifdef HAS_LIME_SDR
    mGpsSim->show();
#else
    QMessageBox::warning(this, "GPS Simulator",
                         "This version of RControlStation is not built with LIME SDR support, which "
                         "is required for the GPS simulator.");
#endif
}

void MainWindow::on_mapDrawUwbTraceBox_toggled(bool checked)
{
    ui->mapLiveWidget->setDrawUwbTrace(checked);
}

void MainWindow::on_actionToggleFullscreen_triggered()
{
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void MainWindow::on_actionToggleCameraFullscreen_triggered()
{
    if (mCars.size() == 1) {
        mCars[0]->toggleCameraFullscreen();
    } else {
        for (int i = 0;i < mCars.size();i++) {
            if (mCars[i]->getId() == ui->mapCarBox->value()) {
                mCars[i]->toggleCameraFullscreen();
            }
        }
    }
}

void MainWindow::on_tabWidget_currentChanged(int index)
{
    // Focus on map widget when changing tab to it
    if (index == 1) {
        ui->mapLiveWidget->setFocus();
    }
}

void MainWindow::on_routeZeroButton_clicked()
{
    ui->mapLiveWidget->zoomInOnRoute(ui->mapRouteBox->value(), 0.1);
}

void MainWindow::on_routeZeroAllButton_clicked()
{
    ui->mapLiveWidget->zoomInOnRoute(-1, 0.1);
}

void MainWindow::on_mapRoutePosAttrBox_currentIndexChanged(int index)
{
    quint32 attr = ui->mapLiveWidget->getRoutePointAttributes();
    attr &= ~ATTR_POSITIONING_MASK;
    attr |= index;
    ui->mapLiveWidget->setRoutePointAttributes(attr);
}

void MainWindow::on_comboBoxAction_currentIndexChanged(int index)
{
    // Get the userData from the comboBoxAction item which contains the attribute value
    int attributeValue = ui->comboBoxAction->itemData(index).toInt();
    
    // Set this value in the map widget so it uses this attribute for coloring
    ui->mapLiveWidget->setSelectedActionAttribute(attributeValue);
}

void MainWindow::on_clearAnchorButton_clicked()
{
    mPacketInterface->clearUwbAnchors(ui->mapCarBox->value());
}

void MainWindow::on_setBoundsRoutePushButton_clicked()
{
    int r = ui->mapLiveWidget->getPathNow();
    ui->boundsRouteSpinBox->setValue(r);
}

void MainWindow::on_boundsFillPushButton_clicked()
{

    MapRoute bounds = ui->mapLiveWidget->getField(ui->mapLiveWidget->getFieldNow());
    double spacing = ui->boundsFillSpacingSpinBox->value();
    if (spacing < 0.5) return;

    QList<LocPoint> routeLP;
    if (ui->generateFrameCheckBox->isChecked())
    {
        routeLP = RouteMagic::fillConvexPolygonWithFramedZigZag(bounds.mRoute, spacing, ui->boundsFillKeepTurnsInBoundsCheckBox->isChecked(), ui->boundsFillSpeedSpinBox->value()/3.6,
                                                                ui->boundsFillSpeedInTurnsSpinBox->value()/3.6, ui->stepsForTurningSpinBox->value(), ui->visitEverySpinBox->value(),
                                                                ui->lowerToolsCheckBox->isChecked() ? ATTR_HYDRAULIC_FRONT_DOWN : 0, ui->raiseToolsCheckBox->isChecked() ? ATTR_HYDRAULIC_FRONT_UP : 0,
                                                                ui->lowerToolsDistanceSpinBox->value()*2, ui->raiseToolsDistanceSpinBox->value()*2);
        // attribute changes at half distance
    } else
    {
        routeLP = RouteMagic::fillConvexPolygonWithZigZag(bounds.mRoute, spacing, ui->boundsFillKeepTurnsInBoundsCheckBox->isChecked(), ui->boundsFillSpeedSpinBox->value()/3.6,
                                                          ui->boundsFillSpeedInTurnsSpinBox->value()/3.6, ui->stepsForTurningSpinBox->value(), ui->visitEverySpinBox->value(),
                                                          ui->lowerToolsCheckBox->isChecked() ? ATTR_HYDRAULIC_FRONT_DOWN : 0, ui->raiseToolsCheckBox->isChecked() ? ATTR_HYDRAULIC_FRONT_UP : 0,
                                                          ui->lowerToolsDistanceSpinBox->value()*2, ui->raiseToolsDistanceSpinBox->value()*2);
    };

    MapRoute route;
    route.mRoute=routeLP;

    ui->mapLiveWidget->addPath(route);
    int r = ui->mapLiveWidget->getPaths().size()-1;
    ui->mapLiveWidget->setPathNow(r);
    // ui->mapRouteBox->setCurrentIndex(r);
    ui->mapRouteBox->setValue(r);
    ui->mapLiveWidget->repaint();
}

void MainWindow::on_lowerToolsCheckBox_stateChanged(int arg1)
{
    ui->lowerToolsDistanceSpinBox->setEnabled(arg1 != 0);
}

void MainWindow::on_raiseToolsCheckBox_stateChanged(int arg1)
{
    ui->raiseToolsDistanceSpinBox->setEnabled(arg1 != 0);
}

void MainWindow::on_AutopilotConfigurePushButton_clicked()
{
    ui->mainTabWidget->setCurrentIndex(ui->mainTabWidget->indexOf(ui->tab));
    ui->carsWidget->setCurrentIndex(ui->mapCarBox->value());

    QWidget *tmp = ui->carsWidget->widget(ui->mapCarBox->value());
    if (tmp) {
        CarInterface *car = dynamic_cast<CarInterface*>(tmp);
        car->showAutoPilotConfiguration();
    }
}

void MainWindow::on_AutopilotStartPushButton_clicked()
{
    ui->throttleOffButton->setChecked(true);
    QWidget *tmp = ui->carsWidget->widget(ui->mapCarBox->value());
    if (tmp) {
        CarInterface *car = dynamic_cast<CarInterface*>(tmp);
        if (ui->radioButton_followRoute->isChecked())
            car->setApMode(AP_MODE_FOLLOW_ROUTE);

        else if (ui->radioButton_followMe->isChecked())
            car->setApMode(AP_MODE_FOLLOW_ME);

        car->setAp(true, false);
    }
}

void MainWindow::on_AutopilotStopPushButton_clicked()
{
    QWidget *tmp = ui->carsWidget->widget(ui->mapCarBox->value());
    if (tmp) {
        CarInterface *car = dynamic_cast<CarInterface*>(tmp);
        car->setAp(false, true);
    }
}

void MainWindow::on_AutopilotRestartPushButton_clicked()
{
    ui->throttleOffButton->setChecked(true);
    QWidget *tmp = ui->carsWidget->widget(ui->mapCarBox->value());
    if (tmp) {
        CarInterface *car = dynamic_cast<CarInterface*>(tmp);
        if (ui->radioButton_followRoute->isChecked())
            car->setApMode(AP_MODE_FOLLOW_ROUTE);

        else if (ui->radioButton_followMe->isChecked())
            car->setApMode(AP_MODE_FOLLOW_ME);

        car->setAp(true, true);
    }
}

void MainWindow::on_AutopilotPausePushButton_clicked()
{
    QWidget *tmp = ui->carsWidget->widget(ui->mapCarBox->value());
    if (tmp) {
        CarInterface *car = dynamic_cast<CarInterface*>(tmp);
        car->setAp(false, false);
    }
}

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))

void MainWindow::pollGamepad() {
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
        return;
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_CONTROLLERDEVICEADDED && !gamepadAttached()) {
            // Dosan inkopplad igen (eller en ny): öppna den direkt, ingen omstart behövs.
            if (connectJoystick()) {
                ui->statusBar->showMessage("Dosan ansluten", 3000);
            }
            continue;
        }
        if (event.type == SDL_CONTROLLERDEVICEREMOVED && mController != nullptr &&
            SDL_GameControllerGetAttached(mController) != SDL_TRUE) {
            SDL_GameControllerClose(mController);
            mController = nullptr;
            continue;
        }
//        qDebug() << "button type: " << event.type;
        if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
            qDebug() << "up or down";
            handleButtonEvent(event.cbutton);
        } else if (event.type == SDL_JOYBUTTONDOWN || event.type == SDL_JOYBUTTONUP) {
            qDebug() << "joy button up or down";
            bool pressed = (event.type == SDL_JOYBUTTONDOWN);
            ui->statusBar->showMessage(QString("Gamepad: Raw Button %1 %2").arg(event.jbutton.button).arg(pressed ? "PRESSED" : "RELEASED"), 3000);
            
            // Map common raw button numbers for L1 (Left Shoulder) and R1 (Right Shoulder) as fallbacks!
            int btn = event.jbutton.button;
            if (btn == 4 || btn == 6 || btn == 9) { // L1 candidates
                handleControllerInput(1, pressed ? 1.0f : 0.0f);
            } else if (btn == 5 || btn == 7 || btn == 10) { // R1 candidates
                handleControllerInput(3, pressed ? 1.0f : 0.0f);
            }
        } else if (event.type == SDL_CONTROLLERAXISMOTION) {
//            qDebug() << "axis";
            handleAxisEvent(event.caxis);
        }
    }
}

void MainWindow::handleButtonEvent(const SDL_ControllerButtonEvent& event) {
//    qDebug() << "button id: " << event.button;
    bool pressed = (event.state == SDL_PRESSED);
    
    // Show a live message in the status bar so the user knows exactly which button is registered!
    QString buttonName;
    switch (event.button) {
    case SDL_CONTROLLER_BUTTON_A: buttonName = "A / Cross"; break;
    case SDL_CONTROLLER_BUTTON_B: buttonName = "B / Circle"; break;
    case SDL_CONTROLLER_BUTTON_X: buttonName = "X / Square"; break;
    case SDL_CONTROLLER_BUTTON_Y: buttonName = "Y / Triangle"; break;
    case SDL_CONTROLLER_BUTTON_BACK: buttonName = "Back / Share"; break;
    case SDL_CONTROLLER_BUTTON_GUIDE: buttonName = "Guide / PS"; break;
    case SDL_CONTROLLER_BUTTON_START: buttonName = "Start / Options"; break;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: buttonName = "Left Stick Click"; break;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: buttonName = "Right Stick Click"; break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: buttonName = "L1 (Left Shoulder)"; break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: buttonName = "R1 (Right Shoulder)"; break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: buttonName = "D-Pad Up"; break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: buttonName = "D-Pad Down"; break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: buttonName = "D-Pad Left"; break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: buttonName = "D-Pad Right"; break;
    default: buttonName = QString("Button %1").arg(event.button); break;
    }
    ui->statusBar->showMessage(QString("Gamepad: %1 %2").arg(buttonName).arg(pressed ? "PRESSED" : "RELEASED"), 3000);

    switch (event.button) {
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        qDebug() << "Button L1" << pressed;
        mArmL1 = pressed;
        updateArms();
        break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        qDebug() << "Button R1" << pressed;
        mArmR1 = pressed;
        updateArms();
        break;
    }
}

void MainWindow::handleAxisEvent(const SDL_ControllerAxisEvent& event) {
    switch (event.axis) {
    case SDL_CONTROLLER_AXIS_LEFTX:
        handleControllerInput(6,event.value/32768.0);
        break;
    case SDL_CONTROLLER_AXIS_LEFTY:
        handleControllerInput(5,-event.value/32768.0);
        break;
    case SDL_CONTROLLER_AXIS_RIGHTX:
        handleControllerInput(8,event.value/32768.0);
        break;
    case SDL_CONTROLLER_AXIS_RIGHTY:
        handleControllerInput(7,-event.value/32768.0);
        break;
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
        ui->statusBar->showMessage(QString("Gamepad: L2 Trigger %1%").arg((int)((float)event.value / 327.68f)), 1500);
        // Avtryckaren är analog: nedtryckt över halvvägs, släppt under en fjärdedel.
        if (!mArmL2 && event.value > 16384) { mArmL2 = true; updateArms(); }
        else if (mArmL2 && event.value < 8192) { mArmL2 = false; updateArms(); }
        break;
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
        ui->statusBar->showMessage(QString("Gamepad: R2 Trigger %1%").arg((int)((float)event.value / 327.68f)), 1500);
        if (!mArmR2 && event.value > 16384) { mArmR2 = true; updateArms(); }
        else if (mArmR2 && event.value < 8192) { mArmR2 = false; updateArms(); }
        break;
    }
}

#endif

void MainWindow::handleAddFieldButton()
{
    addField();
    //ui->fieldTable->update();
}

void MainWindow::handleAddFarmButton()
{
    //    QMessageBox msgBox;
    //    msgBox.setText("Adding farm:" + ui->locationnameEdit->text());
    //    msgBox.exec();
    addFarmToServer(ui->locationnameEdit->text());
}

void MainWindow::addField()
{
    int farmid=currentFarm();
    if (farmid)
    {
        addFieldToServer(ui->fieldnameEdit->text(), farmid, ui->filenameEdit->text());
    } else {
        QMessageBox msgBox;
        msgBox.setText("No row selected!");
        msgBox.exec();
    };
}

int MainWindow::currentFarm()
{
    QModelIndexList selectedIndexes = ui->farmTable->selectionModel()->selectedIndexes();
    if (!selectedIndexes.isEmpty()) {
        int rowIndex = selectedIndexes.first().row();
        QStandardItem* nameItem = farmsModel->item(rowIndex, 0);
        if (nameItem) {
            QString farmId = nameItem->data(Qt::UserRole).toString();
            return farmId.toInt(); // farm id
        }
    }
    return -1;
}

void MainWindow::setCurrentFarm(int farm)
{
    // Find the row with the matching farm ID
    for (int row = 0; row < farmsModel->rowCount(); ++row) {
        QStandardItem* nameItem = farmsModel->item(row, 0);
        if (nameItem) {
            QString farmId = nameItem->data(Qt::UserRole).toString();
            if (farmId.toInt() == farm) {
                // Select the row in the table view
                QModelIndex rowIndex = farmsModel->index(row, 0);
                ui->farmTable->setCurrentIndex(rowIndex);
                ui->farmTable->scrollTo(rowIndex);
                break;
            }
        }
    }
}

QLabel* MainWindow::getLogLabel()
{
    return ui->textAnalysis;
}


void selectRowByPrimaryKey(QTableView* tableView, QSqlRelationalTableModel* model, const QString& primaryKeyColumnName, const QVariant& primaryKeyValue) {
    qDebug() << "primaryKeyValue: " << primaryKeyValue;
    // Find the column index for the primary key
    int primaryKeyColumn = model->fieldIndex(primaryKeyColumnName);

    // Iterate through the rows to find the target primary key
    for (int row = 0; row < model->rowCount(); ++row) {
        QModelIndex index = model->index(row, primaryKeyColumn);
        QVariant currentPrimaryKeyValue = model->data(index);
        qDebug() << "currentPrimaryKeyValue: " << currentPrimaryKeyValue;

        if (currentPrimaryKeyValue == primaryKeyValue) {
            // Select the row in the table view
            QModelIndex rowIndex = model->index(row, 0);
            tableView->setCurrentIndex(rowIndex);
            tableView->scrollTo(rowIndex);
            break;
        }
    }
}

void MainWindow::updateGamepadButton()
{
    if (!mGamepadButton) {
        return;
    }
    if (mJoystickControlEnabled) {
        mGamepadButton->setText("🔒 Lås dosa");
        mGamepadButton->setStyleSheet("QPushButton { background: #c62828; color: white; font-weight: bold; }");
        mGamepadButton->setToolTip("Dosan kör roboten. Klicka för att låsa.");
    } else {
        mGamepadButton->setText("🎮 Aktivera dosa");
        mGamepadButton->setStyleSheet("QPushButton { background: #2e7d32; color: white; font-weight: bold; }");
        mGamepadButton->setToolTip("Dosan är låst. Klicka för att köra med dosan (kryssar i Active och Keyboard control).");
    }
}

void MainWindow::activateGamepad()
{
    QString hinder;
    if (!mTcpClientMulti->isAnyConnected()) {
        hinder = "Anslut till roboten först.";
    }
#ifdef HAS_JOYSTICK
    else if (!JSconnected()) {
        hinder = "Ingen dosa ansluten.";
    }
#endif
    else {
        for (int c: {5, 6, 7, 8}) {     // spakarna (vänster/höger, x/y)
            if (qAbs(mCachedControllerValues.value(c, 0.0f)) > 0.1f) {
                hinder = "Släpp spakarna till mitten först.";
                break;
            }
        }
    }
    if (!hinder.isEmpty()) {
        ui->statusBar->showMessage("Dosan aktiveras inte: " + hinder, 5000);
        QMessageBox::information(this, "Aktivera dosa", hinder);
        return;
    }

    ui->checkBoxActiveKB->setChecked(true);     // setJoystickControlEnabled + knappen
    for (CarInterface *car: mCars) {
        if (car->getId() == mActiveCarId) {
            car->setCtrlKb();                   // Keyboard control i, autopiloten av
        }
    }
    mGamepadActivity.restart();
    ui->statusBar->showMessage("Dosan aktiverad – roboten kan köras med dosan", 5000);
}

void MainWindow::lockGamepad(const QString &why)
{
    if (!mJoystickControlEnabled) {
        return;
    }
    // Fart och styrning till 0 innan dosan stängs av, så att inget ligger kvar.
    for (int c: mCachedControllerValues.keys()) {
        handleControllerInput(c, 0.0f);
    }
    ui->checkBoxActiveKB->setChecked(false);    // setJoystickControlEnabled(false) + knappen
    for (CarInterface *car: mCars) {
        if (car->getId() == mActiveCarId) {
            car->setCtrlKbOff();
        }
    }
    ui->statusBar->showMessage(why, 8000);
}

// Vinkelgivaren (firmware 30.4): tre steg, varje steg sparas i styrkortet med
// terminalkommandona vinkel_noll / vinkel_hoger / vinkel_vanster. Svaren syns i terminalen.
void MainWindow::calibrateSteeringAngle()
{
    if (!mTcpClientMulti->isAnyConnected()) {
        QMessageBox::warning(this, "Kalibrera styrvinkel", "Anslut till roboten först.");
        return;
    }
    const quint8 id = mVehicle->carId();
    struct Steg { const char *text; const char *cmd; };
    const Steg steg[] = {
        {"Steg 1 av 3: Ställ hjulen <b>rakt fram</b>.<br>Tryck OK när de står rakt.", "vinkel_noll"},
        {"Steg 2 av 3: Styr <b>fullt höger</b> och låt hjulen stå kvar där.<br>Tryck OK.", "vinkel_hoger"},
        {"Steg 3 av 3: Styr <b>fullt vänster</b> och låt hjulen stå kvar där.<br>Tryck OK.", "vinkel_vanster"},
    };
    for (const Steg &s: steg) {
        if (QMessageBox::question(this, "Kalibrera styrvinkel", s.text,
                                  QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok) {
            showStatusInfo("Kalibreringen avbruten. Det som redan sparats ligger kvar.", false);
            return;
        }
        mPacketInterface->sendTerminalCmd(id, s.cmd);
    }
    mPacketInterface->sendTerminalCmd(id, "vinkel");
    QMessageBox::information(this, "Kalibrera styrvinkel",
                             "Klart. Svaren från styrkortet står i terminalen.<br>"
                             "Rakt fram = 0°, fullt höger = +30°, fullt vänster = −30°.");
}

void MainWindow::openVehicleSettings()
{
    const QString machine = mVehicleMachine;
    VehicleConfig c = VehicleConfig::load(machine);

    QDialog d(this);
    d.setWindowTitle("Batteri och fordon");
    QFormLayout *form = new QFormLayout(&d);
    form->addRow(new QLabel(machine.isEmpty()
            ? "Ingen maskin ansluten: ändrar <b>standard</b> för alla maskiner."
            : QString("Maskin: <b>%1</b>").arg(machine.toHtmlEscaped())));

    QComboBox *type = new QComboBox(&d);
    type->addItem("LiFePO4 (cellernas vilospänning)", "lifepo4");
    type->addItem("Linjärt mellan tom och full spänning", "linear");
    type->setCurrentIndex(c.batteryType == "linear" ? 1 : 0);
    QSpinBox *cells = new QSpinBox(&d);
    cells->setRange(1, 64);
    cells->setValue(c.seriesCells);
    cells->setSuffix(" celler i serie");
    QDoubleSpinBox *emptyV = new QDoubleSpinBox(&d);
    emptyV->setRange(0.0, 200.0);
    emptyV->setDecimals(1);
    emptyV->setSuffix(" V");
    emptyV->setValue(c.emptyV);
    QDoubleSpinBox *fullV = new QDoubleSpinBox(&d);
    fullV->setRange(0.0, 200.0);
    fullV->setDecimals(1);
    fullV->setSuffix(" V");
    fullV->setValue(c.fullV);
    QDoubleSpinBox *cap = new QDoubleSpinBox(&d);
    cap->setRange(0.0, 100000.0);
    cap->setDecimals(0);
    cap->setSuffix(" Wh");
    cap->setSpecialValueText("okänd (ingen räckvidd)");
    cap->setValue(c.capacityWh);
    QDoubleSpinBox *steer = new QDoubleSpinBox(&d);
    steer->setRange(1.0, 90.0);
    steer->setDecimals(1);
    steer->setSuffix(" °");
    steer->setValue(c.steeringMaxDeg);
    QCheckBox *asDefault = new QCheckBox("Spara även som standard för maskiner utan egen inställning", &d);
    asDefault->setChecked(machine.isEmpty());
    asDefault->setEnabled(!machine.isEmpty());

    form->addRow("Batterityp", type);
    form->addRow("LiFePO4", cells);
    form->addRow("Tom (0 %)", emptyV);
    form->addRow("Full (100 %)", fullV);
    form->addRow("Kapacitet", cap);
    form->addRow("Största styrvinkel", steer);
    form->addRow(asDefault);
    form->addRow(new QLabel("<small>Exempel: 4 × 12,8 V 80 Ah LiFePO4 = 16 celler, 4096 Wh.<br>"
                            "Kapaciteten = spänning × amperetimmar. Används för nedräkning och räckvidd.</small>"));
    auto updateEnabled = [=]() {
        const bool lifepo4 = type->currentData().toString() == "lifepo4";
        cells->setEnabled(lifepo4);
        emptyV->setEnabled(!lifepo4);
        fullV->setEnabled(!lifepo4);
    };
    connect(type, QOverload<int>::of(&QComboBox::currentIndexChanged), &d, updateEnabled);
    updateEnabled();

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &d);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    form->addRow(buttons);

    if (d.exec() != QDialog::Accepted) {
        return;
    }
    if (type->currentData().toString() == "linear" && fullV->value() <= emptyV->value()) {
        QMessageBox::warning(this, "Batteri och fordon", "Full spänning måste vara högre än tom.");
        return;
    }
    c.batteryType = type->currentData().toString();
    c.seriesCells = cells->value();
    c.emptyV = emptyV->value();
    c.fullV = fullV->value();
    c.capacityWh = cap->value();
    c.steeringMaxDeg = steer->value();
    c.save(machine);
    if (!machine.isEmpty() && asDefault->isChecked()) {
        c.save(QString());
    }
    mVehicle->setConfig(c);
}

QString MainWindow::getServerBaseUrl() const
{
    QString adress = mServerEdit ? mServerEdit->text().trimmed() : QString();
    if (adress.isEmpty()) {
        adress = STANDARD_SERVER;
    }
    if (!adress.startsWith("http://") && !adress.startsWith("https://")) {
        adress = "http://" + adress;
    }
    while (adress.endsWith('/')) {
        adress.chop(1);
    }
    QUrl url(adress);
    if (url.port() == -1 && url.scheme() == "http") {
        url.setPort(8080);
    }
    return url.toString();
}

MainWindow* findMainWindow() {
    // Iterate through all top-level widgets
    for (QWidget *widget : qApp->topLevelWidgets()) {
        // Try to cast the widget to MainWindow
        MainWindow *mainWindow = qobject_cast<MainWindow*>(widget);
        if (mainWindow) {
            return mainWindow; // Return the first MainWindow found
        }
    }
    return nullptr; // Return nullptr if no MainWindow is found
}

// Missing method implementations
void MainWindow::routePointSelected(LocPoint pos)
{
    qDebug() << "Route point selected - updating UI with point data:";
    qDebug() << "  Speed:" << pos.getSpeed() << "m/s (" << pos.getSpeed() * 3.6 << "km/h)";
    qDebug() << "  Time:" << pos.getTime() << "ms";
    qDebug() << "  Attributes:" << pos.getAttributes();
    qDebug() << "  Control States:" << pos.getControlStates().size() << "states";
    
    // Update the UI with the selected route point's properties
    qDebug() << "Setting speed to" << pos.getSpeed() * 3.6 << "km/h";
    ui->mapRouteSpeedBox->setValue(pos.getSpeed() * 3.6); // Convert from m/s to km/h
    qDebug() << "Speed box now shows" << ui->mapRouteSpeedBox->value() << "km/h";
    
    QTime time;
    time = time.addMSecs(pos.getTime());
    qDebug() << "Setting time to" << time.toString("HH:mm:ss.zzz");
    ui->mapRouteTimeEdit->setTime(time);
    qDebug() << "Time edit now shows" << ui->mapRouteTimeEdit->time().toString("HH:mm:ss.zzz");
    
    // Update position attributes combo box
    qDebug() << "Setting attributes to" << pos.getAttributes();
    ui->mapRoutePosAttrBox->setCurrentIndex(pos.getAttributes());
    qDebug() << "Attributes combo box now shows index" << ui->mapRoutePosAttrBox->currentIndex();
    
    // Update control states table
    qDebug() << "Updating control states table with" << pos.getControlStates().size() << "states";
    updateControlStatesTableFromRoutePoint();
    qDebug() << "Control states table now has" << ui->controlStatesTable->rowCount() << "rows";
    
    // Update the map widget's current route point properties
    ui->mapLiveWidget->setRoutePointSpeed(pos.getSpeed());
    ui->mapLiveWidget->setRoutePointTime(pos.getTime());
    ui->mapLiveWidget->setRoutePointAttributes(pos.getAttributes());
    ui->mapLiveWidget->setRoutePointControlStates(pos.getControlStates());
}

void MainWindow::activePointChanged(LocPoint point)
{
    qDebug() << "Active point changed - updating UI with point data:";
    qDebug() << "  Speed:" << point.getSpeed() << "m/s (" << point.getSpeed() * 3.6 << "km/h)";
    qDebug() << "  Time:" << point.getTime() << "ms";
    qDebug() << "  Attributes:" << point.getAttributes();
    qDebug() << "  Control States:" << point.getControlStates().size() << "states";
    
    // Update the UI with the active route point's properties
    qDebug() << "Setting speed to" << point.getSpeed() * 3.6 << "km/h";
    ui->mapRouteSpeedBox->setValue(point.getSpeed() * 3.6); // Convert from m/s to km/h
    qDebug() << "Speed box now shows" << ui->mapRouteSpeedBox->value() << "km/h";
    
    QTime time;
    time = time.addMSecs(point.getTime());
    qDebug() << "Setting time to" << time.toString("HH:mm:ss.zzz");
    ui->mapRouteTimeEdit->setTime(time);
    qDebug() << "Time edit now shows" << ui->mapRouteTimeEdit->time().toString("HH:mm:ss.zzz");
    
    // Update position attributes combo box
    qDebug() << "Setting attributes to" << point.getAttributes();
    ui->mapRoutePosAttrBox->setCurrentIndex(point.getAttributes());
    qDebug() << "Attributes combo box now shows index" << ui->mapRoutePosAttrBox->currentIndex();
    
    // Update control states table
    qDebug() << "Updating control states table with" << point.getControlStates().size() << "states";
    updateControlStatesTable(point.getControlStates());
    qDebug() << "Control states table now has" << ui->controlStatesTable->rowCount() << "rows";
    
    // Update the map widget's current route point properties
    ui->mapLiveWidget->setRoutePointSpeed(point.getSpeed());
    ui->mapLiveWidget->setRoutePointTime(point.getTime());
    ui->mapLiveWidget->setRoutePointAttributes(point.getAttributes());
    ui->mapLiveWidget->setRoutePointControlStates(point.getControlStates());
}

void MainWindow::onControlSearchCriteriaChanged()
{
    qDebug() << "Control search criteria changed - searching for matching points";
    
    // Get the selected control ID from comboBoxAction
    int currentIndex = ui->comboBoxAction->currentIndex();
    if (currentIndex < 0) {
        qDebug() << "No control selected in comboBoxAction";
        return;
    }
    
    int controlId = ui->comboBoxAction->itemData(currentIndex).toInt();
    double targetValue = ui->targetvalueSpinBox->value();
    
    qDebug() << "Searching for control ID:" << controlId << "with target value:" << targetValue;
    
    // Call the function to find matching points
    QList<int> selectedPoints = ui->mapLiveWidget->findPointsWithControlState(controlId, targetValue);
    
    // Log the results
    qDebug() << "Found" << selectedPoints.size() << "points matching the criteria:";
    foreach (int pointIndex, selectedPoints) {
        qDebug() << "  Point index:" << pointIndex;
    }
    
    // Force a repaint to ensure the highlights are shown
    ui->mapLiveWidget->update();
}

void MainWindow::on_addControlStateButton_clicked()
{
    // Add a new control state row to the table
    int row = ui->controlStatesTable->rowCount();
    ui->controlStatesTable->insertRow(row);
    
    // Set up combo box for controller selection
    QComboBox *comboBox = new QComboBox();
    // Populate from database like comboBoxAction
    QList<ControllerInfo> controllers = db.getAllControllers();
    for (const ControllerInfo &controller : controllers) {
        comboBox->addItem(controller.name, controller.id);
    }
    ui->controlStatesTable->setCellWidget(row, 0, comboBox);
    
    // Set up spin box for value
    QDoubleSpinBox *spinBox = new QDoubleSpinBox();
    spinBox->setRange(-1000.0, 1000.0);
    spinBox->setDecimals(2);
    ui->controlStatesTable->setCellWidget(row, 1, spinBox);
    
    // Connect signals
    connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::on_controlStateComboChanged);
    connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MainWindow::on_controlStateValueChanged);
}

void MainWindow::on_removeControlStateButton_clicked()
{
    // Remove the selected control state row
    int currentRow = ui->controlStatesTable->currentRow();
    if (currentRow >= 0) {
        ui->controlStatesTable->removeRow(currentRow);
    }
}

void MainWindow::on_removeControlStateRow_clicked()
{
    // This seems to be the same as removeControlStateButton_clicked
    on_removeControlStateButton_clicked();
}

void MainWindow::on_controlStatesTable_cellChanged(int row, int column)
{
    (void)row; (void)column;
    // Handle cell changes if needed
}

void MainWindow::on_controlStateComboChanged(int index)
{
    (void)index;
    // Handle controller selection changes
    QComboBox *comboBox = qobject_cast<QComboBox*>(sender());
    if (comboBox) {
        // Update the control state
        updateCurrentRoutePointControlStates();
    }
}

void MainWindow::on_controlStateValueChanged(double value)
{
    (void)value;
    // Handle value changes
    QDoubleSpinBox *spinBox = qobject_cast<QDoubleSpinBox*>(sender());
    if (spinBox) {
        // Update the control state
        updateCurrentRoutePointControlStates();
    }
}

void MainWindow::updateControlStatesTableFromRoutePoint()
{
    // Get control states from the map widget and update the table
    QList<ControlState> controlStates = ui->mapLiveWidget->getRoutePointControlStates();
    updateControlStatesTable(controlStates);
}

void MainWindow::updateControlStatesTable(const QList<ControlState> &controlStates)
{
    // Clear existing rows
    while (ui->controlStatesTable->rowCount() > 0) {
        ui->controlStatesTable->removeRow(0);
    }
    
    // Set column stretching to fill available width
    ui->controlStatesTable->horizontalHeader()->setStretchLastSection(false);
    ui->controlStatesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->controlStatesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    
    // Add rows for each control state
    for (const ControlState &state : controlStates) {
        int row = ui->controlStatesTable->rowCount();
        ui->controlStatesTable->insertRow(row);
        
        // Set up combo box for controller selection
        QComboBox *comboBox = new QComboBox();
        // Populate from database like comboBoxAction
        QList<ControllerInfo> controllers = db.getAllControllers();
        for (const ControllerInfo &controller : controllers) {
            comboBox->addItem(controller.name, controller.id);
        }
        // Find the index that matches state.controlId
        int foundIndex = -1;
        for (int i = 0; i < comboBox->count(); i++) {
            if (comboBox->itemData(i).toInt() == state.controlId) {
                foundIndex = i;
                break;
            }
        }
        if (foundIndex >= 0) {
            comboBox->setCurrentIndex(foundIndex);
        }
        ui->controlStatesTable->setCellWidget(row, 0, comboBox);
        
        // Set up spin box for value
        QDoubleSpinBox *spinBox = new QDoubleSpinBox();
        spinBox->setRange(-1000.0, 1000.0);
        spinBox->setDecimals(2);
        spinBox->setValue(state.targetValue);
        ui->controlStatesTable->setCellWidget(row, 1, spinBox);
        
        // Connect signals
        connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::on_controlStateComboChanged);
        connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &MainWindow::on_controlStateValueChanged);
    }
}

void MainWindow::updateCurrentRoutePointControlStates()
{
    // Collect control states from the table
    QList<ControlState> controlStates;
    
    for (int row = 0; row < ui->controlStatesTable->rowCount(); row++) {
        QComboBox *comboBox = qobject_cast<QComboBox*>(ui->controlStatesTable->cellWidget(row, 0));
        QDoubleSpinBox *spinBox = qobject_cast<QDoubleSpinBox*>(ui->controlStatesTable->cellWidget(row, 1));
        
        if (comboBox && spinBox) {
            ControlState state;
            state.controlId = comboBox->currentIndex();
            state.targetValue = spinBox->value();
            controlStates.append(state);
        }
    }
    
    // Update the map widget
    ui->mapLiveWidget->setRoutePointControlStates(controlStates);
    
    // If a route point is selected, update it directly
    int selectedPoint = ui->mapLiveWidget->getRoutePointSelected();
    if (selectedPoint >= 0) {
        MapRoute& currentRoute = ui->mapLiveWidget->getCurrentPath();
        if (selectedPoint < currentRoute.size()) {
            currentRoute[selectedPoint].setControlStates(controlStates);
        }
    }
}

void MainWindow::populateControlStateComboBoxes()
{
    // Clear existing items
    ui->comboBoxAction->clear();
    
    // Get controllers from database
    QList<ControllerInfo> controllers = db.getAllControllers();
    
    if (controllers.isEmpty()) {
        // If no controllers in database, add some default ones
        qDebug() << "No controllers found in database, adding defaults";
        db.addController("Front Lift");
        db.addController("Rear Lift");
        db.addController("PTO");
        db.addController("3-Point Hitch");
        db.addController("Sprayer");
        
        // Refresh the list
        controllers = db.getAllControllers();
    }
    
    // Add controllers to combo box using database ID as user data
    for (const ControllerInfo &controller : controllers) {
        ui->comboBoxAction->addItem(controller.name, controller.id);
        qDebug() << "Added controller:" << controller.name << "with ID:" << controller.id;
    }
}


