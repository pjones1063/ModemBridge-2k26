#include "modembridge.h"
#include <QDebug>
#include <QFile>
#include <QDomDocument>

ModemBridge::ModemBridge(QObject *parent) : QObject(parent),
    m_serial(new QSerialPort(this)),
    m_socket(new QTcpSocket(this)),
    m_ssh(new SshClient(this)),
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
        emit statusMessage(m_portName, "Serial port opened.");
        m_serial->setDataTerminalReady(true);
        m_serial->setRequestToSend(true);
    } else {
        emit errorOccurred(m_portName, "Failed to open serial port.");
    }
}

void ModemBridge::stop() {
    if (m_serial->isOpen()) m_serial->close();

    if (m_socket->state() != QAbstractSocket::UnconnectedState) m_socket->disconnectFromHost();
    if (m_ssh->isConnected()) m_ssh->disconnectFromHost();

    m_isActive = false;
    m_isConnected = false;
    emit statusMessage(m_portName, "Bridge stopped.");
}

void ModemBridge::onSerialDataReceived() {
    QByteArray data = m_serial->readAll();
    if (!m_isActive) return;

    emit txActivity(m_portName);

    if (m_isConnected) {
        emit traceData(m_portName, m_isSshMode ? "TX (SSH)" : "TX (TCP)", data);

        for (char c : data) {
            if (c == 126 || c == 127) c = 8;

            if (c == '+') {
                m_escapeBuffer.append(c);
                if (m_escapeBuffer.length() > 3) {
                    if (m_isSshMode) m_ssh->write(m_escapeBuffer);
                    else             m_socket->write(m_escapeBuffer);
                    m_escapeBuffer.clear();
                } else if (m_escapeBuffer.length() == 3) {
                    if (m_escapeTimer) m_escapeTimer->start(1000);
                }
                continue;
            } else {
                if (!m_escapeBuffer.isEmpty()) {
                    if (m_isSshMode) m_ssh->write(m_escapeBuffer);
                    else             m_socket->write(m_escapeBuffer);
                    m_escapeBuffer.clear();
                }
                if (m_escapeTimer) m_escapeTimer->stop();
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
    m_currentConnection = entry;

    QString proto = entry.protocol.toUpper();
    m_isSshMode = proto.startsWith("SSH");

    if (m_isSshMode) {
        QString safeUser = entry.login.isEmpty() ? "guest" : entry.login;

        if (proto == "SSH-AUTH") {
            emit statusMessage(m_portName, "Dialing " + entry.name + " via Authenticated SSH...");
            m_ssh->connectToHost(entry.ip, entry.port, safeUser, entry.password);
        } else {
            emit statusMessage(m_portName, "Dialing " + entry.name + " via Anonymous BBS SSH...");
            m_ssh->connectToHost(entry.ip, entry.port, safeUser, "");
        }
    } else {
        emit statusMessage(m_portName, "Dialing " + entry.name + " via Telnet/TCP...");
        connectTo(entry.ip, entry.port);
    }
}

void ModemBridge::dial(const QString &target) {
    processAtCommand(("ATDT " + target).toUtf8());
}

void ModemBridge::connectTo(const QString &host, int port) {
    if (m_socket->state() != QAbstractSocket::UnconnectedState) m_socket->abort();
    if (m_ssh->isConnected()) m_ssh->disconnectFromHost();
    m_isConnected = false;

    emit statusMessage(m_portName, QString("Dialing %1:%2...").arg(host).arg(port));
    m_serial->write("\r\nDIALING...\r\n");

    QString proto = m_currentConnection.protocol.toUpper();
    bool useSsh = proto.startsWith("SSH") || (port == 22);

    if (useSsh) {
        m_isSshMode = true;
        QString safeUser = m_currentConnection.login.isEmpty() ? "guest" : m_currentConnection.login;

        if (proto == "SSH-AUTH") {
            m_ssh->connectToHost(host, port, safeUser, m_currentConnection.password);
        } else {
            m_ssh->connectToHost(host, port, safeUser, "");
        }
    } else {
        m_isSshMode = false;
        m_socket->connectToHost(host, port);
    }
}

void ModemBridge::onSocketConnected() {
    m_isConnected = true;
    sendToSerial("CONNECT 57600\r\n");
    emit statusMessage(m_portName, "Telnet Connected.");
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
}

void ModemBridge::onSocketError(QAbstractSocket::SocketError socketError) {
    Q_UNUSED(socketError);
    if (m_isSshMode) return;
    emit errorOccurred(m_portName, m_socket->errorString());
    if (!m_isConnected) sendToSerial("\r\nNO CARRIER\r\n");
}

void ModemBridge::onSshConnected() {
    m_isConnected = true;
    sendToSerial("CONNECT 57600\r\n");
    emit statusMessage(m_portName, "SSH Connected (Secure).");
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
}

void ModemBridge::onSshError(const QString &msg) {
    if (m_isSshMode) {
        emit errorOccurred(m_portName, "SSH Error: " + msg);
        if (!m_isConnected) sendToSerial("\r\nNO CARRIER\r\n");
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
        m_escapeBuffer.clear();
        m_isConnected = false;
        m_serial->write("\r\nOK\r\n");
    } else {
        if (!m_escapeBuffer.isEmpty()) {
            if (m_isSshMode) m_ssh->write(m_escapeBuffer);
            else             m_socket->write(m_escapeBuffer);
        }
        m_escapeBuffer.clear();
    }
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
    m_isSshMode = true;
    m_isConnected = false;

    if (m_socket->state() != QAbstractSocket::UnconnectedState) m_socket->abort();
    if (m_ssh->isConnected()) m_ssh->disconnectFromHost();

    emit statusMessage(m_portName, QString("Dialing %1:%2...").arg(m_currentConnection.ip).arg(m_currentConnection.port));
    m_serial->write("\r\nDIALING...\r\n");

    m_ssh->connectToHost(m_currentConnection.ip, m_currentConnection.port, m_currentConnection.login, m_currentConnection.password);
}
