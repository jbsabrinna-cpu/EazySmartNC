#pragma once
#pragma execution_character_set("utf-8")

#include <QWidget>
#include <QSet>
#include <QListWidget>
#include <QSplitter>
#include <QStackedWidget>
#include <QVector>
#include <QJsonArray>
#include <QJsonObject>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLabel>
#include <QFrame>
#include <QPixmap>
#include <QTimer>
#include <QIcon>

#include "uniteditor.h"
#include "cadviewwidget.h"
#include "contoureditor.h"
#include "gcodeengine.h"

// ═══════════════════════════════════════════════════════════════
//  BlkFormWidget — 2 onglets : Dimensions + Paramètres généraux
// ═══════════════════════════════════════════════════════════════
class QTabWidget;
class QComboBox;
class QCheckBox;
class QRadioButton;
class QButtonGroup;
class QFrame;

class BlkFormWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BlkFormWidget(QWidget *parent = nullptr);

    // ── Onglet 1 : Dimensions ─────────────────────────────────
    double xMinus() const; double xPlus()  const;
    double yMinus() const; double yPlus()  const;
    double zMinus() const; double zPlus()  const;
    void   setValues(double xm, double xp,
                     double ym, double yp,
                     double zm, double zp);

    // ── Onglet 2 : Paramètres généraux (hérités par les blocs) ─
    bool   origineDefinie() const;  // true = écrire G54-G59 dans le G-code
    int    origine()       const;   // 0=G54 … 5=G59
    double zSautBride()    const;
    double zLiaison()      const;
    double zApproche()     const;
    double avanceLiaison() const;   // 0 = G0 (rapide)
    bool   m25RetourOrigine() const; // true=M25 (origine machine) false=parking (saut de bride seul)
    void   setParamsGlobaux(bool origineDefinie, int origine,
                            double zSautBride, double zLiaison,
                            double zApproche,  double avanceLiaison,
                            bool   m25RetourOrigine = true);

signals:
    void dataChanged();
    void axisFieldFocused(int axisFace);  // 0=none 1=Xmin 2=Xmax 3=Ymin 4=Ymax 5=Zmin 6=Zmax

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void setupUi();
    void updatePlusPrefix(QDoubleSpinBox *sb);
    QWidget* buildDimensionsTab();
    QWidget* buildParamsTab();

    QTabWidget     *m_tabs      = nullptr;
    // Dimensions
    QDoubleSpinBox *m_xMinus=nullptr, *m_xPlus=nullptr;
    QDoubleSpinBox *m_yMinus=nullptr, *m_yPlus=nullptr;
    QDoubleSpinBox *m_zMinus=nullptr, *m_zPlus=nullptr;
    // (m_chkDefineOrigin est déclaré plus bas avec les params généraux)
    // Paramètres généraux
    QCheckBox      *m_chkDefineOrigin = nullptr; // active G54-G59 dans G-code
    QComboBox      *m_cbOrigine      = nullptr;  // G54-G59
    QDoubleSpinBox *m_spZSautBride   = nullptr;
    QDoubleSpinBox *m_spZLiaison     = nullptr;
    QDoubleSpinBox *m_spZApproche    = nullptr;
    QDoubleSpinBox *m_spAvanceLiaison= nullptr;
    QRadioButton   *m_rbM25Origine   = nullptr;  // M25 - retour origine machine
    QRadioButton   *m_rbM25Parking   = nullptr;  // pas de M25 - saut de bride seul
};

// ─── Type de bloc ────────────────────────────────────────────
enum class BlockType {
    // En-tête
    BlkForm,
    // Poches
    PocheRect,      // Poche rectangulaire
    PocheCirc,      // Poche circulaire
    FraisageTrou,   // Fraisage de trou (alésage hélicoïdal G41)
    // Perçage
    Percage,        // Perçage simple G81
    PercageDebourr, // Perçage débourrant G83
    Pointage,       // Pointage G82
    Taraudage,      // Taraudage G84
    Alesage,        // Alésage G85
    // Fraisage
    Contour,        // Contour / profil
    PocheContour,   // Poche de contour avec ilots
    Surfacage,      // Surfaçage
    Rainure,        // Rainure rectangulaire
    RainureCirc,    // Rainure circulaire
    // Tenons
    TenonRect,      // Tenon rectangulaire
    TenonCirc,      // Tenon circulaire
    // Utilitaires (rétrocompat)
    Poche,          // ancien alias → PocheRect
    Trou            // ancien alias → Percage
};

// ─── Icône Qt pour chaque type ───────────────────────────────
inline QIcon blockIcon(BlockType t) {
    switch(t) {
    case BlockType::BlkForm:       return QIcon(":/unit/blk_form.png");
    case BlockType::PocheRect:     return QIcon(":/unit/fraisage de poche rectangulaire.png");
    case BlockType::PocheCirc:     return QIcon(":/unit/fraisage de poche circulaire.png");
    case BlockType::FraisageTrou:  return QIcon(":/unit/fraisage de trou.png");
    case BlockType::Percage:       return QIcon(":/unit/tous les cycle percage.png");
    case BlockType::PercageDebourr:return QIcon(":/unit/tous les cycle percage.png");
    case BlockType::Pointage:      return QIcon(":/unit/pointage.png");
    case BlockType::Taraudage:     return QIcon(":/unit/tous les cycle filet.png");
    case BlockType::Alesage:       return QIcon(":/unit/alesage a l'alesoir.png");
    case BlockType::Contour:       return QIcon(":/unit/cycle de contournage.png");
    case BlockType::PocheContour:  return QIcon(":/unit/fraisage de poche contour.png");
    case BlockType::Surfacage:     return QIcon(":/unit/surfacage.png");
    case BlockType::Rainure:       return QIcon(":/unit/rainure rectangulaire.png");
    case BlockType::RainureCirc:   return QIcon(":/unit/rainure circulaire.png");
    case BlockType::TenonRect:     return QIcon(":/unit/tenon rectangulaire.png");
    case BlockType::TenonCirc:     return QIcon(":/unit/tenon circulaire.png");
    case BlockType::Poche:         return QIcon(":/unit/fraisage de poche rectangulaire.png");
    case BlockType::Trou:          return QIcon(":/unit/tous les cycle percage.png");
    }
    return QIcon();
}

