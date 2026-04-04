#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QSettings>
#include <QString>
#include <QList>

struct BridgeConfig {
    QString portName;
    QString friendlyName;
    int baudRate = 9600;
    bool flowControl = true;
    bool localEcho = false;
    bool sshEnabled = false;
    QString phonebookPath;

    bool isValid() const { return !portName.isEmpty(); }
};

class AppSettings {
public:
    static AppSettings& instance() {
        static AppSettings instance;
        return instance;
    }

    QList<BridgeConfig> loadBridges();
    void saveBridges(const QList<BridgeConfig>& bridges);

private:
    AppSettings();
    QSettings m_settings;
};

#endif // APPSETTINGS_H
