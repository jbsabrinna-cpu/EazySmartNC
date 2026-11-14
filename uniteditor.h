#pragma once
#include <QVector>
#include <QPointF>
#pragma execution_character_set("utf-8")

#include <QWidget>
#include <QButtonGroup>
#include "tooldatabase.h"
#include "contourelement.h"
#include "modeusin_widget.h"     // ★★★ CORRECTION : ajout indispensable ★★★
#include <QKeyEvent>
#include <functional>
#include <QStringList>
#include <QLineEdit>
#include <QRadioButton>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QTabWidget>
#include <QTableWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class UnitEditor; }
QT_END_NAMESPACE

class BadgeG;   // défini dans uniteditor.cpp

// ═══════════════════════════════════════════════════════════════
//  STRUCTURE POCHE — AVEC modeUsin
// ═══════════════════════════════════════════════════════════════
struct PocheData {
    double  spindleVal   = 0;
    double  descenteVal  = 0;
    double  pendulVal    = 0;   // avance pendulaire (rampe)
    double  avanceVal    = 0;
    double  DL = 0, DR = 0, Routil2 = 0;
    double  longueur     = 0;
    double  largeur      = 0;
    double  rayonAngle   = 0;
    double  diametre     = 0;
    double  profondeur   = 0;
    double  profPasse    = 0;
    double  surepLat     = 0;
    double  surepProf    = 0;
    double  passeFinition= 0;
    double  surepFinLat  = 0;   // surep. laterale finition : + laisse, - enleve plus
    double  surepFinProf = 0;   // surep. fond finition     : + laisse, - enleve plus
    double  avanceFinition  = 0;
    double  rayonEntreeSortie = 0;   // R lead-in/out finition (0 = rayon coin)
    double  posAngulaire   = 0;
    double  ligneP1X = 0, ligneP1Y = 0;
    double  ligneP2X = 0, ligneP2Y = 0;
    double  grilleOX = 0, grilleOY = 0, grilleOZ = 0;
    double  grillePasX = 10, grillePasY = 10;
    double  cercleCX = 0, cercleCY = 0;
    double  cercleRayon = 20;
    double  cercleZ = 0;
    double  cercleAngDep = 0, cercleAngInc = 90;
    double  approche     = 2.0;
    double  sautBride    = 50.0;
    double  zLiaison     = 5.0;     // Z liaison entre usinages
    double  avanceLiaison = 0.0;    // 0 = G0 rapide
    int     origine      = 0;        // 0=G54 .. 5=G59
    bool    origineDefinie = false;  // true = écrire G54-G59
    // Flags "local" : true = valeur redéfinie localement (pas héritée du BLK FORM)
    bool    approcheLocal    = false;
    bool    sautBrideLocal   = false;
    bool    zLiaisonLocal    = false;
    bool    avanceLiaisonLocal = false;
    bool    origineLocal     = false;
    double  positionnementF  = 0;
    double  posX = 0, posY = 0;
    QString toolRef;
    QString fonctionM1;
    QString fonctionM2;
    QString fichierPoints;
    struct TableauPoint { double x=0,y=0,z=0; bool actif=true; };
    QVector<TableauPoint> tableauPts = QVector<TableauPoint>(10);
    int     descenteMode = 0;
    int     pendulMode   = 0;   // mode avance pendulaire
    int     avanceMode   = 0;
    int     avanceFinMode   = 0;
    int     positionPoche  = 5;
    int     positionMode   = 0;
    int     ligneNbPts = 2;
    int     grilleNbX = 2, grilleNbY = 2;
    int     cercleNbPts = 4;
    int     recouvrement = 50;
    int     modeFraisage = 0;
    int     modePlongee  = 0;
    int     modeUsin     = 0;
    bool    isCirculaire = false;
    bool    toolByName   = false;
    bool    spindleByVc  = false;
    bool    brocheCW     = true;
    bool    positionnementG0 = true;
};

