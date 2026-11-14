// ═══════════════════════════════════════════════════════════════
//  cadviewwidget.cpp — Visionneuse DXF/STEP/IGES EazyNC
// ═══════════════════════════════════════════════════════════════
#include <QMap>
#include "cadviewwidget.h"
static QIcon cadIcon(const QString& k);
static bool g_iconLight = false;   // icone claire (pour le dessin sur fond sombre)
static CadSegment orientSeg(const CadSegment& s, bool rev);
static QString segSym(const CadSegment& s);
static QIcon elemIcon(int id, int kind, double d);
#include "contourelement.h"
#include "pointfile.h"
#include <QLineF>
#include <algorithm>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsPathItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QLineF>
#include <QGraphicsTextItem>
#include <QAction>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QListWidgetItem>
#include <QSizePolicy>
#include <QFont>
#include <QApplication>
#include <QCursor>
#include <QtMath>
#include <QTimer>
#include <QFile>
#include <QLocale>
#include <QGraphicsPixmapItem>
#include <QDir>
#include <QPixmap>
#include <QGraphicsPolygonItem>
#include <QGraphicsSimpleTextItem>
#include <QToolButton>
#include <QShortcut>
#include <QGridLayout>
#include <cmath>

// ═══════════════════════════════════════════════════════════════
//  CadGraphicsView
// ═══════════════════════════════════════════════════════════════
CadGraphicsView::CadGraphicsView(QWidget* parent)
    : QGraphicsView(parent)
{
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    // Deplacement libre : pas de barres, zone de scene tres grande
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSceneRect(-1.0e6, -1.0e6, 2.0e6, 2.0e6);
    setBackgroundBrush(QColor(18,18,28));  // fond sombre
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
}

void CadGraphicsView::fitAll()
{
    if(!scene()) return;
    QRectF r=scene()->itemsBoundingRect();
    if(r.isEmpty()) return;
    r.adjust(-r.width()*0.05,-r.height()*0.05,
             r.width()*0.05, r.height()*0.05);
    fitInView(r, Qt::KeepAspectRatio);
}

// Deplace la vue de deltaPx pixels (le dessin suit la souris)
void CadGraphicsView::panBy(const QPoint& deltaPx)
{
    const QPointF c = mapToScene(viewport()->rect().center());
    const QPointF c2 = mapToScene(viewport()->rect().center() - deltaPx);
    const QPointF d = c2 - c;
    const ViewportAnchor old = transformationAnchor();
    setTransformationAnchor(QGraphicsView::NoAnchor);
    centerOn(c + d);
    setTransformationAnchor(old);
}

void CadGraphicsView::wheelEvent(QWheelEvent* e)
{
    double factor = (e->angleDelta().y()>0) ? 1.15 : 1.0/1.15;
    scale(factor, factor);
    e->accept();
}

void CadGraphicsView::mousePressEvent(QMouseEvent* e)
{
    if(e->button()==Qt::MiddleButton){
        m_panning=true;
        m_panStart=e->pos();
        setCursor(Qt::ClosedHandCursor);
        e->accept(); return;
    }
    if(e->button()==Qt::LeftButton){
        QPointF sp=mapToScene(e->pos());
        if(m_measuring){
            if(!m_hasP1){
                m_measureP1=sp;
                m_hasP1=true;
                if(!m_measureLine){
                    m_measureLine=scene()->addLine(
                        sp.x(),sp.y(),sp.x(),sp.y(),
                        QPen(QColor(255,255,0),0,Qt::DashLine));
                    m_measureLine->setZValue(100);
                }
            } else {
                // Deuxième point — mesure terminée
                emit measureRequested(m_measureP1, sp);
                if(m_measureLine){ scene()->removeItem(m_measureLine);
                    delete m_measureLine; m_measureLine=nullptr; }
                m_hasP1=false;
            }
            e->accept(); return;
        }
        // Clic simple ou debut de cadre : decide au relachement
        m_lpDown=true;
        m_lpStart=e->pos();
        e->accept(); return;
    }
    if(e->button()==Qt::RightButton){
        // Decide au relachement : glisse = deplacement, sinon clic droit
        m_rDown=true; m_rMoved=false; m_rStart=e->pos(); m_panStart=e->pos();
        e->accept(); return;
    }
    QGraphicsView::mousePressEvent(e);
}

void CadGraphicsView::mouseMoveEvent(QMouseEvent* e)
{
    if(m_rDown && !m_rMoved && (e->pos()-m_rStart).manhattanLength()>4){
        m_rMoved=true; m_panning=true; setCursor(Qt::ClosedHandCursor);
    }
    if(m_panning){
        QPoint delta=e->pos()-m_panStart;
        m_panStart=e->pos();
        panBy(delta);
        e->accept(); return;
    }
    QPointF sp=mapToScene(e->pos());
    emit mouseMoved(sp);
    if(m_lpDown){
        if(!m_rubber && (e->pos()-m_lpStart).manhattanLength()>6){
            QPen p(QColor(255,255,255),0,Qt::DashLine); p.setCosmetic(true);
            m_rubber=scene()->addRect(QRectF(),p,QBrush(QColor(255,255,255,25)));
            m_rubber->setZValue(200);
        }
        if(m_rubber)
            m_rubber->setRect(QRectF(mapToScene(m_lpStart),sp).normalized());
    }
    if(m_measuring && m_hasP1 && m_measureLine){
        m_measureLine->setLine(m_measureP1.x(),m_measureP1.y(),sp.x(),sp.y());
    }
    QGraphicsView::mouseMoveEvent(e);
}

void CadGraphicsView::mouseReleaseEvent(QMouseEvent* e)
{
    if(e->button()==Qt::MiddleButton){
        m_panning=false;
        setCursor(Qt::ArrowCursor);
        e->accept(); return;
    }
    if(e->button()==Qt::RightButton && m_rDown){
        m_rDown=false;
        if(m_rMoved){ m_panning=false; setCursor(Qt::ArrowCursor); }
        else emit mouseClicked(mapToScene(m_rStart), Qt::RightButton);
        e->accept(); return;
    }
    if(e->button()==Qt::LeftButton && m_lpDown){
        m_lpDown=false;
        if(m_rubber){
            QRectF r=m_rubber->rect();
            scene()->removeItem(m_rubber); delete m_rubber; m_rubber=nullptr;
            emit rectSelected(r);
        } else {
            emit mouseClicked(mapToScene(m_lpStart), Qt::LeftButton);
        }
        e->accept(); return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void CadGraphicsView::keyPressEvent(QKeyEvent* e)
{
    if(e->key()==Qt::Key_Left)       panBy(QPoint( 60, 0));
    else if(e->key()==Qt::Key_Right) panBy(QPoint(-60, 0));
    else if(e->key()==Qt::Key_Up)    panBy(QPoint(0,  60));
    else if(e->key()==Qt::Key_Down)  panBy(QPoint(0, -60));
    else if(e->key()==Qt::Key_F) fitAll();
    else if(e->key()==Qt::Key_Plus||e->key()==Qt::Key_Equal) scale(1.2,1.2);
    else if(e->key()==Qt::Key_Minus) scale(1.0/1.2,1.0/1.2);
    else QGraphicsView::keyPressEvent(e);
}

// ═══════════════════════════════════════════════════════════════
//  CadViewWidget — construction UI
// ═══════════════════════════════════════════════════════════════
CadViewWidget::CadViewWidget(QWidget* parent)
    : QWidget(parent)
{
    setAcceptDrops(true);
    buildUi();
    applyStyleSheet();
}

void CadViewWidget::buildUi()
{
    auto* mainLayout=new QVBoxLayout(this);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->setSpacing(0);

    // ── Toolbar ───────────────────────────────────────────────
    buildToolBar();
    mainLayout->addWidget(m_toolbar);

    // ── Splitter : vue + panneau calques ─────────────────────
    m_splitter=new QSplitter(Qt::Horizontal,this);

    // Vue 2D
    m_scene=new QGraphicsScene(this);
    m_scene->setBackgroundBrush(QColor(18,18,28));
    m_view=new CadGraphicsView(this);
    m_view->setScene(m_scene);
    m_view->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);

    // Panneau droit : calques + infos
    auto* rightPanel=new QWidget(this);
    m_rightPanel=rightPanel;
    rightPanel->setFixedWidth(300);
    rightPanel->setObjectName("cadRight");
    rightPanel->setStyleSheet("#cadRight{background:#eceae6;border-left:1px solid #cfccc6;}");
    auto* rLayout=new QVBoxLayout(rightPanel);
    rLayout->setContentsMargins(4,4,4,4);
    rLayout->setSpacing(4);

    auto* lblLayers=new QLabel("Calques",rightPanel);
    m_lblLayersTitle=lblLayers;
    lblLayers->setStyleSheet("font-size:14px;font-weight:bold;color:#333;padding:2px 0;");
    rLayout->addWidget(lblLayers);

    m_layerList=new QListWidget(rightPanel);
    m_layerList->setStyleSheet(
        "QListWidget{background:#ffffff;color:#111;border:1px solid #d6d3cc;border-radius:6px;"
        "font-size:15px;}"
        "QListWidget::item{padding:5px 6px;}"
        "QListWidget::item:hover{background:#f3f2ef;}"
        "QListWidget::item:selected{background:#dbe7fb;color:#111;}"
        );
    m_layerList->setSelectionMode(QAbstractItemView::NoSelection);
    rLayout->addWidget(m_layerList,1);

    // Origine : valeurs modifiables a la main (repere du dessin)
    m_originBox=new QWidget(rightPanel);
    {
        auto* g=new QGridLayout(m_originBox); g->setContentsMargins(0,0,0,6); g->setSpacing(6);
        auto* t=new QLabel("Origine",m_originBox);
        t->setStyleSheet("font-size:15px;font-weight:bold;color:#222;");
        g->addWidget(t,0,0,1,3);
        auto mk=[&](const char* ax,int row)->QDoubleSpinBox*{
            auto* l=new QLabel(ax,m_originBox);
            l->setStyleSheet("font-family:Consolas,monospace;font-size:16px;font-weight:bold;color:#333;");
            auto* sp=new QDoubleSpinBox(m_originBox);
            sp->setLocale(QLocale::c());             // point decimal, comme la CN
            sp->setRange(-1.0e6,1.0e6); sp->setDecimals(4); sp->setSingleStep(1.0);
            sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
            sp->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
            sp->setKeyboardTracking(false);
            sp->setStyleSheet("QDoubleSpinBox{font-family:Consolas,monospace;font-size:16px;color:#111;"
                              "background:#ffffff;border:1px solid #d6d3cc;border-radius:6px;padding:4px 8px;}"
                              "QDoubleSpinBox:focus{border-color:#3b7ddd;}");
            g->addWidget(l,row,0); g->addWidget(sp,row,1,1,2);
            return sp;
        };
        m_spOx=mk("X",1); m_spOy=mk("Y",2);
        auto apply=[this]{
            const QPointF p(m_spOx->value(), m_spOy->value());
            if(QLineF(p,m_origin).length()<1e-9) return;
            m_originHistory << m_origin;
            setOrigin(p);
        };
        connect(m_spOx,&QDoubleSpinBox::editingFinished,this,apply);
        connect(m_spOy,&QDoubleSpinBox::editingFinished,this,apply);
    }
    m_originBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    rLayout->addWidget(m_originBox);

    // Liste des positions de percage
    m_lblPosTitle=new QLabel(rightPanel);
    m_lblPosTitle->setStyleSheet("font-size:17px;font-weight:bold;color:#222;padding:4px 0;");
    rLayout->addWidget(m_lblPosTitle);
    // En-tete de colonnes facon TNC
    m_lblListHead=new QLabel(" NC   ID   "+QString(QChar(0xC9))+"l"+QString(QChar(0xE9))+"ment",rightPanel);
    m_lblListHead->setStyleSheet("font-family:Consolas,monospace;font-size:13px;color:#555;"
                                 "background:transparent;border:none;padding:2px 2px;");
    rLayout->addWidget(m_lblListHead);
    // Cases a cocher bien visibles sur fond sombre (images generees)
    QString boxOn, boxOff;
    {
        const QString dir=QDir::tempPath();
        boxOn =dir+"/eazync_box_on.png";
        boxOff=dir+"/eazync_box_off.png";
        for(int k=0;k<2;++k){
            QPixmap pm(22,22); pm.fill(Qt::transparent);
            QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(k? QColor(59,125,221) : QColor(150,150,150),1.4));
            p.setBrush(QColor(255,255,255));
            p.drawRoundedRect(QRectF(2.5,2.5,17,17),3,3);
            if(k){
                p.setPen(QPen(QColor(59,125,221),2.4,Qt::SolidLine,Qt::RoundCap));
                p.drawLine(QPointF(6,6),QPointF(16,16));
                p.drawLine(QPointF(16,6),QPointF(6,16));
            }
            p.end();
            pm.save(k? boxOn : boxOff,"PNG");
        }
    }
    m_posList=new QListWidget(rightPanel);
    m_posList->setStyleSheet(
"QListWidget{background:#ffffff;color:#111;border:1px solid #d6d3cc;border-radius:6px;"
        "font-family:Consolas,monospace;font-size:16px;}"
        "QListWidget::item{padding:2px 4px;border-bottom:1px solid #eeece8;}"
        "QListWidget::item:selected{background:#dbe7fb;color:#111;}"
        "QListWidget::indicator{width:22px;height:22px;}"
        "QListWidget::indicator:unchecked{image:url("+boxOff+");}"
        "QListWidget::indicator:checked{image:url("+boxOn+");}");
    m_posList->setIconSize(QSize(92,30));
    rLayout->addWidget(m_posList,1);
    m_posBtns=new QWidget(rightPanel);
    {
        auto* hb=new QHBoxLayout(m_posBtns); hb->setContentsMargins(0,0,0,0); hb->setSpacing(4);
        auto* bDel=new QToolButton(m_posBtns);
        bDel->setText(QString(QChar(0x2716))); bDel->setToolTip("Effacer depuis la ligne choisie (Suppr)");
        bDel->setStyleSheet("QToolButton{color:#d01818;background:#f7f6f3;border:1px solid #d6d3cc;border-radius:6px;min-width:44px;min-height:32px;font-size:18px;font-weight:bold;}"
                            "QToolButton:hover{background:#fff;border-color:#a9c4ea;} QToolButton:pressed{background:#e3e9f3;}");
        auto* bAll=new QToolButton(m_posBtns);
        bAll->setIcon(cadIcon("undopos")); bAll->setIconSize(QSize(26,26));
        bAll->setToolTip("Effacer toutes les positions");
        bAll->setStyleSheet("QToolButton{background:#f7f6f3;border:1px solid #d6d3cc;border-radius:6px;min-width:44px;min-height:32px;}"
                            "QToolButton:hover{background:#fff;border-color:#a9c4ea;} QToolButton:pressed{background:#e3e9f3;}");
        auto* bRev=new QToolButton(m_posBtns);
        {
            QPixmap pm(30,26); pm.fill(Qt::transparent);
            QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
            QPen pn(QColor(20,90,210),2.4,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin); p.setPen(pn);
            p.drawLine(QPointF(5,8),QPointF(24,8));  p.drawLine(QPointF(24,8),QPointF(19,3));  p.drawLine(QPointF(24,8),QPointF(19,13));
            p.drawLine(QPointF(25,18),QPointF(6,18)); p.drawLine(QPointF(6,18),QPointF(11,13)); p.drawLine(QPointF(6,18),QPointF(11,23));
            p.end(); bRev->setIcon(QIcon(pm)); bRev->setIconSize(QSize(30,26));
        }
        bRev->setToolTip("Inverser le sens du contour");
        bRev->setStyleSheet("QToolButton{background:#f7f6f3;border:1px solid #d6d3cc;border-radius:6px;min-width:44px;min-height:32px;}"
                            "QToolButton:hover{background:#fff;border-color:#a9c4ea;} QToolButton:pressed{background:#e3e9f3;}");
        m_btnReverse=bRev;
        connect(bRev,&QToolButton::clicked,this,[this]{ reverseChain(); });
        hb->addWidget(bDel); hb->addWidget(bAll); hb->addWidget(bRev); hb->addStretch();
        connect(bDel,&QToolButton::clicked,this,[this]{ deleteFromRow(m_posList->currentRow()); });
        m_btnClearList=bAll;
        connect(bAll,&QToolButton::clicked,this,[this]{
            if(m_contourMode){ m_chain.clear(); redrawChain(); }
            else { m_pos.clear(); redrawPositions(); } });
    }
    rLayout->addWidget(m_posBtns);
    connect(m_posList,&QListWidget::currentRowChanged,this,&CadViewWidget::onListRowChanged);
    connect(m_posList,&QListWidget::itemChanged,this,&CadViewWidget::onListItemChanged);
    {
        auto* scDel=new QShortcut(QKeySequence::Delete,m_posList);
        connect(scDel,&QShortcut::activated,this,[this]{ deleteFromRow(m_posList->currentRow()); });
    }

    auto* lblSel=new QLabel("Slection",rightPanel);
    lblSel->setStyleSheet("font-size:13px;font-weight:bold;color:#333;padding:2px 0;");
    lblSel->hide();
    rLayout->addWidget(lblSel);

    m_lblSelected=new QLabel("-",rightPanel);
    m_lblSelected->setStyleSheet(
        "font-family:Consolas,monospace;font-size:15px;color:#111;background:#ffffff;"
        "border:1px solid #d6d3cc;border-radius:6px;padding:8px 10px;");
    m_lblSelected->setWordWrap(true);
    m_lblSelected->setAlignment(Qt::AlignLeft|Qt::AlignTop);
    m_lblSelected->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    rLayout->addWidget(m_lblSelected);
    rLayout->addStretch(0);      // prend la place libre quand les listes sont cachees

    m_splitter->addWidget(m_view);
    m_splitter->addWidget(rightPanel);
    QTimer::singleShot(0,this,[this]{ updateRightPanel(); refreshPosList(); });
    m_splitter->setStretchFactor(0,1);
    m_splitter->setStretchFactor(1,0);
    mainLayout->addWidget(m_splitter,1);

    // ── Barre de statut ───────────────────────────────────────
    auto* statusBar=new QWidget(this);
    statusBar->setFixedHeight(26);
    statusBar->setStyleSheet("background:#eceae6;border-top:1px solid #cfccc6;");
    auto* sLayout=new QHBoxLayout(statusBar);
    sLayout->setContentsMargins(8,0,8,0);
    sLayout->setSpacing(16);

    m_lblCoords=new QLabel("X: 0.000  Y: 0.000",statusBar);
    m_lblCoords->setStyleSheet("font-size:13px;color:#111;font-family:Consolas,monospace;");
    m_lblInfo=new QLabel("",statusBar);
    m_lblInfo->setStyleSheet("font-size:13px;color:#111;");

    sLayout->addWidget(m_lblCoords);
    sLayout->addStretch();
    sLayout->addWidget(m_lblInfo);
    // Cases d'etat a droite, comme la TNC : 2D | MM | 4 decimales | XY
    for(const char* t : {"2D","MM","4","XY"}){
        auto* b=new QLabel(t,statusBar);
        if(QString(t)=="MM") m_lblUnit=b;
        b->setStyleSheet("font-family:Consolas,monospace;font-size:12px;color:#333;"
                         "border:1px solid #d6d3cc;border-radius:4px;background:#ffffff;padding:0 6px;");
        sLayout->addWidget(b);
    }
    mainLayout->addWidget(statusBar);

    // ── Connexions ────────────────────────────────────────────
    connect(m_view, &CadGraphicsView::mouseMoved,
            this,   &CadViewWidget::onMouseMoved);
    connect(m_view, &CadGraphicsView::mouseClicked,
            this,   &CadViewWidget::onMouseClicked);
    connect(m_view, &CadGraphicsView::measureRequested,
            this,   &CadViewWidget::onMeasure);
    connect(m_view, &CadGraphicsView::rectSelected,
            this,   &CadViewWidget::onRectSelected);
    connect(m_layerList, &QListWidget::itemChanged,
            this, &CadViewWidget::onLayerVisibilityChanged);
}

