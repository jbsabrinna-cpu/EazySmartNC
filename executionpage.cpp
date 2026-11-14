#pragma execution_character_set("utf-8")
#include "executionpage.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QFont>
#include <QScrollBar>

// ═══════════════════════════════════════════════════════════════
//  Helpers style
// ═══════════════════════════════════════════════════════════════
QLabel* ExecutionPage::makeDroLabel(const QString &init)
{
    auto *l = new QLabel(init);
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->setStyleSheet(
        "QLabel{"
        "  background:#FFFFFF; color:#0000FF;"
        "  border:1px solid rgba(255,255,255,0.07); border-radius:3px;"
        "  font-family:'IBM Plex Mono','Consolas',monospace;"
        "  font-size:19px; font-weight:700; padding:2px 6px;"
        "  min-width:130px; min-height:30px;"
        "}");
    return l;
}

QLabel* ExecutionPage::makeSectionLabel(const QString &t)
{
    auto *l = new QLabel(t);
    l->setStyleSheet(
        "QLabel{color:rgba(255,255,255,0.25);font-size:8px;"
        "font-weight:700;letter-spacing:2px;background:transparent;}");
    return l;
}

QLabel* ExecutionPage::makeAxisLabel(const QString &t)
{
    auto *l = new QLabel(t);
    l->setStyleSheet(
        "QLabel{color:rgba(255,255,255,0.5);font-size:12px;"
        "font-weight:700;background:transparent;}");
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->setMinimumWidth(18);
    return l;
}

QPushButton* ExecutionPage::makeJogBtn(const QString &label, const QString &color)
{
    auto *b = new QPushButton(label);
    b->setFixedSize(52, 40);
    b->setStyleSheet(QString(
                         "QPushButton{"
                         "  background:%1;border:1px solid rgba(255,255,255,0.12);"
                         "  border-radius:4px;color:#000000;"
                         "  font-family:'IBM Plex Mono',monospace;font-size:10px;font-weight:700;"
                         "}"
                         "QPushButton:pressed{background:#00AA00;border-color:#505050;color:#FFFFFF;}").arg(color));
    return b;
}

QPushButton* ExecutionPage::makeCycleBtn(const QString &label,
                                         const QString &bg,
                                         const QString &border,
                                         const QString &color)
{
    auto *b = new QPushButton(label);
    b->setFixedHeight(34);
    b->setStyleSheet(QString(
                         "QPushButton{"
                         "  background:%1;border:2px solid %2;border-radius:4px;"
                         "  color:%3;font-family:'IBM Plex Mono',monospace;"
                         "  font-size:10px;font-weight:700;letter-spacing:1px;padding:0 8px;"
                         "}"
                         "QPushButton:pressed{border-width:3px;}").arg(bg, border, color));
    return b;
}

static QFrame* makeSepH()
{
    auto *f = new QFrame;
    f->setFrameShape(QFrame::HLine);
    f->setFixedHeight(1);
    f->setStyleSheet("QFrame{background:rgba(255,255,255,0.06);}");
    return f;
}

// ═══════════════════════════════════════════════════════════════
ExecutionPage::ExecutionPage(QWidget *parent) : QWidget(parent)
{ setupUi(); }

// ─────────────────────────────────────────────────────────────
void ExecutionPage::setupUi()
{
    setStyleSheet("ExecutionPage{background:#D6D6D6;}");
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0,0,0,0);
    root->setSpacing(0);

    // Colonne gauche : DRO + VCP + messages
    auto *leftCol = new QWidget;
    leftCol->setFixedWidth(290);
    leftCol->setStyleSheet("background:#C8C8C8;border-right:1px solid #808080;");
    auto *leftLay = new QVBoxLayout(leftCol);
    leftLay->setContentsMargins(0,0,0,0);
    leftLay->setSpacing(0);
    setupLeftPanel(leftCol);
    root->addWidget(leftCol);

    // Colonne droite
    auto *rightCol = new QWidget;
    auto *rightLay = new QVBoxLayout(rightCol);
    rightLay->setContentsMargins(0,0,0,0);
    rightLay->setSpacing(0);
    setupRightPanel(rightCol);
    root->addWidget(rightCol, 1);
}