// ═══════════════════════════════════════════════════════════════
//  STRUCTURE TROU
// ═══════════════════════════════════════════════════════════════
struct TrouData {
    double  spindleVal   = 0;
    double  descenteVal  = 0;
    double  DL = 0, DR = 0;
    double  diametre     = 0;
    double  profondeur   = 0;
    double  degagement   = 2.0;
    // ── Perçage profond — cycle unique, 0 = fonction desactivee ──
    double  profPasse           = 0.0;   // increment de plongee (OBLIGATOIRE)
    double  reductionPasse      = 0.0;   // reduction par increment (0 = constante)
    double  passeMin            = 0.0;   // passe minimale (plancher reduction)
    double  profBriseCopeaux    = 0.0;   // profondeur brise-copeaux (0 = off)
    double  retraitBriseCopeaux = 0.2;   // recul casse-copeau (mm)
    double  avancePlongee       = 0.0;   // avance de plongee
    double  avancePrepos        = 0.0;   // avance de pre-positionnement
    double  avanceRetrait       = 0.0;   // avance de retrait
    double  tempoFond           = 0.0;   // temporisation en fond (s)
    double  approche     = 2.0;
    double  sautBride    = 50.0;
    double  zLiaison     = 5.0;
    double  avanceLiaison = 0.0;
    int     origine      = 0;
    bool    origineDefinie = false;
    bool    approcheLocal    = false;
    bool    sautBrideLocal   = false;
    bool    zLiaisonLocal    = false;
    bool    avanceLiaisonLocal = false;
    bool    origineLocal     = false;
    double  ligneP1X = 0, ligneP1Y = 0;
    double  ligneP2X = 0, ligneP2Y = 0;
    double  grilleOX = 0, grilleOY = 0, grilleOZ = 0;
    double  grillePasX = 10, grillePasY = 10;
    double  cercleCX = 0, cercleCY = 0;
    double  cercleRayon = 20;
    double  cercleAngDep = 0, cercleAngFin = 360;
    double  positionnementF  = 0;
    QString toolRef;
    QString fonctionM1;
    QString fonctionM2;
    QString fichierPoints;
    int     descenteMode = 0;
    int     cycle        = 0;
    int     positionMode = 0;
    int     ligneNbPts   = 2;
    int     grilleNbX    = 2, grilleNbY = 2;
    int     cercleNbPts  = 4;
    bool    toolByName   = false;
    bool    spindleByVc  = false;
    bool    brocheCW     = true;
    bool    traversant   = false;
    bool    positionnementG0 = true;
};

// ═══════════════════════════════════════════════════════════════
//  STRUCTURE CONTOUR
// ═══════════════════════════════════════════════════════════════
// Une ligne de la liste « Poche contour » : une poche ou un ilot
struct PocEntry {
    QString file;               // contour .cnt ferme
    bool    ilot       = false; // false = poche (on vide), true = ilot (reste)
    double  profondeur = 10.0;  // poche : profondeur totale depuis le dessus
                                // ilot  : hauteur de l'ilot sous son Z depart
    double  zDepart    = 0.0;   // ilot uniquement : Z du dessus de l'ilot
};

struct ContourData {
    double  spindleVal   = 0;
    double  descenteVal  = 0;
    double  pendulVal    = 0;   // avance pendulaire (rampe)
    double  avanceVal    = 0;
    double  avanceFinition  = 0;
    double  DL = 0, DR = 0, Routil2 = 0;
    double  profondeur   = 10.0;
    double  profPasse    = 3.0;
    double  surepLat     = 0.2;
    double  surepProf    = 0.1;
    double  passeFinition= 0.0;
    double  approche     = 2.0;
    double  sautBride    = 50.0;
    double  zLiaison     = 5.0;
    double  avanceLiaison = 0.0;
    int     origine      = 0;
    bool    origineDefinie = false;
    bool    approcheLocal    = false;
    bool    sautBrideLocal   = false;
    bool    zLiaisonLocal    = false;
    bool    avanceLiaisonLocal = false;
    bool    origineLocal     = false;
    double  origineX     = 0.0;
    double  origineY     = 0.0;
    double  origineZ     = 0.0;
    QString toolRef;
    QString fonctionM1;
    QString fonctionM2;
    QString contourName;
    QString contourFile;
    int     descenteMode = 0;
    int     pendulMode   = 0;   // mode avance pendulaire
    int     avanceMode   = 0;
    int     avanceFinMode   = 0;
    int     modeUsin     = 0;
    int     modeContour  = 0;
    int     modeFraisage = 0;
    int     modePlongee  = 0;
    int     correctionFraise = 40;
    // Position de l'outil par rapport au contour dessine (style TNC) :
    //  0 = Exterieur auto (profil exterieur + ebauche du brut, defaut)
    //  1 = R0 sur la ligne (gorge, rainure, gravure)
    //  2 = RL a gauche du sens de parcours
    //  3 = RR a droite du sens de parcours
    int     positionOutil = 0;
    bool    toolByName   = false;
    bool    spindleByVc  = false;
    bool    brocheCW     = true;

