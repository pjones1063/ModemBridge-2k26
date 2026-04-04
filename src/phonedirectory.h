#ifndef PHONEDIRECTORY_H
#define PHONEDIRECTORY_H

#include <QDialog>
#include <QTreeWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QCloseEvent>
#include "bbsdata.h"

class PhoneDirectory : public QDialog {
    Q_OBJECT
public:
    explicit PhoneDirectory(QWidget *parent = nullptr);
    void loadFromFile(const QString &path);
    BbsEntry getSelectedEntry() const;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onSearch(const QString &text);
    void onDialClicked();
    void onEditClicked();
    void onSaveClicked();
    void onNewXmlClicked();
    void onAddClicked();
    void onDeleteClicked();
    void onCloseClicked();

private:
    QLineEdit *m_searchEdit;
    QTreeWidget *m_tree;
    QPushButton *m_dialBtn;
    QString m_filePath;
    QList<BbsEntry> m_entries;
    bool m_isDirty;

    void parseXml();
    void saveToFile();
    void refreshList(const QString &filter = "");
    bool runEditDialog(BbsEntry &entry);
    bool checkUnsavedChanges(const QString &actionName);
};

#endif // PHONEDIRECTORY_H