// ─── Label court pour chaque type ────────────────────────────
inline QString blockLabel(BlockType t) {
    switch(t) {
    case BlockType::BlkForm:        return "Matiere";
    case BlockType::PocheRect:      return "Poche rect.";
    case BlockType::PocheCirc:      return "Poche circ.";
    case BlockType::FraisageTrou:   return "Fraisage trou";
    case BlockType::Percage:        return "Percage";
    case BlockType::PercageDebourr: return "Percage debourr.";
    case BlockType::Pointage:       return "Pointage";
    case BlockType::Taraudage:      return "Taraudage";
    case BlockType::Alesage:        return "Alesage";
    case BlockType::Contour:        return "Contour";
    case BlockType::PocheContour:   return "Poche contour";
    case BlockType::Surfacage:      return "Surfacage";
    case BlockType::Rainure:        return "Rainure";
    case BlockType::RainureCirc:    return "Rainure circ.";
    case BlockType::TenonRect:      return "Tenon rect.";
    case BlockType::TenonCirc:      return "Tenon circ.";
    case BlockType::Poche:          return "Poche";
    case BlockType::Trou:           return "Trou";
    }
    return "?";
}

// ─── Données d'un bloc ───────────────────────────────────────
struct BlockData {
    int         id   = 0;
    BlockType   type = BlockType::BlkForm;
    QString     name;
    QJsonObject data;
};

// ═══════════════════════════════════════════════════════════════
//  ProgrammePage
// ═══════════════════════════════════════════════════════════════
class ProgrammePage : public QWidget
{
    Q_OBJECT
public:
    explicit ProgrammePage(QWidget *parent = nullptr);

    void    loadEncFile(const QString &filePath);
    bool    saveEncFile(const QString &filePath = {});
    void    saveFiles();
    QString currentEncPath() const { return m_currentPath; }

    UnitEditor* unitEditor() const { return m_editor; }

    // Insertions de blocs
    void insertBlkForm();
    void insertPocheRect();
    void insertPocheCirc();
    void insertFraisageTrou();
    void insertPercage();
    void insertPercageDebourr();
    void insertPointage();
    void insertTaraudage();
    void insertAlesage();
    void insertContour();
    void insertPocheContour();
    void insertSurfacage();
    void insertRainure();
    void insertRainureCirc();
    void insertTenonRect();
    void insertTenonCirc();
    // rétrocompat
    void insertPoche()   { insertPocheRect(); }
    void insertTrou()    { insertPercage(); }

    // Édition
    void copyBlock();
    void pasteBlock();
    void cutBlock();
    void deleteBlock();
    void insertBlock();

    // Mode sélection
    void beginSelection();
    void cancelSelection();
    void selectNextBlock();
    void selectPrevBlock();
    bool hasClipboard() const { return !m_clip.isEmpty(); }

signals:
    void clipboardChanged(bool hasClip);
    // Programme chargé depuis un .enc : pour la simulation seulement
    // (l'exécution reste mise à jour par « Enr enc »)
    void simGCodeReady(const QVector<BlockData> &blocks,
                       const QVector<GCodeLine> &gcode,
                       const QString &name);
    void gCodeReady(const QVector<BlockData>  &blocks,
                    const QVector<GCodeLine>  &gcode,
                    const QString             &name);

protected:
    void keyPressEvent(QKeyEvent *event) override;

public slots:
    void saveCurrentBlock();
    void showHelp(const QString &svgKey);
    void newProgram();

private slots:
    void onBlockSelected(int index);

private:
    void    setupUi();
    void    importCadFile(const QString &cadPath);
    void    addBlock(BlockData b);
    void    refreshList();
    void    showBlockInEditor(int index);
    void    updateSelectionHighlight();
    void    emitGCodeReady();
    QString labelForBlock(const BlockData &b, int pos = -1) const;
    // Helpers serialisation pour les nouveaux types
    // (réutilisent TrouData / PocheData avec modeUsinage adapté)

    QListWidget    *m_list       = nullptr;
    QStackedWidget *m_rightStack = nullptr;
    BlkFormWidget  *m_blkForm    = nullptr;
    UnitEditor     *m_editor     = nullptr;
    CadViewWidget  *m_cadView    = nullptr;
    QFrame         *m_helpFrame  = nullptr;
    QLabel         *m_helpLabel  = nullptr;
    QLabel         *m_helpTextLabel = nullptr;
    class BrutPreviewWidget *m_brutPreview = nullptr;

    QVector<BlockData> m_blocks;
    QLabel             *m_saveOverlay = nullptr;
    int                m_currentIndex = -1;
    int                m_nextId       = 1;
    QString            m_currentPath;

    bool      m_clipValid = false;
    BlockData m_clipboard;
    QVector<BlockData> m_clip;          // blocs copies/coupes (plusieurs possibles)
    int       m_selAnchor = -1;         // debut de la selection (facon TNC)
    QVector<int> selectedRowsSorted() const;
    void      setSelRangeTo(int row);

    bool      m_savingBlock = false;  // guard anti-boucle saveCurrentBlock
    QTimer   *m_saveTimer   = nullptr; // debounce save

    bool      m_selectionMode = false;
    QSet<int> m_selectedRows;
};