    // ── Plongée Z au point d'approche ─────────────────────────
    bool    plongeeRapide      = true;   // true = G0, false = G1 fDescente

    // ── Entrée / sortie matière (indépendantes) ───────────────
    // 0 = perpendiculaire, 1 = arc tangent, 2 = ligne tangente
    int     typeEntree         = 0;
    int     typeSortie         = 0;
    double  paramEntree        = 5.0;    // mm — longueur ou rayon selon type
    double  paramSortie        = 5.0;

    // ── Ébauche adaptative (Clipper) ──────────────────────────
    // Step-over = % du diamètre outil pour les passes d'ébauche
    // 60% = passes espacées de 0.6 * diamètre outil
    int     stepOverPct        = 60;

    // ── Stratégie de finition ─────────────────────────────────
    // 0 = Contour complet (passe entière du contour final)
    // 1 = Coins uniquement (resmat : finit les zones non atteintes
    //     par la fraise d'ébauche précédente)
    int     strategieFinition  = 0;

    // ── Finition multi-passes Z ───────────────────────────────
    // 1 = une seule passe au Z final (par défaut)
    // N = N passes Z descendantes vers le Z final
    int     nbPassesFinition   = 1;

    // ── Outil ébauche précédent (pour stratégie "Coins") ──────
    // Permet à EazyNC de calculer la zone résiduelle laissée
    // par cette fraise pour la reprendre avec la fraise actuelle
    QString toolRefEbauche;              // ex: "T1" (vide = non défini)
    bool    toolRefEbaucheByName = false;

    // ── BLK FORM (brut) - injecte par programmepage au moment ──
    //  de la generation. Utilise par l'ebauche adaptative pour
    //  calculer la zone a usiner (brut - contour_inflate).
    //  blkValid = true si les valeurs ont ete renseignees.
    bool    blkValid = false;
    double  blkXmin = 0, blkXmax = 0;
    double  blkYmin = 0, blkYmax = 0;

    // ── POCHE DE CONTOUR (unit dediee) ───────────────────────
    //  typeCycle : 0 = contournage, 1 = poche de contour avec ilots
    int         typeCycle     = 0;
    QStringList ilotFiles;              // (ancien format, lu pour compatibilite)
    QVector<PocEntry> pocList;          // liste poches / ilots, dans l'ordre (calques)
    int         pocPlongee    = 1;      // 0 directe, 1 helice, 2 rampe
    bool        entreeDefinie = false;  // plongee directe : point impose
    double      entreeX       = 0.0;
    double      entreeY       = 0.0;
};

// ═══════════════════════════════════════════════════════════════
//  STRUCTURE FRAISAGE DE TROU (alésage hélicoïdal)
//  Cycle G41 — compensation contrôleur (pas d'inline comme poches)
// ═══════════════════════════════════════════════════════════════
struct FraisageTrouData {
    // ── Géométrie ─────────────────────────────────────────────
    double  diametreFini    = 45.0;  // Ø trou fini (mm)
    double  diametreAvant   = 10.0;  // Ø avant-trou (mm)
    double  profondeur      = 15.0;  // profondeur (positive, mm)
    double  zSurface        = 0.0;   // Z surface matière

    // ── Hélice ────────────────────────────────────────────────
    double  pasHelicoidal   = 1.5;   // mm/tour
    double  angleRampeMaxi  = 30.0;  // ° — alerte si dépassé

    // ── Finition ──────────────────────────────────────────────
    double  surepLat        = 0.0;   // surépaisseur latérale ébauche (mm)
    double  passeFinition   = 0.0;   // passe finition Z (0 = tout en une passe)

