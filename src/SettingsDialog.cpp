#include "SettingsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSerialPortInfo>
#include <QInputDialog>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QMessageBox>

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("ModemBridge Fleet Settings");
    resize(800, 400);
    setMinimumWidth(700);

    setupUi();

    // 1. Load saved configs into our temporary cache
    QList<BridgeConfig> savedBridges = AppSettings::instance().loadBridges();
    for (const auto& config : savedBridges) {
        m_configCache.insert(config.portName, config);
    }

    // 2. Scan hardware and populate the list
    loadAvailablePorts();
}

void SettingsDialog::setupUi() {
    // FIX: Do not pass 'this' to mainLayout yet!
    QHBoxLayout *mainLayout = new QHBoxLayout();

    // --- LEFT PANEL: The List ---
    QVBoxLayout *leftLayout = new QVBoxLayout();
    m_listPorts = new QListWidget(this);
    connect(m_listPorts, &QListWidget::currentItemChanged, this, &SettingsDialog::onPortSelectionChanged);

    QHBoxLayout *listButtonsLayout = new QHBoxLayout();
    m_btnAddCustom = new QPushButton("Add Custom...", this);
    m_btnRefresh = new QPushButton("Refresh OS", this);

    connect(m_btnAddCustom, &QPushButton::clicked, this, &SettingsDialog::onAddCustomPort);
    connect(m_btnRefresh, &QPushButton::clicked, this, &SettingsDialog::loadAvailablePorts);

    listButtonsLayout->addWidget(m_btnAddCustom);
    listButtonsLayout->addWidget(m_btnRefresh);

    leftLayout->addWidget(m_listPorts);
    leftLayout->addLayout(listButtonsLayout);

    // --- RIGHT PANEL: The Form ---
    QVBoxLayout *rightLayout = new QVBoxLayout();
    QGroupBox *groupConfig = new QGroupBox("Port Configuration", this);
    QFormLayout *formLayout = new QFormLayout(groupConfig);

    m_txtFriendlyName = new QLineEdit(this);
    m_txtFriendlyName->setPlaceholderText("e.g. Atari 800, Pi Node 1...");

    m_chkEnable = new QCheckBox("Enable this Bridge", this);
    m_cmbBaudRate = new QComboBox(this);
    m_cmbBaudRate->addItems({"1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"});

    m_chkFlowControl = new QCheckBox("Hardware Flow Control (RTS/CTS)", this);
    m_chkLocalEcho = new QCheckBox("Local Echo (Half Duplex)", this);
    m_chkSsh = new QCheckBox("SSH Support (Requires libssh)", this);

    QHBoxLayout *phonebookLayout = new QHBoxLayout();

    m_txtPhonebook = new QLineEdit(this);
    m_btnBrowsePhonebook = new QPushButton("Browse...", this);
    m_btnNewPhonebook = new QPushButton("New...", this);
    connect(m_btnBrowsePhonebook, &QPushButton::clicked, this, &SettingsDialog::onBrowsePhonebook);
    connect(m_btnNewPhonebook, &QPushButton::clicked, this, &SettingsDialog::onNewPhonebook);

    phonebookLayout->addWidget(m_txtPhonebook);
    phonebookLayout->addWidget(m_btnBrowsePhonebook);
    phonebookLayout->addWidget(m_btnNewPhonebook);

    formLayout->insertRow(0, "Friendly Name:", m_txtFriendlyName);
    formLayout->addRow(m_chkEnable);
    formLayout->addRow("Baud Rate:", m_cmbBaudRate);
    formLayout->addRow(m_chkFlowControl);
    formLayout->addRow(m_chkLocalEcho);
    formLayout->addRow(m_chkSsh);
    formLayout->addRow("Phonebook XML:", phonebookLayout);

    rightLayout->addWidget(groupConfig);
    rightLayout->addStretch(); // Pushes form to the top

    // --- BOTTOM: Save/Cancel ---
    QDialogButtonBox *btnBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(btnBox, &QDialogButtonBox::accepted, this, &SettingsDialog::onSaveAndClose);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // --- ASSEMBLY ---
    mainLayout->addLayout(leftLayout, 1);
    mainLayout->addLayout(rightLayout, 2);

    // Now we create the master vertical layout, stack them, and apply to 'this'
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->addLayout(mainLayout);
    outerLayout->addWidget(btnBox);

    setLayout(outerLayout);
}


void SettingsDialog::loadAvailablePorts() {
    saveCurrentFormToCache(); // Don't lose edits if they click refresh!
    m_listPorts->clear();

    // 1. Get OS Ports
    QStringList systemPorts;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        systemPorts.append(info.portName());
    }

    // 2. Merge OS ports with saved custom ports from our cache
    QStringList allPorts = systemPorts;
    for (auto it = m_configCache.constBegin(); it != m_configCache.constEnd(); ++it) {
        if (!allPorts.contains(it.key())) {
            allPorts.append(it.key());
        }
    }

    // 3. Populate List
    for (const QString &portName : allPorts) {
        QListWidgetItem *item = new QListWidgetItem(portName, m_listPorts);

        // Add a checkbox to the list item so we can quickly see what's active
        item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);

        // If it's in our cache and enabled, check it. Otherwise, uncheck.
        bool isEnabled = m_configCache.contains(portName) && m_configCache[portName].isValid();
        item->setCheckState(isEnabled ? Qt::Checked : Qt::Unchecked);
    }

    if (m_listPorts->count() > 0) m_listPorts->setCurrentRow(0);
}

