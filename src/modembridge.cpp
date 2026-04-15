#include "modembridge.h"
#include <QDebug>
#include <QFile>
#include <QDomDocument>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <exception>
#include <QSerialPort>
#include <QByteArray>

bool ModemBridge::doServerDate() {
    try {
        std::time_t t = std::time(nullptr);
        std::tm* now = std::localtime(&t);
        int hour_kk = (now->tm_hour == 0) ? 24 : now->tm_hour;
        std::ostringstream oss;
        //oss << "getdtm "  // The Atari looks for this string first!
        oss << std::setfill('0')
            << std::setw(4) << (now->tm_year + 1900) << " "
            << std::setw(2) << (now->tm_mon + 1) << " "
            << std::setw(2) << now->tm_mday << " "
            << std::setw(2) << hour_kk << " "
            << std::setw(2) << now->tm_min << " "
            << std::setw(2) << now->tm_sec;
        QByteArray response = QByteArray::fromStdString(oss.str());
        sendToSerial(response + "\r\n");
    } catch (const std::exception& e) {
        qCritical() << "SEVERE Exception:" << e.what();
        return false;
    }
    return true;
}

bool ModemBridge::doServerY2KDate() {
    try {
        std::time_t t = std::time(nullptr);
        std::tm* now = std::localtime(&t);
        int year30 = (now->tm_year + 1900) - 30;
        int hour_kk = (now->tm_hour == 0) ? 24 : now->tm_hour;
        std::ostringstream oss;
        // oss << "getdtm "  // The Atari looks for this string first!
        oss << std::setfill('0')
            << std::setw(4) << (now->tm_year + 1900) << " "
            << std::setw(2) << (now->tm_mon + 1) << " "
            << std::setw(2) << now->tm_mday << " "
            << std::setw(2) << hour_kk << " "
            << std::setw(2) << now->tm_min << " "
            << std::setw(2) << now->tm_sec;
        QByteArray response = QByteArray::fromStdString(oss.str());
        sendToSerial(response + "\r\n");
    } catch (const std::exception& e) {
        qCritical() << "SEVERE Exception:" << e.what();
        return false;
    }
    return true;
}


ModemBridge::ModemBridge(QObject *parent) : QObject(parent),
    m_serial(new QSerialPort(this)),
    m_socket(new QTcpSocket(this)),
    m_ssh(new SshClient(this)),
    m_tcpServer(new QTcpServer(this)),
    m_pendingSocket(nullptr),
    m_ringTimer(new QTimer(this)),
    m_isActive(false),
    m_isConnected(false),
    m_isSshMode(false),
    m_escapeTimer(new QTimer(this)),
    m_portName("Unassigned") // Default ID
{
    m_localEcho = true;

    m_escapeTimer->setSingleShot(true);
    connect(m_escapeTimer, &QTimer::timeout, this, &ModemBridge::checkEscapeSequence);

    // Serial
    connect(m_serial, &QSerialPort::readyRead, this, &ModemBridge::onSerialDataReceived);

    // TCP (Telnet)
    connect(m_socket, &QTcpSocket::readyRead, this, &ModemBridge::onSocketDataReceived);
    connect(m_socket, &QTcpSocket::connected, this, &ModemBridge::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &ModemBridge::onSocketDisconnected);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &ModemBridge::onSocketError);

    // SSH Connections
    connect(m_ssh, &SshClient::connected, this, &ModemBridge::onSshConnected);
    connect(m_ssh, &SshClient::disconnected, this, &ModemBridge::onSshDisconnected);
    connect(m_ssh, &SshClient::rxData, this, &ModemBridge::onSshDataReceived);
    connect(m_ssh, &SshClient::error, this, &ModemBridge::onSshError);

    connect(m_serial, &QSerialPort::errorOccurred, this, &ModemBridge::onSerialError);

    connect(m_tcpServer, &QTcpServer::newConnection, this, &ModemBridge::onNewConnection);
    connect(m_ringTimer, &QTimer::timeout, this, &ModemBridge::onRingTimeout);

}

ModemBridge::~ModemBridge() {
    stop();
}

