// translator.h
// ─────────────────────────────────────────────────────────────────
//  Systeme de traduction style Centroid CNC12 (.msg).
//
//  Format du fichier eazync.msg :
//     [<contexte>.<cle>]
//      fr: "Texte francais"
//      en: "English text"
//
//  Le contexte est typiquement le nom du fichier source.
//
//  Utilisation via la macro TR_() :
//     m_tabs->addTab(buildDimTab(), TR_("tab_dimensions"));
//     ui->btnPlay->setText(TR_("btn_play"));
//
//  IMPORTANT : la macro s'appelle TR_ (et PAS T) pour eviter une
//  collision avec le parametre de template 'T' utilise partout
//  dans les headers Qt. Ne JAMAIS nommer une macro 'T'.
// ─────────────────────────────────────────────────────────────────
#pragma once

#include <QString>
#include <QHash>
#include <QStringList>

class Translator
{
public:
    static Translator& instance();

    bool init(const QString &lang = QStringLiteral("fr"));
    bool reload();

    void setLanguage(const QString &lang);
    QString language() const { return m_lang; }

    QStringList availableLanguages() const;

    QString t(const QString &context, const QString &key) const;
    QString t(const QString &key) const { return t(QString(), key); }

    QString filePath() const { return m_filePath; }

private:
    Translator() = default;
    Translator(const Translator&) = delete;
    Translator& operator=(const Translator&) = delete;

    bool loadFile(const QString &path);
    void parseContent(const QString &content);

    QHash<QString, QHash<QString, QString>> m_dict;
    QString m_lang = QStringLiteral("fr");
    QString m_filePath;
    QStringList m_availableLangs;
};

// Helper : extrait le basename d'un chemin __FILE__
QString tr_file_ctx_(const char *file);

// ─────────────────────────────────────────────────────────────────
//  Macros de traduction.
//   TR_   : avec contexte automatique (nom du fichier source)
//   TRG_  : cle globale, sans contexte
//
//  NE PAS renommer en 'T' : collision avec les templates Qt.
// ─────────────────────────────────────────────────────────────────
#define TR_(key)  (::Translator::instance().t(tr_file_ctx_(__FILE__), QStringLiteral(key)))
#define TRG_(key) (::Translator::instance().t(QStringLiteral(key)))
