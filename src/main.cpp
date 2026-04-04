#include <QApplication>
#include <QSplashScreen>
#include <QPixmap>
#include <QTimer>
#include <QLockFile>
#include <QDir>
#include <QMessageBox>
#include "TrayManager.h"
#include <libssh/libssh.h>

int main(int argc, char *argv[]) {
    // 1. Global init for libssh
    ssh_init();

    QApplication app(argc, argv);

    // --- SINGLE INSTANCE LOCK ---
    // We create a lock file in the system's temp directory.
    QLockFile lockFile(QDir::tempPath() + "/modembridge_2k26_instance.lock");

    // tryLock(100) attempts to grab the lock for 100ms.
    // If it fails, another instance is already holding the "keys."
    if (!lockFile.tryLock(100)) {
        QMessageBox::warning(nullptr, "Already Running",
                             "An instance of ModemBridge 2k26 is already running.\n"
                             "Check your system tray or task manager.");
        return 0; // Exit immediately
    }

    // 2. Initialize Splash Screen
    QSplashScreen *splash = nullptr;
    QPixmap splashPixmap(":/splash.png");
    if (!splashPixmap.isNull()) {
        // Scaling to a sensible size for desktop use
        QPixmap scaledPixmap = splashPixmap.scaled(640, 480,
                                                   Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation);
        splash = new QSplashScreen(scaledPixmap);
        splash->show();
        app.processEvents();
    }

    // 3. Tray App configuration
    QApplication::setQuitOnLastWindowClosed(false);

    // 4. Initialize the Fleet Manager
    TrayManager trayManager;

    // 5. Extended 5-second Splash Timer per PM instructions
    QTimer::singleShot(5000, [splash]() {
        if (splash) {
            splash->close();
            delete splash;
        }
    });

    int result = app.exec();

    // 6. Final cleanup
    // The lockFile is automatically released when it goes out of scope here.
    ssh_finalize();

    return result;
}