// ─────────────────────────────────────────────────────────────
//  Icônes de la barre d'outils, dessinées dans le code (pas de
//  fichier image à fournir) : traits clairs sur fond sombre.
// ─────────────────────────────────────────────────────────────
static QIcon cadIcon(const QString& k)
{
    const int S = 64;
    QPixmap pm(S, S); pm.fill(Qt::transparent);
    QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
    const QColor fg = g_iconLight ? QColor(216, 228, 255) : QColor(45, 45, 50);
    const QColor blue(g_iconLight ? QColor(77, 163, 255) : QColor(20, 100, 210)), green(g_iconLight ? QColor(60, 220, 110) : QColor(20, 150, 60)),
                 orange(255, 154, 51), red(255, 80, 80);
    auto pen = [&](const QColor& c, double w){ QPen q(c, w); q.setCapStyle(Qt::RoundCap); q.setJoinStyle(Qt::RoundJoin); p.setPen(q); p.setBrush(Qt::NoBrush); };
    auto datum = [&](QPointF c, double r){            // symbole d'origine Heidenhain
        pen(fg, 3); p.drawEllipse(c, r, r);
        p.setPen(Qt::NoPen); p.setBrush(fg);
        p.drawPie(QRectF(c.x()-r, c.y()-r, 2*r, 2*r), 0*16, 90*16);
        p.drawPie(QRectF(c.x()-r, c.y()-r, 2*r, 2*r), 180*16, 90*16);
        pen(fg, 2); p.drawLine(QPointF(c.x()-r-6, c.y()), QPointF(c.x()+r+6, c.y()));
        p.drawLine(QPointF(c.x(), c.y()-r-6), QPointF(c.x(), c.y()+r+6));
    };
    auto magnifier = [&](){ pen(fg, 4); p.drawEllipse(QPointF(26, 26), 16, 16); pen(fg, 7); p.drawLine(QPointF(38, 38), QPointF(54, 54)); };
    auto arrowDown = [&](QPointF top, double len, const QColor& c){
        pen(c, 4); p.drawLine(top, top + QPointF(0, len));
        p.drawLine(top + QPointF(0, len), top + QPointF(-7, len - 7));
        p.drawLine(top + QPointF(0, len), top + QPointF(7, len - 7));
    };
    auto disk = [&](double x, double y, double w){   // disquette (enregistrer)
        const double h = w;
        pen(fg, 3); p.setBrush(QColor(45, 45, 50, 30));
        p.drawPolygon(QPolygonF() << QPointF(x, y) << QPointF(x + w - 6, y) << QPointF(x + w, y + 6)
                                  << QPointF(x + w, y + h) << QPointF(x, y + h));
        p.setBrush(fg); p.setPen(Qt::NoPen);
        p.drawRect(QRectF(x + w*0.25, y + 2, w*0.42, h*0.28));          // volet métallique
        pen(fg, 2.5); p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(x + w*0.18, y + h*0.55, w*0.64, h*0.38));     // étiquette
    };
    if (k == "open") {
        // Dossier ouvert d'où sort un dessin technique (cercle + traits d'axe)
        pen(fg, 3); p.setBrush(QColor(45, 45, 50, 25));
        p.drawPolygon(QPolygonF() << QPointF(4,22) << QPointF(20,22) << QPointF(25,28) << QPointF(54,28) << QPointF(54,56) << QPointF(4,56));
        p.setBrush(QColor(26, 26, 46));
        p.drawRect(QRectF(16, 6, 32, 30));                                  // la feuille
        pen(orange, 3); p.drawEllipse(QPointF(32, 21), 8, 8);
        QPen ax(orange, 1.5, Qt::DashDotLine); p.setPen(ax);
        p.drawLine(QPointF(20, 21), QPointF(44, 21)); p.drawLine(QPointF(32, 9), QPointF(32, 33));
        pen(fg, 3); p.setBrush(QColor(60, 66, 100));
        p.drawPolygon(QPolygonF() << QPointF(4,56) << QPointF(12,36) << QPointF(60,36) << QPointF(54,56));   // rabat ouvert
    } else if (k == "fit") {
        pen(blue, 3); p.drawRect(QRectF(20, 20, 24, 24));
        pen(fg, 4);
        for (QPointF c : { QPointF(8,8), QPointF(56,8), QPointF(8,56), QPointF(56,56) }) {
            const double sx = c.x() < 32 ? 1 : -1, sy = c.y() < 32 ? 1 : -1;
            p.drawLine(c, c + QPointF(10*sx, 0)); p.drawLine(c, c + QPointF(0, 10*sy));
        }
    } else if (k == "zin" || k == "zout") {
        magnifier(); pen(blue, 4); p.drawLine(QPointF(18, 26), QPointF(34, 26));
        if (k == "zin") p.drawLine(QPointF(26, 18), QPointF(26, 34));
    } else if (k == "mminch") {
        QFont f("Consolas"); f.setBold(true); f.setPixelSize(20); p.setFont(f);
        p.setPen(red); p.drawText(QRectF(0,4,64,26),Qt::AlignCenter,"mm");
        p.setPen(QPen(fg,2)); p.drawLine(QPointF(12,32),QPointF(52,32));
        p.setPen(fg); p.drawText(QRectF(0,34,64,26),Qt::AlignCenter,"inch");
    } else if (k == "origin") {
        datum(QPointF(32, 32), 14);
    } else if (k == "undoorigin") {
        datum(QPointF(32, 32), 14);                    // même symbole que « Origine »...
        pen(QColor(26, 26, 46), 9);                    // ...barré d'une croix rouge par-dessus
        p.drawLine(QPointF(12, 12), QPointF(52, 52)); p.drawLine(QPointF(52, 12), QPointF(12, 52));
        pen(red, 5);
        p.drawLine(QPointF(12, 12), QPointF(52, 52)); p.drawLine(QPointF(52, 12), QPointF(12, 52));
    } else if (k == "measure") {
        pen(fg, 3); p.drawLine(QPointF(8, 20), QPointF(8, 50)); p.drawLine(QPointF(56, 20), QPointF(56, 50));
        pen(blue, 3); p.drawLine(QPointF(10, 35), QPointF(54, 35));
        p.drawLine(QPointF(10, 35), QPointF(18, 30)); p.drawLine(QPointF(10, 35), QPointF(18, 40));
        p.drawLine(QPointF(54, 35), QPointF(46, 30)); p.drawLine(QPointF(54, 35), QPointF(46, 40));
    } else if (k == "layers") {
        // 3 feuilles cornées superposées, de couleurs différentes
        const QColor cols[3] = { orange, green, blue };
        for (int n = 0; n < 3; ++n) {
            const double x = 6 + n * 9, y = 20 - n * 7, w = 30, h = 36, f = 9;   // f = coin corné
            const QColor c = cols[n];
            QPolygonF sheet; sheet << QPointF(x, y) << QPointF(x + w - f, y) << QPointF(x + w, y + f)
                                   << QPointF(x + w, y + h) << QPointF(x, y + h);
            QColor fill = c; fill.setAlpha(90);
            pen(c, 3); p.setBrush(QColor(26, 26, 46)); p.drawPolygon(sheet);   // fond opaque : masque la feuille de dessous
            p.setBrush(fill); p.drawPolygon(sheet);
            QPolygonF corner; corner << QPointF(x + w - f, y) << QPointF(x + w - f, y + f) << QPointF(x + w, y + f);
            p.setBrush(c); p.drawPolygon(corner);                            // le coin replié
        }
    } else if (k == "selall" || k == "selnone") {
        QPen d(fg, 2, Qt::DashLine); p.setPen(d); p.setBrush(Qt::NoBrush); p.drawRect(QRectF(6, 6, 52, 52));
        pen(orange, 4); p.drawEllipse(QPointF(22, 24), 7, 7); p.drawLine(QPointF(32, 44), QPointF(50, 30));
        if (k == "selnone") { pen(red, 5); p.drawLine(QPointF(14, 14), QPointF(50, 50)); p.drawLine(QPointF(50, 14), QPointF(14, 50)); }
    } else if (k == "diam") {
        pen(fg, 4); p.drawEllipse(QPointF(32, 32), 20, 20);
        pen(blue, 4); p.drawLine(QPointF(14, 50), QPointF(50, 14));
    } else if (k == "cnt") {
        pen(blue, 4);
        p.drawPolyline(QPolygonF() << QPointF(6, 44) << QPointF(6, 8) << QPointF(24, 8));
        p.drawArc(QRectF(14, 8, 20, 20), 90*16, -90*16);
        p.drawLine(QPointF(34, 18), QPointF(34, 30));
        disk(32, 32, 28);
    } else if (k == "undocontour" || k == "undopos") {
        // Annuler la sélection : même dessin que le mode, barré d'une croix rouge
        if (k == "undocontour") {
            pen(blue, 5);
            p.drawPolyline(QPolygonF() << QPointF(12, 54) << QPointF(12, 12) << QPointF(34, 12));
            p.drawArc(QRectF(22, 12, 24, 24), 90*16, -90*16);
            p.drawLine(QPointF(46, 24), QPointF(46, 54));
        } else {
            p.setPen(Qt::NoPen); p.setBrush(blue);
            for (QPointF c : { QPointF(18,18), QPointF(46,18), QPointF(18,46), QPointF(46,46) }) p.drawEllipse(c, 9, 9);
        }
        pen(QColor(26, 26, 46), 9);
        p.drawLine(QPointF(12, 12), QPointF(52, 52)); p.drawLine(QPointF(52, 12), QPointF(12, 52));
        pen(red, 5);
        p.drawLine(QPointF(12, 12), QPointF(52, 52)); p.drawLine(QPointF(52, 12), QPointF(12, 52));
    } else if (k == "selcontour") {
        // Mode « Sélectionner un contour » : le contour seul, en grand
        pen(blue, 5);
        p.drawPolyline(QPolygonF() << QPointF(12, 54) << QPointF(12, 12) << QPointF(34, 12));
        p.drawArc(QRectF(22, 12, 24, 24), 90*16, -90*16);
        p.drawLine(QPointF(46, 24), QPointF(46, 54));
    } else if (k == "selpos") {
        // Mode « Sélectionner des positions » : les 4 points seuls, en grand
        p.setPen(Qt::NoPen); p.setBrush(blue);
        for (QPointF c : { QPointF(18,18), QPointF(46,18), QPointF(18,46), QPointF(46,46) }) p.drawEllipse(c, 9, 9);
    } else if (k == "pnt") {
        p.setPen(Qt::NoPen); p.setBrush(blue);
        for (QPointF c : { QPointF(10,10), QPointF(28,10), QPointF(10,28), QPointF(28,28) }) p.drawEllipse(c, 6, 6);
        disk(32, 32, 28);
    }
    p.end();
    return QIcon(pm);
}

