// ═══════════════════════════════════════════════════════════════
//  contoureditor.cpp — Éditeur de contour EazyNC
// ═══════════════════════════════════════════════════════════════
#include "contoureditor.h"
#include <QWheelEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QPainter>
#include <QGraphicsPathItem>
#include <QGraphicsLineItem>
#include <QGraphicsTextItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QFont>
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QPalette>
#include <QTimer>
#include <QtMath>
#include <cmath>
#include "eazyfiledialog.h"

// ═══════════════════════════════════════════════════════════════
//  ContourEditorView
// ═══════════════════════════════════════════════════════════════
ContourEditorView::ContourEditorView(QWidget *parent)
    : QGraphicsView(parent), m_panning(false), m_panMoved(false),
      m_panButton(Qt::MiddleButton)
{
    setCursor(Qt::CrossCursor);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(NoDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorUnderMouse);
    setBackgroundBrush(QColor(10,12,18));
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
}

void ContourEditorView::fitAll()
{
    if(!scene()) return;
    QRectF r = scene()->itemsBoundingRect();
    if(r.isEmpty()) return;
    r.adjust(-r.width()*0.1,-r.height()*0.1,
             r.width()*0.1, r.height()*0.1);
    fitInView(r, Qt::KeepAspectRatio);
}

void ContourEditorView::wheelEvent(QWheelEvent *e)
{
    // Zoom centré sur la position du curseur
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    double f = e->angleDelta().y() > 0 ? 1.15 : 1.0/1.15;
    scale(f, f);
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    e->accept();
}

void ContourEditorView::mousePressEvent(QMouseEvent *e)
{
    // Pan : bouton milieu OU clic droit
    if(e->button()==Qt::MiddleButton || e->button()==Qt::RightButton){
        m_panning   = true;
        m_panStart  = e->pos();
        m_panButton = e->button();
        setCursor(Qt::ClosedHandCursor);
        e->accept(); return;
    }
    // Clic gauche : recherche élément le plus proche pour sélection
    if(e->button()==Qt::LeftButton && scene()){
        QPointF sp = mapToScene(e->pos());
        const double tolPx = 8.0;
        // Convertir 8px en unités scène (transform peut varier selon zoom)
        QPointF dxScene = mapToScene(QPoint(int(tolPx),0)) - mapToScene(QPoint(0,0));
        double tolScene = std::max(std::abs(dxScene.x()), std::abs(dxScene.y()));
        if (tolScene < 0.001) tolScene = 0.5;

        int bestIdx = -1; double bestDist = tolScene;
        const auto items = scene()->items(QRectF(sp.x()-tolScene, sp.y()-tolScene,
                                                  tolScene*2, tolScene*2));
        for(QGraphicsItem *it : items){
            QVariant v = it->data(0);
            if(!v.isValid()) continue;
            int idx = v.toInt();
            // Distance approximative au point de clic
            QRectF br = it->sceneBoundingRect();
            double cx = br.center().x(), cy = br.center().y();
            double d = std::hypot(sp.x()-cx, sp.y()-cy);
            if(d < bestDist){ bestDist = d; bestIdx = idx; }
        }
        if(bestIdx >= 0){
            emit elementClicked(bestIdx);
            e->accept(); return;
        }
    }
    QGraphicsView::mousePressEvent(e);
}

void ContourEditorView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if(e->button()==Qt::LeftButton && scene()){
        QPointF sp = mapToScene(e->pos());
        const double tolPx = 8.0;
        QPointF dxScene = mapToScene(QPoint(int(tolPx),0)) - mapToScene(QPoint(0,0));
        double tolScene = std::max(std::abs(dxScene.x()), std::abs(dxScene.y()));
        if (tolScene < 0.001) tolScene = 0.5;

        int bestIdx = -1; double bestDist = tolScene;
        const auto items = scene()->items(QRectF(sp.x()-tolScene, sp.y()-tolScene,
                                                  tolScene*2, tolScene*2));
        for(QGraphicsItem *it : items){
            QVariant v = it->data(0);
            if(!v.isValid()) continue;
            int idx = v.toInt();
            QRectF br = it->sceneBoundingRect();
            double cx = br.center().x(), cy = br.center().y();
            double d = std::hypot(sp.x()-cx, sp.y()-cy);
            if(d < bestDist){ bestDist = d; bestIdx = idx; }
        }
        if(bestIdx >= 0){
            emit elementDoubleClicked(bestIdx);
            e->accept(); return;
        }
    }
    QGraphicsView::mouseDoubleClickEvent(e);
}

void ContourEditorView::mouseMoveEvent(QMouseEvent *e)
{
    if(m_panning){
        QPoint d = e->pos() - m_panStart;
        m_panStart = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue  (verticalScrollBar()->value()   - d.y());
        m_panMoved = true;
        e->accept(); return;
    }
    emit cursorMoved(mapToScene(e->pos()));
    QGraphicsView::mouseMoveEvent(e);
}

