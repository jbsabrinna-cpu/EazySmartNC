// configdialog.cpp
// ─────────────────────────────────────────────────────────────────
//  Implementation de la fenetre de configuration.
// ─────────────────────────────────────────────────────────────────
#include "configdialog.h"
#include "translator.h"

#include <QTabWidget>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QSettings>
#include <QMessageBox>
#include <QDebug>

// ─────────────────────────────────────────────────────────────────
ConfigDialog::ConfigDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(TR_("config_title"));
    setModal(true);
    resize(420, 320);

    m_initialLang = Translator::instance().language();

    auto *mainLay = new QVBoxLayout(this);

    // ── Onglets de configuration ───────────────────────────
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildLanguageTab(), TR_("config_tab_language"));
    // Pour ajouter d'autres categories :
    //   m_tabs->addTab(buildThemeTab(),  TR_("config_tab_theme"));
    //   m_tabs->addTab(buildPathsTab(),  TR_("config_tab_paths"));
    mainLay->addWidget(m_tabs);

    // ── Boutons OK / Annuler ───────────────────────────────
    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btnBox->button(QDialogButtonBox::Ok)->setText(TR_("btn_ok"));
    btnBox->button(QDialogButtonBox::Cancel)->setText(TR_("btn_cancel"));
    mainLay->addWidget(btnBox);

    connect(btnBox, &QDialogButtonBox::accepted,
            this, &ConfigDialog::onAccept);
    connect(btnBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
}

// ─────────────────────────────────────────────────────────────────
//  Onglet "Langue"
// ─────────────────────────────────────────────────────────────────
QWidget* ConfigDialog::buildLanguageTab()
{
    auto *page = new QWidget(this);
    auto *lay  = new QFormLayout(page);

    m_langCombo = new QComboBox(page);

    // Remplit avec les langues disponibles dans le .msg
    QStringList langs = Translator::instance().availableLanguages();
    if (langs.isEmpty())
        langs << "fr" << "en";

    for (const QString &code : std::as_const(langs)) {
        QString label = code;
        if      (code == "fr") label = "Francais";
        else if (code == "en") label = "English";
        else if (code == "de") label = "Deutsch";
        else if (code == "es") label = "Espanol";
        else if (code == "it") label = "Italiano";
        else                   label = code.toUpper();
        m_langCombo->addItem(label, code);
    }

    // Selectionne la langue active
    int idx = m_langCombo->findData(Translator::instance().language());
    if (idx >= 0) m_langCombo->setCurrentIndex(idx);

    lay->addRow(new QLabel(TR_("config_language_label"), page),
                m_langCombo);

    auto *note = new QLabel(TR_("config_language_note"), page);
    note->setWordWrap(true);
    note->setStyleSheet("color: #888; font-size: 10px;");
    lay->addRow(note);

    return page;
}

// ─────────────────────────────────────────────────────────────────
//  Validation
// ─────────────────────────────────────────────────────────────────
void ConfigDialog::onAccept()
{
    QSettings settings;

    // ── Langue ─────────────────────────────────────────────
    if (m_langCombo) {
        QString newLang = m_langCombo->currentData().toString();
        qDebug() << "[ConfigDialog] onAccept newLang=" << newLang
                 << " initialLang=" << m_initialLang;
        if (newLang != m_initialLang) {
            // Sauvegarde pour les prochains demarrages
            settings.setValue("language", newLang);
            settings.sync();
            // Application IMMEDIATE
            Translator::instance().setLanguage(newLang);
            qDebug() << "[ConfigDialog] langue appliquee, emission signal";
            // Le signal declenche le retranslateUi() de la MainWindow
            emit languageChanged(newLang);
        } else {
            qDebug() << "[ConfigDialog] langue inchangee, rien a faire";
        }
    }

    // [Sauvegarde d'autres reglages a ajouter ici]

    accept();
}