void CadViewWidget::buildToolBar()
{
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize({34, 34});
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);   // icônes seules, nom en bulle d'aide
    m_toolbar->setStyleSheet(
        "QToolBar{background:#eceae6;border-bottom:1px solid #cfccc6;spacing:4px;padding:5px;}"
        "QToolBar::separator{background:#cfccc6;width:1px;margin:6px 4px;}"
        "QToolButton{background:#f7f6f3;border:1px solid #d6d3cc;border-radius:6px;padding:4px;}"
        "QToolButton:hover{background:#ffffff;border-color:#a9c4ea;}"
        "QToolButton:pressed{background:#e3e9f3;}"
        "QToolButton:checked{background:#dbe7fb;border:1px solid #3b7ddd;}");

    auto addBtn = [&](const QString& icon, const QString& tip, bool chk=false) -> QAction* {
        QAction* a = m_toolbar->addAction(cadIcon(icon), tip);
        a->setToolTip(tip);
        a->setCheckable(chk);
        return a;
    };

    // Fichier
    connect(addBtn("open", "Ouvrir un DXF"), &QAction::triggered, this, &CadViewWidget::onOpenFile);
    m_toolbar->addSeparator();

    // Affichage
    connect(addBtn("fit",  "Tout voir (F)"), &QAction::triggered, this, &CadViewWidget::fitAll);
    connect(addBtn("zin",  "Zoom +"),        &QAction::triggered, this, &CadViewWidget::zoomIn);
    connect(addBtn("zout", "Zoom -"),        &QAction::triggered, this, &CadViewWidget::zoomOut);
    m_actLayers = addBtn("layers", "Afficher / masquer la liste des calques", true);
    connect(m_actLayers, &QAction::toggled, this, [this](bool){ updateRightPanel(); });
    m_toolbar->addSeparator();

    // Origine
    m_actOrigin = addBtn("origin", "Définir l'origine : cliquer un élément, puis un de ses points "
                                   "(ou un 2e élément pour leur intersection)", true);
    connect(m_actOrigin, &QAction::toggled, this, [this](bool on){
        m_settingOrigin = on;
        updateRightPanel(); syncOriginFields();
        m_view->setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
        if (!on) clearSnaps();
        else emit statusMessage("Origine : cliquez sur un élément (droite, cercle, arc), puis sur un de ses points");
    });
    connect(addBtn("undoorigin", "Annuler l'origine"), &QAction::triggered, this, &CadViewWidget::undoOrigin);
    m_actMeasure = addBtn("measure", "Mesurer une distance", true);
    m_actInch = addBtn("mminch", "Dessin en pouces : convertir en mm (x25,4)", true);
    connect(m_actInch, &QAction::toggled, this, [this](bool on){ setInch(on); });
    connect(m_actMeasure, &QAction::toggled, this, &CadViewWidget::toggleMeasureMode);
    m_toolbar->addSeparator();

    // Sélection
    connect(addBtn("selall",  "Tout sélectionner"),   &QAction::triggered, this, &CadViewWidget::selectAll);
    connect(addBtn("selnone", "Tout désélectionner"), &QAction::triggered, this, &CadViewWidget::selectNone);
    m_actDiam = addBtn("diam", "Mode sélection des perçages : cliquer les trous, ou tracer un cadre (clic gauche maintenu)", true);
    connect(m_actDiam, &QAction::toggled, this, [this](bool on){
        m_holeMode = on;
        if (!on) clearSnaps();
        updateRightPanel(); refreshPosList();
        if (on) {
            m_view->setCursor(Qt::CrossCursor);
            emit statusMessage(QString(QChar(0xD8))+" : clic sur un trou, ou cadre clic gauche maintenu");
        } else {
            m_view->setCursor(Qt::ArrowCursor);
            if (m_diamPanel) m_diamPanel->hide();
        }
    });
    m_actContour = addBtn("selcontour", "Mode sélection de contours : cliquer les éléments du contour", true);
    connect(m_actContour, &QAction::toggled, this, [this](bool on){
        m_contourMode = on;
        m_view->setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
        updateRightPanel(); refreshPosList();
    });
    // Tolerance de chainage (comme la TNC : icone + valeur en mm)
    {
        QPixmap pm(30,24); pm.fill(Qt::transparent);
        QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(45,45,50),2)); p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(2,7,15,10),5,5);
        p.drawRoundedRect(QRectF(13,7,15,10),5,5);
        p.end();
        auto* ic=new QLabel(m_toolbar); ic->setPixmap(pm); ic->setToolTip("Tol"+QString(QChar(0xE9))+"rance de cha"+QString(QChar(0xEE))+"nage");
        m_toolbar->addWidget(ic);
        m_spTol=new QDoubleSpinBox(m_toolbar);
        m_spTol->setLocale(QLocale::c());
        m_spTol->setRange(0.0001, 1.0); m_spTol->setDecimals(4); m_spTol->setSingleStep(0.001);
        m_spTol->setValue(m_chainTol); m_spTol->setSuffix(" mm");
        m_spTol->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_spTol->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
        m_spTol->setFixedWidth(110);
        m_spTol->setToolTip(ic->toolTip());
        m_spTol->setStyleSheet("QDoubleSpinBox{font-family:Consolas,monospace;font-size:15px;color:#111;"
                               "background:#ffffff;border:1px solid #d6d3cc;border-radius:6px;padding:3px 6px;}"
                               "QDoubleSpinBox:focus{border-color:#3b7ddd;}");
        connect(m_spTol,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this](double v){
            m_chainTol=v; if(!m_chain.isEmpty()) redrawChain(); });
        m_toolbar->addWidget(m_spTol);
    }
    // Un seul bouton actif a la fois
    {
        const QList<QAction*> modes{ m_actLayers, m_actOrigin, m_actMeasure, m_actDiam, m_actContour };
        for (QAction* a : modes) {
            connect(a, &QAction::toggled, this, [this, a, modes](bool on){
                if (!on) return;
                for (QAction* o : modes) if (o != a && o->isChecked()) o->setChecked(false);
                m_view->setCursor(a == m_actContour ? Qt::PointingHandCursor
                                  : a == m_actLayers ? Qt::ArrowCursor : Qt::CrossCursor);
            });
        }
    }
    m_toolbar->addSeparator();

    // Enregistrer
    connect(addBtn("cnt", "Enregistrer le contour sélectionné (.cnt)"), &QAction::triggered, this, &CadViewWidget::exportSelectedToCnt);
    connect(addBtn("pnt", "Enregistrer les positions des trous sélectionnés (.pnt)"), &QAction::triggered, this, &CadViewWidget::exportSelectedToPnt);
}

void CadViewWidget::applyStyleSheet()
{
    setStyleSheet("CadViewWidget{background:#eceae6;}"
                  "QToolTip{background:#fffbe6;color:#111;border:1px solid #9a968c;}");
}

// ═══════════════════════════════════════════════════════════════
//  Chargement document
// ═══════════════════════════════════════════════════════════════
void CadViewWidget::loadFile(const QString& filePath)
{
    emit statusMessage("Chargement : "+filePath+" ...");
    QApplication::setOverrideCursor(Qt::WaitCursor);
    CadDocument doc=CadImporter::load(filePath);
    QApplication::restoreOverrideCursor();
    if(!doc.valid){
        emit statusMessage("Erreur : "+doc.error);
        QMessageBox::warning(this,"Erreur lecture",doc.error);
        return;
    }
    m_inch=false;
    if(m_actInch){ m_actInch->blockSignals(true); m_actInch->setChecked(false); m_actInch->blockSignals(false); }
    if(m_lblUnit) m_lblUnit->setText("MM");
    loadDocument(doc);
    emit statusMessage(QString("%1 contours chargs - %2")
                           .arg(doc.contours.size()).arg(QFileInfo(filePath).fileName()));
    // Unite declaree dans l'en-tete DXF : $INSUNITS 70 = 1 -> pouces
    if(filePath.endsWith(".dxf",Qt::CaseInsensitive)){
        QFile f(filePath);
        if(f.open(QIODevice::ReadOnly)){
            const QByteArray head=f.read(200000);
            const int i=head.indexOf("$INSUNITS");
            if(i>=0){
                const QList<QByteArray> L=head.mid(i,80).split('\n');
                for(int k=1;k+1<L.size();++k)
                    if(L[k].trimmed()=="70"){
                        if(L[k+1].trimmed().toInt()==1 && m_actInch){
                            m_actInch->setChecked(true);
                            emit statusMessage("Dessin en pouces d"+QString(QChar(0xE9))+"tect"+QString(QChar(0xE9))+" : converti en mm");
                        }
                        break;
                    }
            }
        }
    }
}

void CadViewWidget::loadDocument(const CadDocument& doc)
{
    m_doc=doc;
    m_selectedIds.clear();
    m_hiddenLayers.clear();
    m_itemMap.clear();
    m_originMarker=nullptr;
    m_pos.clear();
    m_chain.clear();
    rebuildLayerList();
    rebuildScene();
    QTimer::singleShot(50,this,[this]{ m_view->fitAll(); });
}

// ═══════════════════════════════════════════════════════════════
//  Construction de la scène
// ═══════════════════════════════════════════════════════════════
void CadViewWidget::rebuildScene()
{
    m_scene->clear();
    m_snapItems.clear(); m_snapPts.clear(); m_snapSeg = nullptr;
    m_itemMap.clear();
    m_originMarker=nullptr; // scene
    m_posItems.clear();
    m_chainItems.clear();

    // Grille de fond légère
    // (optionnel — désactivée pour performance)

    // Dessiner les contours
    for(const CadContour& c : std::as_const(m_doc.contours)){
        if(m_hiddenLayers.contains(c.layer)) continue;
        if(m_diamFilter){
            double d=c.diameter();
            if(d<m_diamMin||d>m_diamMax) continue;
        }
        drawContour(c);
    }

    // Axes origine
    drawOriginMarker();
    redrawPositions();
    redrawChain();

    // Info nombre de contours
    updateSelectionInfo();
}

QColor CadViewWidget::contourColor(const CadContour& c) const
{
    if(m_selectedIds.contains(c.id))
        return QColor(COLOR_SELECTED);
    // Couleur depuis calque ou contour
    QColor col=c.color;
    if(!col.isValid()||col==Qt::black||col==Qt::white)
        col=QColor(80,180,255);  // bleu poudre par défaut sur fond sombre
    // Rendre plus visible sur fond sombre
    if(col.lightness()<60) col=col.lighter(200);
    return col;
}

QGraphicsPathItem* CadViewWidget::contourToItem(const CadContour& c)
{
    QPainterPath path;
    bool first=true;
    for(const CadSegment& s : std::as_const(c.segments)){
        switch(s.type){
        case CadSegment::Line:
            if(first){ path.moveTo(s.x1,-s.y1); first=false; }
            else       path.lineTo(s.x1,-s.y1);
            path.lineTo(s.x2,-s.y2);
            break;
        case CadSegment::Arc: {
            // QPainterPath::arcTo attend un rectangle + angles en degrés
            double r=s.radius;
            QRectF rect(s.x1-r, -(s.y1+r), 2*r, 2*r);
            double startAngle=s.angle1;
            double span=s.angle2-s.angle1;
            if(s.ccw){ while(span<=0) span+=360.0; }
            else       { while(span>=0) span-=360.0; }
            if(first){ path.moveTo(s.startPt().x(),-s.startPt().y()); first=false; }
            path.arcTo(rect, startAngle, span);
            break;
        }
        case CadSegment::Circle: {
            double r=s.radius;
            path.addEllipse(QPointF(s.x1,-s.y1),r,r);
            first=false;
            break;
        }
        }
    }
    QColor col=contourColor(c);
    QPen pen(col, 0);  // width=0 = 1px cosmétique (invariant au zoom)
    pen.setCosmetic(true);
    auto* item=m_scene->addPath(path,pen);
    item->setData(0,c.id);   // stocker l'ID pour la sélection
    item->setAcceptHoverEvents(true);
    item->setFlag(QGraphicsItem::ItemIsSelectable,false);
    item->setZValue(1);
    return item;
}

void CadViewWidget::drawContour(const CadContour& c)
{
    auto* item=contourToItem(c);
    m_itemMap[c.id]=item;
}

