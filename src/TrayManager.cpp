#include "TrayManager.h"
#include "SettingsDialog.h"
#include "AppSettings.h"
#include "phonedirectory.h"
#include "AboutDialog.h"
#include <QApplication>
#include <QStyle>
#include <QDebug>
#include <QFile>
#include <QHttpServerResponse>
#include <QTcpServer>
#include <QJsonArray>
#include <QJsonObject>
#include <QDomDocument>
#include <QMessageBox>


TrayManager::TrayManager(QObject *parent) : QObject(parent) {
    m_logWindow = new LogWindow();
    setupTrayIcon();
    setupWebServer();
    startBridges();
}

TrayManager::~TrayManager() {
    stopBridges();
    if (m_logWindow) {
        delete m_logWindow;
    }
}

void TrayManager::setupTrayIcon() {
    trayIconMenu = new QMenu();
    trayIcon = new QSystemTrayIcon(this);

    // This handles the standard Right-Click automatically
    trayIcon->setContextMenu(trayIconMenu);

    //trayIcon->setIcon(QApplication::style()->standardIcon(QStyle::SP_DriveNetIcon));
    trayIcon->setIcon(QIcon(":icons/app_icon.png"));
    trayIcon->setToolTip("ModemBridge Fleet");
    trayIcon->show();

    // --- NEW: Handle Left-Click to show the menu ---
    connect(trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        // 'Trigger' usually represents a single left-click on the tray icon
        if (reason == QSystemTrayIcon::Trigger) {
            trayIconMenu->popup(QCursor::pos());
        }
    });
}


void TrayManager::startBridges() {
    stopBridges();
    trayIconMenu->clear();

    QList<BridgeConfig> configs = AppSettings::instance().loadBridges();

    if (configs.isEmpty()) {
        m_logWindow->logError("SYSTEM", "No bridges configured. Use Settings to add a port.");
    }

    for (const BridgeConfig& config : configs) {

        if (!config.isEnabled) {
            continue;
        }

        ModemBridge *bridge = new ModemBridge(this);

        // --- WEB UI ROUTING ---
        // Send Status messages to the Web UI instead of keystrokes
        connect(bridge, &ModemBridge::statusMessage, this, [this](const QString &portName, const QString &msg) {
            QString html = QString("<span style='color: #4CAF50;'>[%1] %2</span>").arg(portName).arg(msg.toHtmlEscaped());
            emit m_webBridge->logTraceReceived(portName, html);
        });

        // Send Error messages to the Web UI
        connect(bridge, &ModemBridge::errorOccurred, this, [this](const QString &portName, const QString &err) {
            QString html = QString("<span style='color: #F44336;'>[%1] ERROR: %2</span>").arg(portName).arg(err.toHtmlEscaped());
            emit m_webBridge->logTraceReceived(portName, html);
        });

        // Keep Port State sync intact
        connect(bridge, &ModemBridge::connectionStateChanged, this, [this](const QString &portName, const QString &state) {
            emit m_webBridge->portStateReceived(portName, state);
        });

        // --- IDENTITY INJECTION ---
        bridge->setSerialPort(config.portName, config.baudRate);
        bridge->setFlowControl(config.flowControl);
        bridge->setLocalEcho(config.localEcho);
        bridge->setPhonebookPath(config.phonebookPath);

        // Ensure the inbound TCP listener spins up correctly on boot
        bridge->setListenPort(config.listenPort);

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
        connect(dialAct, &QAction::triggered, [bridge, config]() {
            PhoneDirectory pd(config.phonebookPath);
            if (pd.exec() == QDialog::Accepted) {
                bridge->dial(pd.getSelectedEntry());
            }
        });

        bridgeMenu->addSeparator();

        QAction *userAct = bridgeMenu->addAction("Send User ID");
        connect(userAct, &QAction::triggered, [bridge]() { bridge->injectMacro('u'); });

        QAction *passAct = bridgeMenu->addAction("Send Password");
        connect(passAct, &QAction::triggered, [bridge]() { bridge->injectMacro('p'); });

        bridgeMenu->addSeparator();

        QAction *hangupAct = bridgeMenu->addAction("Hangup Call");
        connect(hangupAct, &QAction::triggered, [bridge]() { bridge->hangup(); });
    }

    trayIconMenu->addSeparator();

    QAction *settingsAct = trayIconMenu->addAction("Global Settings...");
    connect(settingsAct, &QAction::triggered, this, &TrayManager::showSettings);

    QAction *logAct = trayIconMenu->addAction("View Dashboard");
    connect(logAct, &QAction::triggered, this, &TrayManager::showLogs);

    QAction *abountAct = trayIconMenu->addAction("About");
    connect(abountAct, &QAction::triggered, this, &TrayManager::showAbout);

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

        QList<BridgeConfig> updatedConfigs = AppSettings::instance().loadBridges();

        // 1. Did the fleet size change? (Added or removed a port)
        if (updatedConfigs.size() != bridges.size()) {
            // We need a full restart to build new objects and tray sub-menus
            m_logWindow->logStatus("SYSTEM", "Fleet size changed. Rebuilding all bridges...");
            startBridges();
        }
        // 2. Surgical Hot-Swap: Push settings to existing running bridges
        else {
            for (const BridgeConfig& config : updatedConfigs) {
                for (ModemBridge* bridge : bridges) {
                    if (bridge->portName() == config.portName) {

                        // Push live settings without dropping active BBS calls!
                        bridge->setListenPort(config.listenPort);
                        bridge->setLocalEcho(config.localEcho);
                        bridge->setFlowControl(config.flowControl);
                        bridge->setPhonebookPath(config.phonebookPath);

                        // Handle Enable/Disable toggles
                        if (config.isEnabled && bridge->currentState() == "OFFLINE") {
                            bridge->start();
                        } else if (!config.isEnabled && bridge->currentState() != "OFFLINE") {
                            bridge->stop();
                        }
                        break; // Move to the next config
                    }
                }
            }
            m_logWindow->logStatus("SYSTEM", "Live settings applied to active bridges.");
        }

        // Always apply web UI port hot-swapping
        restartWebServers();
    }
}


