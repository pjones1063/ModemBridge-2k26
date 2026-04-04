#include "AppSettings.h"

AppSettings::AppSettings() : m_settings("PaulJones", "ModemBridgeTray") {}

QList<BridgeConfig> AppSettings::loadBridges() {
    QList<BridgeConfig> bridges;
    int size = m_settings.beginReadArray("Bridges");
    for (int i = 0; i < size; ++i) {
        m_settings.setArrayIndex(i);
        BridgeConfig config;
        config.portName = m_settings.value("PortName", "").toString();
        config.friendlyName = m_settings.value("FriendlyName", "").toString(); // [NEW]
        config.baudRate = m_settings.value("BaudRate", 9600).toInt();
        config.flowControl = m_settings.value("FlowControl", true).toBool();
        config.localEcho = m_settings.value("LocalEcho", false).toBool();
        config.sshEnabled = m_settings.value("SshEnabled", false).toBool();
        config.phonebookPath = m_settings.value("PhonebookPath", "").toString();

        if (config.isValid()) bridges.append(config);
    }
    m_settings.endArray();
    return bridges;
}

void AppSettings::saveBridges(const QList<BridgeConfig>& bridges) {
    m_settings.beginWriteArray("Bridges");
    for (int i = 0; i < bridges.size(); ++i) {
        m_settings.setArrayIndex(i);
        const BridgeConfig& config = bridges.at(i);
        m_settings.setValue("PortName", config.portName);
        m_settings.setValue("FriendlyName", config.friendlyName); // [NEW]
        m_settings.setValue("BaudRate", config.baudRate);
        m_settings.setValue("FlowControl", config.flowControl);
        m_settings.setValue("LocalEcho", config.localEcho);
        m_settings.setValue("SshEnabled", config.sshEnabled);
        m_settings.setValue("PhonebookPath", config.phonebookPath);
    }
    m_settings.endArray();
}