// ─────────────────────────────────────────────────────────────
//  COLONNE GAUCHE
// ─────────────────────────────────────────────────────────────
void ExecutionPage::setupLeftPanel(QWidget *col)
{
    auto *lay = qobject_cast<QVBoxLayout*>(col->layout());

    // En-tête
    {
        auto *w = new QWidget; w->setFixedHeight(30);
        w->setStyleSheet("background:#EAEAEA;border-bottom:1px solid #808080;");
        auto *h = new QHBoxLayout(w); h->setContentsMargins(10,0,10,0);
        auto *lbl = new QLabel("EXECUTION");
        lbl->setStyleSheet("color:#606060;font-size:9pt;font-weight:bold;"
                           "font-weight:700;letter-spacing:3px;background:transparent;");
        m_lblTool = new QLabel("T- H-");
        m_lblTool->setStyleSheet("color:#0000FF;font-size:9pt;font-weight:bold;"
                                 "font-family:'IBM Plex Mono',monospace;background:transparent;");
        m_lblTool->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
        h->addWidget(lbl); h->addWidget(m_lblTool);
        lay->addWidget(w);
    }

    // DRO WORK
    {
        auto *w = new QWidget; w->setStyleSheet("background:#D6D6D6;border-bottom:1px solid #505050;");
        auto *g = new QGridLayout(w);
        g->setContentsMargins(8,5,8,3); g->setHorizontalSpacing(5); g->setVerticalSpacing(3);
        g->addWidget(makeSectionLabel("WORK (G54)"),0,0,1,2);
        m_droWX = makeDroLabel(); m_droWY = makeDroLabel(); m_droWZ = makeDroLabel();
        g->addWidget(makeAxisLabel("X"),1,0); g->addWidget(m_droWX,1,1);
        g->addWidget(makeAxisLabel("Y"),2,0); g->addWidget(m_droWY,2,1);
        g->addWidget(makeAxisLabel("Z"),3,0); g->addWidget(m_droWZ,3,1);
        lay->addWidget(w);
    }
    lay->addWidget(makeSepH());

    // DRO DTG
    {
        auto *w = new QWidget; w->setStyleSheet("background:#D6D6D6;border-bottom:1px solid #505050;");
        auto *g = new QGridLayout(w);
        g->setContentsMargins(8,3,8,3); g->setHorizontalSpacing(5); g->setVerticalSpacing(2);
        g->addWidget(makeSectionLabel("DTG"),0,0,1,2);
        m_droDX = makeDroLabel("0.0000"); m_droDY = makeDroLabel("0.0000"); m_droDZ = makeDroLabel("0.0000");
        auto dtgStyle = [](const QString &s){ return QString(s).replace("#00AA00","#0000FF"); };
        m_droDX->setStyleSheet(dtgStyle(m_droDX->styleSheet()));
        m_droDY->setStyleSheet(dtgStyle(m_droDY->styleSheet()));
        m_droDZ->setStyleSheet(dtgStyle(m_droDZ->styleSheet()));
        g->addWidget(makeAxisLabel("X"),1,0); g->addWidget(m_droDX,1,1);
        g->addWidget(makeAxisLabel("Y"),2,0); g->addWidget(m_droDY,2,1);
        g->addWidget(makeAxisLabel("Z"),3,0); g->addWidget(m_droDZ,3,1);
        lay->addWidget(w);
    }
    lay->addWidget(makeSepH());

    // DRO MACHINE
    {
        auto *w = new QWidget; w->setStyleSheet("background:#D6D6D6;border-bottom:1px solid #505050;");
        auto *g = new QGridLayout(w);
        g->setContentsMargins(8,3,8,3); g->setHorizontalSpacing(5); g->setVerticalSpacing(2);
        g->addWidget(makeSectionLabel("MACHINE"),0,0,1,2);
        m_droMX = makeDroLabel(); m_droMY = makeDroLabel(); m_droMZ = makeDroLabel();
        const QString mc = "rgba(255,255,255,0.35)";
        m_droMX->setStyleSheet(m_droMX->styleSheet().replace("#00AA00",mc));
        m_droMY->setStyleSheet(m_droMY->styleSheet().replace("#00AA00",mc));
        m_droMZ->setStyleSheet(m_droMZ->styleSheet().replace("#00AA00",mc));
        g->addWidget(makeAxisLabel("X"),1,0); g->addWidget(m_droMX,1,1);
        g->addWidget(makeAxisLabel("Y"),2,0); g->addWidget(m_droMY,2,1);
        g->addWidget(makeAxisLabel("Z"),3,0); g->addWidget(m_droMZ,3,1);
        lay->addWidget(w);
    }
    lay->addWidget(makeSepH());

    // S + F
    {
        auto *w = new QWidget; w->setStyleSheet("background:#D6D6D6;border-bottom:1px solid #505050;");
        auto *g = new QGridLayout(w);
        g->setContentsMargins(8,3,8,3); g->setSpacing(3);
        auto makeSF = [](const QString &init, const QString &color){
            auto *l = new QLabel(init);
            l->setStyleSheet(QString("QLabel{background:#0e1216;color:%1;"
                                     "border:1px solid rgba(255,255,255,0.07);border-radius:3px;"
                                     "font-family:'IBM Plex Mono',monospace;font-size:11px;font-weight:700;"
                                     "padding:2px 5px;min-width:70px;}").arg(color));
            l->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
            return l;
        };
        g->addWidget(makeSectionLabel("S"),0,0);
        m_lblRpm     = makeSF("0 tr/min","#CC8800"); g->addWidget(m_lblRpm,0,1);
        m_lblSpinOvr = makeSF("100%","#CC8800");     g->addWidget(m_lblSpinOvr,0,2);
        g->addWidget(makeSectionLabel("F"),1,0);
        m_lblFeed    = makeSF("0 mm/min","#0000FF"); g->addWidget(m_lblFeed,1,1);
        m_lblFeedOvr = makeSF("100%","#0000FF");     g->addWidget(m_lblFeedOvr,1,2);
        lay->addWidget(w);
    }
    lay->addWidget(makeSepH());

    // JOG AXES
    {
        auto *w = new QWidget; w->setStyleSheet("background:#D6D6D6;border-bottom:1px solid #505050;");
        auto *g = new QGridLayout(w);
        g->setContentsMargins(8,5,8,5); g->setSpacing(3);
        g->addWidget(makeSectionLabel("JOG AXES"),0,0,1,4);
        auto *bYP=makeJogBtn("Y+"); auto *bYM=makeJogBtn("Y-");
        auto *bXP=makeJogBtn("X+"); auto *bXM=makeJogBtn("X-");
        auto *bZP=makeJogBtn("Z+","#1a2d1a"); auto *bZM=makeJogBtn("Z-","#2d1a1a");
        bZP->setStyleSheet(bZP->styleSheet().replace("color:#c8d4f0","color:#00AA00"));
        bZM->setStyleSheet(bZM->styleSheet().replace("color:#c8d4f0","color:#CC0000"));
        g->addWidget(bYP,1,1); g->addWidget(bZP,1,3);
        g->addWidget(bXM,2,0); g->addWidget(bXP,2,2);
        g->addWidget(bYM,3,1); g->addWidget(bZM,3,3);
        connect(bXP,&QPushButton::pressed, this,[this](){emit jogXPlus(true);});
        connect(bXP,&QPushButton::released,this,[this](){emit jogXPlus(false);});
        connect(bXM,&QPushButton::pressed, this,[this](){emit jogXMinus(true);});
        connect(bXM,&QPushButton::released,this,[this](){emit jogXMinus(false);});
        connect(bYP,&QPushButton::pressed, this,[this](){emit jogYPlus(true);});
        connect(bYP,&QPushButton::released,this,[this](){emit jogYPlus(false);});
        connect(bYM,&QPushButton::pressed, this,[this](){emit jogYMinus(true);});
        connect(bYM,&QPushButton::released,this,[this](){emit jogYMinus(false);});
        connect(bZP,&QPushButton::pressed, this,[this](){emit jogZPlus(true);});
        connect(bZP,&QPushButton::released,this,[this](){emit jogZPlus(false);});
        connect(bZM,&QPushButton::pressed, this,[this](){emit jogZMinus(true);});
        connect(bZM,&QPushButton::released,this,[this](){emit jogZMinus(false);});
        lay->addWidget(w);
    }

    // PAS + MODE
    {
        auto *w = new QWidget; w->setStyleSheet("background:#D6D6D6;border-bottom:1px solid #505050;");
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(8,3,8,3); h->setSpacing(3);
        auto mk=[](const QString &t){
            auto *b=new QPushButton(t); b->setFixedHeight(28);
            b->setStyleSheet(
                "QPushButton{background:#EBEBEB;border:1px solid #808080;"
                "border-radius:3px;color:rgba(200,212,240,0.55);"
                "font-family:'IBM Plex Mono',monospace;font-size:8px;font-weight:700;}"
                "QPushButton:pressed{background:#00AA00;color:#FFFFFF;border-color:#505050;}");
            return b;
        };
        auto *bIC=mk("INC/CONT"); auto *bR=mk("RAPIDE");
        auto *b1=mk("x1"); auto *b10=mk("x10"); auto *b100=mk("x100");
        h->addWidget(bIC); h->addWidget(bR);
        h->addWidget(b1); h->addWidget(b10); h->addWidget(b100);
        connect(bIC, &QPushButton::clicked,this,[this](){emit incContToggled();});
        connect(bR,  &QPushButton::clicked,this,[this](){emit rapidToggled();});
        connect(b1,  &QPushButton::clicked,this,[this](){emit setX1();});
        connect(b10, &QPushButton::clicked,this,[this](){emit setX10();});
        connect(b100,&QPushButton::clicked,this,[this](){emit setX100();});
        lay->addWidget(w);
    }
    lay->addWidget(makeSepH());

    // AVANCE OVERRIDE
    {
        auto *w = new QWidget; w->setStyleSheet("background:#D6D6D6;border-bottom:1px solid #505050;");
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(8,3,8,3); h->setSpacing(3);
        auto *lbl=makeSectionLabel("AVANCE"); lbl->setMinimumWidth(45);
        auto *bFM=makeJogBtn("F-","#1a2040"); auto *bFP=makeJogBtn("F+","#1a2040");
        auto *bFH=makeCycleBtn("FEED\nHOLD","#3d3000","#CC8800","#CC8800");
        bFP->setStyleSheet(bFP->styleSheet().replace("color:#c8d4f0","color:#0000FF"));
        bFM->setStyleSheet(bFM->styleSheet().replace("color:#c8d4f0","color:#0000FF"));
        bFH->setFixedSize(60,40);
        h->addWidget(lbl); h->addWidget(bFM); h->addWidget(bFP);
        h->addStretch(); h->addWidget(bFH);
        connect(bFP,&QPushButton::pressed, this,[this](){emit feedPlusPressed(true);});
        connect(bFP,&QPushButton::released,this,[this](){emit feedPlusPressed(false);});
        connect(bFM,&QPushButton::pressed, this,[this](){emit feedMinusPressed(true);});
        connect(bFM,&QPushButton::released,this,[this](){emit feedMinusPressed(false);});
        connect(bFH,&QPushButton::clicked, this,[this](){emit feedHoldRequested();});
        lay->addWidget(w);
    }

    // BROCHE
    {
        auto *w = new QWidget; w->setStyleSheet("background:#D6D6D6;border-bottom:1px solid #505050;");
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(8,3,8,3); h->setSpacing(3);
        auto *lbl=makeSectionLabel("BROCHE"); lbl->setMinimumWidth(45);
        auto *bSM=makeJogBtn("S-","#201a10"); auto *bSP=makeJogBtn("S+","#201a10");
        auto *bCW=makeJogBtn("CW","#1a2d1a"); auto *bCCW=makeJogBtn("CCW","#1a2d1a");
        auto *bSS=makeJogBtn("STOP","#2d1a1a");
        bSP->setStyleSheet(bSP->styleSheet().replace("color:#c8d4f0","color:#CC8800"));
        bSM->setStyleSheet(bSM->styleSheet().replace("color:#c8d4f0","color:#CC8800"));
        bCW->setStyleSheet(bCW->styleSheet().replace("color:#c8d4f0","color:#00AA00"));
        bCCW->setStyleSheet(bCCW->styleSheet().replace("color:#c8d4f0","color:#00AA00"));
        bSS->setStyleSheet(bSS->styleSheet().replace("color:#c8d4f0","color:#CC0000"));
        h->addWidget(lbl); h->addWidget(bSM); h->addWidget(bSP);
        h->addWidget(bCW); h->addWidget(bCCW); h->addWidget(bSS);
        connect(bSP, &QPushButton::pressed, this,[this](){emit spindlePlusPressed(true);});
        connect(bSP, &QPushButton::released,this,[this](){emit spindlePlusPressed(false);});
        connect(bSM, &QPushButton::pressed, this,[this](){emit spindleMinusPressed(true);});
        connect(bSM, &QPushButton::released,this,[this](){emit spindleMinusPressed(false);});
        connect(bCW, &QPushButton::clicked, this,[this](){emit spindleCWRequested();});
        connect(bCCW,&QPushButton::clicked, this,[this](){emit spindleCCWRequested();});
        connect(bSS, &QPushButton::clicked, this,[this](){emit spindleStopRequested();});
        lay->addWidget(w);
    }
    lay->addWidget(makeSepH());

    // STOP + RESET
    {
        auto *w = new QWidget; w->setStyleSheet("background:#D6D6D6;border-bottom:1px solid #505050;");
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(8,4,8,6); h->setSpacing(4);
        auto *bStop =makeCycleBtn("STOP","#3d1010","#CC0000","#CC0000");
        auto *bReset=makeCycleBtn("RESET","#1a2040","#0000FF","#0000FF");
        h->addWidget(bStop,1); h->addWidget(bReset,1);
        connect(bStop, &QPushButton::clicked,this,[this](){emit cycleStopRequested();});
        connect(bReset,&QPushButton::clicked,this,[this](){emit resetRequested();});
        lay->addWidget(w);
    }
    lay->addWidget(makeSepH());

    // MESSAGES
    {
        auto *w = new QWidget; w->setStyleSheet("background:#0d0f14;");
        w->setMinimumHeight(55);
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(8,4,8,4); v->setSpacing(2);
        v->addWidget(makeSectionLabel("MESSAGES"));
        m_lblMessages = new QLabel("-");
        m_lblMessages->setWordWrap(true);
        m_lblMessages->setStyleSheet(
            "QLabel{color:rgba(200,212,240,0.5);font-size:9px;"
            "font-family:'IBM Plex Mono',monospace;background:transparent;}");
        v->addWidget(m_lblMessages);
        lay->addWidget(w);
    }

    lay->addStretch();
}

