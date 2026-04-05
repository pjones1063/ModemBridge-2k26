#include "TrayManager.h"
#include "SettingsDialog.h"
#include "AppSettings.h"
#include "phonedirectory.h"
#include <QApplication>
#include <QStyle>
#include <QDebug>
#include <QFile>
#include <QHttpServerResponse>
#include <QTcpServer>
#include <QJsonArray>
#include <QJsonObject>
#include <QDomDocument>



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

        // --- WEB UI ROUTING (Outbound Terminal Data Only) ---
        connect(bridge, &ModemBridge::traceData, this, [this](const QString &portName, const QString &dir, const QByteArray &data) {
            QString color = dir.startsWith("TX") ? "var(--accent-neon)" : "var(--accent-cyan)";
            QString safeData;
            for (char c : data) {
                quint8 byte = static_cast<quint8>(c);
                if (byte >= 32 && byte <= 126) safeData += QChar(byte);
                else if (byte == '\r' || byte == '\n') safeData += QChar(byte);
                else safeData += ".";
            }
            QString html = QString("<span style='color: #888;'>[%1 %2]</span> <span style='color: %3;'>%4</span>")
                               .arg(portName).arg(dir).arg(color).arg(safeData.toHtmlEscaped());

            emit m_webBridge->logTraceReceived(portName, html);
        });

        connect(bridge, &ModemBridge::connectionStateChanged, this, [this](const QString &portName, const QString &state) {
            emit m_webBridge->portStateReceived(portName, state);
        });

        // ----------------------------------------------------

        // Identity Injection
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

    m_webSocketServer = new QWebSocketServer("ModemBridge Web UI", QWebSocketServer::NonSecureMode, this);

    if (!m_webSocketServer->listen(QHostAddress::Any, wsPort)) {
        m_logWindow->logError("SYSTEM", QString("Failed to start WebSocket server on port %1").arg(wsPort));
        return;
    }

    m_clientWrapper = new WebSocketClientWrapper(m_webSocketServer, this);
    m_webChannel = new QWebChannel(this);
    connect(m_clientWrapper, &WebSocketClientWrapper::clientConnected, m_webChannel, &QWebChannel::connectTo);

    m_webBridge = new WebBridge(this);
    m_webChannel->registerObject("modemBridgeUI", m_webBridge);

    // --- 1. PORT TARGETED ROUTING ---
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

    // --- 2. DATA REQUEST ROUTING ---
    connect(m_webBridge, &WebBridge::sigRequestActivePorts, this, [this]() {
        QJsonArray ports;
        for (auto b : bridges) ports.append(b->portName());
        emit m_webBridge->activePortsReceived(ports);
    });

    connect(m_webBridge, &WebBridge::sigRequestPhonebook, this, [this]() {
        QJsonArray jsonEntries;
        QList<BridgeConfig> configs = AppSettings::instance().loadBridges();
        if (!configs.isEmpty()) {
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

    // --- Start HTTP Server ---
    m_httpTcpServer = new QTcpServer(this);
    m_httpServer = new QHttpServer(this);

    // Route 1: Serve HTML with dynamic port injection
    m_httpServer->route("/", []() {
        QFile file(":/webui/index.html");
        if (file.open(QIODevice::ReadOnly)) {
            QString html = file.readAll();

            // Inject the custom port into the JS string
            int dynamicWsPort = AppSettings::instance().webSocketPort();
            html.replace("12345", QString::number(dynamicWsPort));

            return QHttpServerResponse("text/html", html.toUtf8());
        }
        return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
    });

    // Route 2: Serve JS library
    m_httpServer->route("/qwebchannel.js", []() {
        QFile file(":/webui/qwebchannel.js");
        if (file.open(QIODevice::ReadOnly)) return QHttpServerResponse("application/javascript", file.readAll());
        return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
    });

    // Bind and listen on dynamic HTTP port
    int httpPort = AppSettings::instance().httpPort();
    if (!m_httpTcpServer->listen(QHostAddress::Any, httpPort) || !m_httpServer->bind(m_httpTcpServer)) {
        m_logWindow->logError("SYSTEM", QString("Failed to start HTTP Web UI on port %1").arg(httpPort));
        m_httpTcpServer->deleteLater();
    } else {
        m_logWindow->logStatus("SYSTEM", QString("Web UI accessible at http://localhost:%1").arg(httpPort));
    }
}


void TrayManager::restartWebServers() {

    int newWsPort = AppSettings::instance().webSocketPort();
    int newHttpPort = AppSettings::instance().httpPort();

    // Hot-Swap WebSocket Port
    if (m_webSocketServer->serverPort() != newWsPort) {
        m_webSocketServer->close();
        if (m_webSocketServer->listen(QHostAddress::Any, newWsPort)) {
            m_logWindow->logStatus("SYSTEM", QString("WebSocket Uplink moved to port %1").arg(newWsPort));
        } else {
            m_logWindow->logError("SYSTEM", QString("Failed to bind WebSocket to port %1").arg(newWsPort));
        }
    }

    // Hot-Swap HTTP Port
    if (m_httpTcpServer->serverPort() != newHttpPort) {
        m_httpTcpServer->close();
        if (m_httpTcpServer->listen(QHostAddress::Any, newHttpPort)) {
            m_logWindow->logStatus("SYSTEM", QString("Web UI moved to http://localhost:%1").arg(newHttpPort));
        } else {
            m_logWindow->logError("SYSTEM", QString("Failed to bind HTTP to port %1").arg(newHttpPort));
        }
    }
}

void TrayManager::quitApp() {
    QApplication::quit();
}