void ModemBridge::setSerialPort(const QString &portName, int baudRate) {
    if (m_isActive) stop();

    // Save the port name so we know who is talking later
    m_portName = portName;

    m_serial->setPortName(portName);
    m_serial->setBaudRate(baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);
}

void ModemBridge::setFlowControl(bool enable) {
    m_flowControl = enable;
    if (m_serial->isOpen()) {
        m_serial->setFlowControl(enable ? QSerialPort::HardwareControl : QSerialPort::NoFlowControl);
    }
}

void ModemBridge::setLocalEcho(bool enable) {
    m_localEcho = enable;
}

void ModemBridge::setTcpMode(bool enableSsh) {
    Q_UNUSED(enableSsh);
}

void ModemBridge::start() {
    if (m_serial->open(QIODevice::ReadWrite)) {
        m_isActive = true;
        emit statusMessage(m_portName, "Modem Bridge: Serial port opened.");
        m_serial->setDataTerminalReady(true);
        m_serial->setRequestToSend(true);

        if (m_listenPort > 0) {
            setListenPort(m_listenPort);
        }
    } else {
        emit errorOccurred(m_portName, "Modem Bridge: Failed to open serial port.");
    }
}


void ModemBridge::stop() {
    if (m_serial->isOpen()) m_serial->close();

    // Close both types of outbound connections
    if (m_socket->state() != QAbstractSocket::UnconnectedState) m_socket->disconnectFromHost();
    if (m_ssh->isConnected()) m_ssh->disconnectFromHost();

    // [UPDATED] Cleanly shut down the inbound listener
    m_tcpServer->close();

    if (m_pendingSocket) {
        m_pendingSocket->disconnectFromHost();
        m_pendingSocket->deleteLater();
        m_pendingSocket = nullptr;
    }
    m_isActive = false;
    m_isConnected = false;
}


void ModemBridge::onSerialDataReceived() {
    QByteArray data = m_serial->readAll();
    if (!m_isActive) return;

    emit txActivity(m_portName);

    if (m_isConnected) {
        emit traceData(m_portName, m_isSshMode ? "TX (SSH)" : "TX (TCP)", data);

        for (char c : data) {
            if (c == 126 || c == 127) c = 8;

            // --- TIES: Pass-Through Logic ---
            if (c == '+') {
                m_escapeBuffer.append(c);

                // Pass straight to BBS for instant echo!
                if (m_isSshMode) m_ssh->write(QByteArray(1, c));
                else             m_socket->write(QByteArray(1, c));

                if (m_escapeBuffer.length() > 3) {
                    m_escapeBuffer.clear(); // Too many pluses
                } else if (m_escapeBuffer.length() == 3) {
                    if (m_escapeTimer) m_escapeTimer->start(1000);
                }
                continue;
            } else {
                m_escapeBuffer.clear(); // Broken sequence
                if (m_escapeTimer && m_escapeTimer->isActive()) m_escapeTimer->stop();
            }

            if (m_escPressed) {
                m_escPressed = false;
                if (c == 'u' || c == 'U') { injectMacro('u'); continue; }
                if (c == 'p' || c == 'P') { injectMacro('p'); continue; }
                if (c == 'h' || c == 'H') { hangup(); continue; }

                QByteArray esc(1, 0x1B);
                esc.append(c);
                if (m_isSshMode) m_ssh->write(esc);
                else             m_socket->write(esc);
            }
            else if (c == 0x1B) {
                m_escPressed = true;
            }
            else {
                if (m_isSshMode) m_ssh->write(QByteArray(1, c));
                else             m_socket->write(QByteArray(1, c));
            }
        }
    }
    else {
        emit traceData(m_portName, "TX (CMD)", data);

        for (char c : data) {
            if (c == '\r' || c == 155) {
                if (m_localEcho) {
                    sendToSerial("\r\n");
                }
                processAtCommand(m_serialBuffer);
                m_serialBuffer.clear();
            } else if (c != '\n') {
                if (c == 126 || c == 127 || c == 8) {
                    if (!m_serialBuffer.isEmpty()) {
                        m_serialBuffer.chop(1);
                        if (m_localEcho && !m_waitingForSshPassword) {
                            sendToSerial(QByteArray(1, 8));
                            sendToSerial(" ");
                            sendToSerial(QByteArray(1, 8));
                        }
                    }
                } else {
                    if (m_localEcho && !m_waitingForSshPassword) {
                        sendToSerial(QByteArray(1, c));
                    }
                    m_serialBuffer.append(c);
                }
            }
        }
    }
}

