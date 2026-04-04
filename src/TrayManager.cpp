#include "TrayManager.h"
#include "SettingsDialog.h"
#include "AppSettings.h"
#include "phonedirectory.h"
#include <QApplication>
#include <QStyle>
#include <QDebug>

TrayManager::TrayManager(QObject *parent) : QObject(parent) {
    m_logWindow = new LogWindow();
    setupTrayIcon();
    startBridges();
}

// THIS IS THE MISSING DESTRUCTOR
TrayManager::~TrayManager() {
    stopBridges();
    if (m_logWindow) {
        delete m_logWindow;
    }
}

void TrayManager::setupTrayIcon() {
    trayIconMenu = new QMenu();
    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setContextMenu(trayIconMenu);

    // Fallback icon if custom one isn't loaded
    trayIcon->setIcon(QApplication::style()->standardIcon(QStyle::SP_DriveNetIcon));
    trayIcon->setToolTip("ModemBridge Fleet");
    trayIcon->show();
}

void TrayManager::startBridges() {
    stopBridges();
    trayIconMenu->clear();

    QList<BridgeConfig> configs = AppSettings::instance().loadBridges();

    if (configs.isEmpty()) {
        m_logWindow->logError("SYSTEM", "No bridges configured. Use Settings to add a port.");
    }

    for (const BridgeConfig& config : configs) {
        ModemBridge *bridge = new ModemBridge(this);

        // Identity Injection: ModemBridge signals now require portName [cite: 1, 2]
        bridge->setSerialPort(config.portName, config.baudRate);
        bridge->setFlowControl(config.flowControl);
        bridge->setLocalEcho(config.localEcho);
        bridge->setPhonebookPath(config.phonebookPath);

        // Plug diagnostic cables into the LogWindow
        connect(bridge, &ModemBridge::statusMessage, m_logWindow, &LogWindow::logStatus);
        connect(bridge, &ModemBridge::errorOccurred, m_logWindow, &LogWindow::logError);
        connect(bridge, &ModemBridge::traceData, m_logWindow, &LogWindow::logTrace);

        bridges.push_back(bridge);
        bridge->start();

        // Sub-menu for this specific port
        QString menuTitle = config.friendlyName.isEmpty() ? config.portName : config.friendlyName;
        QMenu *bridgeMenu = trayIconMenu->addMenu(QString("[%1]").arg(menuTitle));

        QAction *dialAct = bridgeMenu->addAction("Phonebook / Dial...");
        connect(dialAct, &QAction::triggered, [this, bridge, config]() {
            PhoneDirectory pd;
            pd.loadFromFile(config.phonebookPath);
            if (pd.exec() == QDialog::Accepted) {
                bridge->dial(pd.getSelectedEntry());
            }
        });

        bridgeMenu->addSeparator();

        // Connect Macro slots from the tray [cite: 1, 2]
        QAction *userAct = bridgeMenu->addAction("Send User ID");
        connect(userAct, &QAction::triggered, [bridge]() { bridge->injectMacro('u'); });

        QAction *passAct = bridgeMenu->addAction("Send Password");
        connect(passAct, &QAction::triggered, [bridge]() { bridge->injectMacro('p'); });

        bridgeMenu->addSeparator();

        QAction *stopAct = bridgeMenu->addAction("Hangup / Stop");
        connect(stopAct, &QAction::triggered, [bridge]() { bridge->stop(); });
    }

    trayIconMenu->addSeparator();

    QAction *settingsAct = trayIconMenu->addAction("Global Settings...");
    connect(settingsAct, &QAction::triggered, this, &TrayManager::showSettings);

    QAction *logAct = trayIconMenu->addAction("View Dashboard");
    connect(logAct, &QAction::triggered, this, &TrayManager::showLogs);

    trayIconMenu->addSeparator();

    QAction *quitAct = trayIconMenu->addAction("Quit ModemBridge");
    connect(quitAct, &QAction::triggered, this, &TrayManager::quitApp);
}

void TrayManager::stopBridges() {
    for (auto bridge : bridges) {
        bridge->stop();
        delete bridge;
    }
    bridges.clear();
}

void TrayManager::showSettings() {
    SettingsDialog dialog;
    if (dialog.exec() == QDialog::Accepted) {
        startBridges();
    }
}

void TrayManager::showLogs() {
    m_logWindow->show();
    m_logWindow->raise();
    m_logWindow->activateWindow();
}

void TrayManager::quitApp() {
    QApplication::quit();
}
