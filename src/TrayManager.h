#ifndef TRAYMANAGER_H
#define TRAYMANAGER_H

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <vector>
#include <QWebSocketServer>
#include <QWebChannel>
#include <QHttpServer>
#include "websocketclientwrapper.h"
#include "webbridge.h"
#include "modembridge.h"
#include "LogWindow.h"

class TrayManager : public QObject {
    Q_OBJECT

public:
    explicit TrayManager(QObject *parent = nullptr);
    ~TrayManager(); // Must be defined in the .cpp

private slots:
    void quitApp();
    void showSettings();
    void showLogs();
    void startBridges();
    void stopBridges();

private:
    void setupTrayIcon();
    void setupWebServer();
    void restartWebServers();

    QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;
    QWebSocketServer *m_webSocketServer;
    WebSocketClientWrapper *m_clientWrapper;
    QWebChannel *m_webChannel;
    WebBridge *m_webBridge;
    QHttpServer *m_httpServer;
    QTcpServer *m_httpTcpServer;

    // The Garage: Active engines
    std::vector<ModemBridge*> bridges;

    // The Dashboard
    LogWindow *m_logWindow;
};

#endif // TRAYMANAGER_H