void ContourEditorView::mouseReleaseEvent(QMouseEvent *e)
{
    if(m_panning && e->button()==m_panButton){
        m_panning = false;
        setCursor(Qt::CrossCursor);
        // Clic droit sans mouvement → émettre signal pour snap X/Y
        if(e->button()==Qt::RightButton && !m_panMoved)
            emit rightClicked(mapToScene(e->pos()));
        m_panMoved = false;
        e->accept(); return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

// ═══════════════════════════════════════════════════════════════
//  ContourEditor — construction
// ═══════════════════════════════════════════════════════════════
ContourEditor::ContourEditor(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    applyStyle();
    rebuildScene();
}

void ContourEditor::buildUi()
{
    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0,0,0,0);
    mainLay->setSpacing(0);

    // ── Toolbar ──────────────────────────────────────────────
    buildToolbar();
    mainLay->addWidget(m_toolbar);

    // ── Splitter horizontal : dialogue gauche / vue droite ───
    auto *hSplit = new QSplitter(Qt::Horizontal);

    // ── Panneau dialogue gauche — formulaire tout-en-un ─────────
    m_dlgPanel = new QWidget;
    m_dlgPanel->setFixedWidth(290);
    m_dlgPanel->setAutoFillBackground(true);
    QPalette dlgPal = m_dlgPanel->palette();
    dlgPal.setColor(QPalette::Window, QColor(0x0e,0x0e,0x1e));
    m_dlgPanel->setPalette(dlgPal);
    auto *dlgLay = new QVBoxLayout(m_dlgPanel);
    dlgLay->setContentsMargins(10,10,10,10);
    dlgLay->setSpacing(8);

    // Titre type d'élément
    m_lblElement = new QLabel("── Choisir un élément ──");
    m_lblElement->setStyleSheet(
        "font-size:13px;font-weight:700;color:#88ccff;"
        "background:transparent;padding:4px 0 6px 0;"
        "border-bottom:2px solid #2a3a6a;");
    dlgLay->addWidget(m_lblElement);

    // Zone formulaire (remplie dynamiquement par buildForm)
    m_formWidget = new QWidget;
    m_formLay    = new QVBoxLayout(m_formWidget);
    m_formLay->setContentsMargins(0,4,0,0);
    m_formLay->setSpacing(8);
    dlgLay->addWidget(m_formWidget);

    // Bouton ENT — valide tout le formulaire d'un coup
    m_btnEnt = new QPushButton("  ENT ↵  Valider");
    m_btnEnt->setFixedHeight(42);
    m_btnEnt->setStyleSheet(
        "QPushButton{background:#1a50a0;color:#ffffff;border:2px solid #4488ee;"
        "border-radius:5px;font-size:14px;font-weight:700;}"
        "QPushButton:hover{background:#2060bb;}"
        "QPushButton:pressed{background:#0a3070;}");
    dlgLay->addWidget(m_btnEnt);
    dlgLay->addStretch();

    // Astuce curseur
    auto *lblHint2 = new QLabel("Clic droit \u2192 X/Y");
    lblHint2->setStyleSheet("font-size:10px;color:#445566;background:transparent;");
    lblHint2->setAlignment(Qt::AlignCenter);
    dlgLay->addWidget(lblHint2);

    hSplit->addWidget(m_dlgPanel);

    // ── Zone droite : vue + liste ─────────────────────────────
    auto *rightWidget = new QWidget;
    auto *rightLay    = new QVBoxLayout(rightWidget);
    rightLay->setContentsMargins(0,0,0,0);
    rightLay->setSpacing(0);

    // Vue graphique
    m_scene = new QGraphicsScene(this);
    m_view  = new ContourEditorView(this);
    m_view->setScene(m_scene);
    m_view->setMouseTracking(true);
    // Bloquer la propagation de Enter vers le QDialog parent
    setFocusPolicy(Qt::StrongFocus);

    m_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightLay->addWidget(m_view, 3);

    // Séparateur
    auto *sep = new QFrame; sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("border-top:1px solid #363650;");
    rightLay->addWidget(sep);

    // Liste des éléments saisis
    m_list = new QListWidget;
    m_list->setFixedHeight(130);
    m_list->setStyleSheet(
        "QListWidget{background:#080810;color:#d0d8ff;border:none;"
        "font-size:12px;font-family:monospace;}"
        "QListWidget::item{padding:5px 10px;border-bottom:1px solid #1a1a2e;}"
        "QListWidget::item:selected{background:#1a50a0;color:#ffffff;}");
    rightLay->addWidget(m_list);

    hSplit->addWidget(rightWidget);
    hSplit->setStretchFactor(0,0);
    hSplit->setStretchFactor(1,1);
    mainLay->addWidget(hSplit, 1);

    // ── Barre de statut ───────────────────────────────────────
    auto *statusBar = new QWidget;
    statusBar->setFixedHeight(22);
    statusBar->setStyleSheet("background:#0a0a14;border-top:1px solid #363650;");
    auto *sLay = new QHBoxLayout(statusBar);
    sLay->setContentsMargins(8,0,8,0);
    m_lblCoords = new QLabel("X: 0.000   Y: 0.000");
    m_lblCoords->setStyleSheet("font-size:11px;color:#667799;font-family:monospace;");
    sLay->addWidget(m_lblCoords);
    sLay->addStretch();
    auto *lblHint = new QLabel("Molette = zoom  |  Bouton milieu = pan  |  F = tout voir");
    lblHint->setStyleSheet("font-size:10px;color:#445566;");
    sLay->addWidget(lblHint);
    mainLay->addWidget(statusBar);

    // ── Connexions ────────────────────────────────────────────
    connect(m_btnEnt, &QPushButton::clicked, this, &ContourEditor::onValidate);
    connect(m_view, &ContourEditorView::cursorMoved, this, [this](QPointF p){
        m_lastCursor = QPointF(p.x(), -p.y());  // coordonnées réelles
        m_lblCoords->setText(QString("X: %1   Y: %2")
                                 .arg(p.x(),8,'f',3).arg(-p.y(),8,'f',3));
    });
    // Clic droit sur la vue → injecter les coordonnées dans X/Y
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint &vp){
        QPointF sp = m_view->mapToScene(vp);
        double rx = sp.x(), ry = -sp.y();
        if(m_fldX){ m_fldX->setText(QString::number(rx,'f',3)); }
        if(m_fldY){ m_fldY->setText(QString::number(ry,'f',3)); }
        if(m_fldX) m_fldX->setFocus();
        drawCurrentPreview();
    });
    // Clic liste → surligner élément dans la scène + centrer
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row){
        m_actDelSel->setEnabled(row >= 0);
        rebuildScene();
        if(row >= 0 && row < m_contour.elements.size()){
            const ContourElement &e = m_contour.elements[row];
            double fx=e.x2, fy=e.y2;
            if(e.type==ContourElement::CC){ fx=e.cx; fy=e.cy; }
            m_view->centerOn(fx, -fy);
        }
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item){
        onListDoubleClicked(m_list->row(item));
    });

    // ── Clic sur élément du dessin → sélection liste ─────────────
    connect(m_view, &ContourEditorView::elementClicked, this, [this](int idx){
        if(idx >= 0 && idx < m_contour.elements.size()){
            m_list->setCurrentRow(idx);  // déclenche currentRowChanged → repeint en jaune
        }
    });
    // ── Double-clic sur élément du dessin → édition ──────────────
    connect(m_view, &ContourEditorView::elementDoubleClicked, this, [this](int idx){
        if(idx >= 0 && idx < m_contour.elements.size()){
            m_list->setCurrentRow(idx);
            onListDoubleClicked(idx);
        }
    });

    // Raccourci F = fit all
    auto *actFit = new QAction(this);
    actFit->setShortcut(Qt::Key_F);
    addAction(actFit);
    connect(actFit, &QAction::triggered, this, [this]{ m_view->fitAll(); });
}