    // ── Position ──────────────────────────────────────────────
    QString fichierPoints;          // chemin .pnt associé (vide = local au bloc)
    double  posX            = 0.0;
    double  posY            = 0.0;
    double  posZ            = 0.0;  // Z surface de départ pour ce point
    int     positionMode    = 0;    // 0=Point 1=Ligne 2=Grille 3=Cercle 4=Tableau
    struct TableauPoint { double x=0,y=0,z=0; };
    QVector<TableauPoint> tableauPts = QVector<TableauPoint>(10);
    // Ligne
    double  ligneP1X = 0, ligneP1Y = 0, ligneP2X = 0, ligneP2Y = 0;
    int     ligneNbPts = 1;
    // Grille
    double  grilleOX = 0, grilleOY = 0, grilleOZ = 0;
    double  grillePasX = 0, grillePasY = 0;
    int     grilleNbX = 1, grilleNbY = 1;
    // Cercle
    double  cercleCX = 0, cercleCY = 0, cercleZ = 0, cercleRayon = 0;
    int     cercleNbPts = 4;
    double  cercleAngDep = 0, cercleAngInc = 90;

    // ── Plans Z (héritage BLK FORM) ───────────────────────────
    double  approche        = 2.0;
    double  sautBride       = 50.0;
    double  zLiaison        = 5.0;
    double  avanceLiaison   = 0.0;
    bool    approcheLocal       = false;
    bool    sautBrideLocal      = false;
    bool    zLiaisonLocal       = false;
    bool    avanceLiaisonLocal  = false;
    int     origine         = 0;     // 0=G54..5=G59
    bool    origineDefinie  = false;
    bool    origineLocal    = false;

    // ── Outil ─────────────────────────────────────────────────
    QString toolRef;
    bool    toolByName      = false;

    // ── Broche ────────────────────────────────────────────────
    double  spindleVal      = 0.0;
    bool    spindleByVc     = false;
    bool    brocheCW        = true;  // true=M3(CW)

    // ── Avances ───────────────────────────────────────────────
    double  descenteVal     = 0.05;  // avance hélicoïdale (plongée)
    int     descenteMode    = 2;     // 0=mm/min 1=mm/tr 2=mm/dent
    double  avanceVal       = 0.08;  // avance contournage
    int     avanceMode      = 2;

    // ── Sens ──────────────────────────────────────────────────
    int     modeFraisage    = 0;     // 0=avalant 1=opposition
    int     modeFin         = 0;     // 0=Fin seule 1=Eb seule 2=Eb+Fin
    bool    descenteFinDirecte = false; // true=descente directe G3, false=hélicoïdale (Eb+Fin)

    // ── Fonctions M optionnelles ──────────────────────────────
    QString fonctionM1;
    QString fonctionM2;
};

struct SoftkeyInfo { QString label; QString value; bool active; };

// ═══════════════════════════════════════════════════════════════
//  CLASS UNITEDITOR
// ═══════════════════════════════════════════════════════════════
class UnitEditor : public QWidget
{
    Q_OBJECT
public:
    enum BlockMode {
        Poche,      // rétrocompat → alias PocheRect
        PocheRect,  // poche rectangulaire (sans radio rect/circ)
        PocheCirc,  // poche circulaire    (sans radio rect/circ)
        Trou,
        Contour,
        FraisageTrou  // alésage hélicoïdal G41
    };
    explicit UnitEditor(QWidget *parent = nullptr);
    ~UnitEditor();

    void      load(const PocheData &data);
    PocheData save() const;
    void      load(const TrouData &data);
    TrouData  saveTrou() const;
    void         load(const ContourData &data);
    ContourData  saveContour() const;
    void                 loadFraisageTrou(const FraisageTrouData &data);
    FraisageTrouData     saveFraisageTrou() const;

    void      setBlockMode(BlockMode mode);
    BlockMode blockMode() const { return m_blockMode; }
    void      setReadOnly(bool ro);
    // Reçoit les valeurs héritées du BLK FORM pour les badges G
    void      setBlkFormHerited(bool origineDefinie, int origine,
                                double zSautBride, double zLiaison,
                                double zApproche,  double avanceLiaison);
    bool      isReadOnly() const { return m_readOnly; }
    void      goToPositionMode(int stackIndex);
    QWidget*  posModeToWidget(int mode) const;
    int       posWidgetToMode(QWidget *w) const;
    void      updateFtPointsPreview();  // rafraîchit l'aperçu graphique live

