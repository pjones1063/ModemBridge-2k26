#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QMap>
#include "AppSettings.h"

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private slots:
    void loadAvailablePorts();
    void onPortSelectionChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void onAddCustomPort();
    void onBrowsePhonebook();
    void saveCurrentFormToCache();
    void onSaveAndClose();

private:
    void setupUi();
    void updateFormFromConfig(const BridgeConfig &config);

    // --- UI Elements ---
    QListWidget *m_listPorts;
    QPushButton *m_btnAddCustom;
    QPushButton *m_btnRefresh;

    QCheckBox *m_chkEnable;
    QComboBox *m_cmbBaudRate;
    QCheckBox *m_chkFlowControl;
    QCheckBox *m_chkLocalEcho;
    QCheckBox *m_chkSsh;
    QLineEdit *m_txtPhonebook;
    QPushButton *m_btnBrowsePhonebook;
    QLineEdit *m_txtFriendlyName;

    // --- Data Storage ---
    // We keep a temporary map of configs while the dialog is open.
    // We only commit them to AppSettings when the user clicks "Save".
    QMap<QString, BridgeConfig> m_configCache;
    QString m_currentEditingPort;
};

#endif // SETTINGSDIALOG_H
