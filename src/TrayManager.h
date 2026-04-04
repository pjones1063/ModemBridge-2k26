#ifndef TRAYMANAGER_H
#define TRAYMANAGER_H

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <vector>

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

    QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;

    // The Garage: Active engines
    std::vector<ModemBridge*> bridges;

    // The Dashboard
    LogWindow *m_logWindow;
};

#endif // TRAYMANAGER_H