void CadViewWidget::drawOriginMarker()
{
    if(m_originMarker){
        m_scene->removeItem(m_originMarker);
        delete m_originMarker;
        m_originMarker=nullptr;
    }
    // Meme logo que le bouton Origine, taille fixe a l'ecran, centre sur l'origine
    const int S=44;
    g_iconLight = true;                       // logo clair d'origine, comme avant
    QPixmap pm=cadIcon("origin").pixmap(64,64).scaled(S,S,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    g_iconLight = false;
    auto* it=new QGraphicsPixmapItem(pm);
    it->setOffset(-S/2.0,-S/2.0);
    it->setTransformationMode(Qt::SmoothTransformation);
    it->setFlag(QGraphicsItem::ItemIgnoresTransformations,true);
    it->setPos(m_origin.x(),-m_origin.y());
    it->setZValue(50);
    m_scene->addItem(it);
    m_originMarker=it;
}

// ═══════════════════════════════════════════════════════════════
//  Sélection
// ═══════════════════════════════════════════════════════════════
void CadViewWidget::toggleContourSelection(int id)
{
    if(m_selectedIds.contains(id)){
        m_selectedIds.remove(id);
        highlightContour(id,false);
        emit contourSelected(id,false);
    } else {
        m_selectedIds.insert(id);
        highlightContour(id,true);
        emit contourSelected(id,true);
    }
    updateSelectionInfo();
}

void CadViewWidget::highlightContour(int id, bool selected)
{
    if(!m_itemMap.contains(id)) return;
    QGraphicsPathItem* item=m_itemMap[id];
    // Trouver le contour
    for(const CadContour& c : std::as_const(m_doc.contours)){
        if(c.id==id){
            QColor col=selected ? QColor(COLOR_SELECTED) : contourColor(c);
            QPen pen(col, selected?0:0);
            pen.setCosmetic(true);
            if(selected){ pen.setWidth(0); }
            item->setPen(pen);
            item->setZValue(selected?10:1);
            break;
        }
    }
}

void CadViewWidget::updateSelectionInfo()
{
    int n=m_selectedIds.size();
    if(n==0){
        m_lblSelected->setText(QString("%1 contours\n(aucun slectionn)").arg(m_doc.contours.size()));
        return;
    }
    // Infos sur les contours sélectionnés
    double totalLen=0;
    int nClosed=0;
    for(int id : std::as_const(m_selectedIds)){
        for(const CadContour& c : std::as_const(m_doc.contours)){
            if(c.id==id){
                totalLen+=c.length();
                if(c.closed) nClosed++;
                break;
            }
        }
    }
    m_lblSelected->setText(
        QString("%1 slect. / %2\n%3 ferm(s)\nL tot: %4 mm")
            .arg(n).arg(m_doc.contours.size())
            .arg(nClosed)
            .arg(totalLen,0,'f',2));
}

QVector<int> CadViewWidget::selectedContourIds() const
{
    return QVector<int>(m_selectedIds.begin(), m_selectedIds.end());
}

void CadViewWidget::clearSelection()
{
    QSet<int> was=m_selectedIds;
    m_selectedIds.clear();
    for(int id:was) highlightContour(id,false);
    updateSelectionInfo();
}

void CadViewWidget::selectAll()
{
    for(const CadContour& c : std::as_const(m_doc.contours)){
        if(m_hiddenLayers.contains(c.layer)) continue;
        m_selectedIds.insert(c.id);
        highlightContour(c.id,true);
        emit contourSelected(c.id,true);
    }
    updateSelectionInfo();
}

void CadViewWidget::selectNone()
{
    clearSelection();
}

// ═══════════════════════════════════════════════════════════════
//  Calques
// ═══════════════════════════════════════════════════════════════
void CadViewWidget::rebuildLayerList()
{
    m_layerList->blockSignals(true);
    m_layerList->clear();
    for(const CadLayer& l : std::as_const(m_doc.layers)){
        auto* item=new QListWidgetItem(l.name,m_layerList);
        item->setCheckState(l.visible ? Qt::Checked : Qt::Unchecked);
        // Pastille couleur
        QPixmap px(12,12); px.fill(l.color.isValid()?l.color:Qt::white);
        item->setIcon(QIcon(px));
        item->setData(Qt::UserRole, l.name);
    }
    m_layerList->blockSignals(false);
}

void CadViewWidget::onLayerVisibilityChanged(QListWidgetItem* item)
{
    QString name=item->data(Qt::UserRole).toString();
    bool visible=(item->checkState()==Qt::Checked);
    toggleLayer(name,visible);
}

void CadViewWidget::toggleLayer(const QString& layerName, bool visible)
{
    if(visible) m_hiddenLayers.remove(layerName);
    else        m_hiddenLayers.insert(layerName);
    // Mettre à jour la visibilité des items de ce calque
    for(const CadContour& c : std::as_const(m_doc.contours)){
        if(c.layer==layerName && m_itemMap.contains(c.id)){
            m_itemMap[c.id]->setVisible(visible);
        }
    }
}

bool CadViewWidget::isLayerVisible(const QString& name) const
{
    return !m_hiddenLayers.contains(name);
}

// ═══════════════════════════════════════════════════════════════
//  Actions utilisateur
// ═══════════════════════════════════════════════════════════════
void CadViewWidget::fitAll()       { m_view->fitAll(); }
void CadViewWidget::zoomIn()       { m_view->scale(1.25,1.25); }
void CadViewWidget::zoomOut()      { m_view->scale(0.8,0.8); }

void CadViewWidget::toggleMeasureMode(bool on)
{
    m_measureMode=on;
    // Transmettre à la vue
    m_view->setProperty("measureMode",on);
    // HACK : accéder au membre privé via signal indirect
    // On utilise Qt::QueuedConnection pour poster un event
    if(on) m_view->setCursor(Qt::CrossCursor);
    else   m_view->setCursor(Qt::ArrowCursor);
}

void CadViewWidget::setOrigin(const QPointF& pt)
{
    m_origin=pt;
    drawOriginMarker();
    refreshPosList();
    syncOriginFields();
    emit originChanged(pt);
    emit statusMessage(QString("Origine : X=%1  Y=%2")
                           .arg(pt.x(),0,'f',3).arg(pt.y(),0,'f',3));
}

void CadViewWidget::setOriginInteractive()
{
    m_actOrigin->setChecked(true);
}

// Vrai si le contour est un cercle (Circle, ou arcs de meme centre/rayon)
static bool circleDiam(const CadContour& c, double& d)
{
    if(!c.closed || c.segments.isEmpty()) return false;
    const CadSegment& f=c.segments.first();
    if(f.type==CadSegment::Line) return false;
    for(const CadSegment& s : c.segments){
        if(s.type==CadSegment::Line) return false;
        if(qAbs(s.x1-f.x1)>0.001||qAbs(s.y1-f.y1)>0.001||qAbs(s.radius-f.radius)>0.001) return false;
    }
    d=2.0*f.radius;
    return d>0;
}

void CadViewWidget::filterByDiameter()
{
    // Comme la TNC : liste des diametres trouves, on choisit, tout est selectionne
    QMap<qint64,int> cnt;            // diametre en microns -> nombre
    for(const CadContour& c : std::as_const(m_doc.contours)){
        double d=0;
        if(!circleDiam(c,d)) continue;
        cnt[qRound64(d*1000.0)]++;
    }
    if(cnt.isEmpty()){
        emit statusMessage("Aucun cercle dans le dessin");
        return;
    }
    QStringList items; QList<double> vals;
    for(auto it=cnt.constBegin(); it!=cnt.constEnd(); ++it){
        double d=it.key()/1000.0;
        items << (QString(QChar(0xD8))+" %1   (x%2)").arg(d,0,'f',3).arg(it.value());
        vals << d;
    }
    bool ok=false;
    QString pick=QInputDialog::getItem(this,QString(QChar(0xD8)),
                                       QString(QChar(0xD8))+" :",items,0,false,&ok);
    if(!ok) return;
    int idx=items.indexOf(pick);
    if(idx<0) return;
    double dsel=vals[idx];

    clearSelection();
    for(const CadContour& c : std::as_const(m_doc.contours)){
        double d=0;
        if(!circleDiam(c,d)) continue;
        if(qAbs(d-dsel)<=0.0005){
            m_selectedIds.insert(c.id);
            highlightContour(c.id,true);
            emit contourSelected(c.id,true);
        }
    }
    updateSelectionInfo();
    emit statusMessage((QString(QChar(0xD8))+" %1 : %2").arg(dsel,0,'f',3).arg(m_selectedIds.size()));
}

void CadViewWidget::setDiameterFilter(double dmin, double dmax)
{
    m_diamMin=dmin; m_diamMax=dmax; m_diamFilter=true;
    rebuildScene();
}

void CadViewWidget::clearDiameterFilter()
{
    m_diamFilter=false;
    rebuildScene();
}

// ═══════════════════════════════════════════════════════════════
//  Slots événements
// ═══════════════════════════════════════════════════════════════
void CadViewWidget::onMouseMoved(QPointF pos)
{
    // Convertir : la scène est en Y inversé
    double rx=pos.x()-m_origin.x();
    double ry=-(pos.y())-m_origin.y();
    // Coordonnées relatives à l'origine pièce
    m_lblCoords->setText(
        QString("X: %1   Y: %2")
            .arg(rx,9,'f',3).arg(ry,9,'f',3));
}

void CadViewWidget::onMouseClicked(QPointF scenePos, Qt::MouseButton btn)
{
    if(btn==Qt::LeftButton){
        // Mode origine
        if(m_settingOrigin){
            const QPointF w(scenePos.x(), -scenePos.y());
            const double pxW = 1.0 / qMax(1e-9, std::abs(m_view->transform().m11()));  // 1 pixel en mm
            // 1) Clic sur un point proposé → nouvelle origine
            int best = -1; double bd = 9.0 * pxW;
            for (int i = 0; i < m_snapPts.size(); ++i) {
                const double d = QLineF(w, m_snapPts[i].p).length();
                if (d < bd) { bd = d; best = i; }
            }
            if (best >= 0) {
                const SnapPt sp = m_snapPts[best];
                m_originHistory << m_origin;
                setOrigin(sp.p);
                clearSnaps();
                emit statusMessage(QString("Origine : %1  X=%2  Y=%3").arg(sp.kind)
                                   .arg(sp.p.x(),0,'f',3).arg(sp.p.y(),0,'f',3));
                return;
            }
            // 2) Clic sur un élément → afficher ses points d'accroche
            const CadSegment* nearSeg = nullptr; double nd = 8.0 * pxW;
            for (const CadContour& ct : std::as_const(m_doc.contours)) {
                if (m_hiddenLayers.contains(ct.layer)) continue;
                for (const CadSegment& s : ct.segments) {
                    double d = 1e30;
                    if (s.type == CadSegment::Line) {
                        const QPointF a(s.x1, s.y1), b(s.x2, s.y2);
                        const QPointF ab = b - a; const double L2 = QPointF::dotProduct(ab, ab);
                        double t = L2 > 1e-12 ? QPointF::dotProduct(w - a, ab) / L2 : 0.0;
                        t = qBound(0.0, t, 1.0);
                        d = QLineF(w, a + ab * t).length();
                    } else {
                        const QPointF ctr(s.x1, s.y1);
                        const double dr = std::abs(QLineF(w, ctr).length() - s.radius);
                        if (s.type == CadSegment::Circle) d = dr;
                        else {
                            double a = qRadiansToDegrees(std::atan2(w.y() - s.y1, w.x() - s.x1));
                            auto norm = [](double v){ v = std::fmod(v, 360.0); return v < 0 ? v + 360.0 : v; };
                            const double span = s.ccw ? norm(s.angle2 - s.angle1) : norm(s.angle1 - s.angle2);
                            const double da   = s.ccw ? norm(a - s.angle1)        : norm(s.angle1 - a);
                            d = (da <= span + 1e-9) ? dr
                                : qMin(QLineF(w, s.startPt()).length(), QLineF(w, s.endPt()).length());
                        }
                    }
                    if (d < nd) { nd = d; nearSeg = &s; }
                }
            }
            if (nearSeg && m_snapSeg && nearSeg != m_snapSeg) {
                // Comme la TNC : 2e élément = point d'intersection avec le 1er,
                // même dans le prolongement ; s'il y en a plusieurs, le plus
                // proche du clic. Sinon le 1er élément reste sélectionné.
                const QVector<QPointF> ip = intersectFull(*m_snapSeg, *nearSeg);
                if (!ip.isEmpty()) {
                    QPointF bestP = ip.first();
                    for (const QPointF& p : ip) if (QLineF(w, p).length() < QLineF(w, bestP).length()) bestP = p;
                    m_originHistory << m_origin;
                    setOrigin(bestP);
                    clearSnaps();
                    emit statusMessage(QString("Origine : intersection  X=%1  Y=%2")
                                       .arg(bestP.x(),0,'f',3).arg(bestP.y(),0,'f',3));
                    return;
                }
                emit statusMessage("Ces deux éléments ne se coupent pas (même prolongés) — le 1er reste sélectionné");
                return;
            }
            if (nearSeg) showSnapsFor(*nearSeg);
            else emit statusMessage("Origine : cliquez sur un élément du dessin");
            return;
        }
        // Mode mesure (géré dans la vue)
        if(m_measureMode) return;

        // ── Mode perçages ───────────────────────────────────
        if(m_holeMode){
            const QPointF w(scenePos.x(), -scenePos.y());
            const double pxW = 1.0 / qMax(1e-9, std::abs(m_view->transform().m11()));
            int best=-1; double bd=9.0*pxW;
            for(int i=0;i<m_snapPts.size();++i){
                double d=QLineF(w,m_snapPts[i].p).length();
                if(d<bd){ bd=d; best=i; }
            }
            if(best>=0){ addPosition(m_snapPts[best].p,0); clearSnaps(); return; }
            for(int i=0;i<m_pos.size();++i)
                if(QLineF(w,m_pos[i].p).length()<7.0*pxW){ removePosition(i); return; }
            const CadSegment* sg=findSegAt(w,8.0*pxW);
            if(!sg){
                clearSnaps();
                // clic a l'interieur d'un trou : le plus petit cercle qui contient le clic
                const CadContour* hole=nullptr; double hr=1e30, hd=0;
                for(const CadContour& c : std::as_const(m_doc.contours)){
                    if(m_hiddenLayers.contains(c.layer)) continue;
                    double d=0;
                    if(!circleDiam(c,d)) continue;
                    const CadSegment& f=c.segments.first();
                    if(QLineF(w,QPointF(f.x1,f.y1)).length()<f.radius && f.radius<hr){ hr=f.radius; hole=&c; hd=d; }
                }
                if(hole){
                    const QPointF ctr(hole->segments.first().x1,hole->segments.first().y1);
                    for(int i=0;i<m_pos.size();++i)
                        if(QLineF(ctr,m_pos[i].p).length()<1e-3){ removePosition(i); return; }
                    addPosition(ctr,hd);
                }
                return;
            }
            if(sg->type!=CadSegment::Line){
                for(const CadContour& c : std::as_const(m_doc.contours)){
                    bool mine=false;
                    for(const CadSegment& x : c.segments) if(&x==sg){ mine=true; break; }
                    if(!mine) continue;
                    double d=0;
                    if(circleDiam(c,d)){
                        clearSnaps();
                        QPointF ctr(sg->x1,sg->y1);
                        for(int i=0;i<m_pos.size();++i)
                            if(QLineF(ctr,m_pos[i].p).length()<1e-3){ removePosition(i); return; }
                        addPosition(ctr,d);
                        return;
                    }
                    break;
                }
            }
            if(m_snapSeg && sg!=m_snapSeg){
                const QVector<QPointF> ip=intersectFull(*m_snapSeg,*sg);
                if(!ip.isEmpty()){
                    QPointF bp=ip.first();
                    for(const QPointF& p : ip) if(QLineF(w,p).length()<QLineF(w,bp).length()) bp=p;
                    addPosition(bp,0); clearSnaps(); return;
                }
            }
            showSnapsFor(*sg);
            showInfoSeg(*sg);
            emit statusMessage("Cliquez un point (ou un 2e élément pour l'intersection) : il devient une position");
            return;
        }
        if(!m_contourMode) return;   // aucun mode actif : le clic ne fait rien
        {
            const QPointF w(scenePos.x(), -scenePos.y());
            const double pxW = 1.0 / qMax(1e-9, std::abs(m_view->transform().m11()));
            contourClick(w,pxW);
            return;
        }

        // Sélection : trouver le contour le plus proche du clic
        // Chercher parmi les items à ±5px
        // Qt6 : items(QPoint, mode, order) supprimé → utiliser itemAt ou rect
        QPointF vp = m_view->mapFromScene(scenePos);
        QList<QGraphicsItem*> items = m_view->items(
            QRect(vp.toPoint() - QPoint(5,5), QSize(10,10)));

        // Chercher le premier item qui est un contour
        for(QGraphicsItem* gi : std::as_const(items)){
            int id=gi->data(0).toInt();
            if(id>0){
                toggleContourSelection(id);
                return;
            }
        }
        // Clic dans le vide → désélectionner tout
        // (optionnel — on laisse la sélection pour l'instant)
    }
}

void CadViewWidget::onMeasure(QPointF p1, QPointF p2)
{
    // Convertir coordonnées scène en coordonnées pièce
    double x1=p1.x(), y1=-p1.y();
    double x2=p2.x(), y2=-p2.y();
    double dx=x2-x1, dy=y2-y1;
    double dist=std::sqrt(dx*dx+dy*dy);
    double angle=qRadiansToDegrees(std::atan2(dy,dx));

    // Dessiner la ligne de mesure permanente
    QPen pen(QColor(255,255,0,200),0,Qt::DashLine);
    pen.setCosmetic(true);
    auto* line=m_scene->addLine(p1.x(),p1.y(),p2.x(),p2.y(),pen);
    line->setZValue(90);

    // Label de distance
    QPointF mid((p1.x()+p2.x())/2,(p1.y()+p2.y())/2);
    // Distance : ligne colorée seulement (pas de texte — problèmes font Qt/coords scène)
    // Le résultat est affiché dans la barre de statut via emit measureDone
    // Petit carré jaune au milieu pour indiquer la mesure active
    double ms2 = qAbs(p2.x()-p1.x())*0.03 + qAbs(p2.y()-p1.y())*0.03 + 2.0;
    auto* midMark = m_scene->addEllipse(mid.x()-ms2, mid.y()-ms2, ms2*2, ms2*2,
                                        QPen(QColor(255,255,0,200),0), QBrush(QColor(255,255,0,80)));
    midMark->setZValue(91);

    emit measureDone(dist,QPointF(x1,y1),QPointF(x2,y2));
    emit statusMessage(
        QString("Mesure : %1 mm  |  Dx=%2  Dy=%3  Angle=%4 deg")
            .arg(dist,0,'f',3).arg(dx,0,'f',3).arg(dy,0,'f',3).arg(angle,0,'f',1));

    // Désactiver le mode mesure après la mesure
    m_actMeasure->setChecked(false);
}

void CadViewWidget::onOpenFile()
{
    QString path=QFileDialog::getOpenFileName(
        this,"Ouvrir fichier CAD",QString(),CadImporter::fileFilter());
    if(!path.isEmpty()) loadFile(path);
}

// ═══════════════════════════════════════════════════════════════
//  Drag & Drop
// ═══════════════════════════════════════════════════════════════
void CadViewWidget::dragEnterEvent(QDragEnterEvent* e)
{
    if(e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void CadViewWidget::dropEvent(QDropEvent* e)
{
    for(const QUrl& url : e->mimeData()->urls()){
        QString path=url.toLocalFile();
        if(!path.isEmpty()){
            emit fileDropped(path);
            loadFile(path);
            break;  // un seul fichier à la fois
        }
    }
}

void CadViewWidget::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
}

// ═══════════════════════════════════════════════════════════════
//  Export .cnt — conversion CadContour → ContourFile EazyNC
// ═══════════════════════════════════════════════════════════════
static ContourFile cadContourToCnt(const CadContour& c, const QString& name)
{
    ContourFile cf;
    cf.name = name;
    if(c.segments.isEmpty()) return cf;

    // Premier élément L = point de départ
    QPointF sp = c.segments.first().startPt();
    ContourElement start;
    start.type = ContourElement::L;
    start.x2   = sp.x();
    start.y2   = sp.y();
    cf.elements << start;

    for(const CadSegment& s : c.segments){
        switch(s.type){
        case CadSegment::Line: {
            ContourElement el;
            el.type = ContourElement::L;
            el.x2   = s.x2;
            el.y2   = s.y2;
            cf.elements << el;
            break;
        }
        case CadSegment::Arc: {
            // CC : centre
            ContourElement cc;
            cc.type = ContourElement::CC;
            cc.cx   = s.x1;
            cc.cy   = s.y1;
            cf.elements << cc;
            // CR : arc avec rayon
            QPointF ep = s.endPt();
            ContourElement cr;
            cr.type   = ContourElement::CR;
            cr.x2     = ep.x();
            cr.y2     = ep.y();
            cr.r      = s.radius;
            cr.drPlus = s.ccw;
            cf.elements << cr;
            break;
        }
        case CadSegment::Circle: {
            // CC + CR cercle complet
            ContourElement cc;
            cc.type = ContourElement::CC;
            cc.cx   = s.x1;
            cc.cy   = s.y1;
            cf.elements << cc;
            QPointF ep = s.startPt();
            ContourElement cr;
            cr.type   = ContourElement::CR;
            cr.x2     = ep.x();
            cr.y2     = ep.y();
            cr.r      = s.radius;
            cr.drPlus = s.ccw;          // sens choisi (inverser le sens)
            cf.elements << cr;
            break;
        }
        }
    }
    return cf;
}

void CadViewWidget::exportSelectedToCnt()
{
    int nOn=0; for(const ChainEl& c : std::as_const(m_chain)) if(c.on) ++nOn;
    if(!m_chain.isEmpty() && nOn==0){
        QMessageBox::information(this,"Export .cnt","Cochez les éléments du contour à enregistrer.");
        return;
    }
    if(nOn>0){
        QString path = QFileDialog::getSaveFileName(this, "Exporter contour en .cnt",
                                                    "C:/cncm/ncfiles", "Contours EazyNC (*.cnt)");
        if(path.isEmpty()) return;
        if(!path.endsWith(".cnt",Qt::CaseInsensitive)) path += ".cnt";
        CadContour tmp;
        const double ox=m_origin.x(), oy=m_origin.y();
        for(const ChainEl& c : std::as_const(m_chain)){
            if(!c.on) continue;
            CadSegment o=chainSeg(c);
            o.x1-=ox; o.y1-=oy;
            if(o.type==CadSegment::Line){ o.x2-=ox; o.y2-=oy; }
            tmp.segments << o;
        }
        tmp.closed=chainClosed() && nOn==m_chain.size();
        ContourFile cf=cadContourToCnt(tmp, QFileInfo(path).baseName());
        if(cf.elements.isEmpty()) return;
        if(!cf.save(path)){ QMessageBox::warning(this,"Export .cnt","Impossible d'ecrire : "+path); return; }
        emit statusMessage(QString("Export: %1  (%2 elements)").arg(QFileInfo(path).fileName()).arg(nOn));
        QMessageBox::information(this,"Export .cnt",
            QString("Contour exporte :\n%1\n%2 elements%3").arg(path).arg(nOn)
            .arg(tmp.closed? "  (ferme)" : ""));
        return;
    }
    if(m_selectedIds.isEmpty()){
        QMessageBox::information(this,"Export .cnt",
                                 "Slectionnez au moins un contour avant d'exporter.");
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this, "Exporter contour en .cnt",
        QString(), "Contours EazyNC (*.cnt)");
    if(path.isEmpty()) return;
    if(!path.endsWith(".cnt",Qt::CaseInsensitive)) path += ".cnt";

    // Fusionner tous les contours sélectionnés en un seul ContourFile
    ContourFile cf;
    cf.name = QFileInfo(path).baseName();
    for(const CadContour& c : std::as_const(m_doc.contours)){
        if(!m_selectedIds.contains(c.id)) continue;
        ContourFile tmp = cadContourToCnt(c, cf.name);
        cf.elements += tmp.elements;
    }
    if(cf.elements.isEmpty()){ return; }  // rien à exporter
    if(!cf.save(path)){
        QMessageBox::warning(this,"Export .cnt","Impossible d'ecrire : " + path);
        return;
    }
    emit statusMessage(QString("Export: %1  (%2 elements)")
                           .arg(QFileInfo(path).fileName()).arg(cf.elements.size()));
    QMessageBox::information(this,"Export .cnt",
                             QString("Contour exporte:\n%1\n%2 elements").arg(path).arg(cf.elements.size()));
}
// ─────────────────────────────────────────────────────────────
//  exportSelectedToPnt — centres des cercles sélectionnés → .pnt
//  (comme la TNC : sélection des positions d'usinage dans le DXF).
//  Coordonnées relatives à l'origine choisie dans la visionneuse.
// ─────────────────────────────────────────────────────────────
void CadViewWidget::exportSelectedToPnt()
{
    QVector<QPointF> centres;
    for (const HolePos& hp : std::as_const(m_pos))
        centres << QPointF(hp.p.x() - m_origin.x(), hp.p.y() - m_origin.y());
    if (centres.isEmpty())
    for (const CadContour& c : std::as_const(m_doc.contours)) {
        if (!m_selectedIds.contains(c.id)) continue;
        const bool cercle = (c.segments.size() == 1 && c.segments.first().type == CadSegment::Circle);
        if (!cercle) continue;                       // seuls les cercles donnent une position
        const QPointF ct = c.bbox().center();
        centres << QPointF(ct.x() - m_origin.x(), ct.y() - m_origin.y());
    }
    if (centres.isEmpty()) {
        QMessageBox::information(this, "Export .pnt",
            "Aucune position : activez le mode perçages (Ø) puis cliquez des trous,\n"
            "des points, ou tracez un cadre.");
        return;
    }
    // Ordre de passage : ligne par ligne (Y croissant, puis X)
    std::sort(centres.begin(), centres.end(), [](const QPointF& a, const QPointF& b){
        if (qAbs(a.y() - b.y()) > 1e-6) return a.y() < b.y();
        return a.x() < b.x();
    });
    // Doublons (ex. perçage + lamage concentriques) : une seule position
    QVector<QPointF> uniq;
    for (const QPointF& p : centres) {
        bool dup = false;
        for (const QPointF& q : uniq) if (QLineF(p, q).length() < 1e-3) { dup = true; break; }
        if (!dup) uniq << p;
    }

    QString path = QFileDialog::getSaveFileName(this, "Exporter positions en .pnt",
                                                "C:/cncm/ncfiles", "Positions EazyNC (*.pnt)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".pnt", Qt::CaseInsensitive)) path += ".pnt";

    PointFile pf;
    pf.name = QFileInfo(path).baseName();
    PatternEntry e;
    e.mode = PatternEntry::ModeTableau;
    for (const QPointF& p : uniq) {
        PointEntry pe; pe.x = qRound(p.x() * 10000.0) / 10000.0; pe.y = qRound(p.y() * 10000.0) / 10000.0;
        e.tableauPts << pe;
    }
    pf.entries << e;
    if (!pf.save(path)) {
        QMessageBox::warning(this, "Export .pnt", "Impossible d'écrire : " + path);
        return;
    }
    QString liste;
    for (const QPointF& p : uniq) liste += QString("X%1  Y%2\n").arg(p.x(), 0, 'f', 3).arg(p.y(), 0, 'f', 3);
    emit statusMessage(QString("Export: %1  (%2 positions)").arg(QFileInfo(path).fileName()).arg(uniq.size()));
    QMessageBox::information(this, "Export .pnt",
        QString("%1 positions exportées dans\n%2\n\n%3").arg(uniq.size()).arg(path).arg(liste));
}

// ═══════════════════════════════════════════════════════════════
//  Accrochage du point d'origine (comme le convertisseur DXF TNC)
//  1er clic : l'élément se divise en ses points remarquables
//  (extrémités, milieu, centre, quadrants) + intersections avec les
//  autres éléments. 2e clic sur un de ces points : nouvelle origine.
// ═══════════════════════════════════════════════════════════════
namespace {
double normDeg(double v) { v = std::fmod(v, 360.0); return v < 0 ? v + 360.0 : v; }
bool onArcAngle(const CadSegment& s, double aDeg)
{
    if (s.type == CadSegment::Circle) return true;
    if (s.type != CadSegment::Arc) return false;
    const double span = s.ccw ? normDeg(s.angle2 - s.angle1) : normDeg(s.angle1 - s.angle2);
    const double da   = s.ccw ? normDeg(aDeg - s.angle1)       : normDeg(s.angle1 - aDeg);
    return da <= span + 1e-6 || da >= 360.0 - 1e-6;
}
bool onSeg(const CadSegment& s, const QPointF& p)
{
    if (s.type == CadSegment::Line) {
        const QPointF a(s.x1, s.y1), b(s.x2, s.y2), ab = b - a;
        const double L2 = QPointF::dotProduct(ab, ab);
        if (L2 < 1e-12) return false;
        const double t = QPointF::dotProduct(p - a, ab) / L2;
        return t >= -1e-6 && t <= 1.0 + 1e-6;
    }
    return onArcAngle(s, qRadiansToDegrees(std::atan2(p.y() - s.y1, p.x() - s.x1)));
}
// Intersections entre deux éléments (droite/droite, droite/cercle, cercle/cercle)
QVector<QPointF> intersect(const CadSegment& A, const CadSegment& B)
{
    QVector<QPointF> out, cand;
    const bool aL = A.type == CadSegment::Line, bL = B.type == CadSegment::Line;
    if (aL && bL) {
        const QPointF p(A.x1, A.y1), r(A.x2 - A.x1, A.y2 - A.y1);
        const QPointF q(B.x1, B.y1), s(B.x2 - B.x1, B.y2 - B.y1);
        const double den = r.x() * s.y() - r.y() * s.x();
        if (std::abs(den) < 1e-12) return out;                    // parallèles
        const QPointF qp = q - p;
        const double t = (qp.x() * s.y() - qp.y() * s.x()) / den;
        cand << p + r * t;
    } else if (aL || bL) {
        const CadSegment& L = aL ? A : B;
        const CadSegment& C = aL ? B : A;
        const QPointF a(L.x1, L.y1), d(L.x2 - L.x1, L.y2 - L.y1), f = a - QPointF(C.x1, C.y1);
        const double qa = QPointF::dotProduct(d, d), qb = 2 * QPointF::dotProduct(f, d);
        const double qc = QPointF::dotProduct(f, f) - C.radius * C.radius;
        const double disc = qb * qb - 4 * qa * qc;
        if (qa < 1e-12 || disc < -1e-9) return out;
        const double sq = std::sqrt(qMax(0.0, disc));
        cand << a + d * ((-qb - sq) / (2 * qa));
        if (sq > 1e-9) cand << a + d * ((-qb + sq) / (2 * qa));
    } else {
        const QPointF c0(A.x1, A.y1), c1(B.x1, B.y1);
        const double dd = QLineF(c0, c1).length();
        if (dd < 1e-9 || dd > A.radius + B.radius + 1e-9 || dd < std::abs(A.radius - B.radius) - 1e-9) return out;
        const double a = (A.radius * A.radius - B.radius * B.radius + dd * dd) / (2 * dd);
        const double h = std::sqrt(qMax(0.0, A.radius * A.radius - a * a));
        const QPointF m = c0 + (c1 - c0) * (a / dd);
        const QPointF n(-(c1.y() - c0.y()) / dd, (c1.x() - c0.x()) / dd);
        cand << m + n * h;
        if (h > 1e-9) cand << m - n * h;
    }
    for (const QPointF& p : cand) if (onSeg(A, p) && onSeg(B, p)) out << p;
    return out;
}
}

// Intersection sans limite : droites prolongées, arcs pris comme cercles entiers
QVector<QPointF> CadViewWidget::intersectFull(const CadSegment& A, const CadSegment& B)
{
    auto full = [](CadSegment s) {
        if (s.type == CadSegment::Line) {                  // droite très longue
            const double dx = s.x2 - s.x1, dy = s.y2 - s.y1, L = std::sqrt(dx*dx + dy*dy);
            if (L > 1e-12) {
                const double k = 1e6 / L;
                const double mx = (s.x1 + s.x2) / 2, my = (s.y1 + s.y2) / 2;
                s.x1 = mx - dx * k; s.y1 = my - dy * k; s.x2 = mx + dx * k; s.y2 = my + dy * k;
            }
        } else s.type = CadSegment::Circle;
        return s;
    };
    return intersect(full(A), full(B));
}

void CadViewWidget::clearSnaps()
{
    m_snapSeg = nullptr;
    for (QGraphicsItem* it : std::as_const(m_snapItems)) {
        if (it && it->scene() == m_scene) { m_scene->removeItem(it); delete it; }
    }
    m_snapItems.clear();
    m_snapPts.clear();
}

void CadViewWidget::showSnapsFor(const CadSegment& seg)
{
    clearSnaps();
    m_snapSeg = &seg;
    auto add = [&](const QPointF& p, const QString& k) {
        for (const SnapPt& s : std::as_const(m_snapPts)) if (QLineF(s.p, p).length() < 1e-4) return;
        m_snapPts << SnapPt{ p, k };
    };
    // Points propres à l'élément
    if (seg.type == CadSegment::Line) {
        add(QPointF(seg.x1, seg.y1), "Extrémité");
        add(QPointF(seg.x2, seg.y2), "Extrémité");
        add(QPointF((seg.x1 + seg.x2) / 2, (seg.y1 + seg.y2) / 2), "Milieu");
    } else {
        const QPointF ctr(seg.x1, seg.y1);
        add(ctr, "Centre");
        if (seg.type == CadSegment::Circle) {
            for (int q = 0; q < 4; ++q)
                add(ctr + QPointF(seg.radius * std::cos(q * M_PI / 2), seg.radius * std::sin(q * M_PI / 2)), "Quadrant");
        } else {
            add(seg.startPt(), "Extrémité");
            add(seg.endPt(),   "Extrémité");
            const double span = seg.ccw ? normDeg(seg.angle2 - seg.angle1) : normDeg(seg.angle1 - seg.angle2);
            const double am   = qDegreesToRadians(seg.ccw ? seg.angle1 + span / 2 : seg.angle1 - span / 2);
            add(ctr + QPointF(seg.radius * std::cos(am), seg.radius * std::sin(am)), "Milieu");
            for (int q = 0; q < 4; ++q)
                if (onArcAngle(seg, q * 90.0))
                    add(ctr + QPointF(seg.radius * std::cos(q * M_PI / 2), seg.radius * std::sin(q * M_PI / 2)), "Quadrant");
        }
    }
    // Intersections avec tous les autres éléments visibles
    for (const CadContour& ct : std::as_const(m_doc.contours)) {
        if (m_hiddenLayers.contains(ct.layer)) continue;
        for (const CadSegment& o : ct.segments) {
            if (&o == &seg) continue;
            for (const QPointF& p : intersect(seg, o)) add(p, "Intersection");
        }
    }
    // Élément surligné en vert
    QPainterPath path;
    const QVector<QPointF> poly = seg.toPolyline(96);
    if (!poly.isEmpty()) {
        path.moveTo(poly.first().x(), -poly.first().y());
        for (int i = 1; i < poly.size(); ++i) path.lineTo(poly[i].x(), -poly[i].y());
    }
    QPen hp(QColor(0, 230, 0), 3); hp.setCosmetic(true);
    auto* hl = m_scene->addPath(path, hp);
    hl->setZValue(95);
    m_snapItems << hl;
    // Marqueurs : carrés de taille fixe à l'écran
    for (const SnapPt& s : std::as_const(m_snapPts)) {
        QColor col = s.kind == "Centre" ? QColor(255, 0, 255)
                   : s.kind == "Milieu" ? QColor(0, 220, 255)
                   : s.kind == "Quadrant" ? QColor(0, 220, 255)
                   : s.kind == "Intersection" ? QColor(255, 60, 60)
                   : QColor(255, 220, 0);                       // extrémité
        auto* r = new QGraphicsRectItem(-5, -5, 10, 10);
        QPen mp(Qt::black, 1); mp.setCosmetic(true);
        r->setPen(mp); r->setBrush(col);
        r->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        r->setPos(s.p.x(), -s.p.y());
        r->setZValue(96);
        r->setToolTip(QString("%1  X=%2  Y=%3").arg(s.kind).arg(s.p.x(), 0, 'f', 3).arg(s.p.y(), 0, 'f', 3));
        m_scene->addItem(r);
        m_snapItems << r;
    }
    emit statusMessage(QString("%1 points proposés — cliquez celui qui devient l'origine "
                               "(jaune = extrémité, bleu = milieu/quadrant, violet = centre, rouge = intersection), "
                               "ou cliquez un 2e élément pour prendre leur intersection")
                       .arg(m_snapPts.size()));
}

void CadViewWidget::undoOrigin()
{
    clearSnaps();
    const QPointF prev = m_originHistory.isEmpty() ? QPointF(0, 0) : m_originHistory.takeLast();
    setOrigin(prev);
    m_actOrigin->setChecked(true);                  // on relance tout de suite la sélection
    emit statusMessage(QString("Origine annulée (retour X=%1 Y=%2) — cliquez un élément pour en choisir une autre")
                       .arg(prev.x(), 0, 'f', 3).arg(prev.y(), 0, 'f', 3));
}


// ═══════════════════════════════════════════════════════════════
//  Cadre de selection + panneau Ø mini / Ø maxi (style TNC)
// ═══════════════════════════════════════════════════════════════
static QString diamTxt(double d){ return QString(QChar(0xD8))+QString(" %1").arg(d,0,'f',3); }

void CadViewWidget::buildDiamPanel()
{
    if(m_diamPanel) return;
    m_diamPanel=new QFrame(m_view);
    m_diamPanel->setStyleSheet(
        "QFrame{background:rgba(20,22,30,235);border:1px solid #8a8fa0;border-radius:6px;}"
        "QLabel{color:white;border:none;font-size:15px;font-weight:bold;}"
        "QToolButton{color:white;background:#2c3040;border:1px solid #6a7080;border-radius:4px;"
        "min-width:34px;min-height:28px;font-size:16px;}"
        "QToolButton:hover{background:#3c4258;}");
    auto* g=new QGridLayout(m_diamPanel);
    g->setContentsMargins(10,8,10,8); g->setHorizontalSpacing(6); g->setVerticalSpacing(6);

    m_lblDmin=new QLabel; m_lblDmin->setAlignment(Qt::AlignCenter); m_lblDmin->setMinimumWidth(110);
    m_lblDmax=new QLabel; m_lblDmax->setAlignment(Qt::AlignCenter); m_lblDmax->setMinimumWidth(110);
    m_lblDcnt=new QLabel; m_lblDcnt->setAlignment(Qt::AlignCenter);
    auto mk=[&](const QString& t){ auto* b=new QToolButton; b->setText(t); b->setAutoRaise(false); return b; };
    QToolButton* minL=mk(QString(QChar(0x25C0))); QToolButton* minR=mk(QString(QChar(0x25B6)));
    QToolButton* maxL=mk(QString(QChar(0x25C0))); QToolButton* maxR=mk(QString(QChar(0x25B6)));
    QToolButton* ok =mk(QString(QChar(0x2714)));  QToolButton* ko =mk(QString(QChar(0x2716)));
    ok->setStyleSheet("QToolButton{color:#6fdc7a;}");
    ko->setStyleSheet("QToolButton{color:#ff6a6a;}");

    // Ligne 0 : Ø mini | nombre | Ø maxi
    g->addWidget(m_lblDmin,0,0,1,2);
    g->addWidget(m_lblDcnt,0,2,1,2);
    g->addWidget(m_lblDmax,0,4,1,2);
    // Ligne 1 : fleches sous chaque Ø, valider / annuler au centre
    g->addWidget(minL,1,0); g->addWidget(minR,1,1);
    g->addWidget(ok,1,2);   g->addWidget(ko,1,3);
    g->addWidget(maxL,1,4); g->addWidget(maxR,1,5);

    connect(minL,&QToolButton::clicked,this,[this]{ if(m_iMin>0){ m_iMin--; applyRectDiam(); } });
    connect(minR,&QToolButton::clicked,this,[this]{ if(m_iMin<m_iMax){ m_iMin++; applyRectDiam(); } });
    connect(maxL,&QToolButton::clicked,this,[this]{ if(m_iMax>m_iMin){ m_iMax--; applyRectDiam(); } });
    connect(maxR,&QToolButton::clicked,this,[this]{ if(m_iMax<m_rectDiams.size()-1){ m_iMax++; applyRectDiam(); } });
    connect(ok,&QToolButton::clicked,this,[this]{ closeDiamPanel(true); });
    connect(ko,&QToolButton::clicked,this,[this]{ closeDiamPanel(false); });
    m_diamPanel->hide();
}

void CadViewWidget::onRectSelected(QRectF sr)
{
    if(!m_holeMode) return;
    // scene -> monde (Y inverse)
    QRectF w(QPointF(sr.left(),-sr.bottom()), QPointF(sr.right(),-sr.top()));
    w=w.normalized();

    m_rectHoles.clear(); m_rectDiams.clear();
    for(const CadContour& c : std::as_const(m_doc.contours)){
        if(m_hiddenLayers.contains(c.layer)) continue;
        if(!c.closed || c.segments.isEmpty()) continue;
        const CadSegment& f=c.segments.first();
        if(f.type==CadSegment::Line) continue;
        bool circ=true;
        for(const CadSegment& s : c.segments)
            if(s.type==CadSegment::Line||qAbs(s.x1-f.x1)>0.001||qAbs(s.y1-f.y1)>0.001||qAbs(s.radius-f.radius)>0.001){ circ=false; break; }
        if(!circ) continue;
        // Le cercle doit etre entierement dans le cadre
        QRectF cb(f.x1-f.radius,f.y1-f.radius,2*f.radius,2*f.radius);
        if(!w.contains(cb)) continue;
        double d=qRound64(2.0*f.radius*1000.0)/1000.0;
        m_rectHoles.append({c.id,d});
        if(!m_rectDiams.contains(d)) m_rectDiams.append(d);
    }
    std::sort(m_rectDiams.begin(),m_rectDiams.end());

    if(m_rectDiams.isEmpty()){
        clearSelection(); updateSelectionInfo();
        if(m_diamPanel) m_diamPanel->hide();
        emit statusMessage(QString(QChar(0xD8))+" : 0");
        return;
    }
    buildDiamPanel();
    m_iMin=0; m_iMax=m_rectDiams.size()-1;
    applyRectDiam();
    m_diamPanel->adjustSize();
    m_diamPanel->move((m_view->width()-m_diamPanel->width())/2, 12);
    m_diamPanel->show(); m_diamPanel->raise();
}

void CadViewWidget::applyRectDiam()
{
    if(m_rectDiams.isEmpty()) return;
    const double dmin=m_rectDiams[m_iMin], dmax=m_rectDiams[m_iMax];
    clearSelection();
    for(const auto& h : std::as_const(m_rectHoles)){
        if(h.second>=dmin-0.0005 && h.second<=dmax+0.0005){
            m_selectedIds.insert(h.first);
            highlightContour(h.first,true);
            emit contourSelected(h.first,true);
        }
    }
    updateSelectionInfo();
    m_lblDmin->setText(diamTxt(dmin));
    m_lblDmax->setText(diamTxt(dmax));
    m_lblDcnt->setText(QString("x%1").arg(m_selectedIds.size()));
}

void CadViewWidget::closeDiamPanel(bool keep)
{
    if(m_diamPanel) m_diamPanel->hide();
    if(keep){
        for(const auto& h : std::as_const(m_rectHoles)){
            if(!m_selectedIds.contains(h.first)) continue;
            for(const CadContour& c : std::as_const(m_doc.contours))
                if(c.id==h.first && !c.segments.isEmpty()){
                    addPosition(QPointF(c.segments.first().x1,c.segments.first().y1),h.second); break; }
        }
    }
    clearSelection(); updateSelectionInfo();
}


// ═══════════════════════════════════════════════════════════════
//  Positions de percage : croix + liste
// ═══════════════════════════════════════════════════════════════
const CadSegment* CadViewWidget::findSegAt(const QPointF& w, double tol, bool noCircle) const
{
    const CadSegment* best=nullptr; double nd=tol;
    auto norm=[](double v){ v=std::fmod(v,360.0); return v<0? v+360.0 : v; };
    for(const CadContour& ct : std::as_const(m_doc.contours)){
        if(m_hiddenLayers.contains(ct.layer)) continue;
        for(const CadSegment& s : ct.segments){
            if(noCircle && s.type==CadSegment::Circle) continue;
            double d=1e30;
            if(s.type==CadSegment::Line){
                const QPointF a(s.x1,s.y1), b(s.x2,s.y2), ab=b-a;
                const double L2=QPointF::dotProduct(ab,ab);
                double t=L2>1e-12? QPointF::dotProduct(w-a,ab)/L2 : 0.0;
                t=qBound(0.0,t,1.0);
                d=QLineF(w,a+ab*t).length();
            } else {
                const QPointF ctr(s.x1,s.y1);
                const double dr=std::abs(QLineF(w,ctr).length()-s.radius);
                if(s.type==CadSegment::Circle) d=dr;
                else {
                    double a=qRadiansToDegrees(std::atan2(w.y()-s.y1,w.x()-s.x1));
                    const double span=s.ccw? norm(s.angle2-s.angle1) : norm(s.angle1-s.angle2);
                    const double da  =s.ccw? norm(a-s.angle1)        : norm(s.angle1-a);
                    d=(da<=span+1e-9)? dr : qMin(QLineF(w,s.startPt()).length(),QLineF(w,s.endPt()).length());
                }
            }
            if(d<nd){ nd=d; best=&s; }
        }
    }
    return best;
}

bool CadViewWidget::addPosition(const QPointF& p, double d)
{
    for(const HolePos& h : std::as_const(m_pos))
        if(QLineF(h.p,p).length()<1e-3) return false;   // deja dans la liste
    m_pos.append({p,d});
    if(m_posList){ m_posList->blockSignals(true); m_posList->setCurrentRow(-1); m_posList->blockSignals(false); }
    redrawPositions();
    emit statusMessage(QString("Position %1 : X=%2  Y=%3").arg(m_pos.size())
                       .arg(p.x()-m_origin.x(),0,'f',3).arg(p.y()-m_origin.y(),0,'f',3));
    return true;
}

void CadViewWidget::removePosition(int i)
{
    if(i<0||i>=m_pos.size()) return;
    m_pos.removeAt(i);
    if(m_posList){ m_posList->blockSignals(true); m_posList->setCurrentRow(-1); m_posList->blockSignals(false); }
    redrawPositions();
}

void CadViewWidget::redrawPositions()
{
    for(QGraphicsItem* it : std::as_const(m_posItems))
        if(it && it->scene()==m_scene){ m_scene->removeItem(it); delete it; }
    m_posItems.clear();
    const int cur = (m_posList && !m_contourMode) ? m_posList->currentRow() : -1;
    for(int i=0;i<m_pos.size();++i){
        const bool hi=(i==cur);
        const double k = hi ? 11 : 8;
        const QPointF sp(m_pos[i].p.x(), -m_pos[i].p.y());
        QPainterPath cp;
        cp.moveTo(-k,-k); cp.lineTo(k,k);
        cp.moveTo(-k, k); cp.lineTo(k,-k);
        auto* halo=new QGraphicsPathItem(cp);
        QPen hp(QColor(0,0,0),hi?6:5); hp.setCosmetic(true); hp.setCapStyle(Qt::RoundCap);
        halo->setPen(hp); halo->setZValue(97);
        auto* cr=new QGraphicsPathItem(cp);
        QPen pp(hi?QColor(255,240,0):QColor(255,140,0),hi?3:2); pp.setCosmetic(true); pp.setCapStyle(Qt::RoundCap);
        cr->setPen(pp); cr->setZValue(98);
        auto* num=new QGraphicsSimpleTextItem(QString::number(i+1));
        num->setBrush(hi?QColor(255,240,0):QColor(255,170,60));
        QFont f; f.setPixelSize(12); f.setBold(true); num->setFont(f);
        num->setTransform(QTransform::fromTranslate(k+2,-k-14));
        num->setZValue(98);
        for(QGraphicsItem* it : {static_cast<QGraphicsItem*>(halo),static_cast<QGraphicsItem*>(cr),static_cast<QGraphicsItem*>(num)}){
            it->setFlag(QGraphicsItem::ItemIgnoresTransformations,true);
            it->setPos(sp);
            m_scene->addItem(it);
            m_posItems << it;
        }
    }
    refreshPosList();
}

void CadViewWidget::refreshPosList()
{
    if(!m_posList || m_noListRefresh) return;
    const int cur=m_posList->currentRow();
    m_posList->blockSignals(true);
    m_posList->clear();
    int n=0;
    if(m_contourMode){
        for(int i=0;i<m_chain.size();++i){
            const CadSegment o=chainSeg(m_chain[i]);
            auto* it=new QListWidgetItem(elemIcon(i+1, m_chain[i].s->type==CadSegment::Line? 0 : m_chain[i].s->type==CadSegment::Arc? 1 : 2, 0), QString());
            it->setFlags(it->flags()|Qt::ItemIsUserCheckable);
            it->setCheckState(m_chain[i].on? Qt::Checked : Qt::Unchecked);
            m_posList->addItem(it);
        }
        n=m_chain.size();
        int non=0; for(const ChainEl& c : std::as_const(m_chain)) if(c.on) ++non;
        if(m_lblPosTitle) m_lblPosTitle->setText(QString(QChar(0x21BB))+QString("  %1 / %2").arg(non).arg(n));
        if(m_btnClearList) m_btnClearList->setToolTip("Recommencer le contour");
        if(m_btnClearList) m_btnClearList->setIcon(cadIcon("undocontour"));
    } else {
        for(int i=0;i<m_pos.size();++i){
            const HolePos& h=m_pos[i];
            auto* it=new QListWidgetItem(elemIcon(i+1, h.d>0? 3 : 4, h.d), QString());
            it->setFlags(it->flags()|Qt::ItemIsUserCheckable); it->setCheckState(Qt::Checked);
            m_posList->addItem(it);
        }
        n=m_pos.size();
        if(m_lblPosTitle) m_lblPosTitle->setText(QString(QChar(0x2716))+QString("  x%1").arg(n));
        if(m_btnClearList) m_btnClearList->setToolTip("Effacer toutes les positions");
        if(m_btnClearList) m_btnClearList->setIcon(cadIcon("undopos"));
    }
    if(cur>=0 && cur<n) m_posList->setCurrentRow(cur);
    m_posList->blockSignals(false);
}

void CadViewWidget::updateRightPanel()
{
    if(!m_rightPanel) return;
    const bool lay = m_actLayers && m_actLayers->isChecked();
    const bool pos = m_holeMode || m_contourMode;
    const bool ori = m_settingOrigin;
    m_rightPanel->setVisible(lay||pos||ori);
    if(m_originBox)      m_originBox->setVisible(ori);
    if(m_lblLayersTitle) m_lblLayersTitle->setVisible(lay);
    if(m_layerList)      m_layerList->setVisible(lay);
    if(m_lblPosTitle)    m_lblPosTitle->setVisible(pos);
    if(m_posList)        m_posList->setVisible(pos);
    if(m_posBtns)        m_posBtns->setVisible(pos);
    if(m_lblListHead)    m_lblListHead->setVisible(pos);
    if(m_btnReverse)     m_btnReverse->setVisible(m_contourMode);
}


// ═══════════════════════════════════════════════════════════════
//  Liste a droite (cases a cocher) + fenetre d'informations
//  + selection de contour facon TNC (sens, bleu / vert)
// ═══════════════════════════════════════════════════════════════
static CadSegment orientSeg(const CadSegment& s, bool rev)
{
    CadSegment o=s;
    if(!rev) return o;
    if(s.type==CadSegment::Line){ std::swap(o.x1,o.x2); std::swap(o.y1,o.y2); }
    else if(s.type==CadSegment::Arc){ o.angle1=s.angle2; o.angle2=s.angle1; o.ccw=!s.ccw; }
    else { o.ccw=!s.ccw; }
    return o;
}

// direction (vecteur unitaire) d'un segment oriente au debut (atEnd=false) ou a la fin
static QPointF segTangent(const CadSegment& s, bool atEnd)
{
    if(s.type==CadSegment::Line){
        QPointF d(s.x2-s.x1,s.y2-s.y1); double L=std::hypot(d.x(),d.y());
        return L>1e-12? d/L : QPointF(1,0);
    }
    const double a=qDegreesToRadians(atEnd? s.angle2 : s.angle1);
    QPointF t(-std::sin(a), std::cos(a));          // sens trigo
    return s.ccw? t : -t;
}

QPointF CadViewWidget::chainEnd() const
{
    if(m_chain.isEmpty()) return {};
    return chainSeg(m_chain.last()).endPt();
}

bool CadViewWidget::chainClosed() const
{
    if(m_chain.isEmpty()) return false;
    if(m_chain.size()==1 && m_chain.first().s->type==CadSegment::Circle) return true;
    const QPointF a=chainSeg(m_chain.first()).startPt();
    return m_chain.size()>1 && QLineF(a,chainEnd()).length()<m_chainTol;
}

bool CadViewWidget::inChain(const CadSegment* s) const
{
    for(const ChainEl& c : m_chain) if(c.s==s) return true;
    return false;
}

QVector<CadViewWidget::ChainEl> CadViewWidget::chainCandidates(const QPointF& p, const CadSegment* except) const
{
    QVector<ChainEl> out;
    for(const CadContour& ct : std::as_const(m_doc.contours)){
        if(m_hiddenLayers.contains(ct.layer)) continue;
        for(const CadSegment& s : ct.segments){
            if(&s==except || s.type==CadSegment::Circle || inChain(&s)) continue;
            if(QLineF(s.startPt(),p).length()<m_chainTol) out.append({&s,false});
            else if(QLineF(s.endPt(),p).length()<m_chainTol) out.append({&s,true});
        }
    }
    return out;
}

void CadViewWidget::contourClick(const QPointF& w, double pxW)
{
    const CadSegment* sg=findSegAt(w,8.0*pxW,true);     // droites et arcs en priorite
    if(!sg) sg=findSegAt(w,3.0*pxW);                    // cercle : seulement si clic franc
    if(!sg) return;
    showInfoSeg(*sg);
    // Clic sur un element du contour deja affiche -> cocher / decocher
    for(int i=0;i<m_chain.size();++i){
        if(m_chain[i].s==sg){
            m_chain[i].on=!m_chain[i].on;
            redrawChain();
            return;
        }
    }
    // Contour deja commence et ouvert : prolonger depuis la fin jusqu'a l'element clique
    if(!m_chain.isEmpty()){
        if(chainClosed()){ emit statusMessage("Contour ferm"+QString(QChar(0xE9))+" : effacez des lignes ou recommencez"); return; }
        QVector<ChainEl> path;
        CadSegment cur=chainSeg(m_chain.last());
        QPointF p=cur.endPt();
        const QPointF start=chainSeg(m_chain.first()).startPt();
        QSet<const CadSegment*> used;
        for(int guard=0; guard<3000; ++guard){
            QVector<ChainEl> c2;
            for(const ChainEl& c : chainCandidates(p,nullptr)) if(!used.contains(c.s)) c2.append(c);
            if(c2.isEmpty()) break;
            int pick=-1;
            for(int k=0;k<c2.size();++k) if(c2[k].s==sg){ pick=k; break; }
            if(pick<0){
                const QPointF t0=segTangent(cur,true); double bestA=1e9;
                for(int k=0;k<c2.size();++k){
                    const QPointF t1=segTangent(chainSeg(c2[k]),false);
                    const double ang=std::abs(std::atan2(t0.x()*t1.y()-t0.y()*t1.x(), t0.x()*t1.x()+t0.y()*t1.y()));
                    if(ang<bestA){ bestA=ang; pick=k; }
                }
            }
            ChainEl e=c2[pick]; e.on=true;
            path.append(e); used.insert(e.s);
            if(e.s==sg){ m_chain+=path; redrawChain(); return; }
            cur=chainSeg(e); p=cur.endPt();
            if(QLineF(p,start).length()<m_chainTol) break;
        }
        emit statusMessage(QString(QChar(0xC9))+"l"+QString(QChar(0xE9))+"ment non reli"+QString(QChar(0xE9))+
                           " "+QString(QChar(0xE0))+" la fin du contour (effacez des lignes, ou recommencez)");
        return;
    }
    // Nouveau contour : depart au bout le plus proche du clic, puis tout le contour
    m_chain.clear();
    bool rev=false;
    if(sg->type!=CadSegment::Circle)
        rev = QLineF(w,sg->endPt()).length() < QLineF(w,sg->startPt()).length();
    ChainEl el0{sg,rev,false};
    if(sg->type==CadSegment::Circle)       // cercle : depart a l'endroit du clic
        el0.a0 = qRadiansToDegrees(std::atan2(w.y()-sg->y1, w.x()-sg->x1));
    m_chain.append(el0);
    if(sg->type!=CadSegment::Circle){
        auto walk=[&](CadSegment cur, QPointF p, bool forward, const QPointF& stop){
            QVector<ChainEl> out;
            for(int guard=0; guard<5000; ++guard){
                if(QLineF(p,stop).length()<m_chainTol) break;            // contour referme
                QVector<ChainEl> cand=chainCandidates(p,nullptr);
                QVector<ChainEl> c2;
                for(const ChainEl& c : cand){
                    bool used=false;
                    for(const ChainEl& o : out) if(o.s==c.s){ used=true; break; }
                    if(!used) c2.append(c);
                }
                if(c2.isEmpty()) break;
                const QPointF t0 = forward ? segTangent(cur,true) : -segTangent(cur,false);
                const ChainEl* pick=nullptr; double bestA=1e9;
                for(const ChainEl& c : c2){
                    // en arriere, l'element doit FINIR en p : on l'inverse
                    ChainEl e=c; if(!forward) e.rev=!e.rev;
                    const CadSegment o=chainSeg(e);
                    const QPointF t1 = forward ? segTangent(o,false) : -segTangent(o,true);
                    double ang=std::abs(std::atan2(t0.x()*t1.y()-t0.y()*t1.x(), t0.x()*t1.x()+t0.y()*t1.y()));
                    if(ang<bestA){ bestA=ang; pick=&c; }
                }
                ChainEl e=*pick; if(!forward) e.rev=!e.rev; e.on=false;
                out.append(e);
                cur=chainSeg(e);
                p = forward ? cur.endPt() : cur.startPt();
                // les elements deja pris ne doivent plus etre candidats
                m_chain.append(e);
            }
            // retirer de m_chain ce qui a ete ajoute temporairement
            m_chain.resize(m_chain.size()-out.size());
            return out;
        };
        const CadSegment first=orientSeg(*sg,rev);
        QVector<ChainEl> fwd=walk(first, first.endPt(), true, first.startPt());
        m_chain+=fwd;
        if(!chainClosed()){
            // contour ouvert : completer aussi vers l'arriere
            QVector<ChainEl> bwd=walk(first, first.startPt(), false, QPointF(1e30,1e30));
            QVector<ChainEl> full;
            for(int i=bwd.size()-1;i>=0;--i) full.append(bwd[i]);
            full+=m_chain;
            m_chain=full;
        }
    }
    redrawChain();
    emit statusMessage(QString("Contour : %1 éléments — cochez ceux à garder").arg(m_chain.size()));
}

void CadViewWidget::redrawChain()
{
    for(QGraphicsItem* it : std::as_const(m_chainItems))
        if(it && it->scene()==m_scene){ m_scene->removeItem(it); delete it; }
    m_chainItems.clear();
    auto polyPath=[](const CadSegment& s){
        QPainterPath pp; const QVector<QPointF> pl=s.toPolyline(96);
        if(pl.isEmpty()) return pp;
        pp.moveTo(pl.first().x(),-pl.first().y());
        for(int i=1;i<pl.size();++i) pp.lineTo(pl[i].x(),-pl[i].y());
        return pp;
    };
    const int cur = (m_contourMode && m_posList) ? m_posList->currentRow() : -1;
    for(int i=0;i<m_chain.size();++i){
        const CadSegment o=chainSeg(m_chain[i]);
        QPainterPath pp=polyPath(o);
        const bool on=m_chain[i].on;
        QPen bp(i==cur? QColor(255,240,0) : on? QColor(110,200,255) : QColor(0,200,0), i==cur?5:(on?4:2)); bp.setCosmetic(true);
        auto* bi=m_scene->addPath(pp,bp); bi->setZValue(91); m_chainItems<<bi;
        if(on || i==cur){
            QPen dp(QColor(255,255,255),1,Qt::DashLine); dp.setCosmetic(true);
            auto* di=m_scene->addPath(pp,dp); di->setZValue(92); m_chainItems<<di;
        }
    }
    // Suites possibles en vert depuis la fin (contour ouvert)
    if(!m_chain.isEmpty() && !chainClosed()){
        for(const ChainEl& c : chainCandidates(chainEnd(),nullptr)){
            QPen gp(QColor(0,210,0),2); gp.setCosmetic(true);
            auto* gi=m_scene->addPath(polyPath(*c.s),gp); gi->setZValue(90); m_chainItems<<gi;
        }
    }
    // Une seule fleche au point de depart, dans le sens de parcours (comme la TNC)
    if(!m_chain.isEmpty()){
        const CadSegment o=chainSeg(m_chain.first());
        const QPointF sp=o.startPt();
        const QPointF t=segTangent(o,false);
        QPointF d(t.x(),-t.y());
        QPolygonF tri; const QPointF n(-d.y(),d.x());
        tri << d*16 << (n*8) << (-n*8);
        auto* ai=new QGraphicsPolygonItem(tri);
        ai->setBrush(QColor(200,235,255)); QPen ap(QColor(20,60,120),1.5); ap.setCosmetic(true); ai->setPen(ap);
        ai->setFlag(QGraphicsItem::ItemIgnoresTransformations,true);
        ai->setPos(sp.x(),-sp.y()); ai->setZValue(94);
        m_scene->addItem(ai); m_chainItems<<ai;
    }
    refreshPosList();
}

static QString segSym(const CadSegment& s)
{
    return s.type==CadSegment::Line ? QString(QChar(0x2014))
         : s.type==CadSegment::Arc  ? QString(QChar(0x25E0)) : QString(QChar(0x25CB));
}

void CadViewWidget::showInfoSeg(const CadSegment& seg, bool rev)
{
    if(!m_lblSelected) return;
    const CadSegment s=orientSeg(seg,rev);
    QString layer;
    for(const CadContour& c : std::as_const(m_doc.contours))
        for(const CadSegment& x : c.segments) if(&x==&seg) layer=c.layer;
    const double ox=m_origin.x(), oy=m_origin.y();
    QString t=QString("%1  %2\n").arg(segSym(s), layer);
    if(s.type==CadSegment::Circle){
        t+=QString("CC X%1 Y%2\nR %3\n%4 %5")
             .arg(s.x1-ox,0,'f',3).arg(s.y1-oy,0,'f',3).arg(s.radius,0,'f',3)
             .arg(QChar(0xD8)).arg(2*s.radius,0,'f',3);
    } else {
        const QPointF a=s.startPt(), b=s.endPt();
        t+=QString("%1 X%2 Y%3\n%4 X%5 Y%6")
             .arg(QChar(0x25B6)).arg(a.x()-ox,0,'f',3).arg(a.y()-oy,0,'f',3)
             .arg(QChar(0x25A0)).arg(b.x()-ox,0,'f',3).arg(b.y()-oy,0,'f',3);
        if(s.type==CadSegment::Arc)
            t+=QString("\nCC X%1 Y%2\nR %3").arg(s.x1-ox,0,'f',3).arg(s.y1-oy,0,'f',3).arg(s.radius,0,'f',3);
        else
            t+=QString("\nL %1").arg(QLineF(a,b).length(),0,'f',3);
    }
    m_lblSelected->setText(t);
}

void CadViewWidget::showInfoPos(int i)
{
    if(!m_lblSelected || i<0 || i>=m_pos.size()) return;
    const HolePos& h=m_pos[i];
    QString t=QString("%1  %2\nX %3\nY %4").arg(QChar(0x2716)).arg(i+1)
                .arg(h.p.x()-m_origin.x(),0,'f',3).arg(h.p.y()-m_origin.y(),0,'f',3);
    if(h.d>0) t+=QString("\n%1 %2").arg(QChar(0xD8)).arg(h.d,0,'f',3);
    m_lblSelected->setText(t);
}

void CadViewWidget::onListItemChanged(QListWidgetItem* it)
{
    if(!it) return;
    const int row=m_posList->row(it);
    const bool checked = it->checkState()==Qt::Checked;
    if(m_contourMode){
        if(row<0 || row>=m_chain.size() || m_chain[row].on==checked) return;
        m_chain[row].on=checked;
        QTimer::singleShot(0,this,[this]{ redrawChain(); });
        return;
    }
    if(checked) return;
    QTimer::singleShot(0,this,[this,row]{ removePosition(row); });
}

void CadViewWidget::onListRowChanged(int row)
{
    m_noListRefresh=true;
    struct Reset{ bool& f; ~Reset(){ f=false; } } reset{m_noListRefresh};
    if(m_contourMode){
        if(row>=0 && row<m_chain.size()) showInfoSeg(*m_chain[row].s,m_chain[row].rev);
        redrawChain();
    } else {
        showInfoPos(row);
        redrawPositions();
    }
}


// Icone de la liste facon TNC : n° + vignette (L droite, C arc, cercle, trou, point)
static QIcon elemIcon(int id, int kind, double d)
{
    QPixmap pm(92,30); pm.fill(Qt::transparent);
    QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
    QFont f("Consolas"); f.setPixelSize(15); p.setFont(f);
    p.setPen(QColor(20,20,20));
    p.drawText(QRectF(0,0,26,30),Qt::AlignRight|Qt::AlignVCenter,QString::number(id));
    const QRectF tile(32,1,58,28);
    p.setPen(QPen(QColor(214,211,204),1)); p.setBrush(QColor(247,246,243)); p.drawRoundedRect(tile,4,4);
    QFont f2("Consolas"); f2.setPixelSize(10); f2.setBold(true); p.setFont(f2); p.setPen(QColor(60,60,60));
    const QPointF a(42,23), b(82,8);
    QPen ln(QColor(70,70,70),1.6); ln.setCapStyle(Qt::RoundCap);
    if(kind==0){ p.drawText(QPointF(34,11),"L"); p.setPen(ln); p.drawLine(a,b); }
    else if(kind==1){ p.drawText(QPointF(34,11),"C"); p.setPen(ln); p.setBrush(Qt::NoBrush);
        QPainterPath pp; pp.moveTo(a); pp.quadTo(QPointF(80,28),b); p.drawPath(pp); }
    else if(kind==2){ p.setPen(ln); p.setBrush(Qt::NoBrush); p.drawEllipse(QPointF(61,15),10,10); }
    else {
        p.setPen(ln); p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(46,15),7,7);
        p.drawLine(QPointF(37,15),QPointF(55,15)); p.drawLine(QPointF(46,6),QPointF(46,24));
        p.setFont(f2); p.setPen(QColor(30,30,30));
        if(kind==3) p.drawText(QRectF(56,0,34,30),Qt::AlignCenter,QString::number(d,'g',4));
    }
    if(kind<=1){
        p.setPen(QPen(QColor(70,70,70),1.2)); p.setBrush(QColor(255,255,255)); p.drawEllipse(a,2.6,2.6);
        p.setPen(Qt::NoPen); p.setBrush(QColor(20,90,210)); p.drawEllipse(b,3.2,3.2);
    }
    p.end();
    return QIcon(pm);
}

void CadViewWidget::deleteFromRow(int row)
{
    if(row<0) return;
    if(m_contourMode){
        if(row>=m_chain.size()) return;
        m_chain.resize(row);                       // efface la ligne et toutes celles en dessous
        if(m_posList){ m_posList->blockSignals(true); m_posList->setCurrentRow(-1); m_posList->blockSignals(false); }
        redrawChain();
        emit statusMessage(m_chain.isEmpty() ? QString("Contour effac"+QString(QChar(0xE9)))
                           : QString("Contour coup"+QString(QChar(0xE9))+" : cliquez le chemin voulu sur le dessin"));
    } else if(m_holeMode) {
        removePosition(row);
    }
}


void CadViewWidget::reverseChain()
{
    if(m_chain.isEmpty()) return;
    std::reverse(m_chain.begin(), m_chain.end());
    for(ChainEl& c : m_chain) c.rev = !c.rev;
    if(m_posList){ m_posList->blockSignals(true); m_posList->setCurrentRow(-1); m_posList->blockSignals(false); }
    redrawChain();
    emit statusMessage("Sens du contour invers"+QString(QChar(0xE9)));
}


void CadViewWidget::syncOriginFields()
{
    if(!m_spOx || !m_spOy) return;
    m_spOx->blockSignals(true); m_spOy->blockSignals(true);
    m_spOx->setValue(m_origin.x()); m_spOy->setValue(m_origin.y());
    m_spOx->blockSignals(false); m_spOy->blockSignals(false);
}


CadSegment CadViewWidget::chainSeg(const ChainEl& c) const
{
    CadSegment s = *c.s;
    if (s.type == CadSegment::Circle) {          // cercle -> arc de 360 deg qui part de a0
        s.type = CadSegment::Arc;
        s.angle1 = c.a0;
        s.angle2 = c.a0 + 360.0;
        s.ccw = true;
    }
    return orientSeg(s, c.rev);
}


// Bouton mm/inch : le dessin est en pouces -> tout est multiplie par 25,4
void CadViewWidget::setInch(bool on)
{
    if(on==m_inch) return;
    m_inch=on;
    scaleDocument(on ? 25.4 : 1.0/25.4);
    if(m_lblUnit) m_lblUnit->setText(on ? "INCH" + QString(QChar(0x2192)) + "MM" : "MM");
    emit statusMessage(on ? "Dessin en pouces : converti en mm (x25,4)"
                          : "Dessin en mm (conversion annul"+QString(QChar(0xE9))+"e)");
}

void CadViewWidget::scaleDocument(double k)
{
    for(CadContour& c : m_doc.contours)
        for(CadSegment& s : c.segments){
            s.x1*=k; s.y1*=k; s.x2*=k; s.y2*=k; s.radius*=k;
        }
    m_doc.extents = QRectF(m_doc.extents.topLeft()*k, m_doc.extents.bottomRight()*k);
    m_origin*=k;
    for(QPointF& p : m_originHistory) p*=k;
    for(HolePos& h : m_pos){ h.p*=k; h.d*=k; }
    clearSnaps();
    rebuildScene();
    syncOriginFields();
    QTimer::singleShot(30,this,[this]{ m_view->fitAll(); });
}
