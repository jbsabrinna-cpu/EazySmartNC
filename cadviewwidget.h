#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  cadviewwidget.h — Visionneuse DXF/STEP/IGES pour EazyNC
//  Équivalent de la visionneuse DXF de la TNC 620 Heidenhain
// ═══════════════════════════════════════════════════════════════
#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QToolBar>
#include <QLabel>
#include <QComboBox>
#include <QListWidget>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStatusBar>
#include <QGraphicsRectItem>
#include <QFrame>
#include <QToolButton>
#include <QDoubleSpinBox>
#include "cadimporter.h"

// ───────────────────────────────────────────────────────────────
//  CadGraphicsView — QGraphicsView avec zoom/pan souris
// ───────────────────────────────────────────────────────────────
class CadGraphicsView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit CadGraphicsView(QWidget* parent=nullptr);

    void fitAll();  // Zoom pour voir tout le dessin

signals:
    void mouseMoved(QPointF scenePos);
    void mouseClicked(QPointF scenePos, Qt::MouseButton btn);
    void measureRequested(QPointF p1, QPointF p2);
    void rectSelected(QRectF sceneRect);   // cadre trace clic gauche maintenu

protected:
    void wheelEvent      (QWheelEvent*  e) override;
    void mousePressEvent (QMouseEvent*  e) override;
    void mouseMoveEvent  (QMouseEvent*  e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent   (QKeyEvent*    e) override;

private:
    bool    m_panning   = false;
    QPoint  m_panStart;
    bool    m_rDown     = false;   // clic droit : deplacement si on glisse
    QPoint  m_rStart;
    bool    m_rMoved    = false;
    void    panBy(const QPoint& deltaPx);

    // Mode mesure
    bool    m_measuring = false;
    QPointF m_measureP1;
    bool    m_hasP1     = false;
    QGraphicsLineItem* m_measureLine = nullptr;

    // Cadre de selection (clic gauche maintenu)
    bool    m_lpDown  = false;
    QPoint  m_lpStart;
    QGraphicsRectItem* m_rubber = nullptr;
};

// ───────────────────────────────────────────────────────────────
//  CadViewWidget — widget complet : toolbar + vue + panneau calques
// ───────────────────────────────────────────────────────────────
class CadViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CadViewWidget(QWidget* parent=nullptr);
    ~CadViewWidget() = default;

    // Charger un document (depuis CadImporter::load)
    void loadDocument(const CadDocument& doc);
    void loadFile(const QString& filePath);

    // Accès au document courant
    const CadDocument& document() const { return m_doc; }

    // Contours sélectionnés par l'utilisateur
    QVector<int> selectedContourIds() const;
    void clearSelection();

    // Définir l'origine (point de référence pièce)
    void setOrigin(const QPointF& pt);
    QPointF origin() const { return m_origin; }

    // Filtre par diamètre min/max
    void setDiameterFilter(double dmin, double dmax);
    void clearDiameterFilter();

signals:
    void contourSelected(int contourId, bool selected);
    void originChanged(QPointF newOrigin);
    void measureDone(double distance, QPointF p1, QPointF p2);
    void fileDropped(const QString& filePath);
    void statusMessage(const QString& msg);

public slots:
    void fitAll();
    void zoomIn();
    void zoomOut();
    void toggleLayer(const QString& layerName, bool visible);
    void toggleMeasureMode(bool on);
    void selectAll();
    void selectNone();
    void setOriginInteractive();  // prochain clic = nouvelle origine
    void filterByDiameter();      // ouvre un dialog
    void exportSelectedToCnt();   // export contour sélectionné → .cnt
    void exportSelectedToPnt();   // centres des cercles sélectionnés → .pnt (positions)
    void undoOrigin();            // annule le dernier point d'origine et relance la sélection

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private slots:
    void onMouseMoved(QPointF pos);
    void onMouseClicked(QPointF pos, Qt::MouseButton btn);
    void onMeasure(QPointF p1, QPointF p2);
    void onLayerVisibilityChanged(QListWidgetItem* item);
    void onOpenFile();
    void onRectSelected(QRectF sceneRect);