void ModemBridge::processAtCommand(const QByteArray &cmd) {

    if (cmd == "getdtm") {
        doServerDate();
        return;
    }
    if (cmd == "gety2k") {
        doServerY2KDate();
        return;
    }
    if (m_waitingForSshPassword) {
        m_waitingForSshPassword = false;
        m_currentConnection.password = QString::fromLatin1(cmd).trimmed();
        executeInteractiveSshDial();
        return;
    }

    QString upperCmd = QString::fromLatin1(cmd).trimmed().toUpper();
    if (upperCmd.startsWith("AT")) upperCmd = upperCmd.mid(2);

    if (upperCmd.startsWith("D")) {
        QString originalCmd = QString::fromLatin1(cmd).trimmed();
        int dIndex = originalCmd.toUpper().indexOf("D");
        QString target = originalCmd.mid(dIndex + 1).trimmed();

        if (target.toUpper().startsWith("T")) target = target.mid(1).trimmed();

        if (target.startsWith("SSH:", Qt::CaseInsensitive) && target.contains("@")) {
            parseInteractiveSshTarget(target);
            return;
        }

        QString host = target;
        int port = 23;
        bool found = false;

        for (const BbsEntry &entry : m_phonebook) {
            if (entry.name.compare(target, Qt::CaseInsensitive) == 0) {
                host = entry.ip;
                port = entry.port;
                m_currentConnection = entry;
                found = true;
                break;
            }
        }

        if (!found) {
            m_currentConnection = BbsEntry();

            if (host.startsWith("SSH:")) {
                m_currentConnection.protocol = "SSH";
                host = host.mid(4);
                port = 22;
            }

            QStringList parts = host.split(':');
            host = parts[0];
            if (parts.size() > 1) port = parts[1].toInt();

            m_currentConnection.ip = host;
            m_currentConnection.port = port;
        }

        connectTo(host, port);
    }

    // ANSWERING:
    else if (upperCmd == "A") {
        if (m_pendingSocket && m_pendingSocket->state() == QAbstractSocket::ConnectedState) {
            m_ringTimer->stop();

            m_socket->disconnect();
            m_socket->deleteLater();

            m_socket = m_pendingSocket;
            m_pendingSocket = nullptr;

            connect(m_socket, &QTcpSocket::readyRead, this, &ModemBridge::onSocketDataReceived);
            connect(m_socket, &QTcpSocket::disconnected, this, &ModemBridge::onSocketDisconnected);
            connect(m_socket, &QAbstractSocket::errorOccurred, this, &ModemBridge::onSocketError);

            m_isSshMode = false;
            m_isConnected = true;

            sendToSerial("\r\nCONNECT\r\n");
            emit statusMessage(m_portName, "Inbound Telnet Connected.");
            changeState("CONNECTED");
        } else {
            m_serial->write("\r\nNO CARRIER\r\n");
        }
    }

    else if (upperCmd == "O" || upperCmd.startsWith("O0")) {
        // First, check if the underlying connection actually exists!
        bool hasActiveConnection = (m_socket->state() == QAbstractSocket::ConnectedState) || m_ssh->isConnected();

        if (hasActiveConnection) {
            m_isConnected = true;
            m_serial->write("\r\nCONNECT\r\n"); // Standard Hayes response for going back online
            emit statusMessage(m_portName, "Returned to Online Data State.");
            changeState("CONNECTED");
        } else {
            // If they typed ATO but the BBS had already hung up in the background
            m_serial->write("\r\nNO CARRIER\r\n");
        }
    }


    // AUTO-ANSWER:
    else if (upperCmd.startsWith("S0=")) {
        bool ok;
        int rings = upperCmd.mid(3).toInt(&ok);
        if (ok) {
            m_autoAnswerRings = rings;
        }
        m_serial->write("\r\nOK\r\n");
    }

    else if (upperCmd.startsWith("H")) {
        hangup();
        m_serial->write("\r\nOK\r\n");
    }
    else if (upperCmd.startsWith("Z")) {
        m_suppressCarrierMessage = true;

        m_socket->abort();
        m_ssh->disconnectFromHost();

        m_isConnected = false;
        m_currentConnection = BbsEntry();
        m_escapeBuffer.clear();
        m_serial->write("\r\nOK\r\n");
        m_suppressCarrierMessage = false;
    }
    else {
        m_serial->write("\r\nOK\r\n");
    }
}