// ─────────────────────────────────────────────────────────────
//  COLONNE DROITE
// ─────────────────────────────────────────────────────────────
void ExecutionPage::setupRightPanel(QWidget *col)
{
    auto *lay = qobject_cast<QVBoxLayout*>(col->layout());

    // ── Barre du haut : nom + badge mode + Charger + Cycle Start
    {
        auto *bar = new QWidget; bar->setFixedHeight(48);
        bar->setStyleSheet("background:#EAEAEA;border-bottom:1px solid #808080;");
        auto *h = new QHBoxLayout(bar);
        h->setContentsMargins(10,7,10,7); h->setSpacing(6);

        m_lblProgName = new QLabel("Aucun programme");
        m_lblProgName->setStyleSheet(
            "font-family:'IBM Plex Mono',monospace;font-size:11px;"
            "font-weight:700;color:#c8d4f0;background:transparent;");
        h->addWidget(m_lblProgName, 1);

        // Badge mode ENC / NC
        m_lblModeBadge = new QLabel("ENC");
        m_lblModeBadge->setFixedHeight(22);
        m_lblModeBadge->setStyleSheet(
            "QLabel{background:#1e4d1a;border:1px solid #00AA00;border-radius:3px;"
            "color:#00AA00;font-family:'IBM Plex Mono',monospace;"
            "font-size:8px;font-weight:700;letter-spacing:2px;padding:0 6px;}");
        h->addWidget(m_lblModeBadge);

        auto *btnLoad = new QPushButton("  Charger");
        btnLoad->setFixedHeight(32);
        btnLoad->setStyleSheet(
            "QPushButton{background:#1a2040;border:1px solid rgba(68,170,255,0.4);"
            "border-radius:4px;color:#0000FF;"
            "font-family:'IBM Plex Mono',monospace;font-size:9px;font-weight:700;padding:0 10px;}"
            "QPushButton:hover{background:#222a58;}");
        h->addWidget(btnLoad);
        connect(btnLoad, &QPushButton::clicked, this, &ExecutionPage::onChargerClicked);

        auto *btnCycle = makeCycleBtn("  CYCLE START","#1e4d1a","#00AA00","#00AA00");
        btnCycle->setFixedHeight(32);
        h->addWidget(btnCycle);
        connect(btnCycle, &QPushButton::clicked, this, [this](){ emit cycleStartRequested(); });

        lay->addWidget(bar);
    }

    // ── Stack : vue ENC (blocs) | vue RAW (programme brut)
    m_viewStack = new QStackedWidget;

    // ── Vue ENC : splitter liste blocs + UnitEditor ──────────
    {
        m_encView = new QWidget;
        auto *encLay = new QVBoxLayout(m_encView);
        encLay->setContentsMargins(0,0,0,0); encLay->setSpacing(0);

        auto *splitter = new QSplitter(Qt::Horizontal);
        splitter->setHandleWidth(1);
        splitter->setStyleSheet("QSplitter::handle{background:rgba(0,0,0,0.3);}");

        // Liste blocs
        auto *listPanel = new QWidget;
        listPanel->setFixedWidth(200);
        listPanel->setStyleSheet("background:#1e2128;");
        auto *listLay = new QVBoxLayout(listPanel);
        listLay->setContentsMargins(0,0,0,0); listLay->setSpacing(0);

        auto *listHdr = new QLabel("  BLOCS");
        listHdr->setFixedHeight(26);
        listHdr->setStyleSheet("background:#13151a;color:rgba(255,255,255,0.25);"
                               "font-size:7px;font-weight:700;letter-spacing:3px;");
        listLay->addWidget(listHdr);

        m_blockList = new QListWidget;
        m_blockList->setStyleSheet(
            "QListWidget{background:#1e2128;border:none;outline:none;}"
            "QListWidget::item{"
            "  border-bottom:1px solid rgba(255,255,255,0.04);"
            "  border-left:3px solid transparent;"
            "  padding:8px 10px;color:rgba(255,255,255,0.6);"
            "  font-size:11px;font-family:'IBM Plex Mono',monospace;}"
            "QListWidget::item:hover{background:rgba(255,255,255,0.03);}"
            "QListWidget::item:selected{"
            "  background:rgba(86,212,78,0.08);"
            "  border-left-color:#00AA00;color:#00AA00;}");
        listLay->addWidget(m_blockList, 1);
        connect(m_blockList, &QListWidget::currentRowChanged,
                this, &ExecutionPage::onBlockSelected);
        splitter->addWidget(listPanel);

        // UnitEditor lecture seule
        m_editor = new UnitEditor;
        m_editor->setReadOnly(true);
        splitter->addWidget(m_editor);
        splitter->setStretchFactor(0,0);
        splitter->setStretchFactor(1,1);

        encLay->addWidget(splitter, 1);
        m_viewStack->addWidget(m_encView);   // index 0
    }

    // ── Vue RAW : liste des lignes G-code ────────────────────
    {
        m_rawView = new QWidget;
        m_rawView->setStyleSheet("background:#0d0f14;");
        auto *rawLay = new QVBoxLayout(m_rawView);
        rawLay->setContentsMargins(0,0,0,0); rawLay->setSpacing(0);

        m_rawHdr = new QLabel("  PROGRAMME BRUT");
        m_rawHdr->setFixedHeight(26);
        m_rawHdr->setStyleSheet("background:#13151a;color:rgba(255,255,255,0.25);"
                                "font-size:7px;font-weight:700;letter-spacing:3px;");
        rawLay->addWidget(m_rawHdr);

        m_rawList = new QListWidget;
        m_rawList->setStyleSheet(
            "QListWidget{background:#0d0f14;border:none;outline:none;}"
            "QListWidget::item{"
            "  border-bottom:1px solid rgba(255,255,255,0.03);"
            "  border-left:4px solid transparent;"
            "  padding:4px 10px 4px 6px;"
            "  color:rgba(200,212,240,0.7);"
            "  font-size:11px;font-family:'IBM Plex Mono',monospace;}"
            "QListWidget::item:hover{background:rgba(255,255,255,0.03);}"
            "QListWidget::item:selected{"
            "  background:rgba(86,212,78,0.10);"
            "  border-left-color:#00AA00;color:#00AA00;}");
        rawLay->addWidget(m_rawList, 1);

        m_viewStack->addWidget(m_rawView);   // index 1
    }

    m_viewStack->setCurrentIndex(0);
    lay->addWidget(m_viewStack, 1);
}

