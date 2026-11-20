#pragma once
#ifndef SIMULATIONPAGE_H
#define SIMULATIONPAGE_H
// ═══════════════════════════════════════════════════════════════
//  SimulationPage v4 — liste G-code défilante + ligne surlignée
// ═══════════════════════════════════════════════════════════════
#include <QWidget>
#include <climits>
#include <QListWidget>
#include <QElapsedTimer>
#include <QDebug>
#include <QListWidgetItem>
#include <QCheckBox>
#include <QLabel>
#include <QProgressBar>
#include <QSlider>
#include <QSplitter>
#include <QFrame>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QToolButton>
#include <QInputDialog>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QVector>
#include <QTimer>
#include <QShowEvent>
#include <QRegularExpression>
#include <QComboBox>
#include <QColor>
#include <QFont>

#include "sim3dwidget.h"
#include "sim2dviews.h"
#include "gcodeengine.h"
#include "programmepage.h"
#include "ncfileparser.h"

// ─────────────────────────────────────────────────────────────────────
//  inlineSubFiles — pré-traitement du flux G-code pour simulation
//
//  Pour chaque ligne M98 "xxx.cnc" rencontrée, charge le sous-fichier
//  depuis ncDir et insère ses lignes dans le flux à la place du M98.
//  La ligne M99 du sous-fichier est ignorée (juste un retour).
//
//  Permet à la simulation EazyNC d'afficher la trajectoire COMPLÈTE,
//  y compris ce qui est dans les sous-fichiers contour.
//
//  Note : ne suit PAS la compensation G42/G10 (la trajectoire affichée
//  est celle programmée = centre fraise non compensé). Pour voir l'effet
//  de la compensation, il faudrait un parser plus avancé.
// ─────────────────────────────────────────────────────────────────────
static QVector<GCodeLine> inlineSubFiles(const QVector<GCodeLine> &lines,
                                         const QString &ncDir)
{
    if (ncDir.isEmpty()) return lines;
    QVector<GCodeLine> out;
    out.reserve(lines.size());

    static const QRegularExpression reM98(
        "M98\\s+(?:\"([^\"]+)\"|P?(\\S+))",
        QRegularExpression::CaseInsensitiveOption);

    for (const GCodeLine &l : lines) {
        QRegularExpressionMatch m = reM98.match(l.code);
        if (!m.hasMatch()) { out << l; continue; }

        QString subName = m.captured(1);
        if (subName.isEmpty()) subName = m.captured(2);
        if (subName.isEmpty())  { out << l; continue; }

        // Construire le chemin complet
        QString subPath = ncDir + "/" + subName;
        // Ajouter .cnc si pas d'extension
        if (!subName.contains('.')) subPath += ".cnc";

        QFile f(subPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // Sous-fichier introuvable : laisser la ligne M98 telle quelle
            GCodeLine warn;
            warn.type = GCodeLine::Comment;
            warn.code = QString("( WARN sim: sous-fichier non trouve %1 )").arg(subPath);
            warn.blockId = l.blockId;
            out << warn;
            out << l;
            continue;
        }

        QTextStream in(&f);
        in.setEncoding(QStringConverter::Latin1);

        // Marqueur de début d'inline
        GCodeLine markStart;
        markStart.type = GCodeLine::Comment;
        markStart.code = QString("( --- inline %1 --- )").arg(subName);
        markStart.blockId = l.blockId;
        out << markStart;

        while (!in.atEnd()) {
            QString raw = in.readLine().trimmed();
            if (raw.isEmpty()) continue;
            // Sauter commentaires
            if (raw.startsWith("(") || raw.startsWith(";") || raw.startsWith(":")) continue;
            // Fin du sous-prog
            if (raw.contains("M99", Qt::CaseInsensitive)) break;

            // Convertir en GCodeLine
            GCodeLine subLine;
            subLine.code = raw;
            subLine.blockId = l.blockId;
            // Déterminer le type
            QString up = raw.toUpper();
            if (up.contains(QRegularExpression("\\bG0+\\b|\\bG00\\b")))
                subLine.type = GCodeLine::Rapid;
            else if (up.contains(QRegularExpression("\\bG[23]\\b|\\bG0[23]\\b")))
                subLine.type = GCodeLine::Arc;
            else if (up.contains(QRegularExpression("\\bG0?1\\b|\\bX|\\bY|\\bZ")))
                subLine.type = GCodeLine::Linear;
            else
                subLine.type = GCodeLine::Other;
            out << subLine;
        }
        f.close();

        // Marqueur de fin d'inline
        GCodeLine markEnd;
        markEnd.type = GCodeLine::Comment;
        markEnd.code = QString("( --- end inline %1 --- )").arg(subName);
        markEnd.blockId = l.blockId;
        out << markEnd;
    }
    return out;
}

// ── Couleurs du G-code décidées à l'affichage (lignes visibles seulement) ──
//  Évite de colorer 50 000+ lignes une par une au chargement.
#include <QStyledItemDelegate>
class GcodeCouleurDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
protected:
    void initStyleOption(QStyleOptionViewItem *opt, const QModelIndex &idx) const override
    {
        QStyledItemDelegate::initStyleOption(opt, idx);
        const QString t = idx.data().toString().mid(7).trimmed();   // après le n° de ligne
        QColor c(0x11,0x11,0x22);                                   // G1 : noir
        if (t.startsWith('(')) { c = QColor(0x60,0x80,0x40); opt->font.setItalic(true); }
        else if (t.startsWith("G0 ") || t == "G0" || t.startsWith("G00")) c = QColor(0xcc,0x22,0x22);
        else if (t.startsWith("G2") || t.startsWith("G3") || t.startsWith("G02") || t.startsWith("G03"))
            c = QColor(0x11,0x66,0xcc);
        else if (t.startsWith('T') || t.startsWith("G43")) { c = QColor(0xcc,0x66,0x00); opt->font.setBold(true); }
        else if (t.startsWith('M') || t.startsWith('S')) c = QColor(0x88,0x22,0xaa);
        else if (t.startsWith('G') && !t.startsWith("G1 ") && !t.startsWith("G01")) c = QColor(0x44,0x55,0x66);
        opt->palette.setColor(QPalette::Text, c);
    }
};