void ModemBridge::dial(const BbsEntry &entry) {
    // 1. Sanitize the entry. The AT parser usually does this automatically,
    // but the XML Phonebook might contain invisible trailing spaces!
    BbsEntry safeEntry = entry;
    safeEntry.ip = safeEntry.ip.trimmed();
    safeEntry.protocol = safeEntry.protocol.trimmed().toUpper();
    safeEntry.login = safeEntry.login.trimmed();

    // 2. Set the global context so connectTo knows exactly what to do
    m_currentConnection = safeEntry;

    // 3. Temporarily suppress NO CARRIER. If we are interrupting an existing
    // or half-open connection, aborting it will fire a disconnected signal!
    m_suppressCarrierMessage = true;

    if (m_socket->state() != QAbstractSocket::UnconnectedState) m_socket->abort();
    if (m_ssh->isConnected()) m_ssh->disconnectFromHost();

    m_suppressCarrierMessage = false;

    // 4. Execute the dial!
    connectTo(m_currentConnection.ip, m_currentConnection.port);
}



void ModemBridge::dial(const QString &target) {
    processAtCommand(("ATDT " + target).toUtf8());
}

void ModemBridge::connectTo(const QString &host, int port) {
    // 1. Sanitize EVERYTHING. This strips hidden XML spaces that break DNS lookups!
    QString cleanHost = host.trimmed();
    QString proto = m_currentConnection.protocol.trimmed().toUpper();
    QString safeUser = m_currentConnection.login.trimmed();
    if (safeUser.isEmpty()) safeUser = "guest";
    QString cleanPass = m_currentConnection.password.trimmed();

    bool useSsh = proto.startsWith("SSH") || (port == 22);

    // 2. Unify the SSH paths! If we need a password but don't have one,
    // trigger the interactive terminal prompt just like ATDT ssh: does!
    if (useSsh && proto == "SSH-AUTH" && cleanPass.isEmpty()) {
        m_currentConnection.ip = cleanHost;
        m_currentConnection.port = port;
        m_currentConnection.login = safeUser;
        m_waitingForSshPassword = true;
        sendToSerial("\r\nPASSWORD: ");
        return;
    }

    changeState("DIALING...");

    if (m_socket->state() != QAbstractSocket::UnconnectedState) m_socket->abort();
    if (m_ssh->isConnected()) m_ssh->disconnectFromHost();
    m_isConnected = false;

    // Send status to the PC UI and the Atari Terminal
    emit statusMessage(m_portName, QString("Dialing %1:%2...").arg(cleanHost).arg(port));
    m_serial->write("\r\nDIALING...\r\n");

    if (useSsh) {
        m_isSshMode = true;
        // 3. Never throw away the password! If it exists, pass it.
        m_ssh->connectToHost(cleanHost, port, safeUser, cleanPass);
    } else {
        m_isSshMode = false;
        m_socket->connectToHost(cleanHost, port);
    }
}


void ModemBridge::onSocketConnected() {
    m_isConnected = true;
    sendToSerial("CONNECT \r\n");
    emit statusMessage(m_portName, "Telnet Connected.");
    changeState("CONNECTED");
}

void ModemBridge::onSocketDataReceived() {
    QByteArray data = m_socket->readAll();
    emit traceData(m_portName, "RX (TCP)", data);
    parseTelnet(data);
}