// ═══════════════════════════════════════════════════════════════
//  Bascule de mode
// ═══════════════════════════════════════════════════════════════
void ExecutionPage::switchToEncMode()
{
    m_viewMode = ModeEnc;
    m_viewStack->setCurrentIndex(0);
    if (m_lblModeBadge) {
        m_lblModeBadge->setText("ENC");
        m_lblModeBadge->setStyleSheet(
            "QLabel{background:#1e4d1a;border:1px solid #00AA00;border-radius:3px;"
            "color:#00AA00;font-family:'IBM Plex Mono',monospace;"
            "font-size:8px;font-weight:700;letter-spacing:2px;padding:0 6px;}");
    }
}

void ExecutionPage::switchToRawMode()
{
    m_viewMode = ModeRaw;
    m_viewStack->setCurrentIndex(1);
    if (m_lblModeBadge) {
        m_lblModeBadge->setText("NC");
        m_lblModeBadge->setStyleSheet(
            "QLabel{background:#1a2040;border:1px solid #0000FF;border-radius:3px;"
            "color:#0000FF;font-family:'IBM Plex Mono',monospace;"
            "font-size:8px;font-weight:700;letter-spacing:2px;padding:0 6px;}");
    }
}

// ═══════════════════════════════════════════════════════════════
//  Bouton Charger
// ═══════════════════════════════════════════════════════════════
void ExecutionPage::onChargerClicked()
{
    // Signal vers MainWindow pour ouvrir le gestionnaire de fichiers
    emit requestOpenFile();
}

