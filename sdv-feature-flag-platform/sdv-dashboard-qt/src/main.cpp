#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPalette>

namespace {

constexpr char kIpcServerName[] = "SDV_Dashboard_ipc_v1";

bool tryForwardCarToRunningInstance(const QString& carId) {
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kIpcServerName));
    if (!socket.waitForConnected(500)) {
        return false;
    }

    const QByteArray msg = QByteArray("car:") + carId.toUtf8() + '\n';
    socket.write(msg);
    if (!socket.waitForBytesWritten(1000)) {
        return false;
    }
    if (!socket.waitForReadyRead(1000)) {
        return true;
    }
    socket.readAll();
    return true;
}

void attachIpcServer(QLocalServer* server, sdv::dash::MainWindow* win) {
    QObject::connect(server, &QLocalServer::newConnection, server, [server, win]() {
        QLocalSocket* client = server->nextPendingConnection();
        if (!client) {
            return;
        }

        QObject::connect(client, &QLocalSocket::readyRead, client, [client, win]() {
            const QByteArray raw = client->readAll().trimmed();
            const QString line = QString::fromUtf8(raw);
            if (line.startsWith(QStringLiteral("car:"))) {
                win->selectVehicle(line.mid(4));
            }
            client->write("ok\n");
            client->flush();
            client->disconnectFromServer();
        });
    });
}

}  // namespace

static void applyDarkTheme(QApplication& app) {
    app.setStyle(QStringLiteral("Fusion"));
    QPalette pal;
    pal.setColor(QPalette::Window, QColor(QStringLiteral("#0f141c")));
    pal.setColor(QPalette::WindowText, QColor(QStringLiteral("#e6eaf2")));
    pal.setColor(QPalette::Base, QColor(QStringLiteral("#121923")));
    pal.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#151d29")));
    pal.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#e6eaf2")));
    pal.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#0f141c")));
    pal.setColor(QPalette::Text, QColor(QStringLiteral("#e6eaf2")));
    pal.setColor(QPalette::Button, QColor(QStringLiteral("#161d27")));
    pal.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e6eaf2")));
    pal.setColor(QPalette::BrightText, QColor(QStringLiteral("#ffffff")));
    pal.setColor(QPalette::Highlight, QColor(QStringLiteral("#2563eb")));
    pal.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    pal.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#94a3b8")));
    app.setPalette(pal);
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SDV_Dashboard"));
    QApplication::setOrganizationName(QStringLiteral("Hyundai SDV Demo"));

    applyDarkTheme(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("SDV observability dashboard (subscribe-only over HTTP/MQTT)."));
    parser.addHelpOption();

    QCommandLineOption apiOpt(QStringList() << QStringLiteral("api-base"),
                              QStringLiteral("status-server base URL"),
                              QStringLiteral("url"),
                              QStringLiteral("http://127.0.0.1:8080"));
    QCommandLineOption mqttHost(QStringList() << QStringLiteral("mqtt-host"),
                                QStringLiteral("MQTT broker host"),
                                QStringLiteral("host"),
                                QStringLiteral("127.0.0.1"));
    QCommandLineOption mqttPort(QStringList() << QStringLiteral("mqtt-port"),
                                QStringLiteral("MQTT broker TCP port"),
                                QStringLiteral("port"),
                                QStringLiteral("1884"));
    QCommandLineOption carOpt(QStringList() << QStringLiteral("car-id"),
                              QStringLiteral("Vehicle id car_1..car_10"),
                              QStringLiteral("id"),
                              QStringLiteral("car_1"));
    parser.addOption(apiOpt);
    parser.addOption(mqttHost);
    parser.addOption(mqttPort);
    parser.addOption(carOpt);
    parser.process(app);

    const QString carId = parser.value(carOpt).trimmed().isEmpty() ? QStringLiteral("car_1")
                                                                    : parser.value(carOpt).trimmed();

    if (tryForwardCarToRunningInstance(carId)) {
        return 0;
    }

    QLocalServer::removeServer(QString::fromLatin1(kIpcServerName));
    auto* ipcServer = new QLocalServer(&app);
    if (!ipcServer->listen(QString::fromLatin1(kIpcServerName))) {
        qWarning("SDV_Dashboard: IPC listen failed (%s)", qPrintable(ipcServer->errorString()));
    }

    sdv::dash::MainWindow win;
    attachIpcServer(ipcServer, &win);
    win.applyLaunchOptions(parser.value(apiOpt), carId, parser.value(mqttHost),
                           parser.value(mqttPort).toInt());

    win.show();
    return QApplication::exec();
}