    enum SoftkeyContext {
        SK_TOOL, SK_SPINDLE, SK_DESCENTE, SK_PENDUL, SK_AVANCE, SK_AVANCEFIN,
        SK_POSITIONNEMENT, SK_PLONGEE, SK_FRAISAGE, SK_POINTS,
        SK_POSITION_MODE,
        SK_CONTOUR_FILE, SK_NONE
    };
    SoftkeyContext currentSoftkeyContext() const { return m_skContext; }

signals:
    void softkeyContextChanged(UnitEditor::SoftkeyContext ctx,
                               QStringList labels, int activeIndex,
                               QString activeValue);
    void openToolTable(bool byName);
    void createNewPointFile();
    void openPointFile();
    void editPointFile(const QString &path);
    void openDxfImport();
    void openContourEditor(const QString &filePath, const QString &name);
    void dataChanged();
    void saveRequested();
    void helpRequested(const QString &svgKey);   // ★ aide contextuelle SVG

public slots:
    void onSoftkeyPressed(int index);
    void setToolFromTable(const QString &ref, bool byName);
    void setPointFile(const QString &path);
    void setContourFile(const QString &path, const QString &name);
    // Bascule la page PM Contour en mode « Poche de contour »
    void setPocheContourMode(bool on);
    void applyToolData(const ToolData &tool);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onTypeRectToggled(bool checked);
    void onSomTChanged(const QString &v);
    void onSomSChanged(const QString &v);
    void onSomDescenteChanged(const QString &v);
    void onSomAvanceChanged(const QString &v);
    void onSomLongueurChanged(const QString &v);
    void onSomLargeurChanged(const QString &v);
    void onSomRayonChanged(const QString &v);
    void onSomDiametreChanged(const QString &v);
    void onSomProfondeurChanged(const QString &v);
    void onSomPasseChanged(const QString &v);
    void onSomSurepLatChanged(const QString &v);
    void onSomSurepProfChanged(const QString &v);
    void onToolTChanged(const QString &v);
    void onToolSChanged(const QString &v);
    void onToolDescenteChanged(const QString &v);
    void onToolAvanceChanged(const QString &v);
    void onPMChanged();
    void onSomTFocused();    void onSomSFocused();
    void onSomDescFocused(); void onSomAvanceFocused();
    void onPMAvanceFinFocused(); void onGlobPositFocused();
    void onModelePtsFocused();
    void onContourFileFocused(); void onPlongeeFocused();
    void onFraisageFocused();
    void onCreerTableau(); void onInsererLignes(); void onAjouterLignes();
    void onPositionPocheClicked(int pos);
    void onTabChanged(int index);
    void onFocusLost();

private:
    Ui::UnitEditor *ui;
    BlockMode       m_blockMode      = Poche;
    bool            m_readOnly       = false;
    SoftkeyContext  m_skContext      = SK_NONE;
    int             m_modeS         = 0;
    int             m_modeDescente  = 0;
    int             m_modePendul    = 0;
    int             m_modeAvance    = 0;
    int             m_modeAvanceFin = 0;
    bool            m_posG0         = true;
    int             m_modePlongee   = 0;
    int             m_modeFraisage  = 0;

    // ★★★ MODE USINAGE ★★★
    ModeUsinWidget *m_modeUsin = nullptr;          // Panneau Poche
    int             m_modeUsinValue = 0;
    ModeUsinWidget *m_modeUsinContour = nullptr;   // Panneau Contour
    int             m_modeUsinContourValue = 0;
    class QSpinBox *m_cntStepOverPct = nullptr;    // Step-over % ebauche

    // ── Strategie de finition + outil ebauche precedent ─────
    //  Radio : Contour complet (0) / Coins uniquement (1, rest mach)
    //  toolRefEbauche : reference de l'outil precedent (ex "T1")
    QRadioButton  *m_cntRbFinitContour  = nullptr;
    QComboBox     *m_cntCbPosition      = nullptr;   // Exterieur / R0 / RL / RR
    QRadioButton  *m_cntRbFinitCoins    = nullptr;
    QLineEdit     *m_cntToolRefEbauche  = nullptr;
    QLabel        *m_cntLblToolEbauche  = nullptr;