// ═══════════════════════════════════════════════════════════════
//  loadRawFile — charger un .nc/.cnc en vue programme brut
// ═══════════════════════════════════════════════════════════════
void ExecutionPage::loadRawFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    m_ncPath = path;
    m_rawLines.clear();
    m_rawList->clear();

    QTextStream in(&f);
    int lineNum = 1;
    while (!in.atEnd()) {
        QString line = in.readLine();
        m_rawLines.append(line);

        // Numéro de ligne + contenu
        QString display = QString("%1").arg(lineNum, 4, 10, QChar(' ')) + "  " + line;
        auto *item = new QListWidgetItem(display);
        item->setSizeHint(QSize(0, 26));
        // Colorier différemment les commentaires et les codes G/M
        if (line.trimmed().startsWith("(") || line.trimmed().startsWith(";")) {
            item->setForeground(QColor(255,255,255,80));
        } else if (line.trimmed().startsWith("T") || line.trimmed().startsWith("M6")) {
            item->setForeground(QColor("#CC8800")); // outil = orange
        } else if (line.trimmed().startsWith("M3") || line.trimmed().startsWith("M4")
                   || line.trimmed().startsWith("M5")) {
            item->setForeground(QColor("#00AA00")); // broche = vert
        }
        m_rawList->addItem(item);
        ++lineNum;
    }
    f.close();

    if (m_lblProgName)
        m_lblProgName->setText(QFileInfo(path).fileName());

    switchToRawMode();
}

