/*
    Copyright 2017 Benjamin Vedder	benjamin@vedder.se

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

#include "confcommonwidget.h"
#include "ui_confcommonwidget.h"
#include "carinterface.h"

#include <cstring>

ConfCommonWidget::ConfCommonWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ConfCommonWidget)
{
    ui->setupUi(this);
}

ConfCommonWidget::~ConfCommonWidget()
{
    delete ui;
}

void ConfCommonWidget::populateMotorTypeComboBoxes(const QList<QPair<int, QString>>& motorTypes)
{
    // Clear existing items first
    ui->comboBox_motor1_type->clear();
    ui->comboBox_motor2_type->clear();
    ui->comboBox_motor3_type->clear();
    ui->comboBox_motor4_type->clear();

    // Populate each combo box with motor types
    for (const auto& motorType : motorTypes) {
        int id = motorType.first;
        QString name = motorType.second;
        // Add item to all motor type combo boxes
        ui->comboBox_motor1_type->addItem(name, QVariant(id));
        ui->comboBox_motor2_type->addItem(name, QVariant(id));
        ui->comboBox_motor3_type->addItem(name, QVariant(id));
        ui->comboBox_motor4_type->addItem(name, QVariant(id));
    }
}

void ConfCommonWidget::populateActionsComboBoxes(const QList<QPair<int, QString>>& actions)
{
    // Clear existing items first
    ui->comboBox_motor1_action->clear();
    ui->comboBox_motor2_action->clear();
    ui->comboBox_motor3_action->clear();
    ui->comboBox_motor4_action->clear();

    // Populate each combo box with motor types
    for (const auto& action : actions) {
        int id = action.first;
        QString name = action.second;
        // Add item to all motor type combo boxes
        ui->comboBox_motor1_action->addItem(name, QVariant(id));
        ui->comboBox_motor2_action->addItem(name, QVariant(id));
        ui->comboBox_motor3_action->addItem(name, QVariant(id));
        ui->comboBox_motor4_action->addItem(name, QVariant(id));
    }

    // RobAnt: drivsidorna var för sig, för vändning på stället (firmwaren, aktivitet 20/21)
    const QList<QPair<int, QString>> robant = {
        {20, "Fart vänster (RobAnt)"},
        {21, "Fart höger (RobAnt)"}
    };
    for (const auto& a : robant) {
        if (ui->comboBox_motor1_action->findData(a.first) < 0) {
            ui->comboBox_motor1_action->addItem(a.second, QVariant(a.first));
            ui->comboBox_motor2_action->addItem(a.second, QVariant(a.first));
            ui->comboBox_motor3_action->addItem(a.second, QVariant(a.first));
            ui->comboBox_motor4_action->addItem(a.second, QVariant(a.first));
        }
    }
}

void ConfCommonWidget::populateModesComboBoxes(const QList<QPair<int, QString>>& modes)
{
    // Clear existing items first
    ui->comboBox_motor1_mode->clear();
    ui->comboBox_motor2_mode->clear();
    ui->comboBox_motor3_mode->clear();
    ui->comboBox_motor4_mode->clear();

    // Populate each combo box with motor types
    for (const auto& mode : modes) {
        int id = mode.first;
        QString name = mode.second;
        // Add item to all motor type combo boxes
        ui->comboBox_motor1_mode->addItem(name, QVariant(id));
        ui->comboBox_motor2_mode->addItem(name, QVariant(id));
        ui->comboBox_motor3_mode->addItem(name, QVariant(id));
        ui->comboBox_motor4_mode->addItem(name, QVariant(id));
    }
}



void ConfCommonWidget::getConfGui(MAIN_CONFIG &conf)
{
    conf.mag_use = ui->confMagUseBox->isChecked();
    conf.mag_comp = ui->confMagCompBox->isChecked();
    conf.yaw_mag_gain = ui->confYawMagGainBox->value();

    conf.mag_cal_cx = ui->confMagCxBox->value();
    conf.mag_cal_cy = ui->confMagCyBox->value();
    conf.mag_cal_cz = ui->confMagCzBox->value();
    conf.mag_cal_xx = ui->confMagXxBox->value();
    conf.mag_cal_xy = ui->confMagXyBox->value();
    conf.mag_cal_xz = ui->confMagXzBox->value();
    conf.mag_cal_yx = ui->confMagYxBox->value();
    conf.mag_cal_yy = ui->confMagYyBox->value();
    conf.mag_cal_yz = ui->confMagYzBox->value();
    conf.mag_cal_zx = ui->confMagZxBox->value();
    conf.mag_cal_zy = ui->confMagZyBox->value();
    conf.mag_cal_zz = ui->confMagZzBox->value();

    conf.gps_ant_x = ui->confGpsAntXBox->value();
    conf.gps_ant_y = ui->confGpsAntYBox->value();
    conf.gps_comp = ui->confGpsCorrBox->isChecked();
    conf.gps_req_rtk = ui->confGpsReqRtkBox->isChecked();
    conf.gps_use_rtcm_base_as_enu_ref = ui->confGpsEnuRefBaseBox->isChecked();
    conf.gps_corr_gain_stat = ui->confGpsCorrStatBox->value();
    conf.gps_corr_gain_dyn = ui->confGpsCorrDynBox->value();
    conf.gps_corr_gain_yaw = ui->confGpsCorrYawBox->value();
    conf.gps_send_nmea = ui->confGpsSendNmeaBox->isChecked();
    conf.gps_use_ubx_info = ui->confGpsUbxUseInfoBox->isChecked();
    conf.gps_ubx_max_acc = ui->confGpsUbxMaxAccBox->value();

    conf.uwb_max_corr = ui->confUwbMaxCorrBox->value();

    conf.ap_repeat_routes = ui->confApRepeatBox->isChecked();
    conf.ap_base_rad = ui->confApBaseRadBox->value();
    conf.ap_rad_time_ahead = ui->confApRadTimeBox->value();
    conf.ap_max_speed = ui->confApMaxSpeedBox->value() / 3.6;
    conf.ap_time_add_repeat_ms = ui->confApAddRepeatTimeEdit->time().msecsSinceStartOfDay();

    if (ui->confApModeSpeedButton->isChecked()) {
        conf.ap_mode_time = 0;
    } else if (ui->confApModeTimeAbsButton->isChecked()) {
        conf.ap_mode_time = 1;
    } else if (ui->confApModeTimeRelButton->isChecked()) {
        conf.ap_mode_time = 2;
    }

    conf.log_rate_hz = ui->confLogRateBox->value();
    conf.log_en = ui->confLogEnBox->isChecked();
    strcpy(conf.log_name, ui->confLogNameEdit->text().toLocal8Bit().data());

    if (ui->confLogUartOffButton->isChecked()) {
        conf.log_mode_ext = LOG_EXT_OFF;
    } else if (ui->confLogUartContButton->isChecked()) {
        conf.log_mode_ext = LOG_EXT_UART;
    } else if (ui->confLogUartPolledButton->isChecked()) {
        conf.log_mode_ext = LOG_EXT_UART_POLLED;
    } else if (ui->confLogEthernetButton->isChecked()) {
        conf.log_mode_ext = LOG_EXT_ETHERNET;
    }
    conf.log_uart_baud = ui->confLogUartBaudBox->value();
    conf.vehicle.actuators=ui->doubleSpinBox_actuators->value();

    qDebug() << "Actuators (from RCS): " << conf.vehicle.actuators;
    // Map GUI Database type ID (1=VESC, 2=Hydraulic) to STM32 enum (0=VESC, 1=Hydraulic)
    auto mapGuiTypeToStm32 = [](int guiType) -> int {
        if (guiType == 1) return 0; // VESC -> AT_VESCMOTOR (0)
        if (guiType == 2) return 1; // Hydraulic -> AT_HYDRAULIC (1)
        return guiType;
    };

    conf.vehicle.actuator[0].type = mapGuiTypeToStm32(ui->comboBox_motor1_type->currentData().toInt());
    conf.vehicle.actuator[0].vescid=ui->doubleSpinBox_vescid1->value();
    conf.vehicle.actuator[0].activity=ui->comboBox_motor1_action->currentData().toInt();
    
    // Map GUI Database mode ID (1=duty, 2=rpm, 3=current) to STM32 enum (0=duty, 1=current, 2=current_brake, 3=rpm)
    auto mapGuiModeToStm32 = [](int guiMode) -> int {
        if (guiMode == 1) return 0; // duty -> MOTOR_CONTROL_DUTY (0)
        if (guiMode == 3) return 1; // current -> MOTOR_CONTROL_CURRENT (1)
        if (guiMode == 2) return 3; // rpm -> MOTOR_CONTROL_RPM (3)
        return guiMode;
    };
    
    conf.vehicle.actuator[0].mode = mapGuiModeToStm32(ui->comboBox_motor1_mode->currentData().toInt());
    
    conf.vehicle.actuator[1].type = mapGuiTypeToStm32(ui->comboBox_motor2_type->currentData().toInt());
    conf.vehicle.actuator[1].vescid=ui->doubleSpinBox_vescid2->value();
    conf.vehicle.actuator[1].activity=ui->comboBox_motor2_action->currentData().toInt();
    conf.vehicle.actuator[1].mode = mapGuiModeToStm32(ui->comboBox_motor2_mode->currentData().toInt());
    
    conf.vehicle.actuator[2].type = mapGuiTypeToStm32(ui->comboBox_motor3_type->currentData().toInt());
    conf.vehicle.actuator[2].vescid=ui->doubleSpinBox_vescid3->value();
    conf.vehicle.actuator[2].activity=ui->comboBox_motor3_action->currentData().toInt();
    conf.vehicle.actuator[2].mode = mapGuiModeToStm32(ui->comboBox_motor3_mode->currentData().toInt());
    
    conf.vehicle.actuator[3].type = mapGuiTypeToStm32(ui->comboBox_motor4_type->currentData().toInt());
    conf.vehicle.actuator[3].vescid=ui->doubleSpinBox_vescid4->value();
    conf.vehicle.actuator[3].activity=ui->comboBox_motor4_action->currentData().toInt();
    conf.vehicle.actuator[3].mode = mapGuiModeToStm32(ui->comboBox_motor4_mode->currentData().toInt());
}

void ConfCommonWidget::setConfGui(const MAIN_CONFIG &conf)
{
    ui->confMagUseBox->setChecked(conf.mag_use);
    ui->confMagCompBox->setChecked(conf.mag_comp);
    ui->confYawMagGainBox->setValue(conf.yaw_mag_gain);

    ui->confMagCxBox->setValue(conf.mag_cal_cx);
    ui->confMagCyBox->setValue(conf.mag_cal_cy);
    ui->confMagCzBox->setValue(conf.mag_cal_cz);
    ui->confMagXxBox->setValue(conf.mag_cal_xx);
    ui->confMagXyBox->setValue(conf.mag_cal_xy);
    ui->confMagXzBox->setValue(conf.mag_cal_xz);
    ui->confMagYxBox->setValue(conf.mag_cal_yx);
    ui->confMagYyBox->setValue(conf.mag_cal_yy);
    ui->confMagYzBox->setValue(conf.mag_cal_yz);
    ui->confMagZxBox->setValue(conf.mag_cal_zx);
    ui->confMagZyBox->setValue(conf.mag_cal_zy);
    ui->confMagZzBox->setValue(conf.mag_cal_zz);

    ui->confGpsAntXBox->setValue(conf.gps_ant_x);
    ui->confGpsAntYBox->setValue(conf.gps_ant_y);
    ui->confGpsCorrBox->setChecked(conf.gps_comp);
    ui->confGpsReqRtkBox->setChecked(conf.gps_req_rtk);
    ui->confGpsEnuRefBaseBox->setChecked(conf.gps_use_rtcm_base_as_enu_ref);
    ui->confGpsCorrStatBox->setValue(conf.gps_corr_gain_stat);
    ui->confGpsCorrDynBox->setValue(conf.gps_corr_gain_dyn);
    ui->confGpsCorrYawBox->setValue(conf.gps_corr_gain_yaw);
    ui->confGpsSendNmeaBox->setChecked(conf.gps_send_nmea);
    ui->confGpsUbxUseInfoBox->setChecked(conf.gps_use_ubx_info);
    ui->confGpsUbxMaxAccBox->setValue(conf.gps_ubx_max_acc);

    ui->confUwbMaxCorrBox->setValue(conf.uwb_max_corr);

    ui->confApRepeatBox->setChecked(conf.ap_repeat_routes);
    CarInterface * car = dynamic_cast<CarInterface *>(parentWidget()->parentWidget()->parentWidget()->parentWidget());
    ui->confApResetOnEmergencyStopBox->setChecked(car->getResetApOnEmergencyStop());
    ui->confApBaseRadBox->setValue(conf.ap_base_rad);
    ui->confApRadTimeBox->setValue(conf.ap_rad_time_ahead);
    ui->confApMaxSpeedBox->setValue(conf.ap_max_speed * 3.6);
    ui->confApAddRepeatTimeEdit->setTime(QTime::fromMSecsSinceStartOfDay(conf.ap_time_add_repeat_ms));

    switch (conf.ap_mode_time) {
    case 0: ui->confApModeSpeedButton->setChecked(true); break;
    case 1: ui->confApModeTimeAbsButton->setChecked(true); break;
    case 2: ui->confApModeTimeRelButton->setChecked(true); break;
    default: break;
    }

    ui->confLogRateBox->setValue(conf.log_rate_hz);
    ui->confLogEnBox->setChecked(conf.log_en);
    ui->confLogNameEdit->setText(QString::fromLocal8Bit(conf.log_name));


    qDebug() << "Actuators (from vehicle): " << conf.vehicle.actuators;
    ui->doubleSpinBox_actuators->setValue(conf.vehicle.actuators);
    
    // Map STM32 enum (0=duty, 1=current, 2=current_brake, 3=rpm) to GUI Database mode ID (1=duty, 2=rpm, 3=current)
    auto mapStm32ModeToGui = [](int stm32Mode) -> int {
        if (stm32Mode == 0) return 1; // MOTOR_CONTROL_DUTY (0) -> duty (1)
        if (stm32Mode == 1) return 3; // MOTOR_CONTROL_CURRENT (1) -> current (3)
        if (stm32Mode == 3) return 2; // MOTOR_CONTROL_RPM (3) -> rpm (2)
        return stm32Mode;
    };

    // Map STM32 enum (0=VESC, 1=Hydraulic) back to GUI Database type ID (1=VESC, 2=Hydraulic)
    auto mapStm32TypeToGui = [](int stm32Type) -> int {
        if (stm32Type == 0) return 1; // AT_VESCMOTOR (0) -> VESC (1)
        if (stm32Type == 1) return 2; // AT_HYDRAULIC (1) -> Hydraulic (2)
        return stm32Type;
    };

    setComboBoxByData(ui->comboBox_motor1_type, mapStm32TypeToGui(conf.vehicle.actuator[0].type));
    ui->doubleSpinBox_vescid1->setValue(conf.vehicle.actuator[0].vescid);
    setComboBoxByData(ui->comboBox_motor1_action,conf.vehicle.actuator[0].activity);
    setComboBoxByData(ui->comboBox_motor1_mode, mapStm32ModeToGui(conf.vehicle.actuator[0].mode));

    setComboBoxByData(ui->comboBox_motor2_type, mapStm32TypeToGui(conf.vehicle.actuator[1].type));
    ui->doubleSpinBox_vescid2->setValue(conf.vehicle.actuator[1].vescid);
    setComboBoxByData(ui->comboBox_motor2_action,conf.vehicle.actuator[1].activity);
    setComboBoxByData(ui->comboBox_motor2_mode, mapStm32ModeToGui(conf.vehicle.actuator[1].mode));

    setComboBoxByData(ui->comboBox_motor3_type, mapStm32TypeToGui(conf.vehicle.actuator[2].type));
    ui->doubleSpinBox_vescid3->setValue(conf.vehicle.actuator[2].vescid);
    setComboBoxByData(ui->comboBox_motor3_action,conf.vehicle.actuator[2].activity);
    setComboBoxByData(ui->comboBox_motor3_mode, mapStm32ModeToGui(conf.vehicle.actuator[2].mode));

    setComboBoxByData(ui->comboBox_motor4_type, mapStm32TypeToGui(conf.vehicle.actuator[3].type));
    ui->doubleSpinBox_vescid4->setValue(conf.vehicle.actuator[3].vescid);
    setComboBoxByData(ui->comboBox_motor4_action,conf.vehicle.actuator[3].activity);
    setComboBoxByData(ui->comboBox_motor4_mode, mapStm32ModeToGui(conf.vehicle.actuator[3].mode));

    /*00
    conf.vehicle.actuator[0].activity=ui->comboBox_motor1_action->currentData().toInt();
    conf.vehicle.actuator[0].mode=ui->comboBox_motor1_mode->currentData().toInt();
    conf.vehicle.actuator[1].type=ui->comboBox_motor2_type->currentData().toInt();
    conf.vehicle.actuator[1].vescid=ui->doubleSpinBox_vescid2->value();
    conf.vehicle.actuator[1].activity=ui->comboBox_motor2_action->currentData().toInt();
    conf.vehicle.actuator[1].mode=ui->comboBox_motor2_mode->currentData().toInt();
    conf.vehicle.actuator[2].type=ui->comboBox_motor3_type->currentData().toInt();
    conf.vehicle.actuator[2].vescid=ui->doubleSpinBox_vescid3->value();
    conf.vehicle.actuator[2].activity=ui->comboBox_motor3_action->currentData().toInt();
    conf.vehicle.actuator[2].mode=ui->comboBox_motor3_mode->currentData().toInt();
    conf.vehicle.actuator[3].type=ui->comboBox_motor4_type->currentData().toInt();
    conf.vehicle.actuator[3].vescid=ui->doubleSpinBox_vescid4->value();
    conf.vehicle.actuator[3].activity=ui->comboBox_motor4_action->currentData().toInt();
    conf.vehicle.actuator[3].mode=ui->comboBox_motor4_mode->currentData().toInt();
   */

    switch (conf.log_mode_ext) {
    case LOG_EXT_OFF: ui->confLogUartOffButton->setChecked(true); break;
    case LOG_EXT_UART: ui->confLogUartContButton->setChecked(true); break;
    case LOG_EXT_UART_POLLED: ui->confLogUartPolledButton->setChecked(true); break;
    case LOG_EXT_ETHERNET: ui->confLogEthernetButton->setChecked(true); break;
    default: break;
    }

    ui->confLogUartBaudBox->setValue(conf.log_uart_baud);
}