    bool            m_toolByName    = false;
    QLabel         *m_lblToolName   = nullptr;  // nom outil, onglet Tool
    QLabel         *m_lblSomToolName= nullptr;  // nom outil, Sommaire
    QButtonGroup   *m_posGroup      = nullptr;
    bool            m_syncing       = false;
    int             m_trouTabIndex        = -1;
    int             m_contourTabIndex     = -1;
    int             m_fraisageTrouTabIndex= -1;
    // Pages des onglets PM insérés : les index bougent quand on insère
    // un onglet avant -> on les recalcule depuis les widgets.
    QWidget        *m_trouTabW = nullptr, *m_contourTabW = nullptr, *m_fraisageTrouTabW = nullptr;
    void            refreshPmTabIndexes();

    QWidget      *m_trouTab        = nullptr;
    QLineEdit    *m_trouProf       = nullptr;
    QCheckBox    *m_trouTraversant = nullptr;
    // ── Perçage : cycle unique, comportement pilote par les valeurs ──
    //  (0 = fonction desactivee ; profondeur de passe obligatoire)
    QLineEdit    *m_trouProfPasse     = nullptr;  // increment de plongee (obligatoire)
    QLineEdit    *m_trouReduction     = nullptr;  // reduction de passe (0 = constante)
    QLineEdit    *m_trouPasseMin      = nullptr;  // passe minimale (plancher reduction)
    QLineEdit    *m_trouProfBrise     = nullptr;  // profondeur brise-copeaux (0 = off)
    QLineEdit    *m_trouRetraitBrise  = nullptr;  // retrait brise-copeaux
    QLineEdit    *m_trouTempoFond     = nullptr;  // tempo fond (s)
    QLineEdit    *m_trouApproche  = nullptr;
    QLineEdit    *m_trouSautBride = nullptr;

    // ── Champs Global hérités du BLK FORM (créés dynamiquement) ──
    QLineEdit *m_editGlobZLiaison      = nullptr;
    QLineEdit *m_editGlobAvanceLiaison = nullptr;
    QCheckBox *m_chkGlobOrigine        = nullptr;
    QComboBox *m_cbGlobOrigine         = nullptr;

    // ── Badges G sur la poche (onglet Global) ──
    BadgeG *m_bgApproche      = nullptr;
    BadgeG *m_bgSautBride     = nullptr;
    BadgeG *m_bgZLiaison      = nullptr;
    BadgeG *m_bgAvanceLiaison = nullptr;
    BadgeG *m_bgOrigine       = nullptr;

    // ── Badges G sur Trou ──
    BadgeG *m_bgTrouApproche      = nullptr;
    BadgeG *m_bgTrouSautBride     = nullptr;
    BadgeG *m_bgTrouZLiaison      = nullptr;
    BadgeG *m_bgTrouAvanceLiaison = nullptr;
    BadgeG *m_bgTrouOrigine       = nullptr;
    QLineEdit *m_trouZLiaison      = nullptr;
    QLineEdit *m_trouAvanceLiaison = nullptr;
    QCheckBox *m_trouChkOrigine    = nullptr;
    QComboBox *m_trouCbOrigine     = nullptr;

    // ── Badges G sur Contour ──
    BadgeG *m_bgCntApproche      = nullptr;
    BadgeG *m_bgCntSautBride     = nullptr;
    BadgeG *m_bgCntZLiaison      = nullptr;
    BadgeG *m_bgCntAvanceLiaison = nullptr;
    BadgeG *m_bgCntOrigine       = nullptr;
    QLineEdit *m_cntZLiaison      = nullptr;
    QLineEdit *m_cntAvanceLiaison = nullptr;
    QCheckBox *m_cntChkOrigine    = nullptr;
    QComboBox *m_cntCbOrigine     = nullptr;
    // Valeurs héritées du BLK FORM (pour restauration)
    double m_heritedApproche     = 2.0;
    double m_heritedSautBride    = 50.0;
    double m_heritedZLiaison     = 5.0;
    double m_heritedAvanceLiaison= 0.0;
    int    m_heritedOrigine      = 0;
    bool   m_heritedOrigineDef   = false;

    // Contour
    QWidget      *m_contourTab      = nullptr;
    QLineEdit    *m_cntProfondeur   = nullptr;
    QLineEdit    *m_cntPasse        = nullptr;
    QLineEdit    *m_cntSurepLat     = nullptr;
    QLineEdit    *m_cntSurepProf    = nullptr;
    QLineEdit    *m_cntPasseFin     = nullptr;
    QLineEdit    *m_cntApproche     = nullptr;
    QLineEdit    *m_cntSautBride    = nullptr;
    QLabel       *m_cntNomFichier   = nullptr;
    QString       m_cntFilePath;
    QString       m_cntName;

