#include "phonedirectory.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFile>
#include <QFileDialog>
#include <QDir>
#include <QDomDocument>
#include <QTextStream>
#include <QFormLayout>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QRadioButton>

#include <QFileDialog>
#include <QDir>
// ... (your other includes)

PhoneDirectory::PhoneDirectory(QWidget *parent) : QDialog(parent), m_isDirty(false) {
    setWindowTitle(tr("ModemBridge Phonebook"));
    resize(700, 450);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search BBS Name..."));
    connect(m_searchEdit, &QLineEdit::textChanged, this, &PhoneDirectory::onSearch);
    mainLayout->addWidget(m_searchEdit);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("BBS Name"), tr("Address"), tr("Port"), tr("Protocol"), tr("User ID")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    mainLayout->addWidget(m_tree);

    QHBoxLayout *btnLayout = new QHBoxLayout();

    // --- Renamed and Added Buttons ---
    QPushButton *newEntryBtn = new QPushButton(tr("New Entry"), this);
    QPushButton *editBtn = new QPushButton(tr("Edit"), this);
    QPushButton *delBtn = new QPushButton(tr("Delete"), this);

    QPushButton *newXmlBtn = new QPushButton(tr("New XML"), this); // [NEW]
    QPushButton *saveBtn = new QPushButton(tr("Save XML"), this);

    m_dialBtn = new QPushButton(tr("DIAL"), this);
    QPushButton *closeBtn = new QPushButton(tr("Close"), this);

    connect(newEntryBtn, &QPushButton::clicked, this, &PhoneDirectory::onAddClicked);
    connect(editBtn, &QPushButton::clicked, this, &PhoneDirectory::onEditClicked);
    connect(delBtn, &QPushButton::clicked, this, &PhoneDirectory::onDeleteClicked);

    connect(newXmlBtn, &QPushButton::clicked, this, &PhoneDirectory::onNewXmlClicked); // [NEW]
    connect(saveBtn, &QPushButton::clicked, this, &PhoneDirectory::onSaveClicked);

    connect(m_dialBtn, &QPushButton::clicked, this, &PhoneDirectory::onDialClicked);
    connect(closeBtn, &QPushButton::clicked, this, &PhoneDirectory::onCloseClicked);

    // Assembly
    btnLayout->addWidget(newEntryBtn);
    btnLayout->addWidget(editBtn);
    btnLayout->addWidget(delBtn);
    btnLayout->addSpacing(15); // Visual gap
    btnLayout->addWidget(newXmlBtn);
    btnLayout->addWidget(saveBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_dialBtn);
    btnLayout->addWidget(closeBtn);

    mainLayout->addLayout(btnLayout);
    m_dialBtn->setDefault(true);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &PhoneDirectory::onDialClicked);
}

// --- The New XML Logic ---
void PhoneDirectory::onNewXmlClicked() {
#ifdef Q_OS_MAC
    QString fileName = QFileDialog::getOpenFileName(this, tr("Create New Dial Directory"), QDir::homePath(), tr("XML Files (*.xml);;All Files (*)"));
#else
    QString fileName = QFileDialog::getSaveFileName(this, tr("Create New Dial Directory"), QDir::homePath() + "/phonebook.xml", tr("XML Files (*.xml);;All Files (*)"));
#endif

    if (fileName.isEmpty()) return;

    if (!fileName.endsWith(".xml", Qt::CaseInsensitive)) {
        fileName += ".xml";
    }

    m_filePath = fileName;

    // Clear out the current UI so they start fresh
    m_entries.clear();
    refreshList();
    m_isDirty = true; // Mark as dirty so they know to hit "Save XML" when done adding entries

    // Auto-prompt them to add their first BBS
    onAddClicked();
}

void PhoneDirectory::onSaveClicked() {
    // Fallback just in case they hit Save before hitting New
    if (m_filePath.isEmpty()) {
        onNewXmlClicked();
        return;
    }
    saveToFile();
}

void PhoneDirectory::onSearch(const QString &text) { refreshList(text); }


void PhoneDirectory::loadFromFile(const QString &path) {
    if (path.isEmpty()) return;
    m_filePath = path;
    parseXml();
    refreshList();
    m_isDirty = false;
}

void PhoneDirectory::parseXml() {
    m_entries.clear();
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    QDomDocument doc;
    if (!doc.setContent(&file)) { file.close(); return; }

    QDomNodeList list = doc.elementsByTagName("BBS");
    for (int i = 0; i < list.size(); i++) {
        QDomElement e = list.at(i).toElement();
        BbsEntry bbs;
        bbs.name = e.attribute("name");
        bbs.ip = e.attribute("ip");
        bbs.port = e.attribute("port").toInt();
        bbs.protocol = e.attribute("protocol", "TELNET");
        bbs.login = e.attribute("login");
        bbs.password = e.attribute("password");
        m_entries.append(bbs);
    }
    file.close();
}