void SettingsDialog::onAddCustomPort() {
    bool ok;
    QString text = QInputDialog::getText(this, "Add Custom Port",
                                         "Enter Port Name (e.g., /dev/ttyV0):",
                                         QLineEdit::Normal, "", &ok);
    if (ok && !text.isEmpty()) {
        QListWidgetItem *item = new QListWidgetItem(text, m_listPorts);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        m_listPorts->setCurrentItem(item);
    }
}

void SettingsDialog::onPortSelectionChanged(QListWidgetItem *current, QListWidgetItem *previous) {
    if (previous) {
        saveCurrentFormToCache(); // Save the old one before switching
    }

    if (current) {
        m_currentEditingPort = current->text();

        // Update checkbox based on list selection
        m_chkEnable->blockSignals(true);
        m_chkEnable->setChecked(current->checkState() == Qt::Checked);
        m_chkEnable->blockSignals(false);

        // Load config from cache, or create defaults if it's brand new
        BridgeConfig config;
        if (m_configCache.contains(m_currentEditingPort)) {
            config = m_configCache[m_currentEditingPort];
        } else {
            config.portName = m_currentEditingPort; // Defaults applied by struct
        }

        updateFormFromConfig(config);
    }
}

void SettingsDialog::updateFormFromConfig(const BridgeConfig &config) {
    m_txtFriendlyName->setText(config.friendlyName);
    m_cmbBaudRate->setCurrentText(QString::number(config.baudRate));
    m_chkFlowControl->setChecked(config.flowControl);
    m_chkLocalEcho->setChecked(config.localEcho);
    m_chkSsh->setChecked(config.sshEnabled);
    m_txtPhonebook->setText(config.phonebookPath);
}

void SettingsDialog::saveCurrentFormToCache() {
    if (m_currentEditingPort.isEmpty()) return;

    // Find the item in the list to check if they enabled/disabled it via the form checkbox
    for (int i = 0; i < m_listPorts->count(); ++i) {
        QListWidgetItem *item = m_listPorts->item(i);
        if (item->text() == m_currentEditingPort) {
            item->setCheckState(m_chkEnable->isChecked() ? Qt::Checked : Qt::Unchecked);
            break;
        }
    }

    BridgeConfig config;
    config.friendlyName = m_txtFriendlyName->text();
    config.portName = m_currentEditingPort;
    config.baudRate = m_cmbBaudRate->currentText().toInt();
    config.flowControl = m_chkFlowControl->isChecked();
    config.localEcho = m_chkLocalEcho->isChecked();
    config.sshEnabled = m_chkSsh->isChecked();
    config.phonebookPath = m_txtPhonebook->text();

    // If it's checked, we definitely save it to the cache.
    // If it's unchecked, we can remove it from the cache so it doesn't get saved to AppSettings.
    if (m_chkEnable->isChecked()) {
        m_configCache.insert(m_currentEditingPort, config);
    } else {
        m_configCache.remove(m_currentEditingPort);
    }
}

void SettingsDialog::onBrowsePhonebook() {
    QString fileName = QFileDialog::getOpenFileName(this, "Select Phonebook XML", "", "XML Files (*.xml);;All Files (*)");
    if (!fileName.isEmpty()) {
        m_txtPhonebook->setText(fileName);
    }
}

void SettingsDialog::onSaveAndClose() {
    saveCurrentFormToCache(); // Catch whatever they are looking at right now

    // Extract all valid, enabled configurations from our cache
    QList<BridgeConfig> finalConfigs = m_configCache.values();

    // Save to Registry/Plist
    AppSettings::instance().saveBridges(finalConfigs);

    accept(); // Closes the dialog
}

void SettingsDialog::onNewPhonebook() {
#ifdef Q_OS_MAC
    QString defaultPath = QDir::homePath();
#else
    QString defaultPath = QDir::homePath() + "/phonebook.xml";
#endif

    QString fileName = QFileDialog::getSaveFileName(this, "Create New Phonebook XML", defaultPath, "XML Files (*.xml);;All Files (*)");

    if (!fileName.isEmpty()) {
        if (!fileName.endsWith(".xml", Qt::CaseInsensitive)) {
            fileName += ".xml";
        }

        // Write a minimal valid XML structure so the PhoneDirectory parser doesn't choke later
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << "<EtherTerm>\n  <Phonebook/>\n</EtherTerm>\n";
            file.close();
        }

        // Update the text box
        m_txtPhonebook->setText(fileName);
    }
}
