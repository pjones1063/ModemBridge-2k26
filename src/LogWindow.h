#ifndef LOGWINDOW_H
#define LOGWINDOW_H

#include <QDialog>
#include <QTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QByteArray>
#include <QCloseEvent>

class LogWindow : public QDialog {
    Q_OBJECT
public:
    explicit LogWindow(QWidget *parent = nullptr);

public slots:
    // These match the signals we refactored into ModemBridge exactly!
    void logStatus(const QString &portName, const QString &msg);
    void logError(const QString &portName, const QString &err);
    void logTrace(const QString &portName, const QString &dir, const QByteArray &data);

private slots:
    void clearLogs();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void appendHtml(const QString &html);
    QString getTimestamp() const;

    QTextEdit *m_textEdit;
    QCheckBox *m_chkAutoScroll;
    QPushButton *m_btnClear;
};

#endif // LOGWINDOW_H
