#ifndef WEBBRIDGE_H
#define WEBBRIDGE_H

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>

class WebBridge : public QObject {
    Q_OBJECT
public:
    explicit WebBridge(QObject *parent = nullptr);

public slots:
    // UI actions now require a target port
    void injectUser(const QString &portName);
    void injectPass(const QString &portName);
    void hangup(const QString &portName);
    void dialBbs(const QString &portName, const QString &name, const QString &ip, int port, const QString &protocol, const QString &login, const QString &password);
    void requestPortState(const QString &portName);
    // Data requests from the web UI
    void requestPhonebook();
    void requestActivePorts();


signals:
    // Internal signals routed to TrayManager
    void sigInjectUser(const QString &portName);
    void sigInjectPass(const QString &portName);
    void sigHangup(const QString &portName);
    void sigDialBbs(const QString &portName, const QString &name, const QString &ip, int port, const QString &protocol, const QString &login, const QString &password);
    void sigRequestPhonebook();
    void sigRequestActivePorts();
    void sigRequestPortState(const QString &portName);
    void portStateReceived(const QString &portName, const QString &state);

    // Outbound signals sent TO the JavaScript
    void logTraceReceived(const QString &portName, const QString &htmlData);
    void phonebookDataReceived(const QJsonArray &entries);
    void activePortsReceived(const QJsonArray &ports);
};

#endif // WEBBRIDGE_H