void ModemBridge::parseTelnet(const QByteArray &data) {
    QByteArray cleanData;
    for (char c : data) {
        unsigned char byte = (unsigned char)c;
        switch (m_telnetState) {
        case TelnetState::Normal:
            if (byte == 0xFF) m_telnetState = TelnetState::IacReceived;
            else cleanData.append(c);
            break;
        case TelnetState::IacReceived:
            switch (byte) {
            case 0xFF: cleanData.append((char)0xFF); m_telnetState = TelnetState::Normal; break;
            case 0xFB: m_telnetState = TelnetState::Will; break;
            case 0xFC: m_telnetState = TelnetState::Wont; break;
            case 0xFD: m_telnetState = TelnetState::Do; break;
            case 0xFE: m_telnetState = TelnetState::Dont; break;
            case 0xFA: m_telnetState = TelnetState::SubNegotiation; break;
            default:   m_telnetState = TelnetState::Normal; break;
            }
            break;
        case TelnetState::Will:
        case TelnetState::Wont:
        case TelnetState::Do:
        case TelnetState::Dont:
            if (m_telnetState == TelnetState::Will || m_telnetState == TelnetState::Do) {
                QByteArray reject;
                reject.append((char)0xFF);
                reject.append(m_telnetState == TelnetState::Will ? (char)0xFE : (char)0xFC);
                reject.append((char)byte);
                if (m_socket->state() == QAbstractSocket::ConnectedState) m_socket->write(reject);
            }
            m_telnetState = TelnetState::Normal;
            break;
        case TelnetState::SubNegotiation:
            if (byte == 0xFF) m_telnetState = TelnetState::SubIac;
            break;
        case TelnetState::SubIac:
            if (byte == 0xF0) m_telnetState = TelnetState::Normal;
            else if (byte != 0xFF) m_telnetState = TelnetState::SubNegotiation;
            break;
        }
    }
    if (!cleanData.isEmpty()) sendToSerial(cleanData);
}

void ModemBridge::onSocketDisconnected() {
    if (m_isSshMode) return;
    m_isConnected = false;
    if (!m_suppressCarrierMessage) sendToSerial("\r\nNO CARRIER\r\n");
    emit statusMessage(m_portName, "Disconnected.");

    // ADD THIS LINE to sync the Web UI state!
    changeState("READY");
}


void ModemBridge::onSocketError(QAbstractSocket::SocketError socketError) {
    Q_UNUSED(socketError);
    if (m_isSshMode) return;

    // Grab the human-readable error from Qt (e.g., "Host not found", "Connection refused")
    QString errorMsg = m_socket->errorString();
    emit errorOccurred(m_portName, errorMsg); // Send to PC UI

    if (!m_isConnected) {
        // Send the verbose error to the Atari terminal before dropping carrier
        sendToSerial(("\r\nERROR: " + errorMsg + "\r\n").toUtf8());
        sendToSerial("\r\nNO CARRIER\r\n");
    }
}



void ModemBridge::onSshConnected() {
    m_isConnected = true;
    sendToSerial("CONNECT 57600\r\n");
    emit statusMessage(m_portName, "SSH Connected (Secure).");
    changeState("CONNECTED");
}

void ModemBridge::onSshDataReceived(const QByteArray &data) {
    emit traceData(m_portName, "RX (SSH)", data);
    sendToSerial(data);
}

void ModemBridge::onSshDisconnected() {
    if (!m_isSshMode) return;
    m_isConnected = false;
    if (!m_suppressCarrierMessage) sendToSerial("\r\nNO CARRIER\r\n");
    emit statusMessage(m_portName, "SSH Disconnected.");
    changeState("READY");
}

void ModemBridge::onSshError(const QString &msg) {
    if (m_isSshMode) {
        emit errorOccurred(m_portName, "SSH Error: " + msg); // Send to PC UI

        if (!m_isConnected) {
            // Send the specific SSH error (e.g., "Authentication failed") to the Atari
            sendToSerial(("\r\nERROR: SSH - " + msg + "\r\n").toUtf8());
            sendToSerial("\r\nNO CARRIER\r\n");
        }
    }
}

void ModemBridge::hangup() {
    emit statusMessage(m_portName, "Hangup initiated.");

    // Clear any active buffers so AT command mode is completely fresh
    m_escapeBuffer.clear();
    m_waitingForSshPassword = false;
    m_isConnected = false;

    // Gracefully drop the network connection
    if (m_isSshMode) {
        m_ssh->disconnectFromHost();
    } else {
        m_socket->disconnectFromHost();
    }

    changeState("READY");
    // Note: The async disconnected() signals will fire shortly after this
    // and automatically print "\r\nNO CARRIER\r\n" back to the Atari.
}




