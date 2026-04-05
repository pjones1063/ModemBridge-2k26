#include "LogWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QDateTime>

LogWindow::LogWindow(QWidget *parent) : QDialog(parent) {
    setWindowTitle("ModemBridge Fleet Diagnostics");
    resize(800, 600);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // --- The Terminal Output ---
    m_textEdit = new QTextEdit(this);
    m_textEdit->setReadOnly(true);
    m_textEdit->setFontFamily("Courier");
    m_textEdit->setStyleSheet("background-color: #1e1e1e; color: #d4d4d4; font-size: 10pt;");

    // --- The Controls ---
    QHBoxLayout *bottomLayout = new QHBoxLayout();

    m_chkAutoScroll = new QCheckBox("Auto-Scroll", this);
    m_chkAutoScroll->setChecked(true);

    m_chkHexMode = new QCheckBox("Hex Dump Mode", this);
    m_chkHexMode->setChecked(false);
    connect(m_chkHexMode, &QCheckBox::toggled, this, &LogWindow::refreshView);

    m_btnClear = new QPushButton("Clear Display", this);
    connect(m_btnClear, &QPushButton::clicked, this, &LogWindow::clearLogs);

    bottomLayout->addWidget(m_chkAutoScroll);
    bottomLayout->addWidget(m_chkHexMode);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_btnClear);

    mainLayout->addWidget(m_textEdit);
    mainLayout->addLayout(bottomLayout);
}

void LogWindow::clearLogs() {
    m_history.clear();
    m_textEdit->clear();
}

QString LogWindow::getTimestamp() const {
    return QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
}

void LogWindow::appendHtml(const QString &html) {
    if (html.isEmpty()) return; // Don't append blank lines for filtered data

    m_textEdit->append(html);
    if (m_chkAutoScroll->isChecked()) {
        QScrollBar *sb = m_textEdit->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}

// --- Re-Render Logic ---

void LogWindow::refreshView() {
    m_textEdit->setUpdatesEnabled(false);
    m_textEdit->clear();

    for (const LogEntry &entry : m_history) {
        QString html = buildHtmlForEntry(entry);
        if (!html.isEmpty()) {
            m_textEdit->append(html);
        }
    }

    if (m_chkAutoScroll->isChecked()) {
        QScrollBar *sb = m_textEdit->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
    m_textEdit->setUpdatesEnabled(true);
}


QString LogWindow::buildHtmlForEntry(const LogEntry &entry) {
    if (entry.type == LogEntry::Status) {
        return QString("<span style='color: #4CAF50;'>[%1] [%2] %3</span>")
        .arg(entry.timestamp).arg(entry.portName).arg(entry.msgOrDir.toHtmlEscaped());
    }

    if (entry.type == LogEntry::Error) {
        return QString("<span style='color: #F44336;'>[%1] [%2] ERROR: %3</span>")
        .arg(entry.timestamp).arg(entry.portName).arg(entry.msgOrDir.toHtmlEscaped());
    }

    // --- THE BULLETPROOF FILTER ---
    // The user requested NO keystroke spam by default.
    // If Hex Mode is unchecked, we completely drop trace data.
    if (!m_chkHexMode->isChecked()) {
        return QString();
    }

    QString headerColor = entry.msgOrDir.startsWith("TX") ? "#C45911" : "#1177C4";
    QString dump = QString("<div style='margin-top: 5px; border-left: 3px solid %1; padding-left: 5px;'>"
                           "<pre style='margin: 0; line-height: 1.2; font-family: \"Courier New\", Courier, monospace; color: #d4d4d4;'>"
                           "<font color='#888888'>[%2]</font> <font color='%3'><b>[PORT: %4 %5]</b></font> %6 bytes:\n")
                       .arg(headerColor).arg(entry.timestamp).arg(headerColor)
                       .arg(entry.portName).arg(entry.msgOrDir).arg(entry.data.size());

    // FULL HEX DUMP (Works exactly as it did before when checked)
    for (int i = 0; i < entry.data.size(); i += 16) {
        QByteArray chunk = entry.data.mid(i, 16);
        QString offset = QString("%1").arg(i, 4, 16, QChar('0')).toUpper();

        QString hex;
        for (int j = 0; j < 16; ++j) {
            if (j == 8) hex += " ";
            if (j < chunk.size()) {
                quint8 byte = static_cast<quint8>(chunk[j]);
                QString bHex = QString("%1").arg(byte, 2, 16, QChar('0')).toUpper();
                if (byte == 0x00 || byte == 0x20) {
                    hex += QString("<font color='#444444'>%1</font> ").arg(bHex);
                } else {
                    hex += bHex + " ";
                }
            } else {
                hex += "&nbsp;&nbsp; ";
            }
        }

        QString ascii;
        for (int k = 0; k < chunk.size(); ++k) {
            quint8 byte = static_cast<quint8>(chunk[k]);
            if (byte >= 32 && byte <= 126) ascii += QString(QChar(byte)).toHtmlEscaped();
            else if (byte >= 160 && byte <= 254) ascii += QString(QChar(byte - 128)).toHtmlEscaped();
            else ascii += "<font color='#444444'>.</font>";
        }
        dump += QString("<font color='#888888'>%1:</font>  %2  <font color='#888888'>|</font>  %3\n").arg(offset).arg(hex).arg(ascii);
    }

    dump += "</pre></div>";
    return dump;
}


// --- Signal Catchers ---

void LogWindow::logStatus(const QString &portName, const QString &msg) {
    LogEntry e{LogEntry::Status, getTimestamp(), portName, msg, QByteArray()};
    m_history.append(e);
    if (m_history.size() > MAX_HISTORY) m_history.removeFirst();
    appendHtml(buildHtmlForEntry(e));
}

void LogWindow::logError(const QString &portName, const QString &err) {
    LogEntry e{LogEntry::Error, getTimestamp(), portName, err, QByteArray()};
    m_history.append(e);
    if (m_history.size() > MAX_HISTORY) m_history.removeFirst();
    appendHtml(buildHtmlForEntry(e));
}

void LogWindow::logTrace(const QString &portName, const QString &dir, const QByteArray &data) {
    LogEntry e{LogEntry::Trace, getTimestamp(), portName, dir, data};
    m_history.append(e);
    if (m_history.size() > MAX_HISTORY) m_history.removeFirst();

    // Only append to the live feed if it passes our filter
    QString html = buildHtmlForEntry(e);
    if (!html.isEmpty()) {
        appendHtml(html);
    }
}

void LogWindow::closeEvent(QCloseEvent *event) {
    this->hide();
    event->ignore();
}
