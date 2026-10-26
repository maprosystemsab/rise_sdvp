/*
    Copyright 2019 Benjamin Vedder	benjamin@vedder.se

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

#ifndef TCPCLIENTMULTI_H
#define TCPCLIENTMULTI_H

#include <QObject>
#include <QTcpSocket>
#include "packetinterface.h"

class TcpClientMulti : public QObject
{
    Q_OBJECT
public:
    explicit TcpClientMulti(QObject *parent = nullptr);
    ~TcpClientMulti();

    void addConnection(QString ip, int port);
    bool isAnyConnected();
    QString connectedIp();  // adressen till första anslutna maskinen, annars tom
    void disconnectAll();
    void sendAll(QByteArray data);

signals:
    void stateChanged(QString msg, QString ip, bool isError);
    void packetRx(QByteArray data);

public slots:

private:
    class TcpConn : public QObject {
    public:
        TcpConn(QString ip, int port, TcpClientMulti *client) : QObject(client) {
            socket.abort();
            attempedip=ip;
            socket.connectToHost(ip, port);

            connect(&socket, &QTcpSocket::readyRead, [this]() {
                while (socket.bytesAvailable() > 0) {
                    QByteArray data = socket.readAll();
                    packet.processData(data);
                }
            });

            connect(&socket, &QTcpSocket::connected, [this,client]() {
                // Stäng av Nagle: annars håller TCP små spakkommandon tills förra
                // paketet kvitterats, och över 4G (300-500 ms) kom "släpp" en sekund för sent.
                socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
                emit client->stateChanged("TCP Connected", attempedip, false);
            });

            discConn = connect(&socket, &QTcpSocket::disconnected, [this,client]() {
                emit client->stateChanged("TCP Disconnected", attempedip,  false);
            });


    #if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
            connect(&socket, &QTcpSocket::errorOccurred,
                    [this, client](QAbstractSocket::SocketError e) {
                        (void)e;
/*
                        QHostAddress peerIp = socket.peerAddress();
                        QString ip=peerIp.toString();
                        if (!ip)
                            {
                            ip=attempedip;
                            }
*/
                        QString errorStr = socket.errorString();
                        socket.close();
                        emit client->stateChanged(QString("TCP Error: %1").arg(errorStr), attempedip, true);
                    });
   #else
            connect(&socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
                    [this,client](QAbstractSocket::SocketError e) {
                (void)e;
                QString errorStr = socket.errorString();
                socket.close();
                emit client->stateChanged(QString("TCP Error: %1").arg(errorStr), attempedip, true);
            });
    #endif
            connect(&packet, &PacketInterface::packetReceived,
                    [client](quint8 id, CMD_PACKET cmd, const QByteArray &data) {
                (void)id;
                (void)cmd;
                emit client->packetRx(data);
            });

            connect(&packet, &PacketInterface::dataToSend, [this](QByteArray &data) {
                    socket.write(data);
            });
        }

        ~TcpConn() {
            disconnect(discConn);
        }

        void sendData(QByteArray data) {
            socket.write(data);
        }

        bool isTcpConnected() {
            return socket.isOpen();
        }

        QString ip() const {
            return attempedip;
        }

    private:
        QTcpSocket socket;
        PacketInterface packet;
        QMetaObject::Connection discConn;
        QString attempedip;
    };

    QVector<TcpConn*> mTcpConns;

};

#endif // TCPCLIENTMULTI_H