void ConfCommonWidget::setMagComp(QVector<double> comp)
{
    if (comp.size() == 9) {
        ui->confMagXxBox->setValue(comp.at(0));
        ui->confMagXyBox->setValue(comp.at(1));
        ui->confMagXzBox->setValue(comp.at(2));

        ui->confMagYxBox->setValue(comp.at(3));
        ui->confMagYyBox->setValue(comp.at(4));
        ui->confMagYzBox->setValue(comp.at(5));

        ui->confMagZxBox->setValue(comp.at(6));
        ui->confMagZyBox->setValue(comp.at(7));
        ui->confMagZzBox->setValue(comp.at(8));
    }
}

void ConfCommonWidget::setMagCompCenter(QVector<double> center)
{
    if (center.size() == 3) {
        ui->confMagCxBox->setValue(center.at(0));
        ui->confMagCyBox->setValue(center.at(1));
        ui->confMagCzBox->setValue(center.at(2));
    }
}

void ConfCommonWidget::showAutoPilotConfiguration()
{
    ui->tabWidget_2->setCurrentIndex(ui->tabWidget_2->indexOf(ui->tab_9));
}

void ConfCommonWidget::on_magCalLoadButton_clicked()
{
    emit loadMagCal();
}

void ConfCommonWidget::on_confApResetOnEmergencyStopBox_toggled(bool checked)
{
    CarInterface * car = dynamic_cast<CarInterface *>(parentWidget()->parentWidget()->parentWidget()->parentWidget());
    car->setResetApOnEmergencyStop(checked);
}

void setComboBoxByData(QComboBox* combo, const QVariant& data) {
    int index = combo->findData(data);
    if (index != -1) combo->setCurrentIndex(index);
}