void ModemBridge::injectMacro(char macroType) {
    if (!m_isConnected) {
        emit errorOccurred(m_portName, "Cannot send macro - Not Connected.");
        return;
    }

    QString textToSend;
    if (macroType == 'U' || macroType == 'u') textToSend = m_currentConnection.login;
    else if (macroType == 'P' || macroType == 'p') textToSend = m_currentConnection.password;

    if (!textToSend.isEmpty()) {
        QByteArray bytes = textToSend.toUtf8() + "\r";

        emit traceData(m_portName, m_isSshMode ? "TX (MACRO)" : "TX (MACRO)", bytes);
        emit txActivity(m_portName);

        if (m_isSshMode) m_ssh->write(bytes);
        else             m_socket->write(bytes);

        emit statusMessage(m_portName, QString("Sent Macro %1").arg(macroType));
    }
}

void ModemBridge::sendToSerial(const QByteArray &data) {
    if (m_serial->isOpen()) {
        m_serial->write(data);
        emit rxActivity(m_portName);

        if (!m_isConnected) {
            emit traceData(m_portName, "RX (CMD)", data);
        }
    }
}

void ModemBridge::checkEscapeSequence() {
    if (m_escapeBuffer == "+++") {
        m_isConnected = false;
        emit statusMessage(m_portName, "+++ Escape Sequence triggered. Dropping to Command Mode.");
        m_serial->write("\r\nOK\r\n");
        changeState("READY"); // Sync the Web UI state
    }
    // No flushing needed, just clear the tracker
    m_escapeBuffer.clear();
}



void ModemBridge::setPhonebookPath(const QString &path) {
    if (!path.isEmpty()) loadPhonebook(path);
}

void ModemBridge::loadPhonebook(const QString &path) {
    m_phonebook.clear();
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    QDomDocument doc;
    if (!doc.setContent(&file)) { file.close(); return; }
    file.close();

    QDomNodeList list = doc.elementsByTagName("BBS");
    for (int i = 0; i < list.size(); i++) {
        QDomElement e = list.at(i).toElement();
        BbsEntry bbs;
        bbs.name = e.attribute("name");
        bbs.ip = e.attribute("ip");
        bbs.port = e.attribute("port").toInt();
        bbs.protocol = e.attribute("protocol");
        bbs.login = e.attribute("login");
        bbs.password = e.attribute("password");
        m_phonebook.append(bbs);
    }
    emit statusMessage(m_portName, QString("Loaded %1 phonebook entries.").arg(m_phonebook.size()));
}

BbsEntry ModemBridge::findBbsByName(const QString &name) {
    for (const BbsEntry &entry : m_phonebook) {
        if (entry.name.compare(name, Qt::CaseInsensitive) == 0) return entry;
    }
    return BbsEntry();
}

void ModemBridge::parseInteractiveSshTarget(const QString &target) {
    QString connectionStr = target.mid(4);
    int atIndex = connectionStr.indexOf('@');
    QString user = connectionStr.left(atIndex);
    QString hostPort = connectionStr.mid(atIndex + 1);

    QString host = hostPort;
    int port = 22;
    if (hostPort.contains(":")) {
        QStringList parts = hostPort.split(":");
        host = parts[0];
        port = parts[1].toInt();
    }

    m_currentConnection = BbsEntry();
    m_currentConnection.protocol = "SSH-AUTH";
    m_currentConnection.login = user;
    m_currentConnection.ip = host;
    m_currentConnection.port = port;
    m_currentConnection.name = host;

    m_waitingForSshPassword = true;
    sendToSerial("\r\nPASSWORD: ");
}

void ModemBridge::executeInteractiveSshDial() {
    changeState("DIALING...");

    m_isSshMode = true;
    m_isConnected = false;

    if (m_socket->state() != QAbstractSocket::UnconnectedState) m_socket->abort();
    if (m_ssh->isConnected()) m_ssh->disconnectFromHost();

    emit statusMessage(m_portName, QString("Dialing %1:%2...").arg(m_currentConnection.ip).arg(m_currentConnection.port));
    m_serial->write("\r\nDIALING...\r\n");

    m_ssh->connectToHost(m_currentConnection.ip, m_currentConnection.port, m_currentConnection.login, m_currentConnection.password);
}

