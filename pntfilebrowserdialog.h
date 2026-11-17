#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  pntfilebrowserdialog.h — choix d'un fichier .pnt par le
//  gestionnaire d'EazyNC (voir eazyfiledialog.h)
// ═══════════════════════════════════════════════════════════════
#include <QDialog>
#include "eazyfiledialog.h"

class PntFileBrowserDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PntFileBrowserDialog(QWidget *parent = nullptr) : QDialog(parent), m_parent(parent) {}

    QString chooseExisting(const QString &rootDir)
    {
        return EazyFileDialog::ouvrir(m_parent, "Fichiers de points (.pnt)", rootDir, {"pnt"}, "PNT");
    }
    QString chooseNewName(const QString &rootDir)
    {
        return EazyFileDialog::enregistrer(m_parent, "Nouveau fichier de points", rootDir, ".pnt", "Points_1");
    }

private:
    QWidget *m_parent = nullptr;
};
