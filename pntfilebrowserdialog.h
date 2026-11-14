#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  pntfilebrowserdialog.h
//
//  Sélection/création d'un fichier .pnt via l'explorateur INTERNE
//  d'EazyNC (FileBrowserPage, style PGM MGT TNC 620) — PAS la
//  fenêtre native Windows. Enveloppe FileBrowserPage filtré sur
//  EazyFileType::PNT dans une boîte de dialogue OK/Annuler.
// ═══════════════════════════════════════════════════════════════

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QInputDialog>
#include "filebrowserpage.h"

class PntFileBrowserDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PntFileBrowserDialog(QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowTitle("Fichiers de points (.pnt)");
        resize(820, 560);

        auto *lay = new QVBoxLayout(this);
        m_browser = new FileBrowserPage;
        lay->addWidget(m_browser, 1);

        auto *btnRow = new QHBoxLayout;
        auto *btnNew    = new QPushButton("Nouveau nom...");
        auto *btnOpen   = new QPushButton("Ouvrir");
        auto *btnCancel = new QPushButton("Annuler");
        btnRow->addWidget(btnNew);
        btnRow->addStretch();
        btnRow->addWidget(btnOpen);
        btnRow->addWidget(btnCancel);
        lay->addLayout(btnRow);

        connect(btnOpen, &QPushButton::clicked, m_browser, &FileBrowserPage::onOpen);
        connect(m_browser, &FileBrowserPage::openEncFile, this, [this](const QString &path){
            m_chosenPath = path;
            accept();
        });
        connect(btnNew, &QPushButton::clicked, this, [this]{
            bool ok;
            QString nm = QInputDialog::getText(this, "Nouveau fichier de points",
                                               "Nom du fichier :", QLineEdit::Normal,
                                               "Points_1", &ok);
            if (!ok || nm.isEmpty()) return;
            QString dir = m_browser->rootPath();
            m_chosenPath = dir + "/" + nm + ".pnt";
            accept();
        });
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    void setRootPath(const QString &path) { m_browser->setRootPath(path); }

    // Ouvre en mode "choisir un fichier .pnt existant"
    QString chooseExisting(const QString &rootDir)
    {
        setRootPath(rootDir);
        m_browser->setFilterType(EazyFileType::PNT);
        if (exec() == QDialog::Accepted) return m_chosenPath;
        return {};
    }

    // Ouvre en mode "choisir un nom pour un nouveau fichier .pnt"
    QString chooseNewName(const QString &rootDir)
    {
        setRootPath(rootDir);
        m_browser->setFilterType(EazyFileType::PNT);
        if (exec() == QDialog::Accepted) return m_chosenPath;
        return {};
    }

private:
    FileBrowserPage *m_browser = nullptr;
    QString          m_chosenPath;
};
