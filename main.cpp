#include "MainWindow.h"
#include "crashhandler.h"
#include "tnc620theme.h"
#include "translator.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QSettings>

int main(int argc, char *argv[])
{
    CrashHandler::install();

    QSurfaceFormat fmt;
    // MSAA 8x : anti-aliasing matériel — lisse les arêtes (lignes
    // noires des contours, axes, trajectoires) sans coût CPU
    fmt.setSamples(8);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    CrashHandler::log("QApplication creation...");
    QApplication a(argc, argv);
    CrashHandler::log("QApplication OK");

    // ── Identite de l'application (pour QSettings) ──────────
    QApplication::setOrganizationName("EazyNC");
    QApplication::setApplicationName("EazyNC");

    // ── Systeme de traduction (.msg style Centroid CNC12) ───
    // Charge la langue sauvegardee, ou francais par defaut.
    {
        QSettings settings;
        QString lang = settings.value("language",
                                      QStringLiteral("fr")).toString();
        if (!Translator::instance().init(lang)) {
            CrashHandler::log("Translator: eazync.msg introuvable - "
                              "interface en francais sans traduction.");
        } else {
            CrashHandler::log("Translator OK");
        }
    }

    // Theme TNC 620 — palette officielle Heidenhain
    // Couleurs extraites de heidenhain640_theme RPM + startup_1024x768.bmp
    // Fond: #D6D6D6  Bordure: #737373  Selection: #0000FF
    // Police: TNC640Global (placer TNC640Global.ttf a cote de EazyNC.exe)
    applyTnc620Theme(a);

    CrashHandler::log("MainWindow creation...");
    MainWindow w;
    CrashHandler::log("MainWindow OK");

    w.show();
    CrashHandler::log("show() OK - entree boucle evenements");
    return a.exec();
}