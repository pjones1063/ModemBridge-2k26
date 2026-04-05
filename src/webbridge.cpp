#include "webbridge.h"

WebBridge::WebBridge(QObject *parent) : QObject(parent) {}

void WebBridge::injectUser(const QString &portName) { emit sigInjectUser(portName); }
void WebBridge::injectPass(const QString &portName) { emit sigInjectPass(portName); }
void WebBridge::hangup(const QString &portName) { emit sigHangup(portName); }
void WebBridge::requestPhonebook() { emit sigRequestPhonebook(); }
void WebBridge::requestActivePorts() { emit sigRequestActivePorts(); }
void WebBridge::requestPortState(const QString &portName) { emit sigRequestPortState(portName); }
void WebBridge::dialBbs(const QString &portName, const QString &name, const QString &ip, int port, const QString &protocol, const QString &login, const QString &password) {
    emit sigDialBbs(portName, name, ip, port, protocol, login, password);
}
