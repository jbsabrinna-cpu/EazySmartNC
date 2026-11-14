#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  crashhandler.h — declaration uniquement, pas d'include Windows
//  Implementation dans crashhandler.cpp
// ═══════════════════════════════════════════════════════════════
#include <QString>

class CrashHandler
{
public:
    // Installer les handlers — appeler au tout debut de main()
    static void install();

    // Ecrire une ligne dans crash_log.txt
    static void log(const QString &msg);
};