    // Radios Sommaire FraisageTrou (créés dynamiquement, insérés dans frameDescente/frameAvance)
    QRadioButton *m_somFtRbG0        = nullptr;  // positionnement G0
    QRadioButton *m_somFtRbFpos      = nullptr;  // positionnement F valeur
    QRadioButton *m_somFtRbF         = nullptr;  // travail mm/min
    QRadioButton *m_somFtRbFz        = nullptr;  // travail mm/dent
    QRadioButton *m_somFtRbRpm       = nullptr;  // broche tr/min
    QRadioButton *m_somFtRbVc        = nullptr;  // broche Vc

    // Sommaire : accès rapide Point 1 / Point 2 (X/Y/Z)
    // Sommaire : accès rapide à 4 points (X/Y/Z), vides par défaut
    static const int kFtSomPts = 4;
    QLineEdit *m_somFtPX[kFtSomPts] = {nullptr,nullptr,nullptr,nullptr};
    QLineEdit *m_somFtPY[kFtSomPts] = {nullptr,nullptr,nullptr,nullptr};
    QLineEdit *m_somFtPZ[kFtSomPts] = {nullptr,nullptr,nullptr,nullptr};
    QLabel    *m_somFtLbl[kFtSomPts] = {nullptr,nullptr,nullptr,nullptr};
    QLabel    *m_somFtModeIcon = nullptr;  // icône mode position, dupliquée dans le Sommaire
    class PointsPreviewWidget *m_ftPointsPreview = nullptr;  // aperçu graphique live

    // Onglet Position : page dédiée FraisageTrou — 10 points, même
    // style que le Sommaire (remplace le mode "Tableau" générique,
    // redondant avec "Point" pour ce cycle).
    static const int kFtPosPts = 10;
    QWidget   *m_ftPositionPage = nullptr;
    QLabel    *m_lblPositionFichierActif = nullptr;  // message "positions gérées par fichier"
    QLineEdit *m_ftPosX[kFtPosPts] = {};
    QLineEdit *m_ftPosY[kFtPosPts] = {};
    QLineEdit *m_ftPosZ[kFtPosPts] = {};

    // Données FraisageTrou stockées en mémoire
    FraisageTrouData m_fraisageTrouData;

    // ── Fraisage trou : onglet PM ──────────────────────────────
    QWidget      *m_ftTab            = nullptr;
    // Broche
    QLineEdit    *m_ftS              = nullptr;
    QRadioButton *m_ftRbRpm          = nullptr;   // tr/min
    QRadioButton *m_ftRbVc           = nullptr;   // Vc m/min
    QRadioButton *m_ftRbM3           = nullptr;   // M3 CW
    QRadioButton *m_ftRbM4           = nullptr;   // M4 CCW
    // Géométrie
    QLineEdit    *m_ftDiamFini       = nullptr;
    QLineEdit    *m_ftDiamAvant      = nullptr;
    QLineEdit    *m_ftProfondeur     = nullptr;
    QLineEdit    *m_ftPasHelic       = nullptr;
    QLineEdit    *m_ftSurepLat       = nullptr;
    QLineEdit    *m_ftPasseFin       = nullptr;
    // Avance contournage (tour de finition/ébauche)
    QLineEdit    *m_ftAvance         = nullptr;
    QRadioButton *m_ftRbFmin         = nullptr;   // mm/min (F)
    QRadioButton *m_ftRbFz           = nullptr;   // mm/dent (Fz)
    // Avance plongée (descente hélicoïdale) — distincte de l'avance
    // de contournage. Avant : forcée = m_ftAvance (bug), maintenant
    // réglable indépendamment (ex : plongée plus prudente).
    QLineEdit    *m_ftDescente       = nullptr;
    QRadioButton *m_ftRbFminDesc     = nullptr;   // mm/min (F)
    QRadioButton *m_ftRbFzDesc       = nullptr;   // mm/dent (Fz)
    // Sens
    QRadioButton *m_ftRbAvalant      = nullptr;
    QRadioButton *m_ftRbOpposition   = nullptr;
    // Mode finition
    QRadioButton *m_ftRbModeFin      = nullptr;   // finition seule
    QRadioButton *m_ftRbModeEb       = nullptr;   // ébauche seule
    QRadioButton *m_ftRbModeEbFin    = nullptr;   // ébauche + finition
    QRadioButton *m_ftRbDescenteHelico  = nullptr;  // finition : descente hélicoïdale
    QRadioButton *m_ftRbDescenteDirecte = nullptr;  // finition : descente directe
    // Sécurités
    QLineEdit    *m_ftApproche       = nullptr;
    QLineEdit    *m_ftSautBride      = nullptr;
    QLineEdit    *m_ftZLiaison       = nullptr;
    QLineEdit    *m_ftAvanceLiaison  = nullptr;
    QRadioButton *m_ftRbLiaisonG0    = nullptr;   // G0
    QRadioButton *m_ftRbLiaisonF     = nullptr;   // F valeur
    QCheckBox    *m_ftChkOrigine     = nullptr;
    QComboBox    *m_ftCbOrigine      = nullptr;
    BadgeG       *m_bgFtApproche     = nullptr;
    BadgeG       *m_bgFtSautBride    = nullptr;
    BadgeG       *m_bgFtZLiaison     = nullptr;
    BadgeG       *m_bgFtAvanceLiaison= nullptr;
    BadgeG       *m_bgFtOrigine      = nullptr;

