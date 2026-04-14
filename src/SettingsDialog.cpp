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


    m_spinListenPort = new QSpinBox(this);
    m_spinListenPort->setRange(0, 65535); // 0 disables the listener
    m_spinListenPort->setSpecialValueText("0 (Disabled)");


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
    formLayout->addRow("Inbound BBS Port:", m_spinListenPort);
    formLayout->addRow(m_chkFlowControl);
    formLayout->addRow(m_chkLocalEcho);
    formLayout->addRow("Phonebook XML:", phonebookLayout);

    rightLayout->addWidget(groupConfig);

    QGroupBox *groupGlobal = new QGroupBox("Global Dashboard Settings", this);
    QFormLayout *globalLayout = new QFormLayout(groupGlobal);

    m_spinHttpPort = new QSpinBox(this);
    m_spinHttpPort->setRange(1024, 65535);
    m_spinHttpPort->setValue(AppSettings::instance().httpPort());

    m_spinWsPort = new QSpinBox(this);
    m_spinWsPort->setRange(1024, 65535);
    m_spinWsPort->setValue(AppSettings::instance().webSocketPort());

    globalLayout->addRow("HTTP Web Port:", m_spinHttpPort);
    globalLayout->addRow("WebSocket Data Port:", m_spinWsPort);

    rightLayout->addWidget(groupGlobal);
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

    // setLayout(outerLayout);
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
        // --- NEW: Determine visual display text ---
        QString displayText = portName;
        if (m_configCache.contains(portName) && !m_configCache[portName].friendlyName.isEmpty()) {
            displayText = m_configCache[portName].friendlyName + " (" + portName + ")";
        }

        QListWidgetItem *item = new QListWidgetItem(displayText, m_listPorts);

        // --- NEW: Store the raw portName invisibly so we don't break the cache lookup ---
        item->setData(Qt::UserRole, portName);

        item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        bool isEnabled = m_configCache.contains(portName) && m_configCache[portName].isEnabled;
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

        // --- NEW: Store the invisible key ---
        item->setData(Qt::UserRole, text);

        item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        m_listPorts->setCurrentItem(item);
    }
}



void SettingsDialog::onPortSelectionChanged(QListWidgetItem *current, QListWidgetItem *previous) {
    if (previous) {
        saveCurrentFormToCache(); // Save the old one before switching
    }

    if (current) {
        // --- FIX: Read the invisible key, NOT the visible text ---
        m_currentEditingPort = current->data(Qt::UserRole).toString();

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
    m_spinListenPort->setValue(config.listenPort);
    m_txtPhonebook->setText(config.phonebookPath);
}

void SettingsDialog::saveCurrentFormToCache() {
    if (m_currentEditingPort.isEmpty()) return;

    // Find the item in the list using the hidden data key
    for (int i = 0; i < m_listPorts->count(); ++i) {
        QListWidgetItem *item = m_listPorts->item(i);
        if (item->data(Qt::UserRole).toString() == m_currentEditingPort) {
            item->setCheckState(m_chkEnable->isChecked() ? Qt::Checked : Qt::Unchecked);

            // --- NEW: Update the visual list text dynamically when saving ---
            QString currentFriendly = m_txtFriendlyName->text();
            if (!currentFriendly.isEmpty()) {
                item->setText(currentFriendly + " (" + m_currentEditingPort + ")");
            } else {
                item->setText(m_currentEditingPort);
            }
            break;
        }
    }

    BridgeConfig config;
    config.friendlyName = m_txtFriendlyName->text();
    config.portName = m_currentEditingPort;
    config.baudRate = m_cmbBaudRate->currentText().toInt();
    config.flowControl = m_chkFlowControl->isChecked();
    config.localEcho = m_chkLocalEcho->isChecked();
    config.phonebookPath = m_txtPhonebook->text();
    config.isEnabled = m_chkEnable->isChecked();
    config.listenPort = m_spinListenPort->value();
    m_configCache.insert(m_currentEditingPort, config);
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

void SettingsDialog::onBrowsePhonebook() {
    QString fileName = QFileDialog::getOpenFileName(this, "Select Phonebook XML", "", "XML Files (*.xml);;All Files (*)");
    if (!fileName.isEmpty()) {
        m_txtPhonebook->setText(fileName);
    }
}


void SettingsDialog::onSaveAndClose() {
    int newHttp = m_spinHttpPort->value();
    int newWs = m_spinWsPort->value();

    // The Bouncer Part 1: Web UI Port Conflict
    if (newHttp == newWs) {
        QMessageBox::warning(this, "Port Conflict",
                             "The HTTP Web UI and the WebSocket Data Port cannot use the same port number. Please choose different ports.");
        return; // Stop the save process so the dialog stays open
    }

    saveCurrentFormToCache(); // Catch whatever they are looking at right now

    // Extract all valid configurations from our cache
    QList<BridgeConfig> finalConfigs = m_configCache.values();

    // --- NEW: The Fleet Port Collision Check ---
    QSet<int> usedListenPorts;

    for (const BridgeConfig& config : finalConfigs) {
        // We only care if the bridge is actively enabled and has a port assigned (> 0)
        if (config.isEnabled && config.listenPort > 0) {

            // Check 1: Is another Atari already using this port?
            if (usedListenPorts.contains(config.listenPort)) {
                QMessageBox::warning(this, "BBS Listener Conflict",
                                     QString("Multiple active bridges are trying to use inbound port %1.\n\nEach enabled bridge must have a unique listening port, or be set to 0 (Disabled).")
                                         .arg(config.listenPort));
                return; // Stop save
            }
            usedListenPorts.insert(config.listenPort);

            // Check 2: Is it colliding with our Web Dashboard?
            if (config.listenPort == newHttp || config.listenPort == newWs) {
                QMessageBox::warning(this, "Reserved Port Conflict",
                                     QString("Bridge '%1' is trying to use port %2, which is reserved for the Web Dashboard.\n\nPlease assign a different inbound port.")
                                         .arg(config.friendlyName.isEmpty() ? config.portName : config.friendlyName)
                                         .arg(config.listenPort));
                return; // Stop save
            }
        }
    }

    // If we made it past all the bouncers, save to disk!
    AppSettings::instance().saveBridges(finalConfigs);
    AppSettings::instance().setHttpPort(newHttp);
    AppSettings::instance().setWebSocketPort(newWs);
    accept(); // Closes the dialog
}
