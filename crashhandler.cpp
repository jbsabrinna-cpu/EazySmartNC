#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  crashhandler.cpp — implementation
//  <windows.h> inclus ICI seulement, pas dans le .h
// ═══════════════════════════════════════════════════════════════

// Windows headers EN PREMIER, avant Qt
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dbghelp.h>
#  pragma comment(lib, "dbghelp.lib")
#endif

#include "crashhandler.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>
#include <csignal>
#include <cstdio>

// ── Variables globales (dans ce .cpp seulement) ───────────────
static QtMessageHandler g_prevHandler = nullptr;
static QFile            g_logFile;
static QTextStream      g_logStream;

// ── Stack trace Windows ───────────────────────────────────────
#ifdef _WIN32
static QString captureStackTrace()
{
    const int MAX_FRAMES = 32;
    void *stack[MAX_FRAMES];
    USHORT frames = CaptureStackBackTrace(2, MAX_FRAMES, stack, nullptr);

    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);

    char symBuf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    SYMBOL_INFO *sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
    sym->MaxNameLen   = MAX_SYM_NAME;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);

    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    QString result;
    for (USHORT i = 0; i < frames; ++i) {
        DWORD64 addr = reinterpret_cast<DWORD64>(stack[i]);
        DWORD   disp = 0;
        QString funcName = "??";
        QString fileName = "";
        int     lineNum  = 0;

        if (SymFromAddr(process, addr, nullptr, sym))
            funcName = QString::fromLocal8Bit(sym->Name);
        if (SymGetLineFromAddr64(process, addr, &disp, &line)) {
            fileName = QString::fromLocal8Bit(line.FileName);
            int sl = fileName.lastIndexOf('\\');
            if (sl >= 0) fileName = fileName.mid(sl+1);
            lineNum = static_cast<int>(line.LineNumber);
        }

        result += QString("  #%1  %2").arg(i, 2).arg(funcName);
        if (!fileName.isEmpty())
            result += QString("  [%1:%2]").arg(fileName).arg(lineNum);
        result += "\n";

        if (funcName == "main" || funcName == "WinMain") break;
    }
    return result;
}
#else
static QString captureStackTrace()
{
    return "  (stack trace non disponible)\n";
}
#endif

// ── Handler Qt (qWarning, qCritical, qFatal) ─────────────────
static void eazyMessageHandler(QtMsgType type, const QMessageLogContext &ctx,
                                const QString &msg)
{
    if (g_logFile.isOpen()) {
        QString level;
        switch(type) {
        case QtDebugMsg:    level = "[DBG]"; break;
        case QtInfoMsg:     level = "[INF]"; break;
        case QtWarningMsg:  level = "[WRN]"; break;
        case QtCriticalMsg: level = "[CRT]"; break;
        case QtFatalMsg:    level = "[FAT]"; break;
        }

        QString ts = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        g_logStream << ts << " " << level << " " << msg;
        if (ctx.file)
            g_logStream << "  [" << ctx.file << ":" << ctx.line << "]";
        g_logStream << "\n";

        // Detecter setPointSize(-1) et capturer la pile immediatement
        if (msg.contains("setPointSize") && msg.contains("-1")) {
            g_logStream << ">>> CRASH IMMINENT : setPointSize(-1) detecte !\n";
            g_logStream << ">>> STACK TRACE :\n";
            g_logStream << captureStackTrace();
            g_logStream << ">>> FIN STACK\n";
        }
        g_logStream.flush();
    }

    if (g_prevHandler)
        g_prevHandler(type, ctx, msg);
    if (type == QtFatalMsg)
        abort();
}

// ── Handler de signal POSIX ───────────────────────────────────
static void signalHandler(int sig)
{
    const char *name = "INCONNU";
    switch(sig) {
    case SIGSEGV: name = "SIGSEGV (acces memoire illegal)"; break;
    case SIGABRT: name = "SIGABRT (abort/assertion)";       break;
    case SIGFPE:  name = "SIGFPE (erreur arithmetique)";    break;
    case SIGILL:  name = "SIGILL (instruction illegale)";   break;
    }

    if (g_logFile.isOpen()) {
        g_logStream << "\n===========================================\n";
        g_logStream << "CRASH SIGNAL : " << name << "\n";
        g_logStream << "STACK TRACE  :\n" << captureStackTrace();
        g_logStream << "Heure        : "
                    << QDateTime::currentDateTime().toString() << "\n";
        g_logStream << "===========================================\n";
        g_logStream.flush();
        g_logFile.close();
    }
    fprintf(stderr, "\n[EAZYNC CRASH] Signal: %s\n", name);
    fprintf(stderr, "[EAZYNC CRASH] Voir crash_log.txt\n");
    fflush(stderr);

    signal(sig, SIG_DFL);
    raise(sig);
}

// ── API publique ──────────────────────────────────────────────
void CrashHandler::install()
{
    QString logPath = QDir::currentPath() + "/crash_log.txt";
    g_logFile.setFileName(logPath);
    if (g_logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        g_logStream.setDevice(&g_logFile);
        g_logStream << "EazyNC - Log de crash\n";
        g_logStream << "Demarrage : "
                    << QDateTime::currentDateTime().toString() << "\n";
        g_logStream << "===================================================\n\n";
        g_logStream.flush();
        fprintf(stderr, "[CrashHandler] Log : %s\n",
                logPath.toLocal8Bit().constData());
    } else {
        fprintf(stderr, "[CrashHandler] ERREUR ouverture log %s\n",
                logPath.toLocal8Bit().constData());
    }

    g_prevHandler = qInstallMessageHandler(eazyMessageHandler);
    signal(SIGSEGV, signalHandler);
    signal(SIGABRT, signalHandler);
    signal(SIGFPE,  signalHandler);
    signal(SIGILL,  signalHandler);
}

void CrashHandler::log(const QString &msg)
{
    if (g_logFile.isOpen()) {
        g_logStream << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
                    << " [APP] " << msg << "\n";
        g_logStream.flush();
    }
}