    // ── Nouveaux contrôles UI contour : plongée + entrée + sortie ──
    QRadioButton *m_cntRbPlongeeG0    = nullptr;
    QRadioButton *m_cntRbPlongeeF     = nullptr;
    QRadioButton *m_cntRbEntreePerp   = nullptr;
    QRadioButton *m_cntRbEntreeArc    = nullptr;
    QRadioButton *m_cntRbEntreeLigne  = nullptr;
    QRadioButton *m_cntRbSortiePerp   = nullptr;
    QRadioButton *m_cntRbSortieArc    = nullptr;
    QRadioButton *m_cntRbSortieLigne  = nullptr;
    QLineEdit    *m_cntParamEntree    = nullptr;
    QLineEdit    *m_cntParamSortie    = nullptr;
    // ── Poche de contour ─────────────────────────────────
    bool          m_pocMode           = false;
    QFormLayout  *m_cntForm           = nullptr;
    QList<QWidget*> m_cntOnlyRows;                 // lignes propres au contournage
    QList<QWidget*> m_pocOnlyRows;                 // lignes propres a la poche
    QTableWidget *m_pocTable          = nullptr;
    void              pocAddRow(const PocEntry &e);
    void              pocSetList(const QVector<PocEntry> &l);
    QVector<PocEntry> pocEntries() const;
    QRadioButton *m_pocRbDirecte      = nullptr;
    QRadioButton *m_pocRbHelice       = nullptr;
    QRadioButton *m_pocRbRampe        = nullptr;
    QCheckBox    *m_pocChkEntree      = nullptr;
    QLineEdit    *m_pocEntreeX        = nullptr;
    QLineEdit    *m_pocEntreeY        = nullptr;
    QLabel       *m_cntLblParamEntree = nullptr;
    QLabel       *m_cntLblParamSortie = nullptr;

    void prevTab();
    void nextTab();
    void focusNextField();
    void focusPrevField();

    BadgeG* installBadgeG(QWidget *field, const QString &tooltip,
                         std::function<void()> onReset);
    void setupBlkFormHeritedFields();   // poche
    void setupBlkFormHeritedFieldsTrou();    // trou
    void setupBlkFormHeritedFieldsContour(); // contour
    void applyStyleSheet();
    void setupConnections();
    void setupPositionPocheGroup();
    void setupPlongeeIcons();
    void setupPositionnementWidget();
    void setupTrouTab();
    void setupContourTab();
    void setupFraisageTrouTab();
    void setupToolSelectButton();
    void updatePocheTypeVisibility(bool isCirc);
    void setPendulVisible(bool visible);   // masque l'avance pendulaire (mode Trou)
    void updateBadgeS();
    void updateBadgeDescente();
    void updateBadgePendul();
    void onSomPendulChanged(const QString &v);
    void onSomPendulFocused();
    void updateBadgeAvance();
    void updateBadgeAvanceFin();
    void setSkContext(SoftkeyContext ctx, QStringList labels,
                      int activeIndex, const QString &activeValue);
    void emitDefaultContext();
    void installLeEventFilters();
    void updateSommairePosition();
};