void ContourEditor::buildToolbar()
{
    m_toolbar = new QToolBar(this);
    m_toolbar->setStyleSheet(
        "QToolBar{background:#0e0e1e;border-bottom:2px solid #2a2a50;"
        "spacing:4px;padding:4px 8px;}"
        "QToolButton{color:#d0d8ff;background:#181828;border:1px solid #2a2a48;"
        "border-radius:4px;padding:5px 16px;font-size:13px;font-weight:700;""min-width:40px;}"
        "QToolButton:hover{background:#20203c;border-color:#4a4a80;}"
        "QToolButton:checked{background:#1a50a0;border-color:#5090ff;""color:#ffffff;font-size:14px;}"
        );

    auto addAct=[&](const QString &label, const QString &tip, ContourElement::Type t) -> QAction* {
        QAction *a = m_toolbar->addAction(label);
        a->setToolTip(tip);
        a->setCheckable(true);
        connect(a, &QAction::triggered, this, [this,t,a](bool ch){
            if(ch) startElement(t);
            else   cancelElement();
        });
        return a;
    };

    m_actL   = addAct("L",   "Droite — point final X Y",                ContourElement::L);
    m_actCHF = addAct("CHF", "Chanfrein — longueur",                    ContourElement::CHF);
    m_actRND = addAct("RND", "Arrondi d'angle — rayon",                 ContourElement::RND);
    m_actCC  = addAct("CC",  "Centre de cercle — X Y",                  ContourElement::CC);
    m_actC   = addAct("C",   "Arc (centre CC) — X Y DR+/-",             ContourElement::C);
    m_actCR  = addAct("CR",  "Arc (rayon) — X Y R DR+/-",               ContourElement::CR);
    m_actCT  = addAct("CT",  "Arc tangentiel — X Y",                    ContourElement::CT);

    m_toolbar->addSeparator();

    m_actDel = m_toolbar->addAction("← Annuler");
    m_actDel->setToolTip("Supprimer le dernier élément (DEL)");
    connect(m_actDel, &QAction::triggered, this, &ContourEditor::onDeleteLast);

    m_actDelSel = m_toolbar->addAction("⌫ Supprimer");
    m_actDelSel->setToolTip("Supprimer l'élément sélectionné dans la liste");
    m_actDelSel->setEnabled(false);   // activé seulement quand un item est sélectionné
    connect(m_actDelSel, &QAction::triggered, this, [this]{
        int row = m_list->currentRow();
        if(row < 0 || row >= m_contour.elements.size()) return;
        if(m_editing && m_editingRow == row) cancelElement();
        m_contour.elements.removeAt(row);
        rebuildListWidget();
        rebuildScene();
        m_actDelSel->setEnabled(false);
        emit contourChanged();
    });

    m_actClr = m_toolbar->addAction("⊘ Tout");
    m_actClr->setToolTip("Effacer tout le contour");
    connect(m_actClr, &QAction::triggered, this, &ContourEditor::onClearAll);

    m_toolbar->addSeparator();

    m_actSave = m_toolbar->addAction("💾 Sauvegarder");
    m_actSave->setToolTip("Sauvegarder le contour (.cnt)");
    connect(m_actSave, &QAction::triggered, this, [this]{
        QString p = EazyFileDialog::enregistrer(this, "Sauvegarder le contour", m_contour.filePath, ".cnt",
                                                m_contour.filePath);
        if(!p.isEmpty()) save(p);
    });

    // Grouper les actions de type (exclusif)
    QVector<QAction*> typeActs = {m_actL,m_actCHF,m_actRND,m_actCC,m_actC,m_actCR,m_actCT};
    for(QAction *a : typeActs){
        for(QAction *b : typeActs){
            if(a!=b) connect(a, &QAction::triggered, b, [b](bool){ b->setChecked(false); });
        }
    }
}

void ContourEditor::applyStyle()
{
    // Fond sombre global
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x0a,0x0a,0x14));
    pal.setColor(QPalette::WindowText, QColor(0xd0,0xd8,0xff));
    setPalette(pal);
    setAutoFillBackground(true);
    setStyleSheet(
        "ContourEditor{background:#0a0a14;}"
        "QSplitter{background:#0a0a14;}"
        "QSplitter::handle{background:#2a2a44;width:3px;}"
        );
}

// ═══════════════════════════════════════════════════════════════
//  buildForm — formulaire tout-en-un par type d'élément
// ═══════════════════════════════════════════════════════════════
static QLabel* mkLbl(const QString &t){
    auto *l = new QLabel(t);
    l->setStyleSheet("font-size:11px;color:#8899bb;background:transparent;");
    return l;
}
static QLineEdit* mkFld(const QString &ph="0"){
    auto *e = new QLineEdit;
    e->setPlaceholderText(ph);
    e->setStyleSheet(
        "QLineEdit{background:#1a1a2e;color:#e8eeff;border:1px solid #3a3a60;"
        "border-radius:4px;padding:5px 8px;font-size:14px;font-family:monospace;}"
        "QLineEdit:focus{border-color:#6688ff;background:#1e1e38;}");
    e->setFixedHeight(34);
    return e;
}

