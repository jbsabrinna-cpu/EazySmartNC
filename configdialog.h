// configdialog.h
// ─────────────────────────────────────────────────────────────────
//  Fenetre de configuration d'EazyNC.
//
//  Concue pour etre EXTENSIBLE : chaque categorie de reglages est
//  un onglet (QTabWidget). Pour l'instant : onglet "Langue".
//  Ajouter un onglet = ajouter une methode buildXxxTab() et
//  l'enregistrer dans le constructeur.
//
//  Les reglages sont lus/ecrits via QSettings.
// ─────────────────────────────────────────────────────────────────
#pragma once

#include <QDialog>

class QTabWidget;
class QComboBox;

class ConfigDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ConfigDialog(QWidget *parent = nullptr);

signals:
    // Emis quand l'utilisateur a change la langue (apres OK).
    // MainWindow peut s'y connecter pour rafraichir / proposer
    // un redemarrage.
    void languageChanged(const QString &langCode);

private slots:
    void onAccept();

private:
    QWidget* buildLanguageTab();
    // Pour ajouter d'autres categories plus tard :
    //   QWidget* buildThemeTab();
    //   QWidget* buildPathsTab();
    //   QWidget* buildUnitsTab();

    QTabWidget *m_tabs       = nullptr;
    QComboBox  *m_langCombo  = nullptr;

    QString m_initialLang;   // langue au moment de l'ouverture
};
