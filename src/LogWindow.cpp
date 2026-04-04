#include "LogWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QDateTime>

LogWindow::LogWindow(QWidget *parent) : QDialog(parent) {
    setWindowTitle("ModemBridge Fleet Diagnostics");
    resize(800, 600); // Nice and wide for hex dumps

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // --- The Terminal Output ---
    m_textEdit = new QTextEdit(this);
    m_textEdit->setReadOnly(true);
    // Use a monospace font so the hex columns line up perfectly
    m_textEdit->setFontFamily("Courier");
    // Dark mode styling for that proper hacker terminal feel
    m_textEdit->setStyleSheet("background-color: #1e1e1e; color: #d4d4d4; font-size: 10pt;");

    // --- The Controls ---
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    m_chkAutoScroll = new QCheckBox("Auto-Scroll", this);
    m_chkAutoScroll->setChecked(true); // Default to keeping up with traffic

    m_btnClear = new QPushButton("Clear Display", this);
    connect(m_btnClear, &QPushButton::clicked, this, &LogWindow::clearLogs);

    bottomLayout->addWidget(m_chkAutoScroll);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_btnClear);

    mainLayout->addWidget(m_textEdit);
    mainLayout->addLayout(bottomLayout);
}

void LogWindow::clearLogs() {
    m_textEdit->clear();
}

QString LogWindow::getTimestamp() const {
    return QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
}

void LogWindow::appendHtml(const QString &html) {
    m_textEdit->append(html);

    // Force the scrollbar to the bottom if Auto-Scroll is on
    if (m_chkAutoScroll->isChecked()) {
        QScrollBar *sb = m_textEdit->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}

// --- Signal Catchers ---

void LogWindow::logStatus(const QString &portName, const QString &msg) {
    // Neon Green for standard status messages
    QString html = QString("<span style='color: #4CAF50;'>[%1] [%2] %3</span>")
                       .arg(getTimestamp()).arg(portName).arg(msg.toHtmlEscaped());
    appendHtml(html);
}

void LogWindow::logError(const QString &portName, const QString &err) {
    // Angry Red for errors
    QString html = QString("<span style='color: #F44336;'>[%1] [%2] ERROR: %3</span>")
                       .arg(getTimestamp()).arg(portName).arg(err.toHtmlEscaped());
    appendHtml(html);
}
\


void LogWindow::logTrace(const QString &portName, const QString &dir, const QByteArray &data) {
    // 1. Determine Colors and Headers
    // Using your specific Hex colors: Orange for TX, Blue for RX
    QString headerColor = dir.startsWith("TX") ? "#C45911" : "#1177C4";

    // We'll leave latencyHeader empty for now, or you can calculate it later
    QString latencyHeader = "";

    // 2. Build the Header
    QString dump = QString("<div style='margin-top: 5px; border-left: 3px solid %1; padding-left: 5px;'>")
                       .arg(headerColor);

    dump += QString("<pre style='margin: 0; line-height: 1.2; font-family: \"Courier New\", Courier, monospace; color: #d4d4d4;'>")
            + QString("<font color='#888888'>[%1]</font> <font color='%2'><b>[PORT: %3 %4]</b></font> %5 bytes%6:\n")
                  .arg(getTimestamp())
                  .arg(headerColor)
                  .arg(portName)
                  .arg(dir)
                  .arg(data.size())
                  .arg(latencyHeader);

    // 3. The 16-byte Hex Grid Loop
    for (int i = 0; i < data.size(); i += 16) {
        QByteArray chunk = data.mid(i, 16);
        QString offset = QString("%1").arg(i, 4, 16, QChar('0')).toUpper();

        // --- Build Hex Part ---
        QString hex;
        for (int j = 0; j < 16; ++j) {
            if (j == 8) hex += " "; // Middle Gutter for readability

            if (j < chunk.size()) {
                quint8 byte = static_cast<quint8>(chunk[j]);
                QString bHex = QString("%1").arg(byte, 2, 16, QChar('0')).toUpper();

                // Dimmable Bytes: Darker gray for 0x00 and 0x20 (Space)
                if (byte == 0x00 || byte == 0x20) {
                    hex += QString("<font color='#444444'>%1</font> ").arg(bHex);
                } else {
                    hex += bHex + " ";
                }
            } else {
                hex += "&nbsp;&nbsp; "; // Alignment padding for short lines
            }
        }

        // --- Build Atari-Aware ASCII Part ---
        QString ascii;
        for (int k = 0; k < chunk.size(); ++k) {
            quint8 byte = static_cast<quint8>(chunk[k]);

            // Standard ASCII
            if (byte >= 32 && byte <= 126) {
                ascii += QString(QChar(byte)).toHtmlEscaped(); // Wrap it in QString!
            }
            // Atari High-Bit Translation (Inverse/Graphic characters)
            else if (byte >= 160 && byte <= 254) {
                ascii += QString(QChar(byte - 128)).toHtmlEscaped(); // Wrap it here too!
            }
            else {
                ascii += "<font color='#444444'>.</font>"; // Dimmable dot
            }
        }

        // --- Format the Row ---
        dump += QString("<font color='#888888'>%1:</font>  %2  <font color='#888888'>|</font>  %3\n")
                    .arg(offset).arg(hex).arg(ascii);
    }

    dump += "</pre></div>";

    // Send it to the text edit
    appendHtml(dump);
}

void LogWindow::closeEvent(QCloseEvent *event) {
    // Hide the window to the system tray instead of destroying it
    this->hide();
    event->ignore();
}