void ContourEditor::clearForm()
{
    // Nullifier les pointeurs membres AVANT tout
    m_fldX = m_fldY = m_fldR = m_fldLen = nullptr;
    m_btnDRplus = m_btnDRminus = nullptr;

    // Approche conteneur : supprimer m_formWidget et en creer un nouveau.
    // Qt gere le cleanup de tous les enfants automatiquement via parent-child.
    // Evite tout probleme de signal pendant la destruction manuelle.
    if (m_formWidget && m_formLay) {
        // Retirer du layout parent avant de detruire
        m_formWidget->hide();
        m_formWidget->setParent(nullptr);
        m_formWidget->deleteLater();
    }

    // Recreer un formWidget vide
    m_formWidget = new QWidget(m_dlgPanel);
    m_formLay    = new QVBoxLayout(m_formWidget);
    m_formLay->setContentsMargins(0,4,0,0);
    m_formLay->setSpacing(8);

    // Reinserer dans le panneau dialogue (apres m_lblElement, avant m_btnEnt)
    // m_dlgPanel layout : lblElement(0), formWidget(1), btnEnt(2), stretch(3), hint(4)
    auto *panLay = qobject_cast<QVBoxLayout*>(m_dlgPanel->layout());
    if (panLay) {
        panLay->insertWidget(1, m_formWidget);
    }
    m_formWidget->show();
}

void ContourEditor::buildForm(ContourElement::Type t)
{
    // Pas de pré-remplissage : les champs restent vides. Si l'utilisateur
    // valide un champ vide, fvX/fvY (dans onValidate) garderont la coord
    // précédente. Permet la saisie partielle (ex: après X=49 Y=0,
    // saisir juste Y=-49 pour ligne droite verticale).
    clearForm();

    auto addXY = [&](const QString &lblX, const QString &lblY){
        auto *row = new QHBoxLayout; row->setSpacing(6);
        auto *lx = mkLbl(lblX); lx->setMinimumWidth(60);
        m_fldX = mkFld("");                 // VIDE — pas de pré-remplissage
        auto *ly = mkLbl(lblY); ly->setMinimumWidth(60);
        m_fldY = mkFld("");                 // VIDE — pas de pré-remplissage
        m_fldX->setPlaceholderText("inchangé");
        m_fldY->setPlaceholderText("inchangé");
        row->addWidget(lx); row->addWidget(m_fldX);
        row->addWidget(ly); row->addWidget(m_fldY);
        m_formLay->addLayout(row);
        // Return key passe X→Y→ENT
        connect(m_fldX, &QLineEdit::returnPressed, this, [this]{
            m_fldY->setFocus(); m_fldY->selectAll();
        });
        connect(m_fldY, &QLineEdit::returnPressed, this, [this]{
            onValidate(); // valide sans propager Enter au dialog parent
        });
        // Live preview
        // Timer pour éviter les appels en cascade sur chaque touche
        connect(m_fldX, &QLineEdit::textChanged, this, [this]{
            QTimer::singleShot(150, this, &ContourEditor::drawCurrentPreview);
        });
        connect(m_fldY, &QLineEdit::textChanged, this, [this]{
            QTimer::singleShot(150, this, &ContourEditor::drawCurrentPreview);
        });
    };

    auto addR = [&](){
        auto *row = new QHBoxLayout; row->setSpacing(6);
        auto *lr = mkLbl("R (mm)"); lr->setMinimumWidth(60);
        m_fldR = mkFld("0.000");
        row->addWidget(lr); row->addWidget(m_fldR);
        row->addStretch();
        m_formLay->addLayout(row);
        connect(m_fldR, &QLineEdit::returnPressed, this, &ContourEditor::onValidate);
        connect(m_fldR, &QLineEdit::textChanged, this, [this]{
            QTimer::singleShot(150, this, &ContourEditor::drawCurrentPreview);
        });
    };

    auto addDR = [&](){
        auto *row = new QHBoxLayout; row->setSpacing(6);
        auto *ldr = mkLbl("Sens"); ldr->setMinimumWidth(60);
        m_btnDRplus  = new QPushButton("DR+  ↺  CCW");
        m_btnDRminus = new QPushButton("DR-  ↻  CW");
        auto drStyle = [](bool active){
            return active
                       ? "QPushButton{background:#1a4a90;color:#fff;border:2px solid #4488ee;"
                         "border-radius:4px;padding:5px 10px;font-size:12px;font-weight:700;}"
                       : "QPushButton{background:#1a1a2e;color:#8899bb;border:1px solid #3a3a60;"
                         "border-radius:4px;padding:5px 10px;font-size:12px;}";
        };
        m_btnDRplus->setStyleSheet(drStyle(true));
        m_btnDRminus->setStyleSheet(drStyle(false));
        m_btnDRplus->setCheckable(true);  m_btnDRplus->setChecked(true);
        m_btnDRminus->setCheckable(true); m_btnDRminus->setChecked(false);
        connect(m_btnDRplus,  &QPushButton::clicked, this, [=]{
            m_btnDRplus->setChecked(true); m_btnDRminus->setChecked(false);
            m_btnDRplus->setStyleSheet(drStyle(true));
            m_btnDRminus->setStyleSheet(drStyle(false));
            drawCurrentPreview();
        });
        connect(m_btnDRminus, &QPushButton::clicked, this, [=]{
            m_btnDRminus->setChecked(true); m_btnDRplus->setChecked(false);
            m_btnDRminus->setStyleSheet(drStyle(true));
            m_btnDRplus->setStyleSheet(drStyle(false));
            drawCurrentPreview();
        });
        row->addWidget(ldr); row->addWidget(m_btnDRplus); row->addWidget(m_btnDRminus);
        m_formLay->addLayout(row);
    };

    switch(t){
    case ContourElement::L:
        m_formLay->addWidget(mkLbl("Point final :"));
        addXY("X fin", "Y fin");
        break;
    case ContourElement::CHF:
        m_formLay->addWidget(mkLbl("Chanfrein (inséré avant le prochain élément) :"));
        { auto *row=new QHBoxLayout; row->setSpacing(6);
            auto *ll=mkLbl("Long.(mm)"); ll->setMinimumWidth(80);
            m_fldLen = mkFld("1.0");
            row->addWidget(ll); row->addWidget(m_fldLen); row->addStretch();
            m_formLay->addLayout(row);
            connect(m_fldLen,&QLineEdit::returnPressed,this,&ContourEditor::onValidate); }
        break;
    case ContourElement::RND:
        m_formLay->addWidget(mkLbl("Arrondi (inséré avant le prochain élément) :"));
        { auto *row=new QHBoxLayout; row->setSpacing(6);
            auto *lr=mkLbl("Rayon(mm)"); lr->setMinimumWidth(80);
            m_fldR = mkFld("1.0");
            row->addWidget(lr); row->addWidget(m_fldR); row->addStretch();
            m_formLay->addLayout(row);
            connect(m_fldR,&QLineEdit::returnPressed,this,&ContourEditor::onValidate); }
        break;
    case ContourElement::CC:
        m_formLay->addWidget(mkLbl("Centre de cercle :"));
        addXY("X ctr", "Y ctr");
        break;
    case ContourElement::C:
        m_formLay->addWidget(mkLbl("Arc (autour du dernier CC) :"));
        addXY("X fin", "Y fin");
        addDR();
        break;
    case ContourElement::CR:
        m_formLay->addWidget(mkLbl("Arc avec rayon :"));
        addXY("X fin", "Y fin");
        addR();
        addDR();
        break;
    case ContourElement::CT:
        m_formLay->addWidget(mkLbl("Arc tangentiel :"));
        addXY("X fin", "Y fin");
        break;
    }

    // Enter est géré par keyPressEvent() au niveau du ContourEditor
    // Focus sur le premier champ
    if(m_fldX)        { m_fldX->setFocus(); m_fldX->selectAll(); }
    else if(m_fldLen) { m_fldLen->setFocus(); m_fldLen->selectAll(); }
    else if(m_fldR)   { m_fldR->setFocus();  m_fldR->selectAll(); }
}

