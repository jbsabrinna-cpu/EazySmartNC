#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  Usinage3DEditor — page de saisie du bloc « Usinage 3D »
//
//  Données du bloc (JSON) :
//    communes : fichier, origine / brut (BLK FORM figé), facesExclues
//               (cavités rouges), zFondsAuto, bbMin / bbMax ...
//    operations : liste ordonnée ; chacune = un cycle (type), un outil,
//               ses paramètres et SA zone (facesZone, contoursZone,
//               zBasModele, zBas, decoupeSous). Voir Usinage3DGenerator.
//    Un ancien bloc (modeUsin + outilEb/outilFin...) est converti à la lecture.
// ═══════════════════════════════════════════════════════════════
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>

class QLineEdit;
class QLabel;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QButtonGroup;
class QFormLayout;
class QPushButton;
class QListWidget;
class QToolButton;
class ModeUsinWidget;
class CadView3D;
class QTimer;
struct CadDocument;
#include <memory>

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
    // Vue 3D intégrée à la page (colonne de droite) : zone cliquée, couches en direct
    void        setVue(CadView3D *v);
    void        majVueMaintenant();
    // Barre des familles de cycles (posée au-dessus de la vue 3D par la page)
    QWidget    *barreFamilles();

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
    CadView3D *m_vue = nullptr;
    QTimer    *m_tmrVue = nullptr;
    std::shared_ptr<CadDocument> m_doc;
    QString    m_docChemin;
    bool       m_selAJour = false;         // zone de la vue à recharger depuis le bloc
    void majVue();
    void zoneDepuisVue();
    QJsonObject m_blk;
    QList<QWidget*> m_lignesOrig;          // section origine / brut propre au bloc (cachée)
    QPushButton *m_bParc = nullptr;
    QJsonObject m_d;                       // garde les clés non éditées ici (faces...)

    // ── Opérations ──
    QJsonArray   m_ops;                    // opérations du bloc (l'opération courante est dans les champs)
    int          m_opCour = -1;
    bool         m_majListe = false;
    QListWidget *m_listeOps = nullptr;
    QWidget     *m_grpEb = nullptr, *m_grpFin = nullptr, *m_grpCt = nullptr;
    class QFormLayout *m_formEb = nullptr;                      // pour masquer les lignes propres à l'ébauche
    class QPushButton *m_bCouches = nullptr;
    // Contournage
    class QCheckBox *m_cbFinCt = nullptr;
    QDoubleSpinBox  *m_avFinCt = nullptr, *m_rEntreeCt = nullptr;
    class QComboBox *m_sensCt = nullptr;
    QLabel      *m_titreOp = nullptr, *m_infoZoneOp = nullptr;
    QWidget     *m_barreFam = nullptr;
    QPushButton *m_bHaut = nullptr, *m_bBas = nullptr, *m_bDup = nullptr, *m_bSuppr = nullptr;
    void ajouterOperation(const QString &type);
    void ajouterEbaucheFinition();
    void choisirOperation(int i);
    void majListeOps();
    void sauverOpCourante();
    void chargerOp(const QJsonObject &op);
    QJsonObject opDepuisChamps(const QJsonObject &base) const;
    QJsonObject opCourante() const;
    QJsonObject opParDefaut(const QString &type) const;
    int  premiereOp(bool ebauche) const;
    QLineEdit *m_fichier = nullptr;
    QLabel    *m_infoFichier = nullptr;
    QLineEdit *m_outilEb = nullptr;  QLabel *m_nomEb = nullptr;
    QLineEdit *m_outilFin = nullptr; QLabel *m_nomFin = nullptr;
    QDoubleSpinBox *m_passeEb = nullptr, *m_surepLat = nullptr, *m_surepFond = nullptr;
    QDoubleSpinBox *m_passeFin = nullptr, *m_pasFin = nullptr;
    QComboBox *m_stratFin = nullptr;
    QDoubleSpinBox *m_angleSep = nullptr;
    class QCheckBox *m_cbProf = nullptr; QDoubleSpinBox *m_passeProf = nullptr;   // balayage : passes en profondeur                    // finition mixte : pente de séparation
    class QCheckBox *m_cbEb = nullptr, *m_cbFin = nullptr, *m_cbPave = nullptr;   // « Afficher »
    QPushButton *m_bModeZone = nullptr, *m_bModeExcl = nullptr;
    QPushButton *m_bRecalc = nullptr;
    bool m_couchesCachees = false;      // sélection en cours : couches masquées
    void cacherCouches();
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
