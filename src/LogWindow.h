#ifndef LOGWINDOW_H
#define LOGWINDOW_H

#include <QDialog>
#include <QTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QByteArray>
#include <QCloseEvent>
#include <QList>

// [NEW] A structure to remember the raw data so we can re-render it
struct LogEntry {
    enum Type { Status, Error, Trace } type;
    QString timestamp;
    QString portName;
    QString msgOrDir;
    QByteArray data;
};

class LogWindow : public QDialog {
    Q_OBJECT
public:
    explicit LogWindow(QWidget *parent = nullptr);

public slots:
    void logStatus(const QString &portName, const QString &msg);
    void logError(const QString &portName, const QString &err);
    void logTrace(const QString &portName, const QString &dir, const QByteArray &data);

private slots:
    void clearLogs();
    void refreshView(); // [NEW] Triggered by the checkbox

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QString getTimestamp() const;
    void appendHtml(const QString &html);
    QString buildHtmlForEntry(const LogEntry &entry); // [NEW] The HTML generator

    QTextEdit *m_textEdit;
    QCheckBox *m_chkAutoScroll;
    QCheckBox *m_chkHexMode;
    QPushButton *m_btnClear;

    QList<LogEntry> m_history; // [NEW] Our rolling memory
    const int MAX_HISTORY = 500; // Keep it snappy
};

#endif // LOGWINDOW_H