// ═══════════════════════════════════════════════════════════════
//  offerProgram — depuis ProgrammePage (.enc)
// ═══════════════════════════════════════════════════════════════
void ExecutionPage::offerProgram(const QVector<BlockData> &blocks,
                                 const QVector<GCodeLine> &gcode,
                                 const QString            &name)
{
    m_blocks = blocks;
    m_gcode  = gcode;

    // Chemin .nc = C:/cncm/<nom>.nc (déjà écrit par ProgrammePage)
    QString baseName = name;
    if (baseName.endsWith(".enc", Qt::CaseInsensitive)) baseName.chop(4);
    if (baseName.isEmpty()) baseName = "eazyncc";
    m_ncPath = "C:/cncm/" + baseName + ".nc";

    if (m_lblProgName)
        m_lblProgName->setText(name.isEmpty() ? "Programme" : name);

    // Remplir la liste blocs
    m_blockList->clear();
    for (const BlockData &b : blocks) {
        QString label;
        switch (b.type) {
        case BlockType::BlkForm: label = QString("[%1]  BlkForm").arg(b.id); break;
        case BlockType::Poche:   label = QString("[%1]  Poche - %2").arg(b.id).arg(b.name); break;
        case BlockType::Trou:    label = QString("[%1]  Trou - %2").arg(b.id).arg(b.name);  break;
        case BlockType::Contour: label = QString("[%1]  Contour - %2").arg(b.id).arg(b.name); break;
        }
        auto *item = new QListWidgetItem(label);
        item->setSizeHint(QSize(0, 40));
        m_blockList->addItem(item);
    }
    if (!m_blocks.isEmpty())
        m_blockList->setCurrentRow(0);

    switchToEncMode();
}