void PhoneDirectory::saveToFile() {
    if (m_filePath.isEmpty()) return;
    QDomDocument doc;
    QDomElement root = doc.createElement("EtherTerm");
    doc.appendChild(root);
    QDomElement pb = doc.createElement("Phonebook");
    root.appendChild(pb);

    for (const BbsEntry &entry : m_entries) {
        QDomElement tag = doc.createElement("BBS");
        tag.setAttribute("name", entry.name);
        tag.setAttribute("ip", entry.ip);
        tag.setAttribute("port", entry.port);
        tag.setAttribute("protocol", entry.protocol);
        tag.setAttribute("login", entry.login);
        tag.setAttribute("password", entry.password);
        pb.appendChild(tag);
    }

    QFile file(m_filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << doc.toString();
        file.close();
        m_isDirty = false;
    }
}

bool PhoneDirectory::checkUnsavedChanges(const QString &actionName) {
    if (!m_isDirty) return true;
    auto res = QMessageBox::question(this, "Unsaved Changes", QString("Save before %1?").arg(actionName),
                                     QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (res == QMessageBox::Yes) { saveToFile(); return true; }
    return (res == QMessageBox::No);
}

void PhoneDirectory::refreshList(const QString &filter) {
    m_tree->clear();
    for (int i = 0; i < m_entries.size(); ++i) {
        const BbsEntry &e = m_entries[i];
        if (filter.isEmpty() || e.name.contains(filter, Qt::CaseInsensitive)) {
            QTreeWidgetItem *item = new QTreeWidgetItem(m_tree);
            item->setText(0, e.name);
            item->setText(1, e.ip);
            item->setText(2, QString::number(e.port));
            item->setText(3, e.protocol);
            item->setText(4, e.login);
            item->setData(0, Qt::UserRole, i);
        }
    }
}

void PhoneDirectory::onDialClicked() {
    if (m_tree->currentItem()) {
        if (checkUnsavedChanges("dialing")) accept();
    }
}

void PhoneDirectory::onCloseClicked() { if (checkUnsavedChanges("closing")) reject(); }

void PhoneDirectory::closeEvent(QCloseEvent *event) {
    if (checkUnsavedChanges("closing")) event->accept();
    else event->ignore();
}

BbsEntry PhoneDirectory::getSelectedEntry() const {
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item) return BbsEntry();
    int index = item->data(0, Qt::UserRole).toInt();
    return m_entries.at(index);
}

bool PhoneDirectory::runEditDialog(BbsEntry &entry) {
    QDialog dlg(this);
    dlg.setWindowTitle("Edit BBS Entry");
    QFormLayout layout(&dlg);
    dlg.resize(450, 250);

    QLineEdit *nameEdit = new QLineEdit(entry.name);
    QLineEdit *ipEdit = new QLineEdit(entry.ip);
    QLineEdit *portEdit = new QLineEdit(QString::number(entry.port));
    QLineEdit *userEdit = new QLineEdit(entry.login);
    QLineEdit *passEdit = new QLineEdit(entry.password);

    QRadioButton *rbTelnet = new QRadioButton("Telnet");
    QRadioButton *rbSsh = new QRadioButton("SSH (BBS)");
    QRadioButton *rbSshAuth = new QRadioButton("SSH (Auth)");

    if (entry.protocol == "SSH-AUTH") rbSshAuth->setChecked(true);
    else if (entry.protocol == "SSH") rbSsh->setChecked(true);
    else rbTelnet->setChecked(true);

    layout.addRow("Name:", nameEdit);
    layout.addRow("Address:", ipEdit);
    layout.addRow("Port:", portEdit);
    layout.addRow("Telnet:", rbTelnet);
    layout.addRow("SSH (BBS):", rbSsh);
    layout.addRow("SSH (Auth):", rbSshAuth);
    layout.addRow("User ID:", userEdit);
    layout.addRow("Password:", passEdit);

    QDialogButtonBox btns(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addRow(&btns);
    connect(&btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(&btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        entry.name = nameEdit->text();
        entry.ip = ipEdit->text();
        entry.port = portEdit->text().toInt();
        entry.login = userEdit->text();
        entry.password = passEdit->text();
        if (rbSshAuth->isChecked()) entry.protocol = "SSH-AUTH";
        else if (rbSsh->isChecked()) entry.protocol = "SSH";
        else entry.protocol = "TELNET";
        return true;
    }
    return false;
}

void PhoneDirectory::onAddClicked() {
    BbsEntry e; e.name = "New BBS"; e.port = 23; e.protocol = "TELNET";
    if (runEditDialog(e)) { m_entries.append(e); refreshList(); m_isDirty = true; }
}

void PhoneDirectory::onEditClicked() {
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item) return;
    int index = item->data(0, Qt::UserRole).toInt();
    if (runEditDialog(m_entries[index])) { refreshList(); m_isDirty = true; }
}

void PhoneDirectory::onDeleteClicked() {
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item) return;
    m_entries.removeAt(item->data(0, Qt::UserRole).toInt());
    refreshList();
    m_isDirty = true;
}