void ModemBridge::changeState(const QString &newState) {
    if (m_currentState != newState) {
        m_currentState = newState;
        emit connectionStateChanged(m_portName, newState);
    }
}

void ModemBridge::onSerialError(QSerialPort::SerialPortError error) {
    // If the error is a critical hardware disconnect (e.g., USB unplugged)
    if (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError) {

        // 1. Grab the OS-level error message before we close the port
        QString errMsg = QString("HARDWARE DISCONNECTED: %1").arg(m_serial->errorString());

        // 2. Shut down the dead serial handle immediately so we don't segfault
        m_serial->close();
        m_isActive = false;

        // 3. Drop the active phone call if one is happening
        m_suppressCarrierMessage = true; // Don't try to send NO CARRIER to a dead port!
        if (m_socket->state() != QAbstractSocket::UnconnectedState) m_socket->abort();
        if (m_ssh->isConnected()) m_ssh->disconnectFromHost();
        m_isConnected = false;
        m_suppressCarrierMessage = false;

        // 4. Alert the C++ Dashboard and the Web UI
        emit errorOccurred(m_portName, errMsg);

        // 5. Change the Web UI badge to a new FAULT state
        changeState("FAULT");
    }
}


void ModemBridge::setListenPort(int port) {
    m_listenPort = port;

    // If the port is valid and the bridge is active, start listening
    if (m_listenPort > 0 && m_isActive) {

        // If already listening on the correct port, do nothing
        if (m_tcpServer->isListening() && m_tcpServer->serverPort() == m_listenPort) {
            return;
        }

        // Restart the server on the new port
        m_tcpServer->close();
        if (m_tcpServer->listen(QHostAddress::Any, m_listenPort)) {
            emit statusMessage(m_portName, QString("Modem Bridge: Listening for inbound callers on port %1").arg(m_listenPort));
        } else {
            emit errorOccurred(m_portName, QString("Modem Bridge: Failed to start listener on port %1").arg(m_listenPort));
        }
    }
    // If port is 0, or the bridge is inactive, shut down the listener
    else {
        if (m_tcpServer->isListening()) {
            m_tcpServer->close();
            emit statusMessage(m_portName, "Modem Bridge: BBS listener stopped.");
        }
    }
}



void ModemBridge::onNewConnection() {
    QTcpSocket *client = m_tcpServer->nextPendingConnection();

    // --- BULLETPROOF SMART CALL REJECTION ---
    // Instead of just relying on the m_isConnected boolean, we check the actual
    // hardware/socket states to see if the modem is tied up.
    bool isBusy = m_isConnected ||
                  m_pendingSocket != nullptr ||
                  m_currentState == "DIALING..." ||
                  m_waitingForSshPassword ||
                  m_socket->state() != QAbstractSocket::UnconnectedState ||
                  m_ssh->isConnected();

    if (isBusy) {
        client->write("\r\nBUSY\r\n");
        // Flush ensures the BUSY text actually leaves the PC before we drop the line
        client->flush();
        client->disconnectFromHost();
        client->deleteLater();
        return;
    }

    // If the line is truly free, accept the call and start ringing
    m_pendingSocket = client;
    m_ringCount = 0;

    onRingTimeout();
    m_ringTimer->start(3000);
    changeState("RINGING");
}



void ModemBridge::onRingTimeout() {
    // If the caller hung up before we answered
    if (!m_pendingSocket || m_pendingSocket->state() != QAbstractSocket::ConnectedState) {
        m_ringTimer->stop();
        if (m_pendingSocket) m_pendingSocket->deleteLater();
        m_pendingSocket = nullptr;
        changeState("READY");
        return;
    }

    m_ringCount++;
    sendToSerial("\r\nRING\r\n");

    // Check for ATS0 auto-answer
    if (m_autoAnswerRings > 0 && m_ringCount >= m_autoAnswerRings) {
        processAtCommand("ATA");
    }
}