// ─────────────────────────────────────────────────────────────
//  Sélection bloc → UnitEditor lecture seule
// ─────────────────────────────────────────────────────────────
void ExecutionPage::onBlockSelected(int row)
{
    if (row < 0 || row >= m_blocks.size() || !m_editor) return;
    m_currentBlock = row;
    m_editor->setReadOnly(true);   // ← avant load() pour bloquer saveRequested
    const BlockData &b = m_blocks[row];

    if (b.type == BlockType::Trou && !b.data.isEmpty()) {
        TrouData d;
        d.toolRef          = b.data["toolRef"].toString();
        d.toolByName       = b.data["toolByName"].toBool();
        d.spindleVal       = b.data["spindleVal"].toDouble();
        d.spindleByVc      = b.data["spindleByVc"].toBool();
        d.descenteVal      = b.data["descenteVal"].toDouble();
        d.descenteMode     = b.data["descenteMode"].toInt();
        d.fonctionM1       = b.data["fonctionM1"].toString();
        d.fonctionM2       = b.data["fonctionM2"].toString();
        d.DL               = b.data["DL"].toDouble();
        d.DR               = b.data["DR"].toDouble();
        d.brocheCW         = b.data["brocheCW"].toBool(true);
        d.diametre         = b.data["diametre"].toDouble(10.0);
        d.profondeur       = b.data["profondeur"].toDouble(20.0);
        d.traversant       = b.data["traversant"].toBool();
        d.cycle            = b.data["cycle"].toInt();
        d.degagement       = b.data["degagement"].toDouble(3.0);
        d.approche         = b.data["approche"].toDouble(2.0);
        d.sautBride        = b.data["sautBride"].toDouble(50.0);
        d.positionnementG0 = b.data["posG0"].toBool(true);
        d.positionnementF  = b.data["positionnementF"].toDouble();
        d.fichierPoints    = b.data["fichierPoints"].toString();
        m_editor->load(d);
    } else if (b.type != BlockType::BlkForm && !b.data.isEmpty()) {
        PocheData d;
        d.toolRef          = b.data["toolRef"].toString();
        d.toolByName       = b.data["toolByName"].toBool();
        d.spindleVal       = b.data["spindleVal"].toDouble();
        d.spindleByVc      = b.data["spindleByVc"].toBool();
        d.descenteVal      = b.data["descenteVal"].toDouble();
        d.descenteMode     = b.data["descenteMode"].toInt();
        d.avanceVal        = b.data["avanceVal"].toDouble();
        d.avanceMode       = b.data["avanceMode"].toInt();
        d.fonctionM1       = b.data["fonctionM1"].toString();
        d.fonctionM2       = b.data["fonctionM2"].toString();
        d.DL               = b.data["DL"].toDouble();
        d.DR               = b.data["DR"].toDouble();
        d.Routil2              = b.data["Routil2"].toDouble();
        d.brocheCW         = b.data["brocheCW"].toBool(true);
        d.isCirculaire     = b.data["isCirculaire"].toBool();
        d.longueur         = b.data["longueur"].toDouble();
        d.largeur          = b.data["largeur"].toDouble();
        d.rayonAngle       = b.data["rayonAngle"].toDouble();
        d.diametre         = b.data["diametre"].toDouble();
        d.profondeur       = b.data["profondeur"].toDouble();
        d.profPasse        = b.data["profPasse"].toDouble(2.0);
        d.surepLat         = b.data["surepLat"].toDouble();
        d.surepProf        = b.data["surepProf"].toDouble();
        d.passeFinition    = b.data["passeFinition"].toDouble();
        d.avanceFinition   = b.data["avanceFin"].toDouble();
        d.avanceFinMode    = b.data["avanceFinMode"].toInt();
        d.posAngulaire     = b.data["posAngulaire"].toDouble();
        d.positionPoche    = b.data["positionPoche"].toInt(5);
        d.fichierPoints    = b.data["fichierPoints"].toString();
        d.approche         = b.data["approche"].toDouble(2.0);
        d.sautBride        = b.data["sautBride"].toDouble(50.0);
        d.recouvrement     = b.data["recouvrement"].toInt(75);
        d.positionnementG0 = b.data["posG0"].toBool(true);
        d.positionnementF  = b.data["positionnementF"].toDouble();
        d.modeFraisage     = b.data["modeFraisage"].toInt();
        d.modePlongee      = b.data["modePlongee"].toInt();
        m_editor->load(d);
    }
}

