#include "phonedirectory.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFile>
#include <QDir>
#include <QDomDocument>
#include <QTextStream>
#include <QFormLayout>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QRadioButton>

PhoneDirectory::PhoneDirectory(const QString &filePath, QWidget *parent)
    : QDialog(parent), m_filePath(filePath) {

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

    // --- Buttons (New XML and Save XML removed) ---
    QPushButton *newEntryBtn = new QPushButton(tr("New Entry"), this);
    QPushButton *editBtn = new QPushButton(tr("Edit"), this);
    QPushButton *delBtn = new QPushButton(tr("Delete"), this);

    m_dialBtn = new QPushButton(tr("DIAL"), this);
    QPushButton *closeBtn = new QPushButton(tr("Close"), this);

    connect(newEntryBtn, &QPushButton::clicked, this, &PhoneDirectory::onAddClicked);
    connect(editBtn, &QPushButton::clicked, this, &PhoneDirectory::onEditClicked);
    connect(delBtn, &QPushButton::clicked, this, &PhoneDirectory::onDeleteClicked);

    connect(m_dialBtn, &QPushButton::clicked, this, &PhoneDirectory::onDialClicked);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    // Assembly
    btnLayout->addWidget(newEntryBtn);
    btnLayout->addWidget(editBtn);
    btnLayout->addWidget(delBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_dialBtn);
    btnLayout->addWidget(closeBtn);

    mainLayout->addLayout(btnLayout);
    m_dialBtn->setDefault(true);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &PhoneDirectory::onDialClicked);

    // Automatically load the data if a path was provided
    if (!m_filePath.isEmpty()) {
        loadFromFile(m_filePath);
    }
}

void PhoneDirectory::onSearch(const QString &text) {
    refreshList(text);
}

void PhoneDirectory::loadFromFile(const QString &path) {
    if (path.isEmpty()) return;
    m_filePath = path;
    parseXml();
    refreshList();
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
    if (m_filePath.isEmpty()) {
        QMessageBox::warning(this, "Save Error", "No phonebook file path specified. Cannot save.");
        return;
    }

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
    }
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
        accept();
    }
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

    // --- Password Field with Toggle ---
    QLineEdit *passEdit = new QLineEdit(entry.password);
    passEdit->setEchoMode(QLineEdit::Password);

    QPushButton *togglePassBtn = new QPushButton("Show");
    togglePassBtn->setCheckable(true);
    togglePassBtn->setFixedWidth(60);

    connect(togglePassBtn, &QPushButton::toggled, [passEdit, togglePassBtn](bool checked) {
        passEdit->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
        togglePassBtn->setText(checked ? "Hide" : "Show");
    });

    QWidget *passWidget = new QWidget();
    QHBoxLayout *passLayout = new QHBoxLayout(passWidget);
    passLayout->setContentsMargins(0, 0, 0, 0);
    passLayout->addWidget(passEdit);
    passLayout->addWidget(togglePassBtn);
    // ----------------------------------

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
    layout.addRow("Password:", passWidget);

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

// Data is now auto-saved directly after any modification
void PhoneDirectory::onAddClicked() {
    BbsEntry e; e.name = "New BBS"; e.port = 23; e.protocol = "TELNET";
    if (runEditDialog(e)) {
        m_entries.append(e);
        refreshList();
        saveToFile();
    }
}

void PhoneDirectory::onEditClicked() {
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item) return;
    int index = item->data(0, Qt::UserRole).toInt();
    if (runEditDialog(m_entries[index])) {
        refreshList();
        saveToFile();
    }
}

void PhoneDirectory::onDeleteClicked() {
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item) return;
    m_entries.removeAt(item->data(0, Qt::UserRole).toInt());
    refreshList();
    saveToFile();
}