// Garde buildParams vide pour compatibilité
void ContourEditor::buildParams(ContourElement::Type t){ Q_UNUSED(t); }

// ═══════════════════════════════════════════════════════════════
//  Démarrer un élément
// ═══════════════════════════════════════════════════════════════
void ContourEditor::startElement(ContourElement::Type t)
{
    m_current = ContourElement{};
    m_current.type = t;
    m_editing = true;
    m_editingRow = -1;   // nouveau élément (pas en modification)
    m_paramIdx = 0;

    // Titre
    static const char* names[] = {"DROITE L","CHANFREIN CHF","ARRONDI RND",
                                  "CENTRE CC","ARC C","ARC CR","ARC CT"};
    m_lblElement->setText(QString("── %1 ──").arg(names[(int)t]));

    buildForm(t);
    drawCurrentPreview();
}

void ContourEditor::cancelElement()
{
    m_editing = false;
    m_editingRow = -1;
    m_params.clear();
    m_paramIdx = 0;
    m_lblElement->setText("── Choisir un élément ──");
    clearForm();
    for(QAction *a : {m_actL,m_actCHF,m_actRND,m_actCC,m_actC,m_actCR,m_actCT})
        a->setChecked(false);
    rebuildScene();
}

// showCurrentParam vide — remplacé par buildForm
void ContourEditor::showCurrentParam(){ }

// ═══════════════════════════════════════════════════════════════
//  onValidate — lit le formulaire et crée l'élément
// ═══════════════════════════════════════════════════════════════
void ContourEditor::onValidate()
{
    if(!m_editing) return;

    // Lecture champ : vide = garder la coordonnée précédente
    QPointF lastPt = m_contour.lastPoint();
    auto fv = [](QLineEdit *e) -> double {
        if(!e) return 0.0;
        return e->text().trimmed().toDouble();
    };
    auto fvX = [&](QLineEdit *e) -> double {
        if(!e || e->text().trimmed().isEmpty()) return lastPt.x();
        return e->text().trimmed().toDouble();
    };
    auto fvY = [&](QLineEdit *e) -> double {
        if(!e || e->text().trimmed().isEmpty()) return lastPt.y();
        return e->text().trimmed().toDouble();
    };

    ContourElement &e = m_current;
    switch(e.type){
    case ContourElement::L:
    case ContourElement::CT:
        e.x2 = fvX(m_fldX); e.y2 = fvY(m_fldY);
        break;
    case ContourElement::C:
        e.x2 = fvX(m_fldX); e.y2 = fvY(m_fldY);
        e.drPlus = !m_btnDRminus || !m_btnDRminus->isChecked();
        break;
    case ContourElement::CR:
        if(!m_fldR || m_fldR->text().trimmed().isEmpty()) return;
        e.x2 = fvX(m_fldX); e.y2 = fvY(m_fldY);
        e.r  = fv(m_fldR);
        e.drPlus = !m_btnDRminus || !m_btnDRminus->isChecked();
        break;
    case ContourElement::CC:
        e.cx = fvX(m_fldX); e.cy = fvY(m_fldY);
        break;
    case ContourElement::CHF:
        if(!m_fldLen || m_fldLen->text().trimmed().isEmpty()) return;
        e.longueur = fv(m_fldLen);
        break;
    case ContourElement::RND:
        if(!m_fldR || m_fldR->text().trimmed().isEmpty()) return;
        e.r = fv(m_fldR);
        break;
    }
    e.comp   = ContourElement::R0;  // correction rayon dans le bloc
    e.f      = 0.0;
    e.mFunc  = -1;

    // Differer finishElement via QTimer pour sortir du signal returnPressed
    // avant de detruire les widgets — evite delete d'un objet dont le signal
    // est encore sur la pile d'appels (undefined behavior)
    QTimer::singleShot(0, this, [this]{ finishElement(); });
}