class SimulationPage : public QWidget
{
    Q_OBJECT
public:
    explicit SimulationPage(QWidget *parent=nullptr): QWidget(parent){ buildUi(); }

    // ── Chargement .enc ─────────────────────────────────────────
    // On peut passer en option le dossier où chercher les sous-fichiers .cnc
    // (typiquement le dossier du .enc). Si vide, on utilise m_ncDir membre.
    void load(const QVector<BlockData> &blocks, const QVector<GCodeLine> &gcode,
              const QString &ncDir = QString())
    {
        m_lastBlocks=blocks;
        // Pré-traitement : inliner les sous-fichiers .cnc référencés par M98
        QString dir = ncDir.isEmpty() ? m_ncDir : ncDir;
        m_lastGcode = inlineSubFiles(gcode, dir);

        StockData stock;
        for(const BlockData &b:blocks){
            if(b.type==BlockType::BlkForm){
                stock.xMin=float(b.data["xMinus"].toDouble());
                stock.xMax=float(b.data["xPlus"] .toDouble());
                stock.yMin=float(b.data["yMinus"].toDouble());
                stock.yMax=float(b.data["yPlus"] .toDouble());
                stock.zMin=float(b.data["zMinus"].toDouble());
                stock.zMax=float(b.data["zPlus"] .toDouble());
                stock.valid=true; break;
            }
        }
        populateGcodeList(m_lastGcode);
        loadIntoView(stock, m_lastGcode,
                     QString("%1 blocs  |  %2 lignes NC").arg(blocks.size()).arg(m_lastGcode.size()));
    }

    // ── Chargement .nc ──────────────────────────────────────────
    void loadNcFile(const QString &path)
    {
        QElapsedTimer chrono; chrono.start();
        QVector<GCodeLine> lines = NcFileParser::parse(path);
        qDebug() << "[SIM] lecture .nc :" << lines.size() << "lignes en" << chrono.elapsed() << "ms";
        if (lines.isEmpty()) return;
        // Inliner depuis le dossier du .nc
        m_ncDir = QFileInfo(path).absolutePath();
        lines = inlineSubFiles(lines, m_ncDir);
        m_lastGcode = lines;
        m_lastBlocks.clear();
        StockData stock = inferStock(lines);
        populateGcodeList(lines);
        QElapsedTimer c2; c2.start();
        loadIntoView(stock, lines,
                     QString("Fichier NC  |  %1 lignes").arg(lines.size()));
        qDebug() << "[SIM] mise en vue 3D en" << c2.elapsed() << "ms, total" << chrono.elapsed() << "ms";
    }

    // Permet de définir le dossier où chercher les sous-fichiers (depuis mainwindow)
    void setNcDir(const QString &dir) { m_ncDir = dir; }
    // Brut et rayon d'outil déduits du G-code (aussi pour la vue 3D de la page Exécution)
    static StockData stockDepuisGcode(const QVector<GCodeLine> &l) { return inferStock(l); }
    static double    rayonOutil(const QVector<GCodeLine> &l)       { return extractRTool(l); }

    // ── API softkeys ────────────────────────────────────────────
    void fitAll()            { if(m_view) m_view->fitAll(); if(m_plan2D) m_plan2D->fitAll(); }
    void toggleRapids()      { bool v=!m_chkRapids->isChecked(); m_chkRapids->setChecked(v); if(m_view) m_view->setShowRapids(v); if(m_plan2D) m_plan2D->setShowRapids(v); }
    void toggleStock()       { bool v=!m_chkStock->isChecked();  m_chkStock->setChecked(v);  if(m_view) m_view->setShowStock(v); }
    void toggleTransparent() { /* géré par les boutons cube */ }
    void togglePath()        { bool v=!m_chkPath->isChecked();   m_chkPath->setChecked(v);   if(m_view) m_view->setShowToolPath(v); }
    void togglePlanView()    { onTogglePlanView(); }
    // ── Wrappers softkeys barre du bas ─────────────────────
    void skPlay()   { onPlay(); }
    void skPause()  { onPause(); }
    void skReset()  { onReset(); }
    void skGoto()   { onGoto(); }
    void skMesure() { onMesure(); }
    void skClearMesure() { if(m_view) m_view->clearMeasure(); m_lblMesure->setVisible(false); }
    void skLoadEnc(){ emit loadEncRequested(); }
    void skLoadNc() { onChargerNc(); }
    void skResetCnc(){ emit resetRequested(); }
    void skSetStockOpaque(bool opaque){
        m_stockMode = opaque ? 1 : 0;
        if(m_view){ m_view->setStockAlpha(opaque ? 0.975f : kStockAlphaTransp); m_view->setStockTransparent(false); }
    }
    // Bascule matière pleine ↔ transparente (softkey « Matière »)
    void toggleStockMode() { skSetStockOpaque(m_stockMode != 1); }
    bool isStockOpaque() const { return m_stockMode == 1; }
    static constexpr float kStockAlphaTransp = 0.60f;   // opacité du mode transparent
    // Opacité variable du brut (0.0 → 1.0)
    void skSetStockAlpha(float a){
        if(m_view){ m_view->setStockAlpha(a); m_view->setStockTransparent(false); }
    }
    float stockAlpha() const { return m_view ? m_view->stockAlpha() : 0.5f; }
    Sim3DWidget *view() const { return m_view; }

signals:
    void loadEncRequested();
    void cycleStartRequested();
    void resetRequested();
    void gotoLineRequested(int line);
    void requestOpenFile();

protected:
    void showEvent(QShowEvent *e) override
    {
        QWidget::showEvent(e);
        if(m_viewCreated) return;
        m_viewCreated=true;

        m_view=new Sim3DWidget;
        m_view->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        connect(m_view, &Sim3DWidget::parcoursPret, this, [this]{
            m_lblInfo->setText(m_infoVue + QString::fromUtf8("  —  ▶ Play pour simuler"));
            qDebug() << "[SIM] vue 3D prete";
        });

        // ── Vues 2D plan (dessin technique) ──────────────────
        m_plan2D = new Sim2DViews;
        m_plan2D->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);