// ═══════════════════════════════════════════════════════════════
//  Mises à jour DRO
// ═══════════════════════════════════════════════════════════════
void ExecutionPage::updateDro(double wx,double wy,double wz,
                              double mx,double my,double mz,
                              double dtgx,double dtgy,double dtgz)
{
    auto f=[](double v){return QString::asprintf("%+.4f",v);};
    if(m_droWX) m_droWX->setText(f(wx));
    if(m_droWY) m_droWY->setText(f(wy));
    if(m_droWZ) m_droWZ->setText(f(wz));
    if(m_droMX) m_droMX->setText(f(mx));
    if(m_droMY) m_droMY->setText(f(my));
    if(m_droMZ) m_droMZ->setText(f(mz));
    if(m_droDX) m_droDX->setText(f(dtgx));
    if(m_droDY) m_droDY->setText(f(dtgy));
    if(m_droDZ) m_droDZ->setText(f(dtgz));
}

void ExecutionPage::updateSpindle(int rpm, int ovr)
{
    if(m_lblRpm)     m_lblRpm->setText(QString("%1 tr/min").arg(rpm));
    if(m_lblSpinOvr) m_lblSpinOvr->setText(QString("%1%").arg(ovr));
}

void ExecutionPage::updateFeed(double f, int ovr)
{
    if(m_lblFeed)    m_lblFeed->setText(QString("%1 mm/min").arg(qRound(f)));
    if(m_lblFeedOvr) m_lblFeedOvr->setText(QString("%1%").arg(ovr));
}

void ExecutionPage::updateTool(int tNum, int hNum)
{
    if(m_lblTool) m_lblTool->setText(QString("T%1  H%2").arg(tNum).arg(hNum));
}

void ExecutionPage::updateMessages(const QString &msg)
{
    if(m_lblMessages && !msg.isEmpty())
        m_lblMessages->setText(msg);
}

// ─────────────────────────────────────────────────────────────
//  Surligner le bloc en cours (mode ENC)
// ─────────────────────────────────────────────────────────────
void ExecutionPage::highlightBlock(int blockId)
{
    if (m_viewMode != ModeEnc || !m_blockList || blockId <= 0) return;
    for (int i = 0; i < m_blocks.size(); ++i) {
        auto *item = m_blockList->item(i);
        if (!item) continue;
        if (m_blocks[i].id == blockId) {
            item->setBackground(QColor(86,212,78,28));
            item->setForeground(QColor("#00AA00"));
            item->setFont(QFont("IBM Plex Mono",10,QFont::Bold));
            m_blockList->scrollToItem(item);
        } else {
            item->setBackground(QColor(0,0,0,0));
            item->setForeground(QColor(255,255,255,153));
            item->setFont(QFont("IBM Plex Mono",10));
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  Surligner la ligne en cours (mode RAW)
// ─────────────────────────────────────────────────────────────
void ExecutionPage::highlightLine(int lineNumber)
{
    if (m_viewMode != ModeRaw || !m_rawList || lineNumber <= 0) return;
    const int idx = lineNumber - 1;  // base 0
    for (int i = 0; i < m_rawList->count(); ++i) {
        auto *item = m_rawList->item(i);
        if (!item) continue;
        if (i == idx) {
            item->setBackground(QColor(86,212,78,28));
            item->setForeground(QColor("#00AA00"));
            item->setFont(QFont("IBM Plex Mono",10,QFont::Bold));
            m_rawList->scrollToItem(item);
        } else {
            item->setBackground(QColor(0,0,0,0));
            item->setForeground(QColor(200,212,240,178));
            item->setFont(QFont("IBM Plex Mono",10));
        }
    }
}