void ContourEditor::onSkip()   { /* plus utilisé */ }
void ContourEditor::advanceDialog() { /* plus utilisé */ }
void ContourEditor::onFieldChanged(){ drawCurrentPreview(); }

// ═══════════════════════════════════════════════════════════════
//  Terminer et valider l'élément
// ═══════════════════════════════════════════════════════════════
void ContourEditor::finishElement()
{
    // onValidate() a déjà rempli m_current

    ContourElement &e = m_current;

    if (m_editingRow >= 0 && m_editingRow < m_contour.elements.size()) {
        // ── MODIFICATION d'un élément existant ──
        m_contour.elements[m_editingRow] = e;
    } else {
        // ── AJOUT d'un nouvel élément ──
        m_contour.elements << e;
    }

    // Réinitialiser
    m_editingRow = -1;
    m_editing = false;
    m_params.clear();
    m_paramIdx = 0;
    for(QAction *a : {m_actL,m_actCHF,m_actRND,m_actCC,m_actC,m_actCR,m_actCT})
        a->setChecked(false);
    m_lblElement->setText("── Choisir le prochain élément ──");
    m_btnEnt->setText("  ENT ↵  Valider");
    clearForm();

    // Reconstruire la liste (les numéros peuvent avoir changé)
    rebuildListWidget();
    rebuildScene();
    emit contourChanged();
}

// Reconstruire entièrement le QListWidget depuis m_contour.elements
void ContourEditor::rebuildListWidget()
{
    m_list->clear();
    for (int i = 0; i < m_contour.elements.size(); ++i) {
        m_list->addItem(QString("[%1] %2")
                            .arg(i+1)
                            .arg(m_contour.elements[i].summary()));
    }
}

// ═══════════════════════════════════════════════════════════════
//  Supprimer / effacer
// ═══════════════════════════════════════════════════════════════
void ContourEditor::onDeleteLast()
{
    if(m_editing){ cancelElement(); return; }
    if(m_contour.elements.isEmpty()) return;
    m_contour.elements.removeLast();
    if(m_list->count()>0) delete m_list->takeItem(m_list->count()-1);
    // Renuméroter
    for(int i=0;i<m_list->count();++i)
        if(auto *it=m_list->item(i))
            it->setText(QString("[%1] %2").arg(i+1)
                            .arg(m_contour.elements[i].summary()));
    rebuildScene();
    emit contourChanged();
}

void ContourEditor::onClearAll()
{
    if(QMessageBox::question(this,"Effacer tout",
                              "Effacer tout le contour ?",
                              QMessageBox::Yes|QMessageBox::No) != QMessageBox::Yes) return;
    m_contour.elements.clear();
    m_list->clear();
    cancelElement();
    emit contourChanged();
}

void ContourEditor::onListDoubleClicked(int row)
{
    if(row < 0 || row >= m_contour.elements.size()) return;
    const ContourElement &el = m_contour.elements[row];

    // Annuler une saisie en cours si besoin
    if(m_editing) cancelElement();

    // Mémoriser la ligne en édition
    m_editingRow = row;
    m_editing    = true;

    // Activer le bouton de type correspondant
    QAction *acts[] = {m_actL,m_actCHF,m_actRND,m_actCC,m_actC,m_actCR,m_actCT};
    for(QAction *a : acts) a->setChecked(false);
    acts[(int)el.type]->setChecked(true);

    // Titre
    static const char* names[] = {"DROITE L","CHANFREIN CHF","ARRONDI RND",
                                  "CENTRE CC","ARC C","ARC CR","ARC CT"};
    m_lblElement->setText(QString("── Modifier [%1] %2 ──").arg(row+1).arg(names[(int)el.type]));

    // Construire le formulaire ET le remplir avec les valeurs existantes
    buildForm(el.type);
    populateForm(el);

    // Centrer sur l'élément
    double fx = (el.type==ContourElement::CC) ? el.cx : el.x2;
    double fy = (el.type==ContourElement::CC) ? el.cy : el.y2;
    m_view->centerOn(fx, -fy);
}

// populateForm : remplir les champs du formulaire avec un élément existant
void ContourEditor::populateForm(const ContourElement &el)
{
    auto setFld = [](QLineEdit *f, double v){
        if(f) f->setText(v != 0.0 ? QString::number(v,'f',3) : QString());
    };
    switch(el.type){
    case ContourElement::L:
    case ContourElement::CT:
        setFld(m_fldX, el.x2); setFld(m_fldY, el.y2);
        break;
    case ContourElement::CC:
        setFld(m_fldX, el.cx); setFld(m_fldY, el.cy);
        break;
    case ContourElement::C:
        setFld(m_fldX, el.x2); setFld(m_fldY, el.y2);
        if(m_btnDRplus && m_btnDRminus){
            m_btnDRplus ->setChecked( el.drPlus);
            m_btnDRminus->setChecked(!el.drPlus);
        }
        break;
    case ContourElement::CR:
        setFld(m_fldX, el.x2); setFld(m_fldY, el.y2); setFld(m_fldR, el.r);
        if(m_btnDRplus && m_btnDRminus){
            m_btnDRplus ->setChecked( el.drPlus);
            m_btnDRminus->setChecked(!el.drPlus);
        }
        break;
    case ContourElement::CHF:
        if(m_fldLen) m_fldLen->setText(el.longueur != 0.0
                                        ? QString::number(el.longueur,'f',3) : QString());
        break;
    case ContourElement::RND:
        setFld(m_fldR, el.r);
        break;
    }

    // Changer le libellé du bouton pour signaler qu'on est en modification
    m_btnEnt->setText("  ✓ Modifier  ");
    if(m_fldX) m_fldX->setFocus();
    drawCurrentPreview();
}