        // Stack 3D / Plan2D
        m_viewStack = new QStackedWidget;
        m_viewStack->addWidget(m_view);     // index 0 = 3D
        m_viewStack->addWidget(m_plan2D);   // index 1 = Plan 2D
        m_viewStack->setCurrentIndex(0);
        // Overlay top-right : résultat détection face
        m_lblFace = new QLabel(m_view);
        m_lblFace->setVisible(false);
        m_lblFace->setTextFormat(Qt::RichText);
        m_lblFace->setWordWrap(true);
        m_lblFace->setAlignment(Qt::AlignLeft|Qt::AlignTop);
        m_lblFace->setStyleSheet(
            "QLabel{"
            "background:rgba(10,12,22,0.82);"
            "border:1px solid rgba(100,140,200,0.5);"
            "border-radius:5px;"
            "color:#00AA00;"
            "font-size:11px;"
            "font-family:'IBM Plex Mono','Courier New',monospace;"
            "padding:8px 12px;"
            "}");
        m_lblFace->setFixedWidth(240);
        m_lblFace->move(8,8);
        // Légende chemin outil — bas-droite du widget 3D
        m_lblLegend = new QLabel(m_view);
        m_lblLegend->setTextFormat(Qt::RichText);
        m_lblLegend->setStyleSheet(
            "QLabel{"
            "background:rgba(20,25,35,0.88);"
            "border:1px solid rgba(180,190,210,0.5);"
            "border-radius:6px;"
            "color:#FFFFFF;"
            "font-size:13px;"
            "font-weight:600;"
            "font-family:'IBM Plex Mono','Courier New',monospace;"
            "padding:10px 14px;"
            "}");
        m_lblLegend->setText(
            "<span style='color:#FF4444;'>&#9644;</span> G0 Rapide<br>"
            "Matière usinée :<br>"
            "<span style='color:#5A8CE6;'>&#9632;</span> T1 &nbsp;"
            "<span style='color:#66C773;'>&#9632;</span> T2 &nbsp;"
            "<span style='color:#F29940;'>&#9632;</span> T3 &nbsp;"
            "<span style='color:#AD73E0;'>&#9632;</span> T4<br>"
            "Fraise : <span style='color:#E6382F;'>&#9679;</span> coupe &nbsp;"
            "<span style='color:#40B852;'>&#9679;</span> dans le vide");
        m_lblLegend->adjustSize();
        m_lblLegend->setVisible(true);
        m_splitter->addWidget(m_viewStack);
        m_splitter->setStretchFactor(0,0);
        m_splitter->setStretchFactor(1,1);
        m_splitter->setSizes({340,9999});

        connect(m_chkPath,   &QCheckBox::toggled, m_view, &Sim3DWidget::setShowToolPath);
        connect(m_chkRapids, &QCheckBox::toggled, m_view, &Sim3DWidget::setShowRapids);
        connect(m_chkStock,  &QCheckBox::toggled, m_view, &Sim3DWidget::setShowStock);
        // État de départ : chemin d'outil masqué, matière pleine ou
        // transparente selon le mode choisi (softkey « Matière »)
        m_view->setShowToolPath(m_chkPath->isChecked());
        m_view->setStockAlpha(m_stockMode==1 ? 0.975f : kStockAlphaTransp);

        // Progression heightmap
        connect(m_view,&Sim3DWidget::progressChanged,this,[this](int v){
            m_barHmap->setValue(v); m_barHmap->setVisible(v<100);
        });
        // Progression animation
        connect(m_view,&Sim3DWidget::animProgressChanged,m_barAnim,&QProgressBar::setValue);
        // Play/pause
        connect(m_view,&Sim3DWidget::playingChanged,this,[this](bool p){
            m_btnPlay->setVisible(!p); m_btnPause->setVisible(p);
        });
        // Suivi ligne + position outil dans les vues 2D
        connect(m_view,&Sim3DWidget::currentLineChanged,this,[this](int li){
            highlightLine(li);
            // Synchroniser position outil dans plan 2D
            if(m_plan2D && m_view){
                QVector3D pos = m_view->property("toolPos").value<QVector3D>();
                // On lit m_toolPos via le signal : on utilise les moves
                // La position est accessible via le dernier point de la ligne courante
                Q_UNUSED(pos);
            }
        });
        // Mesure
        connect(m_view,&QOpenGLWidget::resized,this,[this]{ repositionFaceLabel(); });
        connect(m_view,&Sim3DWidget::faceDetected,this,[this](const QString &html){
            if(html.isEmpty()){ m_lblFace->setVisible(false); return; }
            m_lblFace->setText(html);
            m_lblFace->adjustSize();
            // Positionner top-right du widget 3D
            int margin=10;
            m_lblFace->move(m_view->width()-m_lblFace->width()-margin, margin);
            m_lblFace->setVisible(true);
        });
        connect(m_view,&Sim3DWidget::measureResult,this,[this](const QString &html){
            m_lblMesure->setText(html);
            m_lblMesure->setVisible(true);
        });
        // Vitesse
        connect(m_sliderSpeed,&QSlider::valueChanged,this,[this](int v){
            if(m_view) m_view->setSpeed(v);
            m_lblSpeed->setText(QString("Vitesse : %1x").arg(v));
        });
        // Résolution automatique (≈0,15 mm/case) : voir setResolution ci-dessous

