#ifndef PHONEDIRECTORY_H
#define PHONEDIRECTORY_H

#include <QDialog>
#include <QTreeWidget>
#include <QLineEdit>
#include <QPushButton>
#include "bbsdata.h"

class PhoneDirectory : public QDialog {
    Q_OBJECT
public:
    // Updated constructor to accept the file path immediately
    explicit PhoneDirectory(const QString &filePath = "", QWidget *parent = nullptr);
    void loadFromFile(const QString &path);
    BbsEntry getSelectedEntry() const;

private slots:
    void onSearch(const QString &text);
    void onDialClicked();
    void onEditClicked();
    void onAddClicked();
    void onDeleteClicked();

private:
    QLineEdit *m_searchEdit;
    QTreeWidget *m_tree;
    QPushButton *m_dialBtn;
    QString m_filePath;
    QList<BbsEntry> m_entries;

    void parseXml();
    void saveToFile();
    void refreshList(const QString &filter = "");
    bool runEditDialog(BbsEntry &entry);
};

#endif // PHONEDIRECTORY_H
