#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  Usinage3DEditor — page de saisie du bloc « Usinage 3D »
//
//  Données du bloc (JSON, clés) :
//    fichier      chemin du STEP / IGES
//    modeUsin     0 = Ébauche + Finition, 1 = Ébauche, 2 = Finition
//    outilEb      outil d'ébauche (numéro T)
//    passeEb      profondeur de passe d'ébauche (mm) → couches Z
//    recouvrement recouvrement d'ébauche (%)
//    surepLat / surepFond  surépaisseurs laissées par l'ébauche (mm)
//    outilFin     outil de finition (numéro T)
//    passeFin     profondeur de passe de finition (mm)
//    pasFin       pas latéral du balayage de finition (mm)
//    sEb / avanceEb / avPlongeeEb      broche, avance, avance plongée ébauche
//    modeSEb (0 S, 1 Vc) / modeAvanceEb, modeAvPlongeeEb (0 F, 1 Fu, 2 Fz)
//    idem ...Fin pour la finition
//    contoursZone boucles d'arêtes choisies [[x,y,z],...] -> limite de la zone
//    facesZone    faces choisies (n° de face B-rep)
//    zBas         Z bas de l'usinage (rempli au choix de la zone, modifiable)
//    decoupeSous  descendre encore de ... sous le Z bas (mm)
//    origPos 1..9 (pavé : 7 8 9 en haut), origRef 0 brut / 1 pièce,
//    origZ 0 dessus brut / 1 dessus pièce / 2 dessous pièce
//    brutMode 0 autour de la pièce / 1 dimensions fixes
//    brutSx brutSy brutZp brutZm (autour), brutL brutW brutH (fixe)
//    bbMin bbMax  encombrement du STEP ; origine, brutMin, brutMax (repère STEP)
//    zBasModele   Z bas dans le repère du STEP (zBas = repère pièce)
//    zFondsAuto   Z des faces planes horizontales (couches exprès), haut -> bas
// ═══════════════════════════════════════════════════════════════
#include <QWidget>
#include <QJsonObject>

class QLineEdit;
class QLabel;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QButtonGroup;
class QFormLayout;
class QPushButton;
class ModeUsinWidget;

class Usinage3DEditor : public QWidget
{
    Q_OBJECT
public:
    explicit Usinage3DEditor(QWidget *parent = nullptr);

    void        charger(const QJsonObject &d);
    // Modèle 3D du BLK FORM (clé « modele3d ») : s'il est figé, le bloc
    // prend son STEP, son sens des axes, son origine et son brut
    void        setModeleBlk(const QJsonObject &m);
    bool        blkFige() const { return m_blk["verrou"].toBool() && !m_blk["fichier"].toString().isEmpty(); }
    QJsonObject donnees() const;

signals:
    void dataChanged();
    // remplir le BLK FORM du programme : xMinus xPlus yMinus yPlus zMinus zPlus
    void remplirBlkForm(const QJsonObject &limites);
    void helpRequested(const QString &key);

private:
    void choisirFichier();
    void ouvrirVue3D();
    void majNomOutil(QLineEdit *t, QLabel *nom);
    void majCalculs();
    void majInfoFonds();
    bool assurerBBox();                    // charge l'encombrement du STEP si besoin
    void majOrigine();
    void voirCouches();                    // calcule et montre les couches d'ébauche                     // recalcule origine + brut, met à jour l'affichage
    QDoubleSpinBox *champ(double val, double mini, double maxi, int dec, const QString &suffixe);

    bool m_chargement = false;
    QJsonObject m_blk;
    QList<QWidget*> m_lignesOrig;          // section origine / brut propre au bloc (cachée)
    QPushButton *m_bParc = nullptr;
    QJsonObject m_d;                       // garde les clés non éditées ici (faces...)

    ModeUsinWidget *m_mode = nullptr;
    QLineEdit *m_fichier = nullptr;
    QLabel    *m_infoFichier = nullptr;
    QLineEdit *m_outilEb = nullptr;  QLabel *m_nomEb = nullptr;
    QLineEdit *m_outilFin = nullptr; QLabel *m_nomFin = nullptr;
    QDoubleSpinBox *m_passeEb = nullptr, *m_surepLat = nullptr, *m_surepFond = nullptr;
    QDoubleSpinBox *m_passeFin = nullptr, *m_pasFin = nullptr;
    QDoubleSpinBox *m_zBas = nullptr, *m_decoupe = nullptr;
    double m_zBasModele = 0.0;             // Z bas dans le repère du STEP
    // origine pièce + brut
    QButtonGroup *m_grpOrig = nullptr;
    QComboBox *m_refOrig = nullptr, *m_zOrig = nullptr, *m_modeBrut = nullptr;
    QDoubleSpinBox *m_bSx = nullptr, *m_bSy = nullptr, *m_bZp = nullptr, *m_bZm = nullptr;
    QDoubleSpinBox *m_bL = nullptr, *m_bW = nullptr, *m_bH = nullptr;
    QFormLayout *m_formBrut = nullptr;
    QLabel *m_infoBrut = nullptr;
    QLabel *m_infoCouches = nullptr;
    QSpinBox *m_recouvrement = nullptr;
    // paramètres de coupe d'un outil (valeur + mode S/Vc, F/Fu/Fz)
    struct Coupe3D {
        QLineEdit *t = nullptr;
        QDoubleSpinBox *s = nullptr, *f = nullptr, *fp = nullptr;
        QComboBox *ms = nullptr, *mf = nullptr, *mfp = nullptr;
        QLabel *calc = nullptr;
    };
    Coupe3D m_cEb, m_cFin;
};
