// translator.cpp
// ─────────────────────────────────────────────────────────────────
//  Implementation du systeme de traduction style Centroid .msg
// ─────────────────────────────────────────────────────────────────
#include "translator.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <QDebug>

// ── Utilitaire : basename d'un chemin de fichier ────────────────
//  __FILE__ contient le chemin complet du source (ex sous MSVC :
//  "C:\Users\...\programmepage.cpp"). On ne garde que le nom du
//  fichier pour servir de contexte.
QString tr_file_ctx_(const char *file)
{
    if (!file) return QString();
    QString f = QString::fromLocal8Bit(file);
    // Garde uniquement le nom de fichier (sans repertoire)
    int slash = f.lastIndexOf(QChar('/'));
    int bslash = f.lastIndexOf(QChar('\\'));
    int cut = qMax(slash, bslash);
    return (cut >= 0) ? f.mid(cut + 1) : f;
}

// ── Singleton ──────────────────────────────────────────────────
Translator& Translator::instance()
{
    static Translator inst;
    return inst;
}

// ── Initialisation ──────────────────────────────────────────────
bool Translator::init(const QString &lang)
{
    m_lang = lang;
    m_dict.clear();
    m_availableLangs.clear();

    // Cherche le fichier dans plusieurs emplacements
    QStringList candidates;
    candidates << QCoreApplication::applicationDirPath() + "/lang/eazync.msg"
               << QDir::currentPath() + "/lang/eazync.msg"
               << ":/lang/eazync.msg"; // ressource Qt si embarque

    for (const QString &path : std::as_const(candidates)) {
        if (QFile::exists(path)) {
            if (loadFile(path)) {
                m_filePath = path;
                qDebug() << "[Translator] charge :" << path
                         << " langues:" << m_availableLangs
                         << " entrees:" << m_dict.size();
                return true;
            }
        }
    }
    qWarning() << "[Translator] fichier eazync.msg introuvable !";
    return false;
}

bool Translator::reload()
{
    if (m_filePath.isEmpty()) return init(m_lang);
    return loadFile(m_filePath);
}

void Translator::setLanguage(const QString &lang)
{
    m_lang = lang;
}

QStringList Translator::availableLanguages() const
{
    return m_availableLangs;
}

// ── Recherche d'une traduction ─────────────────────────────────
QString Translator::t(const QString &context, const QString &key) const
{
    // Cle composee "context.key" si contexte fourni, sinon juste "key"
    QString fullKey = context.isEmpty() ? key
                                        : QString("%1.%2").arg(context, key);

    auto it = m_dict.constFind(fullKey);
    if (it != m_dict.constEnd()) {
        // Trouve. On essaye la langue courante en priorite.
        auto lit = it->constFind(m_lang);
        if (lit != it->constEnd() && !lit->isEmpty())
            return *lit;
        // Fallback : francais
        lit = it->constFind(QStringLiteral("fr"));
        if (lit != it->constEnd() && !lit->isEmpty())
            return *lit;
        // Fallback : anglais
        lit = it->constFind(QStringLiteral("en"));
        if (lit != it->constEnd() && !lit->isEmpty())
            return *lit;
    }

    // Pas de traduction : retourner la cle, suffixee d'un marqueur
    // pour reperer les chaines non traduites. Commente en production.
    return key;
    // return QString("?%1?").arg(key); // version debug
}

// ── Chargement d'un fichier .msg ────────────────────────────────
bool Translator::loadFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[Translator] ouverture echouee :" << path
                   << f.errorString();
        return false;
    }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    QString content = ts.readAll();
    f.close();

    parseContent(content);
    return !m_dict.isEmpty();
}

// ── Parser du format .msg ──────────────────────────────────────
//  Format reconnu :
//     [contexte.cle]
//      fr: "texte"
//      en: "english"
//      ...
//
//  Le texte peut s'etendre sur plusieurs lignes avec l'operateur '+':
//      fr: "Premiere ligne\n" +
//          "Deuxieme ligne"
//
//  On collecte chaque "code: \"...\"" et on concatene les '+'.
//  Le BOM UTF-8 et les CRLF sont geres par QTextStream.
void Translator::parseContent(const QString &content)
{
    m_dict.clear();
    QSet<QString> langsSeen;

    QString currentKey;
    QString currentLang;
    QString currentText;
    bool inMultiLine = false;

    // Helper : flush du texte courant dans le dict
    auto flushCurrent = [&]() {
        if (!currentKey.isEmpty() && !currentLang.isEmpty()) {
            m_dict[currentKey][currentLang] = currentText;
            langsSeen.insert(currentLang);
        }
        currentText.clear();
        // Vider currentLang : evite qu'un flush final en fin de
        // fichier re-flush la derniere entree avec un texte vide
        // (ce qui ecrasait la derniere traduction du .msg).
        currentLang.clear();
        inMultiLine = false;
    };

    // Regex
    static const QRegularExpression reSection(
        R"(^\s*\[\s*([^\]]+?)\s*\]\s*$)");
    static const QRegularExpression reEntry(
        R"(^\s*([a-zA-Z]{2,4})\s*:\s*\"(.*?)\"(\s*\+\s*)?\s*$)");
    static const QRegularExpression reContinuation(
        R"(^\s*\"(.*?)\"(\s*\+\s*)?\s*$)");

    const QStringList lines = content.split(
        QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);

    for (const QString &raw : lines) {
        QString line = raw;
        // Retirer le BOM UTF-8 si present au tout debut
        if (!line.isEmpty() && line.front() == QChar(0xFEFF))
            line.remove(0, 1);

        // Lignes commentees ou directives
        QString trim = line.trimmed();
        if (trim.isEmpty()) continue;
        if (trim.startsWith('#')) continue;
        if (trim.startsWith('{') && trim.endsWith('}')) continue; // ex {CURRENT}

        // Cas multi-ligne en cours (continuation avec "+")
        if (inMultiLine) {
            auto m = reContinuation.match(line);
            if (m.hasMatch()) {
                QString chunk = m.captured(1);
                // Decoder les escapes basiques
                chunk.replace("\\n", "\n").replace("\\t", "\t")
                    .replace("\\\"", "\"").replace("\\\\", "\\");
                currentText += chunk;
                bool hasPlus = !m.captured(2).isEmpty();
                if (!hasPlus) flushCurrent();
                continue;
            }
            // Ligne ne correspond pas a une continuation : flush et continue
            flushCurrent();
        }

        // Nouvelle section [contexte.cle]
        auto mSec = reSection.match(line);
        if (mSec.hasMatch()) {
            // Flush l'ancien si en cours
            if (!currentKey.isEmpty()) flushCurrent();
            currentKey = mSec.captured(1).trimmed();
            currentLang.clear();
            continue;
        }

        // Nouvelle entree de langue dans la section courante
        auto mEntry = reEntry.match(line);
        if (mEntry.hasMatch() && !currentKey.isEmpty()) {
            currentLang = mEntry.captured(1).toLower();
            QString chunk = mEntry.captured(2);
            chunk.replace("\\n", "\n").replace("\\t", "\t")
                .replace("\\\"", "\"").replace("\\\\", "\\");
            currentText = chunk;
            bool hasPlus = !mEntry.captured(3).isEmpty();
            inMultiLine = hasPlus;
            if (!hasPlus) flushCurrent();
        }
    }

    // Flush du dernier si en cours
    if (!currentKey.isEmpty() && !currentLang.isEmpty())
        flushCurrent();

    // Liste des langues
    m_availableLangs.clear();
    for (const QString &l : std::as_const(langsSeen))
        m_availableLangs << l;
    m_availableLangs.sort();
}