#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  eazyfiledialog.h — choix de fichier par le gestionnaire d'EazyNC
//
//  Remplace partout la fenêtre Windows (QFileDialog) : c'est la page
//  FICHIERS (style PGM MGT TNC) en mode « choix », avec le tri par type
//  du besoin (STEP, CNT, PNT, ENC…). SELECT. TYPE bascule entre ce tri
//  et tous les fichiers.
//
//   EazyFileDialog::ouvrir(parent, titre, dossier, {"cnt"}, "CNT")
//   EazyFileDialog::enregistrer(parent, titre, dossier, ".cnt", "nom")
// ═══════════════════════════════════════════════════════════════
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QFile>
#include "filebrowserpage.h"

class EazyFileDialog
{
public:
    static QString racine() { return QStringLiteral("C:/cncm/ncfiles"); }
    // Dossier du programme ouvert (rempli par la page Programme)
    static QString &dossierProgramme() { static QString d; return d; }

    // Choisir un fichier existant
    static QString ouvrir(QWidget *parent, const QString &titre, const QString &dossier,
                          const QStringList &extensions, const QString &libelle)
    {
        QDialog dlg(parent);
        dlg.setWindowTitle(titre);
        auto *lay = new QVBoxLayout(&dlg);
        auto *br = preparer(dlg, lay, dossier, extensions, libelle);
        QString choisi;
        auto *ligne = new QHBoxLayout;
        auto *bOuvrir = new QPushButton("Ouvrir");
        auto *bAnnuler = new QPushButton("Annuler");
        for (QPushButton *b : {bOuvrir, bAnnuler}) { b->setMinimumSize(150, 44); b->setStyleSheet("font-size:12pt;"); }
        bOuvrir->setStyleSheet("font-size:12pt;font-weight:bold;background:#2a7a2a;color:white;");
        ligne->addStretch(); ligne->addWidget(bOuvrir); ligne->addWidget(bAnnuler);
        lay->addLayout(ligne);
        QObject::connect(br, &FileBrowserPage::fichierChoisi, &dlg, [&](const QString &p){ choisi = p; dlg.accept(); });
        QObject::connect(bOuvrir, &QPushButton::clicked, br, &FileBrowserPage::onOpen);
        QObject::connect(bAnnuler, &QPushButton::clicked, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return {};
        return proposerCopie(parent, titre, choisi, dossier);
    }

    // Fichier pris hors du dossier programmes (clé USB, bureau…) : on
    // propose de le copier près du programme, pour qu'il reste trouvé
    static QString proposerCopie(QWidget *parent, const QString &titre, const QString &fichier, const QString &dossier)
    {
        const QString rac = QDir::cleanPath(racine());
        const QString f = QDir::cleanPath(QFileInfo(fichier).absoluteFilePath());
        if (fichier.isEmpty() || f.startsWith(rac + "/", Qt::CaseInsensitive)) return fichier;
        QString dest = rac;
        for (const QString &cand : {dossierProgramme(), dossier}) {      // programme en cours d'abord
            if (cand.isEmpty()) continue;
            const QFileInfo fi(cand);
            const QString d = QDir::cleanPath(fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath());
            if (QFileInfo(d).isDir()) { dest = d; break; }
        }
        const auto r = QMessageBox::question(parent, titre,
            QString::fromUtf8("Voulez-vous copier « %1 » vers le dossier du programme en cours ?\n\n"
                              "%2\n\n"
                              "Oui : copie (conseillé, le programme le retrouvera toujours)\n"
                              "Non : l'utiliser là où il est")
                .arg(QFileInfo(f).fileName(), QDir::toNativeSeparators(dest)),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
        if (r == QMessageBox::Cancel) return {};
        if (r == QMessageBox::No) return fichier;
        const QString cible = QDir(dest).filePath(QFileInfo(f).fileName());
        if (QFileInfo::exists(cible)) {
            if (QMessageBox::question(parent, titre,
                    QString::fromUtf8("« %1 » existe déjà dans ce dossier. Le remplacer ?\n(Non : garder celui qui y est)")
                        .arg(QFileInfo(cible).fileName())) != QMessageBox::Yes)
                return cible;
            QFile::remove(cible);
        }
        if (!QFile::copy(f, cible)) {
            QMessageBox::warning(parent, titre, QString::fromUtf8("Copie impossible, le fichier est utilisé là où il est."));
            return fichier;
        }
        return cible;
    }

    // Choisir un dossier + un nom pour enregistrer
    static QString enregistrer(QWidget *parent, const QString &titre, const QString &dossier,
                               const QString &extension, const QString &nomDefaut = QString())
    {
        QDialog dlg(parent);
        dlg.setWindowTitle(titre);
        auto *lay = new QVBoxLayout(&dlg);
        const QString ext = extension.startsWith('.') ? extension : "." + extension;
        auto *br = preparer(dlg, lay, dossier, {ext.mid(1)}, ext.mid(1).toUpper());
        auto *ligne = new QHBoxLayout;
        auto *lab = new QLabel("Nom :"); lab->setStyleSheet("font-size:12pt;font-weight:bold;");
        auto *nom = new QLineEdit(nomDefaut.isEmpty() ? QString() : QFileInfo(nomDefaut).completeBaseName());
        nom->setStyleSheet("font-size:13pt;padding:4px;");
        auto *labExt = new QLabel(ext.toUpper()); labExt->setStyleSheet("font-size:12pt;");
        auto *bOk = new QPushButton("Enregistrer");
        auto *bAnnuler = new QPushButton("Annuler");
        for (QPushButton *b : {bOk, bAnnuler}) b->setMinimumSize(150, 44);
        bOk->setStyleSheet("font-size:12pt;font-weight:bold;background:#2a7a2a;color:white;");
        bAnnuler->setStyleSheet("font-size:12pt;");
        ligne->addWidget(lab); ligne->addWidget(nom, 1); ligne->addWidget(labExt);
        ligne->addSpacing(20); ligne->addWidget(bOk); ligne->addWidget(bAnnuler);
        lay->addLayout(ligne);
        QString chemin;
        // clic sur un fichier existant : reprend son nom
        QObject::connect(br, &FileBrowserPage::fileSelected, &dlg, [nom](const QString &p, EazyFileType t){
            if (t != EazyFileType::Directory && t != EazyFileType::Unknown) nom->setText(QFileInfo(p).completeBaseName());
        });
        QObject::connect(br, &FileBrowserPage::fichierChoisi, &dlg, [nom](const QString &p){
            nom->setText(QFileInfo(p).completeBaseName());
        });
        auto valider = [&]{
            QString n = nom->text().trimmed();
            if (n.isEmpty()) { nom->setFocus(); return; }
            if (!n.endsWith(ext, Qt::CaseInsensitive)) n += ext;
            const QString p = QDir(br->dossierCourant()).filePath(n);
            if (QFileInfo::exists(p) &&
                QMessageBox::question(&dlg, titre, QString::fromUtf8("« %1 » existe déjà. Le remplacer ?").arg(n))
                    != QMessageBox::Yes) return;
            chemin = p;
            dlg.accept();
        };
        QObject::connect(bOk, &QPushButton::clicked, &dlg, valider);
        QObject::connect(nom, &QLineEdit::returnPressed, &dlg, valider);
        QObject::connect(bAnnuler, &QPushButton::clicked, &dlg, &QDialog::reject);
        nom->setFocus();
        if (dlg.exec() != QDialog::Accepted) return {};
        return chemin;
    }

private:
    static FileBrowserPage *preparer(QDialog &dlg, QVBoxLayout *lay, const QString &dossier,
                                     const QStringList &extensions, const QString &libelle)
    {
        lay->setContentsMargins(4, 4, 4, 4);
        auto *br = new FileBrowserPage(&dlg);
        br->setModeChoix(true);
        br->setRootPath(racine());
        br->setFiltre(extensions, libelle);
        if (!dossier.isEmpty()) {
            const QFileInfo fi(dossier);
            br->allerA(fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath());
        }
        lay->addWidget(br, 1);
        dlg.resize(1250, 760);
        return br;
    }
};