// ═══════════════════════════════════════════════════════════════
//  Dessin
// ═══════════════════════════════════════════════════════════════
void ContourEditor::setWorkArea(double xMin, double xMax,
                                double yMin, double yMax)
{
    m_xMin=xMin; m_xMax=xMax; m_yMin=yMin; m_yMax=yMax;
    rebuildScene();
    QTimer::singleShot(50, this, [this]{ m_view->fitAll(); });
}

void ContourEditor::rebuildScene()
{
    if(m_rebuilding) return;
    m_rebuilding = true;
    m_scene->clear();
    drawWorkArea();
    drawGrid();
    drawAxes();
    drawContour();
    m_rebuilding = false;
}

void ContourEditor::drawWorkArea()
{
    // Rectangle BlkForm — fond légèrement teinté
    double x0=m_xMin, x1=m_xMax, y0=-m_yMax, y1=-m_yMin;
    auto *rect = m_scene->addRect(x0,y0,x1-x0,y1-y0,
                                  QPen(QColor(60,70,100,180),0),
                                  QBrush(QColor(20,25,40,120)));
    rect->setZValue(-10);

    // Texte BlkForm
    // (pas de texte BLK — problèmes font Qt avec coords mm)
}

void ContourEditor::drawGrid()
{
    // Grille légère
    double range = qMax(m_xMax-m_xMin, m_yMax-m_yMin);
    double step = range > 500 ? 50 : range > 200 ? 20 : range > 100 ? 10 : 5;
    QPen gridPen(QColor(40,50,70,120), 0);

    int ixMin = (int)std::ceil(m_xMin/step);
    int ixMax = (int)std::floor(m_xMax/step);
    for(int ix = ixMin; ix <= ixMax; ++ix){
        double x = ix * step;
        auto *l = m_scene->addLine(x,-m_yMax,x,-m_yMin,gridPen);
        l->setZValue(-9);
    }
    int iyMin = (int)std::ceil(m_yMin/step);
    int iyMax = (int)std::floor(m_yMax/step);
    for(int iy = iyMin; iy <= iyMax; ++iy){
        double y = iy * step;
        auto *l = m_scene->addLine(m_xMin,-y,m_xMax,-y,gridPen);
        l->setZValue(-9);
    }
}

void ContourEditor::drawAxes()
{
    QPen xPen(QColor(200,60,60,180),0);
    QPen yPen(QColor(60,200,60,180),0);
    m_scene->addLine(m_xMin,0,m_xMax,0,xPen)->setZValue(-8);
    m_scene->addLine(0,-m_yMax,0,-m_yMin,yPen)->setZValue(-8);
    // Étiquettes
    // Marqueurs d'axes (petits carrés colorés à la place des textes)
    double ms = (m_xMax - m_xMin) * 0.015;  // taille = 1.5% de la zone
    // Flèche X+
    QPolygonF arrowX;
    arrowX << QPointF(m_xMax,    0)
           << QPointF(m_xMax-ms, ms*0.5)
           << QPointF(m_xMax-ms,-ms*0.5);
    m_scene->addPolygon(arrowX, QPen(Qt::NoPen), QBrush(QColor(200,80,80,200)))->setZValue(-7);
    // Flèche Y+
    QPolygonF arrowY;
    arrowY << QPointF(0,    -m_yMax)
           << QPointF(-ms*0.5, -m_yMax+ms)
           << QPointF( ms*0.5, -m_yMax+ms);
    m_scene->addPolygon(arrowY, QPen(Qt::NoPen), QBrush(QColor(80,200,80,200)))->setZValue(-7);
    // Origine
    m_scene->addEllipse(-ms*0.4, -ms*0.4, ms*0.8, ms*0.8,
                        QPen(QColor(200,200,100,180),0), QBrush(QColor(200,200,100,100)))->setZValue(-6);
}

