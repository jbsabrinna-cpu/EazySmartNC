#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  BlkModele3DWidget — onglet « Modèle 3D » du BLK FORM
//
//  Mise en place façon Fusion, dans la page du STEP (« Mise en place 3D ») :
//   1. Sens des axes : X+ / Y+ / Z+ puis clic sur une face de la boîte
//      pièce, le 3e axe est déduit. Ne déplace jamais l'origine.
//   2. Origine : point de la boîte du brut (27 points) ou point sur la
//      pièce (sommet, centre de rayon, face). Gardée dans le repère du STEP.
//   3. Brut : autour de la pièce ou dimensions fixes.
//  « Calculer et figer » remplit le BLK FORM et verrouille l'origine.
//
//  JSON (clé « modele3d » du BLK FORM) :
//   fichier, orient, origSrc ("boite" | "piece"), origBoite [i,j,k]
//   (0 min 1 milieu 2 max, axes du STEP), origPtModele [x,y,z] (STEP),
//   brutMode, brutSx, brutSy, brutZp, brutZm, brutL, brutW, brutH,
//   brutZal (0 pièce en haut, 1 en bas), verrou, bbMin, bbMax, origine,
//   brutMin, brutMax
//   + pour les blocs : origMode = 1, origPoint (repère machine), origRef3
// ═══════════════════════════════════════════════════════════════
#include <QWidget>
#include <QJsonObject>

class QLineEdit;
class QLabel;
class QPushButton;
struct CadDocument;

class BlkModele3DWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BlkModele3DWidget(QWidget *parent = nullptr);

    void        charger(const QJsonObject &d);
    QJsonObject donnees() const;
    bool        verrouille() const { return m_verrou; }

signals:
    void dataChanged();
    // limites du brut dans le repère pièce (à recopier dans le BLK FORM)
    void brutCalcule(double xm, double xp, double ym, double yp, double zm, double zp);
    void verrouChange(bool verrou);

private:
    void choisirFichier();
    void miseEnPlace();
    void calculerEtFiger(const CadDocument *doc = nullptr);
    void debloquer();
    void majEtat();
    // origine (machine) + brut (machine) + pièce (machine) ; écrit origMode/origPoint
    void calculer(const CadDocument &doc, double pMin[3], double pMax[3],
                  double o[3], double bMin[3], double bMax[3]);
    QString texteAxes() const;
    QString texteOrigine() const;
    QString texteBrut() const;

    bool m_chargement = false;
    bool m_verrou = false;
    QJsonObject m_d;

    QLineEdit *m_fichier = nullptr;
    QLabel *m_info = nullptr, *m_resume = nullptr, *m_resultat = nullptr, *m_etat = nullptr;
    QPushButton *m_btnChoisir = nullptr, *m_btnMep = nullptr, *m_btnFiger = nullptr, *m_btnDebloquer = nullptr;
};
