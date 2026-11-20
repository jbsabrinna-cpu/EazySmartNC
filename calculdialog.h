#pragma once
// ═══════════════════════════════════════════════════════════════
//  calculdialog.h — fenêtre « Calcul du G-code » : fraise animée en
//  3D (couches d'ébauche autour d'un îlot), message, avancement réel
//  (Progression::*) et bouton Annuler. Fermée par terminer().
// ═══════════════════════════════════════════════════════════════
#include <QDialog>
#include <QStringList>
#include <QVector>
#include <QPointF>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

class AnimFraise3D : public QWidget
{
public:
    explicit AnimFraise3D(QWidget *parent = nullptr);
    void avancer();
protected:
    void paintEvent(QPaintEvent *) override;
private:
    void nouvelleCouche();
    int m_k = 0, m_i = 0;
    double m_ang = 0;
    QVector<QPointF> m_chemin, m_trace;
};

class CalculDialog : public QDialog
{
public:
    CalculDialog(const QStringList &nomsBlocs, const QVector<double> &poids, QWidget *parent = nullptr);
    void terminer();                // calcul fini (ou annulé) : la fenêtre se ferme
protected:
    void reject() override;         // Échap / croix = Annuler
private:
    void majTexte();
    QStringList   m_noms;
    QVector<double> m_poids;              // part de chaque bloc dans la barre
    AnimFraise3D *m_anim = nullptr;
    QLabel       *m_msg = nullptr, *m_detail = nullptr, *m_pct = nullptr;
    QProgressBar *m_barre = nullptr;
    QPushButton  *m_annuler = nullptr;
    QTimer       *m_timer = nullptr;
    int           m_tic = 0;
    double        m_max = 0;
    bool          m_fini = false;
};