private:
    // ── UI ────────────────────────────────────────────────────
    void buildUi();
    void buildToolBar();
    void applyStyleSheet();

    // ── Dessin ────────────────────────────────────────────────
    void rebuildScene();
    void drawContour(const CadContour& c);
    void drawOriginMarker();
    QColor contourColor(const CadContour& c) const;
    QGraphicsPathItem* contourToItem(const CadContour& c);

    // ── Sélection ─────────────────────────────────────────────
    void toggleContourSelection(int id);
    void highlightContour(int id, bool selected);
    void updateSelectionInfo();

    // ── Panneau Ø (apres cadre) ───────────────────────────────
    void buildDiamPanel();
    void applyRectDiam();
    void closeDiamPanel(bool keep);
    bool           m_holeMode  = false;   // mode selection de percages
    bool           m_contourMode = false; // mode selection de contours
    QAction*       m_actLayers  = nullptr;
    QAction*       m_actContour = nullptr;
    QWidget*       m_rightPanel = nullptr;
    QLabel*        m_lblLayersTitle = nullptr;
    QLabel*        m_lblPosTitle = nullptr;
    QListWidget*   m_posList    = nullptr;
    QWidget*       m_posBtns    = nullptr;
    struct HolePos { QPointF p; double d; };
    QVector<HolePos>       m_pos;        // positions choisies (repere dessin)
    QList<QGraphicsItem*>  m_posItems;   // croix affichees
    void updateRightPanel();
    void redrawPositions();
    void refreshPosList();
    bool addPosition(const QPointF& p, double d);
    void removePosition(int i);
    const CadSegment* findSegAt(const QPointF& w, double tol, bool noCircle=false) const;
    // Contour facon TNC
    struct ChainEl { const CadSegment* s; bool rev; bool on = false; double a0 = 0.0; };
    QVector<ChainEl>       m_chain;
    QList<QGraphicsItem*>  m_chainItems;
    QToolButton*           m_btnClearList = nullptr;
    bool                   m_noListRefresh = false;
    QPointF chainEnd() const;
    bool    chainClosed() const;
    bool    inChain(const CadSegment* s) const;
    QVector<ChainEl> chainCandidates(const QPointF& p, const CadSegment* except) const;
    void    contourClick(const QPointF& w, double pxW);
    void    redrawChain();
    void    showInfoSeg(const CadSegment& s, bool rev=false);
    void    showInfoPos(int i);
    void    onListItemChanged(QListWidgetItem* it);
    void    onListRowChanged(int row);
    void    deleteFromRow(int row);
    void    reverseChain();
    // Saisie de l'origine a la main (facon TNC)
    QWidget*        m_originBox = nullptr;
    QDoubleSpinBox* m_spOx = nullptr;
    QDoubleSpinBox* m_spOy = nullptr;
    void    syncOriginFields();
    double  m_chainTol = 0.001;             // tolerance de chainage (mm), comme la TNC
    QDoubleSpinBox* m_spTol = nullptr;
    CadSegment chainSeg(const ChainEl& c) const;   // element oriente (cercle : depart a a0)
    // Unite du dessin : pouces -> mm (bouton mm/inch facon TNC)
    QAction* m_actInch = nullptr;
    bool     m_inch = false;
    QLabel*  m_lblUnit = nullptr;
    void     setInch(bool on);
    void     scaleDocument(double k);
    QToolButton* m_btnReverse = nullptr;
    QLabel* m_lblListHead = nullptr;
    QAction*       m_actDiam   = nullptr;
    QFrame*        m_diamPanel = nullptr;
    QLabel*        m_lblDmin   = nullptr;
    QLabel*        m_lblDmax   = nullptr;
    QLabel*        m_lblDcnt   = nullptr;
    QVector<double> m_rectDiams;            // diametres distincts dans le cadre (tries)
    QVector<QPair<int,double>> m_rectHoles; // (id contour, diametre) dans le cadre
    int            m_iMin = 0, m_iMax = 0;

    // ── Calques ───────────────────────────────────────────────
    void rebuildLayerList();
    bool isLayerVisible(const QString& name) const;

    // ── Membres ───────────────────────────────────────────────
    CadDocument     m_doc;
    QPointF         m_origin       = {0,0};
    bool            m_settingOrigin= false;
    bool            m_measureMode  = false;
    double          m_diamMin      = 0, m_diamMax = 1e9;
    bool            m_diamFilter   = false;

    // Calques visibles
    QSet<QString>   m_hiddenLayers;

    // Contours sélectionnés
    QSet<int>       m_selectedIds;

    // Map contourId → item graphique
    QMap<int, QGraphicsPathItem*> m_itemMap;

    // Marqueur d'origine
    QGraphicsItem*  m_originMarker = nullptr;

    // ── Accrochage du point d'origine (façon TNC) ─────────────
    struct SnapPt { QPointF p; QString kind; };
    QVector<SnapPt>        m_snapPts;       // points proposés pour l'élément cliqué
    QList<QGraphicsItem*>  m_snapItems;     // marqueurs affichés
    QVector<QPointF>       m_originHistory; // pour « Annuler origine »
    const CadSegment*      m_snapSeg = nullptr; // 1er élément cliqué (pointe dans m_doc)
    void showSnapsFor(const CadSegment& seg);
    static QVector<QPointF> intersectFull(const CadSegment& A, const CadSegment& B);
    void clearSnaps();

    // ── Widgets ───────────────────────────────────────────────
    QSplitter*          m_splitter      = nullptr;
    CadGraphicsView*    m_view          = nullptr;
    QGraphicsScene*     m_scene         = nullptr;
    QToolBar*           m_toolbar       = nullptr;
    QListWidget*        m_layerList     = nullptr;
    QLabel*             m_lblCoords     = nullptr;
    QLabel*             m_lblInfo       = nullptr;
    QLabel*             m_lblSelected   = nullptr;
    QAction*            m_actMeasure    = nullptr;
    QAction*            m_actOrigin     = nullptr;

    // Couleurs de sélection
    static constexpr QRgb COLOR_SELECTED   = 0xFFFF8800;  // orange
    static constexpr QRgb COLOR_ORIGIN     = 0xFF00FF00;  // vert
    static constexpr QRgb COLOR_MEASURE    = 0xFFFFFF00;  // jaune
};