        // Positionner la légende dès que le widget a sa taille
        QTimer::singleShot(50,this,[this]{ repositionFaceLabel(); });
        m_view->setResolution(0);     // 0 = automatique selon la taille du brut

        if(!m_pendingGcode.isEmpty()){
            if(m_pendingStock.valid) m_view->setStock(m_pendingStock);
            updateLegend(m_pendingGcode);
            m_view->fitAll();
            m_lblInfo->setText(QString::fromUtf8("Préparation de la vue 3D…"));
            m_view->buildMovesOnlyAsync(m_pendingGcode,extractRTool(m_pendingGcode));
            // Plan 2D : calculé seulement quand il est affiché (sinon au passage en Plan 2D)
            if(m_plan2D && m_viewStack && m_viewStack->currentIndex()==1) m_plan2D->loadFromGCode(m_pendingGcode);
            m_pendingGcode.clear();
        }
    }

private slots:
    void onPlay()  { if(m_view) m_view->play(); }
    void onTogglePlanView() {
        if(!m_viewStack) return;
        bool goingTo2D = (m_viewStack->currentIndex() == 0);
        m_viewStack->setCurrentIndex(goingTo2D ? 1 : 0);
        m_btnToggleView->setText(goingTo2D ? "◧ Vue 3D" : "▦ Plan 2D");
        m_btnToggleView->setStyleSheet(btnS(goingTo2D ? "#EBEBEB" : "#EBEBEB",
                                            goingTo2D ? "#122a5a" : "#C8C8C8"));
        // Synchroniser les rapides
        if(goingTo2D && m_plan2D && !m_lastGcode.isEmpty()){
            m_plan2D->loadFromGCode(m_lastGcode);
            m_plan2D->fitAll();
        }
    }
    void onPause() { if(m_view) m_view->pause(); }
    void onReset() {
        if(!m_view) return;
        m_view->resetSim();
        m_barAnim->setValue(0);
        m_btnPlay->setVisible(true); m_btnPause->setVisible(false);
        highlightLine(-1);
    }
    void onGoto() {
        bool ok;
        int l=QInputDialog::getInt(this,"Goto ligne","Aller à la ligne :",
                                     m_highlightedRow+1,1,
                                     m_gcodeList->count()>0?m_gcodeList->count():99999,
                                     1,&ok);
        if(!ok) return;
        int idx=l-1;
        // Scroller dans la liste G-code
        if(m_gcodeList && idx>=0 && idx<m_gcodeList->count()){
            highlightLine(idx);
            if (m_view) m_view->seekToLine(idx);
        }
        // Notifier pour sync avec la simulation
        emit gotoLineRequested(l);
    }
    void repositionFaceLabel() {
        int margin=10;
        if(m_lblFace&&m_view&&m_lblFace->isVisible())
            m_lblFace->move(m_view->width()-m_lblFace->width()-margin, margin);
        if(m_lblLegend&&m_view)
            m_lblLegend->move(m_view->width()-m_lblLegend->width()-margin,
                              m_view->height()-m_lblLegend->height()-margin);
    }

    void onMesure() {
        m_measureActive = !m_measureActive;
        if(m_view) m_view->setMeasureMode(m_measureActive);
        if(m_measureActive){
            m_btnMesure->setStyleSheet(btnS("#EBEBEB","#C8C8C8"));
            m_btnMesure->setText("📏 Mesure ON");
            m_lblMesure->setText("<span style='color:#00AA00'>Clic sur la surface pour mesurer.<br>2 clics = distance entre points.</span>");
            m_lblMesure->setVisible(true);
        } else {
            m_btnMesure->setStyleSheet(btnS("#EBEBEB","#C8C8C8"));
            m_btnMesure->setText("📏 Mesure");
            if(m_view) m_view->clearMeasure();
            m_lblMesure->setVisible(false);
        }
    }
    void onChargerNc() {
        // Signal vers MainWindow pour ouvrir le gestionnaire de fichiers
        emit requestOpenFile();
    }