void TrayManager::showLogs() {
    m_logWindow->show();
    m_logWindow->raise();
    m_logWindow->activateWindow();
}


void TrayManager::setupWebServer() {
    int wsPort = AppSettings::instance().webSocketPort();
    int httpPort = AppSettings::instance().httpPort();

    // 1. Initialize ALL pointers immediately so they are never null or garbage.
    m_webSocketServer = new QWebSocketServer("ModemBridge Web UI", QWebSocketServer::NonSecureMode, this);
    m_clientWrapper = new WebSocketClientWrapper(m_webSocketServer, this);
    m_webChannel = new QWebChannel(this);
    m_webBridge = new WebBridge(this);
    m_httpTcpServer = new QTcpServer(this);
    m_httpServer = new QHttpServer(this);

    connect(m_clientWrapper, &WebSocketClientWrapper::clientConnected, m_webChannel, &QWebChannel::connectTo);
    m_webChannel->registerObject("modemBridgeUI", m_webBridge);

    // --- PORT TARGETED ROUTING ---
    connect(m_webBridge, &WebBridge::sigHangup, this, [this](const QString &portName) {
        for (auto b : bridges) if (b->portName() == portName) b->hangup();
    });
    connect(m_webBridge, &WebBridge::sigInjectUser, this, [this](const QString &portName) {
        for (auto b : bridges) if (b->portName() == portName) b->injectMacro('u');
    });
    connect(m_webBridge, &WebBridge::sigInjectPass, this, [this](const QString &portName) {
        for (auto b : bridges) if (b->portName() == portName) b->injectMacro('p');
    });
    connect(m_webBridge, &WebBridge::sigDialBbs, this, [this](const QString &portName, const QString &name, const QString &ip, int port, const QString &protocol, const QString &login, const QString &password) {
        for (auto b : bridges) {
            if (b->portName() == portName) {
                BbsEntry entry;
                entry.name = name; entry.ip = ip; entry.port = port;
                entry.protocol = protocol; entry.login = login; entry.password = password;
                b->dial(entry);
            }
        }
    });

    // --- DATA REQUEST ROUTING ---
    connect(m_webBridge, &WebBridge::sigRequestActivePorts, this, [this]() {
        QJsonArray ports;
        for (auto b : bridges) ports.append(b->portName());
        emit m_webBridge->activePortsReceived(ports);
    });

    connect(m_webBridge, &WebBridge::sigRequestPhonebook, this, [this]() {
        QJsonArray jsonEntries;
        QList<BridgeConfig> configs = AppSettings::instance().loadBridges();
        if (!configs.isEmpty() && !configs.first().phonebookPath.isEmpty()) {
            QFile file(configs.first().phonebookPath);
            if (file.open(QIODevice::ReadOnly)) {
                QDomDocument doc;
                if (doc.setContent(&file)) {
                    QDomNodeList list = doc.elementsByTagName("BBS");
                    for (int i = 0; i < list.size(); i++) {
                        QDomElement e = list.at(i).toElement();
                        QJsonObject obj;
                        obj["name"] = e.attribute("name"); obj["ip"] = e.attribute("ip");
                        obj["port"] = e.attribute("port").toInt(); obj["protocol"] = e.attribute("protocol");
                        obj["login"] = e.attribute("login"); obj["password"] = e.attribute("password");
                        jsonEntries.append(obj);
                    }
                }
                file.close();
            }
        }
        emit m_webBridge->phonebookDataReceived(jsonEntries);
    });

    connect(m_webBridge, &WebBridge::sigRequestPortState, this, [this](const QString &portName) {
        for (auto b : bridges) {
            if (b->portName() == portName) {
                emit m_webBridge->portStateReceived(portName, b->currentState());
                break;
            }
        }
    });

    // --- 2. Start WebSocket Server safely (IPv4 Forced) ---
    if (!m_webSocketServer->listen(QHostAddress::AnyIPv4, wsPort)) {
        QString errMsg = QString("Failed to start WebSocket server on port %1. It may be in use by another application.").arg(wsPort);
        m_logWindow->logError("SYSTEM", errMsg);
        QMessageBox::critical(nullptr, "Port Conflict", errMsg);
    } else {
        m_logWindow->logStatus("SYSTEM", QString("WebSocket Uplink ACTIVE on port %1").arg(m_webSocketServer->serverPort()));
    }

    // --- 3. Start HTTP Server safely (IPv4 Forced) ---
    m_httpServer->route("/", []() {
        QFile file(":/webui/index.html");
        if (file.open(QIODevice::ReadOnly)) {
            QString html = file.readAll();
            int dynamicWsPort = AppSettings::instance().webSocketPort();
            html.replace("12345", QString::number(dynamicWsPort));
            return QHttpServerResponse("text/html", html.toUtf8());
        }
        return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
    });

    m_httpServer->route("/qwebchannel.js", []() {
        QFile file(":/webui/qwebchannel.js");
        if (file.open(QIODevice::ReadOnly)) return QHttpServerResponse("application/javascript", file.readAll());
        return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
    });

    if (!m_httpTcpServer->listen(QHostAddress::AnyIPv4, httpPort) || !m_httpServer->bind(m_httpTcpServer)) {
        QString errMsg = QString("Failed to start HTTP Web UI on port %1. It may be in use by another application.").arg(httpPort);
        m_logWindow->logError("SYSTEM", errMsg);
        QMessageBox::critical(nullptr, "Port Conflict", errMsg);
        // We DO NOT delete m_httpTcpServer here so it's safe to check later
    } else {
        m_logWindow->logStatus("SYSTEM", QString("Web UI accessible at http://localhost:%1").arg(httpPort));
    }
}


