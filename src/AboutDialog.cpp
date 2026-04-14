#include "AboutDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("About ModemBridge-2k26"));
    resize(580, 440); // Slightly taller to accommodate the text

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    textBrowser = new QTextBrowser(this);
    textBrowser->setOpenExternalLinks(true);

    // HTML/CSS string mimicking the AspeQt-2k26 style exactly
    QString html = R"(
        <table width="100%" cellspacing="0" cellpadding="0" border="0">
            <tr>
                <td bgcolor="#2b2b2b" width="25"></td>

                <td bgcolor="#2b2b2b" style="padding-top: 25px; padding-bottom: 20px;">
                    <span style="color: white; font-size: 28px; font-family: sans-serif;">ModemBridge-2k26</span><br><br>
                    <span style="color: #4CAF50; font-size: 11px; font-weight: bold; font-family: sans-serif; letter-spacing: 1px;">RETRO-TO-MODERN FLEET MANAGER FOR QT6</span>
                </td>
            </tr>
            <tr>
                <td colspan="2" bgcolor="#4CAF50" height="4"></td>
            </tr>
        </table>

        <table width="100%" cellspacing="0" cellpadding="25" border="0">
            <tr>
                <td style="color: #333; font-family: sans-serif;">
                    <p style="margin-top: 0;"><b>Bridging the Gap Between Vintage and Modern.</b></p>

                    <p>ModemBridge transforms your modern PC into a fleet-aware, serial-to-network bridge. Emulating a Hayes-compatible dial-up modem, it allows retro computers (like the Atari 8-bit series) to seamlessly connect to modern TCP Telnet and SSH BBS systems.</p>

                    <p><b>Key Features:</b></p>
                    <ul>
                        <li>Stay-Resident Fleet Management via System Tray</li>
                        <li>Inbound TCP BBS Listener & Smart Call Rejection</li>
                        <li>Real-Time Web UI Dashboard via WebSockets</li>
                        <li>Cross-Platform Qt6 Architecture</li>
                    </ul>

                    <p style="font-size: 11px; color: #999; margin-top: 30px; border-top: 1px solid #eee; padding-top: 10px;">
                        ModemBridge-2k26 Project<br>
                        C++ / Qt6 Implementation.
                    </p>
                </td>
            </tr>
        </table>
    )";

    textBrowser->setHtml(html);
    mainLayout->addWidget(textBrowser);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    closeButton = new QPushButton(tr("&Close"), this);
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);

    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}