private:
    QVector<BlockData>  m_lastBlocks;
    QVector<GCodeLine>  m_lastGcode;
    QVector<GCodeLine>  m_pendingGcode;
    StockData           m_pendingStock;
    bool                m_viewCreated=false;
    int                 m_highlightedRow=-1;
    QString             m_ncDir;   // dossier où chercher les sous-fichiers .cnc (M98)
    QString             m_infoVue; // texte d'information du programme affiché

    Sim3DWidget   *m_view       =nullptr;
    Sim2DViews    *m_plan2D     =nullptr;
    QStackedWidget*m_viewStack  =nullptr;
    QPushButton   *m_btnToggleView=nullptr;
    QSplitter    *m_splitter   =nullptr;
    QListWidget  *m_gcodeList  =nullptr;
    QCheckBox    *m_chkRapids  =nullptr;
    QCheckBox    *m_chkPath    =nullptr;
    QCheckBox    *m_chkStock   =nullptr;
    QCheckBox    *m_chkTransp  =nullptr;  // gardé pour toggleTransparent() existant
    QToolButton  *m_btnCubeTransp=nullptr;
    QToolButton  *m_btnCubePlein =nullptr;
    int           m_stockMode  =1;  // 0=transparent, 1=plein (défaut)
    QProgressBar *m_barHmap    =nullptr;
    QProgressBar *m_barAnim    =nullptr;
    QLabel       *m_lblInfo    =nullptr;
    QLabel       *m_lblSpeed   =nullptr;
    QLabel       *m_lblMesure  =nullptr;   // résultat mesure
    QSlider      *m_sliderSpeed=nullptr;
    QPushButton  *m_btnPlay    =nullptr;
    QPushButton  *m_btnPause   =nullptr;
    QPushButton  *m_btnMesure  =nullptr;
    bool          m_measureActive = false;
    QLabel       *m_lblFace    =nullptr;   // overlay analyse face
    QLabel       *m_lblLegend  =nullptr;   // légende couleurs chemin outil

    // ── Peupler la liste avec les lignes G-code ──────────────────
    void populateGcodeList(const QVector<GCodeLine> &gcode)
    {
        // Gros programmes (50 000+ lignes) : toutes les lignes ajoutées
        // d'un coup ; les couleurs sont faites à l'affichage (délégué).
        QElapsedTimer chrono; chrono.start();
        if (!m_gcodeList->itemDelegate() || !dynamic_cast<GcodeCouleurDelegate*>(m_gcodeList->itemDelegate()))
            m_gcodeList->setItemDelegate(new GcodeCouleurDelegate(m_gcodeList));
        QFont f("Consolas",10); f.setFixedPitch(true);
        m_gcodeList->setFont(f);
        QStringList lignes;
        lignes.reserve(gcode.size());
        for(int i=0;i<gcode.size();i++){
            const GCodeLine &gl=gcode[i];
            QString text=gl.code.trimmed();
            if(!gl.comment.isEmpty()){
                if(text.isEmpty()) text="( "+gl.comment+" )";
                else text+="  ( "+gl.comment+" )";
            }
            if(text.isEmpty()) text=" ";
            lignes << QString("%1").arg(i+1,5)+"  "+text;
        }
        m_gcodeList->setUpdatesEnabled(false);
        m_gcodeList->clear();
        m_highlightedRow=-1;
        m_gcodeList->addItems(lignes);
        m_gcodeList->setUpdatesEnabled(true);
        qDebug() << "[SIM] liste G-code :" << gcode.size() << "lignes en" << chrono.elapsed() << "ms";
    }

    // ── Surligner la ligne courante et scroller ──────────────────
    void highlightLine(int li)
    {
        // Plus de passage en gras : changer la police d'une ligne force Qt
        // à recalculer toute la liste (des dizaines de ms à chaque ligne,
        // c'est ce qui freinait la simu). La sélection suffit à la repérer.
        if(li==INT_MAX) li=m_gcodeList->count()-1;   // fin de programme
        m_highlightedRow=li;
        if(li<0||li>=m_gcodeList->count()){ m_gcodeList->clearSelection(); return; }
        auto *item=m_gcodeList->item(li);
        // Sélection → applique le cadre rectangulaire vert du stylesheet
        m_gcodeList->setCurrentItem(item);
        m_gcodeList->scrollToItem(item,QAbstractItemView::PositionAtCenter);
    }

    // Legende : les outils reellement utilises, avec leur couleur
    void updateLegend(const QVector<GCodeLine> &gcode)
    {
        if(!m_lblLegend) return;
        static const char* pal[8]={"#5A8CE6","#66C773","#F29940","#AD73E0",
                                   "#4DC7D1","#EBD14D","#E06673","#80AD8C"};
        QList<int> tools;
        static const QRegularExpression re("\\bT\\s*(\\d+)");
        for(const GCodeLine &l : gcode){
            if(l.code.contains("M6") || l.code.contains("M06")){
                auto m=re.match(l.code);
                if(m.hasMatch()){ int t=m.captured(1).toInt(); if(t>0 && !tools.contains(t)) tools<<t; }
            }
        }
        QString txt="<span style='color:#FF4444;'>&#9644;</span> G0 Rapide<br>Matière usinée :<br>";
        if(tools.isEmpty()) txt+="-<br>";
        int n=0;
        for(int t : tools){
            txt+=QString("<span style='color:%1;'>&#9632;</span> T%2 &nbsp;").arg(pal[(t-1)%8]).arg(t);
            if(++n%4==0) txt+="<br>";
        }
        if(n%4!=0) txt+="<br>";
        txt+="Fraise : <span style='color:#E6382F;'>&#9679;</span> coupe &nbsp;"
             "<span style='color:#40B852;'>&#9679;</span> dans le vide";
        m_lblLegend->setText(txt);
        m_lblLegend->adjustSize();
        if(m_view){
            const int margin=10;
            m_lblLegend->move(m_view->width()-m_lblLegend->width()-margin,
                              m_view->height()-m_lblLegend->height()-margin);
        }
    }

    void loadIntoView(const StockData &stock,const QVector<GCodeLine> &gcode,
                      const QString &info)
    {
        QElapsedTimer chronoVue; chronoVue.start();
        updateLegend(gcode);
        m_barHmap->setVisible(false);
        m_barAnim->setValue(0);
        m_infoVue = info;
        m_lblInfo->setText(info+"  —  ▶ Play pour simuler");
        m_btnPlay->setVisible(true);
        m_btnPause->setVisible(false);

        if(m_viewCreated && m_view){
            if(stock.valid) {
                m_view->setStock(stock);
                m_view->onRebuildHeightmap();   // 🔥 reconstruction du solide
            }

            m_view->fitAll();
            m_lblInfo->setText(QString::fromUtf8("Préparation de la vue 3D…"));
            m_view->buildMovesOnlyAsync(gcode, extractRTool(gcode));   // lecture dans un autre fil : l'écran reste libre

            qDebug() << "[SIM] brut + lancement lecture" << chronoVue.elapsed() << "ms";
            if(m_plan2D && m_viewStack && m_viewStack->currentIndex()==1){   // sinon au passage en Plan 2D
                m_plan2D->loadFromGCode(gcode);
                m_plan2D->fitAll();
            }
            qDebug() << "[SIM] vue plan 2D" << chronoVue.elapsed() << "ms";
        }
        else {
            m_pendingGcode = gcode;
            m_pendingStock = stock;
        }
    }

    static StockData inferStock(const QVector<GCodeLine> &lines)
    {
        // ── Algorithme ────────────────────────────────────────
        // XY : max des moves coupe (trajectoires centre outil) — marge 1 mm seulement
        // ZMax : cherche un G0 Z positif ou nul avant les coupes = surface du brut
        //         fallback : 0 (convention usinage depuis Z=0)
        // ZMin : minimum des coupes - 2mm buffer
        double xMn=1e9,xMx=-1e9,yMn=1e9,yMx=-1e9;
        double zMnCut=1e9, zMnAll=1e9;
        double zSurface=0.0;
        bool   anyXY=false, anyZ=false, surfaceFound=false;
        double x=0,y=0,z=0;

        auto pv=[](const QString &c,QChar ch,double &out)->bool{
            // Supprimer commentaires ; avant analyse
            QString cc=c; { int sc=cc.indexOf(';'); if(sc>=0) cc=cc.left(sc); }
            QString up=cc.toUpper(); QChar u=ch.toUpper();
            int i=-1; for(int k=0;k<up.size();k++) if(up[k]==u){i=k;break;}
            if(i<0) return false;
            int j=i+1; while(j<up.size()&&up[j]==' ')++j;
            int st=j; if(j<up.size()&&(up[j]=='-'||up[j]=='+'))++j;
            while(j<up.size()&&(up[j].isDigit()||up[j]=='.'))++j;
            if(st>=up.size()||j>up.size()||j<=st) return false;
            if(st>=c.size()||j>c.size()) return false;
            bool ok; out=c.mid(st,j-st).toDouble(&ok); return ok;
        };

        static const QRegularExpression reRapid(R"(\bG0*0\b)");
        static const QRegularExpression reG1(R"(\bG0*1\b)");
        static const QRegularExpression reG23(R"(\bG0*[23]\b)");

        // ── Stock Centroid/Intercon : :::: X- = -75, X+ = 75 ───
        // Format : :::: X- = <val>, X+ = <val>
        //          :::: Y- = <val>, Y+ = <val>
        //          :::: Z- = <val>, Z+ = <val>
        static const QRegularExpression reCentroidStock(
            R"(::::.*X-\s*=\s*(-?[\d.]+).*X\+\s*=\s*(-?[\d.]+))",
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reCentroidStockY(
            R"(::::.*Y-\s*=\s*(-?[\d.]+).*Y\+\s*=\s*(-?[\d.]+))",
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reCentroidStockZ(
            R"(::::.*Z-\s*=\s*(-?[\d.]+).*Z\+\s*=\s*(-?[\d.]+))",
            QRegularExpression::CaseInsensitiveOption);
        StockData centroidStock; centroidStock.valid=false;
        for(const GCodeLine &gl:lines){
            QString raw=gl.code + " " + gl.comment;  // chercher dans code ET comment
            auto mX=reCentroidStock.match(raw);
            auto mY=reCentroidStockY.match(raw);
            auto mZ=reCentroidStockZ.match(raw);
            if(mX.hasMatch()){
                centroidStock.xMin=mX.captured(1).toFloat();
                centroidStock.xMax=mX.captured(2).toFloat();
                centroidStock.valid=true;
            }
            if(mY.hasMatch()){
                centroidStock.yMin=mY.captured(1).toFloat();
                centroidStock.yMax=mY.captured(2).toFloat();
            }
            if(mZ.hasMatch()){
                centroidStock.zMin=mZ.captured(1).toFloat();
                centroidStock.zMax=mZ.captured(2).toFloat();
            }
        }
        if(centroidStock.valid) return centroidStock;   // stock exact fourni par Centroid

        bool inferG91=false;   // G91 dans le fichier affecte le calcul de stock aussi

        for(const GCodeLine &gl:lines){
            if(gl.type==GCodeLine::Comment||gl.type==GCodeLine::Blank) continue;
            const QString &code=gl.code;
            QString up=code.toUpper().simplified();
            if(up.contains(R"(G90)")) inferG91=false;
            if(up.contains(R"(G91)")) inferG91=true;
            bool isRapid = gl.type==GCodeLine::Rapid || up.contains(reRapid);
            bool isCut   = (!isRapid) && (gl.type==GCodeLine::Linear||
                                        gl.type==GCodeLine::Arc||
                                        up.contains(reG1)||up.contains(reG23));

            double px,py,pz;
            bool hx=false,hy=false,hz=false;
            if(pv(code,'X',px)){
                if(inferG91){ x+=px; } else { x=px; }
                hx=true;
            }
            if(pv(code,'Y',py)){
                if(inferG91){ y+=py; } else { y=py; }
                hy=true;
            }
            if(pv(code,'Z',pz)){
                if(inferG91){ z+=pz; } else { z=pz; }
                hz=true;
            }

            // XY : depuis les coupes ET les rapides (évite de rater des zones éloignées)
            if((isCut||isRapid)&&(hx||hy)){
                xMn=qMin(xMn,x);xMx=qMax(xMx,x);
                yMn=qMin(yMn,y);yMx=qMax(yMx,y);
                anyXY=true;
            }
            // Z surface : G0 Z positif ou nul avant toute coupe
            if(isRapid&&hz&&!anyZ&&z>=0.0&&!surfaceFound){
                zSurface=z;  // ex: G0 Z2.0 → surface approche
                surfaceFound=true;
            }
            // Z min : minimum des coupes
            if(isCut&&hz){
                zMnCut=qMin(zMnCut,z);
                anyZ=true;
            }
            // Z min absolu : tous moves (G0 retrait saut bride donne le fond potentiel)
            if(hz) zMnAll=qMin(zMnAll,z);
        }
        StockData s; if(!anyXY) return s;

        // Surface brut = légèrement au-dessus du plan Z=0 (ou du G0 d'approche)
        // Si G0 Z2.0 trouvé → surface à Z=0 (l'approche est au-dessus de la surface)
        // Convention : surface brut = 0 sauf si G0 Z négatif (brut déjà en dépouille)
        double zTop = (surfaceFound && zSurface>=0) ? 0.0 : zSurface;
        // zBot : prendre le minimum entre zMnCut-surépaisseur et zMnAll
        // zMnAll capture les G0 de dégagement profonds (ex: saut bride Z50 n'affecte pas)
        double zBotCut = anyZ  ? zMnCut - 2.0 : -50.0;
        double zBotAll = (zMnAll<1e8 && zMnAll<0) ? zMnAll - 2.0 : zBotCut;
        double zBot    = qMin(zBotCut, zBotAll);
        // Plancher minimum raisonnable : au moins 10mm sous la coupe la plus profonde
        zBot = qMin(zBot, anyZ ? zMnCut - 10.0 : -50.0);

        // XY : ajouter rayon outil pour avoir le brut réel
        // (les coordonnées G-code = centre outil, le brut est plus grand d'un rayon)
        float rT = float(extractRTool(lines));
        if(rT < 0.5f) rT = 5.0f;   // fallback 5mm si non trouvé
        s.xMin=float(xMn)-rT; s.xMax=float(xMx)+rT;
        s.yMin=float(yMn)-rT; s.yMax=float(yMx)+rT;
        s.zMin=float(zBot);   s.zMax=float(zTop);
        s.valid=true; return s;
    }
    static double extractRTool(const QVector<GCodeLine> &g)
    {
        // Regex "[DR]outil = X" → X est le RAYON direct (ex: "D = Routil = 5.95 mm")
        static const QRegularExpression r1(R"([DR]outil\s*=\s*([0-9.]+))",QRegularExpression::CaseInsensitiveOption);
        // Regex "D=X mm" seul → X est un DIAMETRE (ex: "D=5.95 mm" dans commentaire vitesse)
        static const QRegularExpression r2(R"(D\s*=\s*([0-9.]+)\s*mm)",QRegularExpression::CaseInsensitiveOption);
        // 1er passage : chercher Routil (source la plus fiable)
        for(const GCodeLine &gl:g){
            auto m=r1.match(gl.comment);
            if(m.hasMatch()) return m.captured(1).toDouble();  // rayon direct
        }
        // 2ème passage : fallback sur D=X mm (diamètre → /2)
        for(const GCodeLine &gl:g){
            auto m2=r2.match(gl.comment);
            if(m2.hasMatch()) return m2.captured(1).toDouble()/2.0;
        }
        return 3.0;
    }

    static QString btnS(const QString &bg,const QString &bgh="")
    {
        QString h=bgh.isEmpty()?bg:bgh;
        return QString(
                   "QPushButton{background:%1;color:#000000;font-size:10pt;font-weight:bold;"
                   "border:1px solid rgba(166,206,57,0.25);border-radius:4px;padding:5px 4px;}"
                   "QPushButton:hover{background:%2;color:#000000;border:1px solid #00AA00;}"
                   "QPushButton:pressed{background:%1;color:#000000;"
                   "border:1px solid #606060;}").arg(bg,h);
    }

    void buildUi()
    {
        auto *root=new QVBoxLayout(this);
        root->setContentsMargins(0,0,0,0); root->setSpacing(0);

        m_splitter=new QSplitter(Qt::Horizontal);
        m_splitter->setHandleWidth(5);
        m_splitter->setStyleSheet("QSplitter::handle{background:#808080;width:3px;}QSplitter::handle:hover{background:#0000FF;}");

        // ════════════════════════════════════════════════════════
        //  PANNEAU GAUCHE
        // ════════════════════════════════════════════════════════
        auto *left=new QWidget;
        left->setMinimumWidth(500); left->setMaximumWidth(800);
        left->setStyleSheet("background:#C8C8C8;border-right:1px solid #808080;");
        auto *lv=new QVBoxLayout(left);
        lv->setContentsMargins(7,7,7,7); lv->setSpacing(4);

        // Titre
        auto *lblT=new QLabel("Simulation EazyNC");
        lblT->setStyleSheet("font-size:11pt;font-weight:bold;color:#404040;"
                            "padding-bottom:4px;border-bottom:1px solid rgba(166,206,57,0.25);");
        lv->addWidget(lblT);

        // ── Widgets non-ajoutés (pilotés via softkeys du bas) ──
        // On garde les variables m_btn* pour conserver les slots existants,
        // mais les boutons ne sont PAS ajoutés au layout.
        m_btnPlay  = new QPushButton("▶  Play",  this);  m_btnPlay->setVisible(false);
        m_btnPause = new QPushButton("⏸ Pause", this);  m_btnPause->setVisible(false);
        m_btnToggleView = new QPushButton("▦ Plan 2D", this); m_btnToggleView->setVisible(false);

        // Vitesse
        auto *hlSpd=new QHBoxLayout; hlSpd->setSpacing(5);
        m_lblSpeed=new QLabel("Vitesse : 10x");
        m_lblSpeed->setStyleSheet("font-size:9pt;color:#404040;min-width:76px;");
        m_sliderSpeed=new QSlider(Qt::Horizontal);
        m_sliderSpeed->setRange(1,300); m_sliderSpeed->setValue(10);
        m_sliderSpeed->setStyleSheet(
            "QSlider::groove:horizontal{height:4px;background:#808080;border-radius:2px;}"
            "QSlider::handle:horizontal{background:#00AA00;width:13px;height:13px;"
            "margin:-5px 0;border-radius:7px;}"
            "QSlider::sub-page:horizontal{background:#00AA00;border-radius:2px;}");
        hlSpd->addWidget(m_lblSpeed); hlSpd->addWidget(m_sliderSpeed,1);
        lv->addLayout(hlSpd);

        // Barre anim
        m_barAnim=new QProgressBar;
        m_barAnim->setRange(0,100); m_barAnim->setValue(0);
        m_barAnim->setFixedHeight(5); m_barAnim->setTextVisible(false);
        m_barAnim->setStyleSheet(
            "QProgressBar{background:#EBEBEB;border:1px solid #808080;border-radius:2px;}"
            "QProgressBar::chunk{background:#00AA00;border-radius:2px;}");
        lv->addWidget(m_barAnim);

        // Goto + Reset CNC (cachés — pilotés via softkeys du bas)
        auto *btnGoto=new QPushButton("Goto ligne", this); btnGoto->setVisible(false);
        auto *btnMch =new QPushButton("Reset CNC", this);  btnMch->setVisible(false);

        // ── Liste G-code défilante ────────────────────────────
        auto *sep2=new QFrame; sep2->setFrameShape(QFrame::HLine);
        sep2->setStyleSheet("color:#808080;margin:2px 0;"); lv->addWidget(sep2);

        auto *lblCode=new QLabel("G-code programme");
        lblCode->setStyleSheet("font-size:10px;font-weight:700;color:#00AA00;");
        lv->addWidget(lblCode);

        m_gcodeList=new QListWidget;
        m_gcodeList->setSelectionMode(QAbstractItemView::SingleSelection);
        // Toutes les lignes ont la même hauteur : évite de recalculer la
        // mise en page de toute la liste à chaque ligne surlignée (simu fluide)
        m_gcodeList->setUniformItemSizes(true);
        m_gcodeList->setStyleSheet(
            "QListWidget{background:#FFFFFF;border:1px solid #808080;"
            "color:#111122;font-size:11px;"
            "font-family:'Consolas','Courier New',monospace;}"
            "QListWidget::item{padding:1px 4px;border-bottom:1px solid #f0f0f0;}"
            "QListWidget::item:hover{background:#f0f8f0;}"
            "QListWidget::item:selected{background:#eafaea;color:#111122;"
            "border:2px solid #1f9e1f;}");   // cadre vert rectangulaire
        m_gcodeList->addItem("(aucun programme chargé)");
        lv->addWidget(m_gcodeList, 1);

        // ── Checkboxes cachées (pilotées via softkeys du bas) ──
        m_chkPath   = new QCheckBox("Chemin",     this); m_chkPath->setChecked(false);   m_chkPath->setVisible(false);  // masqué au démarrage
        m_chkRapids = new QCheckBox("Rapides G0", this); m_chkRapids->setChecked(false); m_chkRapids->setVisible(false);
        m_chkStock  = new QCheckBox("Brut",       this); m_chkStock->setChecked(true);   m_chkStock->setVisible(false);
        // (m_stockMode : plein par défaut, voir déclaration)
        m_btnCubeTransp = new QToolButton(this); m_btnCubeTransp->setVisible(false);
        m_btnCubePlein  = new QToolButton(this); m_btnCubePlein->setVisible(false);

        // Barre heightmap + info
        m_barHmap=new QProgressBar;
        m_barHmap->setRange(0,100); m_barHmap->setVisible(false);
        m_barHmap->setFixedHeight(5); m_barHmap->setTextVisible(false);
        m_barHmap->setStyleSheet(
            "QProgressBar{background:#EBEBEB;border:1px solid #808080;border-radius:2px;}"
            "QProgressBar::chunk{background:#00AA00;border-radius:2px;}");
        lv->addWidget(m_barHmap);

        // Boutons mesure cachés — pilotés via softkeys
        m_btnMesure      = new QPushButton("📏 Mesure",  this); m_btnMesure->setVisible(false);
        auto *btnClearMesure = new QPushButton("✕ Effacer", this); btnClearMesure->setVisible(false);

        // Panneau résultat mesure
        m_lblMesure=new QLabel;
        m_lblMesure->setVisible(false);
        m_lblMesure->setWordWrap(true);
        m_lblMesure->setTextFormat(Qt::RichText);
        m_lblMesure->setStyleSheet(
            "QLabel{"
            "background:#0a1a0a;"
            "border:1px solid rgba(166,206,57,0.30);"
            "border-radius:4px;"
            "color:#00AA00;"
            "font-size:10px;"
            "font-family:'IBM Plex Mono','Courier New',monospace;"
            "padding:6px 8px;"
            "}");
        lv->addWidget(m_lblMesure);

        m_lblInfo=new QLabel("Aucun programme");
        m_lblInfo->setStyleSheet("font-size:9px;color:rgba(166,206,57,0.40);padding:1px 0;");
        m_lblInfo->setWordWrap(true);
        lv->addWidget(m_lblInfo);

        // Résolution heightmap : 512×512 fixe (pas de combo)

        m_splitter->addWidget(left);
        root->addWidget(m_splitter,1);

        // ── Connexions ────────────────────────────────────────────
        connect(m_btnToggleView,&QPushButton::clicked,this,&SimulationPage::onTogglePlanView);  // gardé (invisible mais compatible)
        connect(m_btnPlay,    &QPushButton::clicked, this, &SimulationPage::onPlay);
        connect(m_btnPause,   &QPushButton::clicked, this, &SimulationPage::onPause);
        connect(btnGoto,      &QPushButton::clicked, this, &SimulationPage::onGoto);  // gardé (invisible mais compatible)
        connect(btnMch,       &QPushButton::clicked, this, &SimulationPage::resetRequested);  // gardé (invisible mais compatible)
        connect(m_btnMesure,  &QPushButton::clicked, this, &SimulationPage::onMesure);  // gardé (invisible)
        // btnClearMesure connecté depuis MainWindow via skClearMesure()
        connect(m_gcodeList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item){
            int row = m_gcodeList->row(item);
            highlightLine(row);
            if (m_view) m_view->seekToLine(row);   // la simu se place sur cette ligne
            emit gotoLineRequested(row+1);
        });
        connect(m_sliderSpeed,&QSlider::valueChanged,this,[this](int v){
            m_lblSpeed->setText(QString("Vitesse : %1x").arg(v));
            if(m_view) m_view->setSpeed(v);
        });
    }
};
#endif