void TrayManager::restartWebServers() {
    int newWsPort = AppSettings::instance().webSocketPort();
    int newHttpPort = AppSettings::instance().httpPort();

    // Hot-Swap WebSocket Port (Checking if port changed OR if it failed to listen previously)
    if (m_webSocketServer->serverPort() != newWsPort || !m_webSocketServer->isListening()) {
        m_webSocketServer->close();
        if (m_webSocketServer->listen(QHostAddress::AnyIPv4, newWsPort)) {
            m_logWindow->logStatus("SYSTEM", QString("WebSocket Uplink moved to port %1").arg(newWsPort));
        } else {
            QString errMsg = QString("Failed to bind WebSocket to port %1. The port may be in use.").arg(newWsPort);
            m_logWindow->logError("SYSTEM", errMsg);
            QMessageBox::warning(nullptr, "Port Conflict", errMsg);
        }
    }

    // Hot-Swap HTTP Port (Checking if port changed OR if it failed to listen previously)
    if (m_httpTcpServer->serverPort() != newHttpPort || !m_httpTcpServer->isListening()) {
        m_httpTcpServer->close();
        if (m_httpTcpServer->listen(QHostAddress::AnyIPv4, newHttpPort)) {
            m_logWindow->logStatus("SYSTEM", QString("Web UI moved to http://localhost:%1").arg(newHttpPort));
        } else {
            QString errMsg = QString("Failed to bind HTTP Web UI to port %1. The port may be in use.").arg(newHttpPort);
            m_logWindow->logError("SYSTEM", errMsg);
            QMessageBox::warning(nullptr, "Port Conflict", errMsg);
        }
    }
}


void TrayManager::showAbout() {
    AboutDialog dialog;
    dialog.exec();
}

void TrayManager::quitApp() {QApplication::quit();}