void ContourEditor::drawContour()
{
    auto segs = m_contour.toSegments();
    int selectedRow = m_list ? m_list->currentRow() : -1;
    // segs[i] correspond à m_contour.elements[i] (approx — CC/CHF/RND = 1 seg vide)

    int idx = 0;
    for(const ContourFile::Segment &seg : std::as_const(segs)){
        bool isSelected = (idx == selectedRow);
        QColor colorBase = seg.isArc ? QColor(60,180,255) : QColor(50,220,110);
        QColor colorSel  = QColor(255,200,50);  // jaune = sélectionné

        if(seg.pts.size() >= 2){
            QPainterPath path;
            path.moveTo(seg.pts[0].x(), -seg.pts[0].y());
            for(int i=1;i<seg.pts.size();++i)
                path.lineTo(seg.pts[i].x(), -seg.pts[i].y());
            QPen pen(isSelected ? colorSel : colorBase, 0);
            pen.setCosmetic(true);
            pen.setWidth(isSelected ? 3 : 2);
            auto *item = m_scene->addPath(path, pen);
            item->setZValue(isSelected ? 7 : 5);
            item->setData(0, idx);  // stocker l'index pour clic

            // Flèche directionnelle au milieu
            int mid = seg.pts.size()/2;
            QPointF a = seg.pts[qMax(0,mid-1)];
            QPointF b = seg.pts[mid];
            double dx=b.x()-a.x(), dy=b.y()-a.y();
            double len=std::sqrt(dx*dx+dy*dy);
            if(len>1e-6){
                dx/=len; dy/=len;
                double mx=(a.x()+b.x())/2, my=(a.y()+b.y())/2;
                double sz=4;
                QPolygonF arrow;
                arrow << QPointF(mx+dx*sz, -(my+dy*sz))
                      << QPointF(mx-dx*sz+dy*sz*0.5, -(my-dy*sz-dx*sz*0.5))
                      << QPointF(mx-dx*sz-dy*sz*0.5, -(my-dy*sz+dx*sz*0.5));
                QColor ac = isSelected ? colorSel : colorBase;
                m_scene->addPolygon(arrow,QPen(Qt::NoPen),QBrush(ac))->setZValue(8);
            }
        }
        // Numéro de l'élément au point de départ
        if(!seg.pts.isEmpty()){
            QPointF p0 = seg.pts[0];
            // Petit point sur chaque nœud (à la place du numéro)
            QColor dotC = isSelected ? colorSel : QColor(180,200,255,120);
            double ds = (m_xMax-m_xMin)*0.008;
            auto *dot = m_scene->addEllipse(
                p0.x()-ds, -p0.y()-ds, ds*2, ds*2,
                QPen(dotC,0), QBrush(dotC));
            dot->setZValue(9);
        }
        ++idx;
    }

    // Point courant (dernier point du contour)
    QPointF lp = m_contour.lastPoint();
    if(!m_contour.elements.isEmpty()){
        QPen ptPen(QColor(255,200,0),0); ptPen.setCosmetic(true);
        double ps = (m_xMax-m_xMin)*0.012;
        auto *pt = m_scene->addEllipse(lp.x()-ps,-lp.y()-ps,ps*2,ps*2,
                                       ptPen, QBrush(QColor(255,200,0,200)));
        pt->setZValue(12);
    }
}

void ContourEditor::drawCurrentPreview()
{
    if(m_inPreview) return;
    m_inPreview = true;
    rebuildScene();
    m_inPreview = false;
    if(!m_editing) return;

    QPointF cur = m_contour.lastPoint();
    double x2 = m_fldX ? m_fldX->text().toDouble() : cur.x();
    double y2 = m_fldY ? m_fldY->text().toDouble() : cur.y();
    double r  = m_fldR ? m_fldR->text().toDouble() : 0.0;

    if(m_current.type == ContourElement::CHF || m_current.type == ContourElement::RND)
        return;  // pas de preview géométrique

    // Si c'est le tout premier élément (contour vide) → pas de ligne depuis l'origine
    // On affiche juste le point de départ candidat
    bool isFirstElement = m_contour.elements.isEmpty();
    if (!isFirstElement) {
        QPen prevPen(QColor(255,200,50,150), 0, Qt::DashLine);
        prevPen.setCosmetic(true);
        m_scene->addLine(cur.x(), -cur.y(), x2, -y2, prevPen)->setZValue(8);
    }

    // Pour CR : dessiner l'arc approximatif
    if(m_current.type == ContourElement::CR && r > 1e-3){
        double dx=x2-cur.x(), dy=y2-cur.y(), d=std::sqrt(dx*dx+dy*dy);
        if(r >= d/2.0 + 1e-6){
            double h=std::sqrt(r*r-(d/2)*(d/2));
            double mx=(cur.x()+x2)/2, my=(cur.y()+y2)/2;
            bool drP = !m_btnDRminus || !m_btnDRminus->isChecked();
            double sign = drP ? 1.0 : -1.0;
            double px=-dy/d, py=dx/d;
            double ocx=mx+sign*h*px, ocy=my+sign*h*py;
            double a1=std::atan2(cur.y()-ocy,cur.x()-ocx);
            double a2=std::atan2(y2-ocy,x2-ocx);
            if(drP){ while(a2<a1) a2+=2*M_PI; }
            else    { while(a2>a1) a2-=2*M_PI; }
            QPainterPath arc;
            int n=qMax(8,int(48*qAbs(a2-a1)/(2*M_PI)));
            arc.moveTo(ocx+r*std::cos(a1), -(ocy+r*std::sin(a1)));
            for(int i=1;i<=n;++i){
                double a=a1+i*(a2-a1)/n;
                arc.lineTo(ocx+r*std::cos(a), -(ocy+r*std::sin(a)));
            }
            QPen arcPen(QColor(100,200,255,200),0,Qt::DashLine);
            arcPen.setCosmetic(true);
            m_scene->addPath(arc, arcPen)->setZValue(8);
        }
    }

    // Point destination
    double pd = (m_xMax-m_xMin)*0.012;
    auto *ptDst = m_scene->addEllipse(x2-pd,-y2-pd,pd*2,pd*2,
                                      QPen(QColor(255,200,50),0), QBrush(QColor(255,200,50,120)));
    ptDst->setZValue(11);
}

// ═══════════════════════════════════════════════════════════════
//  keyPressEvent — bloquer Enter/Escape de remonter au QDialog
// ═══════════════════════════════════════════════════════════════
void ContourEditor::keyPressEvent(QKeyEvent *e)
{
    // Enter/Return → valider si on est en saisie
    if(e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter){
        if(m_editing) onValidate();
        e->accept();  // ne pas propager au QDialog
        return;
    }
    // Escape → annuler élément en cours (pas fermer le dialog)
    if(e->key() == Qt::Key_Escape){
        if(m_editing) cancelElement();
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

// ═══════════════════════════════════════════════════════════════
//  Fichier
// ═══════════════════════════════════════════════════════════════
bool ContourEditor::save(const QString &path)
{
    bool ok = m_contour.save(path);
    if(ok) emit saved(m_contour.filePath);
    return ok;
}

void ContourEditor::loadFile(const QString &path)
{
    if(!m_contour.load(path)) return;
    rebuildListWidget();
    rebuildScene();
    QTimer::singleShot(50, this, [this]{ m_view->fitAll(); });
}

void ContourEditor::setContourName(const QString &n)
{
    m_contour.name = n;
}
