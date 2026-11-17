#include <QStackedWidget>
#include <QScreen>
#include <QTimer>
#include "mainwindow.h"
#include "tnc620theme.h"
#include "ui_EazyNC_MainWindow_v4.h"
#include "filebrowserpage.h"
#include "ProgrammePage.h"
#include "executionpage.h"
#include "gcodeengine.h"
#include "tooltabledialog.h"
#include "tooldatabase.h"
#include "stockalphasoftkey.h"
#include <memory>
#include <functional>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QFileDialog>
#include <QMenu>
#include <QPixmap>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include "crashhandler.h"

// ═══════════════════════════════════════════════════════════
//  THÈME TNC7 — Couleurs Heidenhain extraites de tncvbcntl.exe
//  Vert  : #A6CE39  Vert actif : #00B400  Jaune : #FFFF77
// Constantes couleur → voir tnc620theme.h
static const QString TNC_GREEN2   = "#00AA00";
static const QString TNC_PANEL    = "#C8C8C8";

// Style global supprimé - appliqué via tnc620theme.h

static const QString S_INC   = "QPushButton{background:#EBEBEB;border:2px solid #00AA00;border-radius:5px;color:#000000;font-weight:bold;font-size:8pt;padding:6px;}QPushButton:hover{background:#D0EED0;border-color:#007700;}";
static const QString S_CONT  = "QPushButton{background:#EBEBEB;border:2px solid #808080;border-radius:5px;color:#000000;font-weight:bold;font-size:8pt;padding:6px;}QPushButton:hover{background:#DBDBDB;border-color:#404040;}";
static const QString S_LENT  = "QPushButton{background:#EBEBEB;border:2px solid #00AA00;border-radius:5px;color:#000000;font-weight:bold;font-size:8pt;padding:6px;}QPushButton:hover{background:#D0EED0;border-color:#007700;}";
static const QString S_RAPID = "QPushButton{background:#FFE0D0;border:2px solid #CC0000;border-radius:5px;color:#CC0000;font-weight:bold;font-size:8pt;padding:6px;}QPushButton:hover{background:#FFCCBB;border-color:#AA0000;}";
static const QString S_MAN   = "QPushButton{background:#EBEBEB;border:2px solid #808080;border-radius:5px;color:#000000;font-weight:bold;font-size:8pt;padding:6px;}QPushButton:hover{background:#DBDBDB;border-color:#404040;}";
static const QString S_AUTO  = "QPushButton{background:#E0EEFF;border:2px solid #0000FF;border-radius:5px;color:#0000FF;font-weight:bold;font-size:8pt;padding:6px;}QPushButton:hover{background:#C8DDFF;border-color:#0000CC;}";
static const QString S_ACTIVE = "QPushButton{background:#D0EED0;border:2px solid #00AA00;border-radius:4px;color:#000000;font-weight:bold;font-size:7pt;}QPushButton:hover{background:#BBDDBB;border-color:#007700;}";
static const QString S_NORMAL = "QPushButton{background:#EBEBEB;color:#404040;border:1px solid #808080;border-radius:4px;font-weight:bold;font-size:7pt;padding:3px;}"
                                "QPushButton:hover{border:1px solid #000000;color:#000000;}";
static const QString DIAL_STYLE =
    "QDial{"
    "background:#EBEBEB;"
    "border:2px solid #808080;"
    "}";

static void setFullIcon(QToolButton* btn, const QString& path, QSize sz)
{
    QPixmap px(path);
    if (px.isNull()) return;
    btn->setStyleSheet("QToolButton{padding:0px;margin:0px;border:none;background:transparent;}"
                       "QToolButton:hover{background:rgba(0,0,0,0.06);}");
    btn->setIcon(QIcon(px));
    btn->setIconSize(sz);
    btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    btn->setText("");
}

// Icône centrée avec ratio conservé sur fond #D6D6D6 (softkeys cycles)
static void setSoftkeyIcon(QToolButton* btn, const QString& resPath,
                           const QString& tooltip = QString())
{
    btn->setText("");
    btn->setToolTip(tooltip);
    btn->setToolButtonStyle(Qt::ToolButtonIconOnly);

    QPixmap src(resPath);
    if (!src.isNull()) {
        // Créer un pixmap carré 64x64 avec fond EazyNC et icône centrée au ratio
        QPixmap canvas(64, 64);
        canvas.fill(QColor("#D6D6D6"));
        QPainter p(&canvas);
        QPixmap scaled = src.scaled(60, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = (64 - scaled.width()) / 2;
        int y = (64 - scaled.height()) / 2;
        p.drawPixmap(x, y, scaled);
        p.end();
        btn->setIcon(QIcon(canvas));
        btn->setIconSize(QSize(82, 82));
    }

    btn->setStyleSheet(
        "QToolButton {"
        "  border:1px solid #707070;"
        "  border-radius:3px;"
        "  padding:0px; margin:2px;"
        "  background:#D6D6D6;"
        "}"
        "QToolButton:hover { border:2px solid #0000CC; background:#C8D8FF; }"
        "QToolButton:pressed { border:2px solid #0000FF; background:#B0C8FF; }"
        );
}

// ─────────────────────────────────────────────────────────────
//  Helper : envoyer une commande via RunCommand
//  CNC_Job_RunCommand(cmd, 0) = execution immediate
// ─────────────────────────────────────────────────────────────
void MainWindow::sendCommand(const QString &cmd)
{
    if (!bridge || !bridge->isLoaded()) return;
    bridge->sendMdiCommand(cmd);
    // Afficher dans l'historique MDI
    ui->txtMdiHistory->append(QString("<span style='color:#0000AA;'>%1</span>").arg(cmd));
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::EazyNC_MainWindow)
{
    ui->setupUi(this);
    // ── THÈME TNC 620 GLOBAL ─────────────────────────────────
    qApp->setStyleSheet(tnc620StyleSheet());
    // showMaximized respecte la barre des tâches Windows
    // showFullScreen() la cachait complètement
    showMaximized();
    // Les pages sont créées APRÈS ce showMaximized() : leur taille
    // minimale peut agrandir la fenêtre au-delà de l'écran et Windows
    // la sort alors du plein écran. On re-maximise une fois tout construit,
    // et on note les tailles dans la sortie pour diagnostic.
    QTimer::singleShot(300, this, [this]{
        const QRect ecran = screen() ? screen()->availableGeometry() : QRect();
        qDebug() << "[Fenetre] ecran dispo" << ecran.size()
                 << "taille mini" << minimumSizeHint() << "maximisee" << isMaximized();
        for (QStackedWidget *sw : findChildren<QStackedWidget*>())
            for (int i = 0; i < sw->count(); ++i)
                if (sw->widget(i)->minimumSizeHint().height() > 700)
                    qDebug() << "[Fenetre]  page" << sw->objectName() << i
                             << sw->widget(i)->metaObject()->className()
                             << sw->widget(i)->objectName() << sw->widget(i)->minimumSizeHint();
        showNormal();
        showMaximized();
    });

    // ── PAGES INJECTÉES ──────────────────────────────────────
    CrashHandler::log("new ProgrammePage...");
    CrashHandler::log("new ProgrammePage...");
    m_programmePage = new ProgrammePage(this);
    CrashHandler::log("ProgrammePage OK");
    CrashHandler::log("ProgrammePage OK");
    {
        QVBoxLayout *lay = new QVBoxLayout(ui->pageProgramm);
        lay->setContentsMargins(0,0,0,0);
        lay->setSpacing(0);
        lay->addWidget(m_programmePage);
    }

    // ── Page Exécution ────────────────────────────────────────
    CrashHandler::log("new ExecutionPage...");
    m_executionPage = new ExecutionPage(this);
    CrashHandler::log("ExecutionPage OK");
    {
        QVBoxLayout *lay = new QVBoxLayout(ui->pageExecution);
        lay->setContentsMargins(0,0,0,0);
        lay->setSpacing(0);
        lay->addWidget(m_executionPage);
    }

    // ── Page Simulation 3D ───────────────────────────────────────
    CrashHandler::log("new SimulationPage...");
    CrashHandler::log("new SimulationPage...");
    m_simPage = new SimulationPage(this);
    // Mode plein uniquement — pas de transparent
    m_simPage->skSetStockOpaque(true);
    CrashHandler::log("SimulationPage OK");
    CrashHandler::log("SimulationPage OK");
    {
        // Retirer le QLabel placeholder du simLayout
        QLayout *simLay = ui->pageSimulation->layout();
        if (simLay) {
            while (simLay->count() > 0) {
                QLayoutItem *item = simLay->takeAt(0);
                if (item->widget()) { item->widget()->deleteLater(); }
                delete item;
            }
            simLay->addWidget(m_simPage);
        } else {
            QVBoxLayout *lay = new QVBoxLayout(ui->pageSimulation);
            lay->setContentsMargins(0,0,0,0);
            lay->setSpacing(0);
            lay->addWidget(m_simPage);
        }
    }

    // ── Page Gestionnaire fichiers (PGM MGT) ────────────────
    m_fileBrowserPage = new FileBrowserPage(this);
    m_fileBrowserPage->setRootPath("C:/cncm/ncfiles");
    {
        QWidget *pageFichiers = new QWidget(this);
        QVBoxLayout *fLay = new QVBoxLayout(pageFichiers);
        fLay->setContentsMargins(0,0,0,0);
        fLay->setSpacing(0);
        fLay->addWidget(m_fileBrowserPage);
        ui->stackedPages->addWidget(pageFichiers);  // index 5
    }
    connect(m_fileBrowserPage, &FileBrowserPage::openEncFile,
            this, [this](const QString &path){
                // Chargement selon la page d'où on vient :
                //  Simulation / Exécution + .enc : on prend directement le
                //  .nc déjà généré à côté (pas de recalcul). S'il n'existe pas,
                //  le .enc passe par la page Programme qui génère le G-code.
                const QFileInfo fi(path);
                const bool estEnc = fi.suffix().compare("enc", Qt::CaseInsensitive) == 0;
                const QString nc = fi.absolutePath() + "/" + fi.completeBaseName() + ".nc";
                if (estEnc && (m_previousPage == 4 || m_previousPage == 2) && QFileInfo::exists(nc)) {
                    if (m_previousPage == 4 && m_simPage) m_simPage->loadNcFile(nc);
                    else m_executionPage->loadRawFile(nc);
                    return;
                }
                m_programmePage->loadEncFile(path);
            });
    connect(m_fileBrowserPage, &FileBrowserPage::openNcFile,
            this, [this](const QString &path){
                // Si on vient de la page Simulation, charger DANS la sim
                if (m_previousPage == 4 && m_simPage) {
                    m_simPage->loadNcFile(path);
                } else {
                    m_executionPage->loadRawFile(path);
                }
            });
    connect(m_fileBrowserPage, &FileBrowserPage::navigateTo,
            this, [this](const QString &page){
                // Retour à la page d'où on a ouvert le gestionnaire
                // (Simulation ou Exécution) ; sinon selon le type de fichier
                if (m_previousPage == 4 || m_previousPage == 2) {
                    switchPage(m_previousPage);
                    return;
                }
                if (page == "PROGRAMME") switchPage(1);
                else if (page == "EXECUTION") switchPage(2);
            });

    // ExecutionPage : bouton Charger → ouvre le gestionnaire
    connect(m_executionPage, &ExecutionPage::requestOpenFile,
            this, [this](){
                m_previousPage = m_currentPage;
                switchPage(5);
            });

    // SimulationPage : bouton Charger NC → ouvre le gestionnaire
    connect(m_simPage, &SimulationPage::requestOpenFile,
            this, [this](){
                m_previousPage = m_currentPage; // memoriser qu on vient de sim
                switchPage(5);
            });

    // ── Signaux SimulationPage ────────────────────────────────
    connect(m_simPage, &SimulationPage::loadEncRequested, this, [this](){
        // Ouvrir le gestionnaire de fichiers filtre sur .enc
        m_fileBrowserPage->setProperty("filterHint","enc");
        m_previousPage = m_currentPage;     // le programme sera chargé DANS la simulation
        switchPage(5);
    });
    connect(m_simPage, &SimulationPage::cycleStartRequested, this, [this](){
        const QString path = m_executionPage->currentNcPath();
        if (!path.isEmpty() && bridge && bridge->isLoaded()) {
            bridge->loadFile(path);
            QTimer::singleShot(200, this, [this](){ vcp->cycleStart(); });
        } else {
            vcp->cycleStart();
        }
    });
    connect(m_simPage, &SimulationPage::resetRequested,
            this, [this](){ vcp->reset(); });
    connect(m_simPage, &SimulationPage::gotoLineRequested,
            this, [this](int /*line*/){ /* TODO: bridge->gotoLine(line) */ });

    // Signal auto : ProgrammePage → ExecutionPage + SimulationPage
    // Déclenché à chaque modification de bloc si machine à l'arrêt
    connect(m_programmePage, &ProgrammePage::gCodeReady,
            this, &MainWindow::onGCodeReady);
    connect(m_programmePage, &ProgrammePage::simGCodeReady, this,
            [this](const QVector<BlockData> &blocks, const QVector<GCodeLine> &gcode, const QString &){
                if (!m_simPage) return;
                QString encPath = m_programmePage->currentEncPath();
                QString ncDir = encPath.isEmpty() ? QString() : QFileInfo(encPath).absolutePath();
                m_simPage->load(blocks, gcode, ncDir);
            });

    // ── Base outils ───────────────────────────────────────────
    // tools.json stocké dans le répertoire programme
    const QString toolsJson = QApplication::applicationDirPath() + "/tools.json";
    ToolDatabase::instance().init(bridge, toolsJson);
    ToolDatabase::instance().load();

    // ── Connexion : UnitEditor → ToolTableDialog ─────────────
    connect(m_programmePage->unitEditor(),
            &UnitEditor::openToolTable,
            this, [this](bool byName) {
                openToolTableDialog(byName,
                                    [this](const ToolData &tool){
                                        m_programmePage->unitEditor()->applyToolData(tool);
                                    });
            });



    // ── Signal softkeys contextuels de l'UnitEditor ──────────
    // Créer une page contextuelle dédiée dans softkeyStack
    m_ctxSoftkeyPage = new QWidget(this);
    m_ctxSoftkeyPage->setStyleSheet("QWidget { background: #D6D6D6; }");
    m_ctxSoftkeyLayout = new QHBoxLayout(m_ctxSoftkeyPage);
    m_ctxSoftkeyLayout->setContentsMargins(2,2,2,2);
    m_ctxSoftkeyLayout->setSpacing(3);
    ui->softkeyStack->addWidget(m_ctxSoftkeyPage);
    m_ctxSoftkeyPageIndex = ui->softkeyStack->indexOf(m_ctxSoftkeyPage);

    connect(m_programmePage->unitEditor(),
            SIGNAL(softkeyContextChanged(UnitEditor::SoftkeyContext,QStringList,int,QString)),
            this,
            SLOT(onUnitEditorSoftkeyContext(UnitEditor::SoftkeyContext,QStringList,int,QString)));

    // ── API D'ABORD (avant les connects qui l'utilisent) ─────
    vcp    = new VcpController(this);
    bridge = new CentroidBridgeWrapper();
    bridge->init();
    state.load("CentroidBridgeState.dll");
    vcp->setApi(bridge);

    // ── NAVIGATION ───────────────────────────────────────────
    setupNavigation();
    setupSoftkeys();
    setupVcp();
    setupMdi();
    setupDroButtons();

    // ── ICÔNES ───────────────────────────────────────────────
    setFullIcon(ui->btnSpindleStart, ":/icons/broche_marche.png", QSize(98,98));
    setFullIcon(ui->btnSpindleStop,  ":/icons/broche_stop.png",   QSize(98,98));
    setFullIcon(ui->btnCycleStart,   ":/icons/cycle_go.png",      QSize(129,128));
    setFullIcon(ui->btnCycleStop,    ":/icons/cycle_red.png",      QSize(129,128));

    // ── DIALS ────────────────────────────────────────────────
    ui->dialFeedOverride->setStyleSheet(DIAL_STYLE);
    ui->dialFeedOverride->setRange(0, 200);
    ui->dialFeedOverride->setValue(100);
    ui->dialFeedOverride->setNotchesVisible(true);
    ui->dialFeedOverride->setNotchTarget(10.0);

    ui->dialSpindleOverride->setStyleSheet(DIAL_STYLE);
    ui->dialSpindleOverride->setRange(0, 200);
    ui->dialSpindleOverride->setValue(100);
    ui->dialSpindleOverride->setNotchesVisible(true);
    ui->dialSpindleOverride->setNotchTarget(10.0);

    ui->dialMdiFeedOverride->setStyleSheet(DIAL_STYLE);
    ui->dialMdiFeedOverride->setRange(0, 200);
    ui->dialMdiFeedOverride->setValue(100);
    ui->dialMdiSpindleOverride->setStyleSheet(DIAL_STYLE);
    ui->dialMdiSpindleOverride->setRange(0, 200);
    ui->dialMdiSpindleOverride->setValue(100);

    connect(ui->dialFeedOverride, &QDial::valueChanged, this, [this](int val) {
        QString c = (val<=50)?"#d06010":(val<=100)?"#3a8f34":"#c02020";
        ui->lblDialFeed->setText(QString("%1%").arg(val));
        ui->lblDialFeed->setStyleSheet(QString("QLabel{color:%1;font-size:10pt;font-weight:900;background:transparent;}").arg(c));
    });
    connect(ui->dialSpindleOverride, &QDial::valueChanged, this, [this](int val) {
        QString c = (val<=50)?"#d06010":(val<=100)?"#3a8f34":"#c02020";
        ui->lblDialSpindle->setText(QString("%1%").arg(val));
        ui->lblDialSpindle->setStyleSheet(QString("QLabel{color:%1;font-size:10pt;font-weight:900;background:transparent;}").arg(c));
    });

    // ── ÉTAT INITIAL ─────────────────────────────────────────
    refreshIncCont();
    refreshLentRapide();
    refreshManAuto();
    refreshJogSteps();

    if (ui->gaugeSpindleSpeed) {
        ui->gaugeSpindleSpeed->setMinimum(0);
        ui->gaugeSpindleSpeed->setMaximum(200);
        ui->gaugeSpindleSpeed->setValue(2);
    }

    // ── TIMER ─────────────────────────────────────────────────
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::updateDisplay);
    timer->start(100);

    switchPage(0);
}

MainWindow::~MainWindow() { delete ui; }

// ─────────────────────────────────────────────────────────────
//  NAVIGATION ONGLETS
// ─────────────────────────────────────────────────────────────
void MainWindow::setupNavigation()
{
    connect(ui->tabModeManuel,    &QToolButton::clicked, this, [this](){ switchPage(0); });
    connect(ui->tabProgrammation, &QToolButton::clicked, this, [this](){ switchPage(1); });
    connect(ui->tabExecution,     &QToolButton::clicked, this, [this](){ switchPage(2); });
    connect(ui->tabMDI,           &QToolButton::clicked, this, [this](){ switchPage(3); });
    connect(ui->tabSimulation,    &QToolButton::clicked, this, [this](){ switchPage(4); });
    connect(ui->tabFichiers,      &QToolButton::clicked, this, [this](){
        m_previousPage = m_currentPage;
        switchPage(5);
    });
}

// ─────────────────────────────────────────────────────────────
//  VCP — boutons communs Manuel + MDI
// ─────────────────────────────────────────────────────────────
void MainWindow::setupVcp()
{
    // ── FEED OVERRIDE Manuel ──────────────────────────────────
    connect(ui->btnFeedPlus,  &QPushButton::pressed,  this, [this](){ vcp->feedPlus(true); });
    connect(ui->btnFeedPlus,  &QPushButton::released, this, [this](){ vcp->feedPlus(false); });
    connect(ui->btnFeedMinus, &QPushButton::pressed,  this, [this](){ vcp->feedMinus(true); });
    connect(ui->btnFeedMinus, &QPushButton::released, this, [this](){ vcp->feedMinus(false); });
    connect(ui->btnFeedHold,  &QPushButton::clicked,  this, [this](){ vcp->feedHold(); });
    connect(ui->btnSpindlePlus,  &QPushButton::pressed,  this, [this](){ vcp->spindlePlus(true); });
    connect(ui->btnSpindlePlus,  &QPushButton::released, this, [this](){ vcp->spindlePlus(false); });
    connect(ui->btnSpindleMinus, &QPushButton::pressed,  this, [this](){ vcp->spindleMinus(true); });
    connect(ui->btnSpindleMinus, &QPushButton::released, this, [this](){ vcp->spindleMinus(false); });

    // ── FEED OVERRIDE MDI (memes actions) ────────────────────
    connect(ui->btnMdiFeedPlus,  &QPushButton::pressed,  this, [this](){ vcp->feedPlus(true); });
    connect(ui->btnMdiFeedPlus,  &QPushButton::released, this, [this](){ vcp->feedPlus(false); });
    connect(ui->btnMdiFeedMinus, &QPushButton::pressed,  this, [this](){ vcp->feedMinus(true); });
    connect(ui->btnMdiFeedMinus, &QPushButton::released, this, [this](){ vcp->feedMinus(false); });
    connect(ui->btnMdiFeedHold,  &QPushButton::clicked,  this, [this](){ vcp->feedHold(); });
    connect(ui->btnMdiSpindlePlus,  &QPushButton::pressed,  this, [this](){ vcp->spindlePlus(true); });
    connect(ui->btnMdiSpindlePlus,  &QPushButton::released, this, [this](){ vcp->spindlePlus(false); });
    connect(ui->btnMdiSpindleMinus, &QPushButton::pressed,  this, [this](){ vcp->spindleMinus(true); });
    connect(ui->btnMdiSpindleMinus, &QPushButton::released, this, [this](){ vcp->spindleMinus(false); });

    // ── BI-ÉTAT Manuel ────────────────────────────────────────
    connect(ui->btnJogIncCont, &QPushButton::clicked, this, [this](){
        vcp->toggleIncCont(); modeInc=!modeInc; refreshIncCont();
    });
    connect(ui->btnJogRapid, &QPushButton::clicked, this, [this](){
        vcp->toggleRapid(); modeRapid=!modeRapid; refreshLentRapide();
    });
    connect(ui->btnSpindleAutoMan, &QPushButton::clicked, this, [this](){
        vcp->toggleManAuto(); modeMan=!modeMan; refreshManAuto();
    });

    // ── BI-ÉTAT MDI ───────────────────────────────────────────
    connect(ui->btnMdiJogIncCont, &QPushButton::clicked, this, [this](){
        vcp->toggleIncCont(); modeInc=!modeInc;
        ui->btnMdiJogIncCont->setText(modeInc ? "INC" : "CONT");
        ui->btnMdiJogIncCont->setStyleSheet(modeInc ? S_INC : S_CONT);
    });
    connect(ui->btnMdiJogRapid, &QPushButton::clicked, this, [this](){
        vcp->toggleRapid(); modeRapid=!modeRapid;
        ui->btnMdiJogRapid->setText(modeRapid ? "RAPIDE" : "LENT");
        ui->btnMdiJogRapid->setStyleSheet(modeRapid ? S_RAPID : S_LENT);
    });

    // ── JOG AXES Manuel ──────────────────────────────────────
    connect(ui->btnJogXPlus,  &QPushButton::pressed,  this, [this](){ vcp->jogXPlus(true); });
    connect(ui->btnJogXPlus,  &QPushButton::released, this, [this](){ vcp->jogXPlus(false); });
    connect(ui->btnJogXMinus, &QPushButton::pressed,  this, [this](){ vcp->jogXMinus(true); });
    connect(ui->btnJogXMinus, &QPushButton::released, this, [this](){ vcp->jogXMinus(false); });
    connect(ui->btnJogYPlus,  &QPushButton::pressed,  this, [this](){ vcp->jogYPlus(true); });
    connect(ui->btnJogYPlus,  &QPushButton::released, this, [this](){ vcp->jogYPlus(false); });
    connect(ui->btnJogYMinus, &QPushButton::pressed,  this, [this](){ vcp->jogYMinus(true); });
    connect(ui->btnJogYMinus, &QPushButton::released, this, [this](){ vcp->jogYMinus(false); });
    connect(ui->btnJogZPlus,  &QPushButton::pressed,  this, [this](){ vcp->jogZPlus(true); });
    connect(ui->btnJogZPlus,  &QPushButton::released, this, [this](){ vcp->jogZPlus(false); });
    connect(ui->btnJogZMinus, &QPushButton::pressed,  this, [this](){ vcp->jogZMinus(true); });
    connect(ui->btnJogZMinus, &QPushButton::released, this, [this](){ vcp->jogZMinus(false); });

    // ── JOG AXES MDI (memes axes) ────────────────────────────
    connect(ui->btnMdiJogXPlus,  &QPushButton::pressed,  this, [this](){ vcp->jogXPlus(true); });
    connect(ui->btnMdiJogXPlus,  &QPushButton::released, this, [this](){ vcp->jogXPlus(false); });
    connect(ui->btnMdiJogXMinus, &QPushButton::pressed,  this, [this](){ vcp->jogXMinus(true); });
    connect(ui->btnMdiJogXMinus, &QPushButton::released, this, [this](){ vcp->jogXMinus(false); });
    connect(ui->btnMdiJogYPlus,  &QPushButton::pressed,  this, [this](){ vcp->jogYPlus(true); });
    connect(ui->btnMdiJogYPlus,  &QPushButton::released, this, [this](){ vcp->jogYPlus(false); });
    connect(ui->btnMdiJogYMinus, &QPushButton::pressed,  this, [this](){ vcp->jogYMinus(true); });
    connect(ui->btnMdiJogYMinus, &QPushButton::released, this, [this](){ vcp->jogYMinus(false); });
    connect(ui->btnMdiJogZPlus,  &QPushButton::pressed,  this, [this](){ vcp->jogZPlus(true); });
    connect(ui->btnMdiJogZPlus,  &QPushButton::released, this, [this](){ vcp->jogZPlus(false); });
    connect(ui->btnMdiJogZMinus, &QPushButton::pressed,  this, [this](){ vcp->jogZMinus(true); });
    connect(ui->btnMdiJogZMinus, &QPushButton::released, this, [this](){ vcp->jogZMinus(false); });

    // ── JOG PAS Manuel ───────────────────────────────────────
    connect(ui->btnJogX1,   &QPushButton::clicked, this, [this](){ vcp->setX1();   jogStep=1;   refreshJogSteps(); });
    connect(ui->btnJogX10,  &QPushButton::clicked, this, [this](){ vcp->setX10();  jogStep=10;  refreshJogSteps(); });
    connect(ui->btnJogX100, &QPushButton::clicked, this, [this](){ vcp->setX100(); jogStep=100; refreshJogSteps(); });
    connect(ui->btnJogMPG,  &QPushButton::clicked, this, [this](){ vcp->toggleMPG(); modeMPG=!modeMPG; });

    // ── JOG PAS MDI ──────────────────────────────────────────
    connect(ui->btnMdiJogX1,   &QPushButton::clicked, this, [this](){ vcp->setX1();   jogStep=1; });
    connect(ui->btnMdiJogX10,  &QPushButton::clicked, this, [this](){ vcp->setX10();  jogStep=10; });
    connect(ui->btnMdiJogX100, &QPushButton::clicked, this, [this](){ vcp->setX100(); jogStep=100; });
    connect(ui->btnMdiJogMPG,  &QPushButton::clicked, this, [this](){ vcp->toggleMPG(); modeMPG=!modeMPG; });

    // ── BROCHE Manuel ────────────────────────────────────────
    connect(ui->btnSpindleCW,    &QPushButton::clicked, this, [this](){ vcp->spindleCW(); });
    connect(ui->btnSpindleCCW,   &QPushButton::clicked, this, [this](){ vcp->spindleCCW(); });
    connect(ui->btnSpindleStop,  &QPushButton::clicked, this, [this](){ vcp->spindleStop(); });
    connect(ui->btnSpindleStart, &QPushButton::clicked, this, [this](){ vcp->spindleStart(); });

    // ── BROCHE MDI ───────────────────────────────────────────
    connect(ui->btnMdiSpindleCW,    &QPushButton::clicked, this, [this](){ vcp->spindleCW(); });
    connect(ui->btnMdiSpindleCCW,   &QPushButton::clicked, this, [this](){ vcp->spindleCCW(); });
    connect(ui->btnMdiSpindleStop,  &QToolButton::clicked, this, [this](){ vcp->spindleStop(); });
    connect(ui->btnMdiSpindleStart, &QToolButton::clicked, this, [this](){ vcp->spindleStart(); });

    // ── CYCLE Manuel ──────────────────────────────────────────
    connect(ui->btnCycleStart, &QToolButton::clicked, this, [this](){ vcp->cycleStart(); });
    connect(ui->btnCycleStop,  &QToolButton::clicked, this, [this](){ vcp->cycleStop(); });
    connect(ui->btnCycleReset, &QPushButton::clicked, this, [this](){ vcp->reset(); });

    // ── CYCLE MDI ────────────────────────────────────────────
    connect(ui->btnMdiCycleStart, &QToolButton::clicked, this, [this](){ vcp->cycleStart(); });
    connect(ui->btnMdiCycleStop,  &QToolButton::clicked, this, [this](){ vcp->cycleStop(); });
    connect(ui->btnMdiCycleReset, &QPushButton::clicked, this, [this](){ vcp->reset(); });
}

// ─────────────────────────────────────────────────────────────
//  MDI — saisie et validation de commande
// ─────────────────────────────────────────────────────────────
void MainWindow::setupMdi()
{
    // Valider avec bouton OU touche Enter
    auto sendMdi = [this](){
        QString cmd = ui->txtMdiInput->text().trimmed();
        if (cmd.isEmpty()) return;
        sendCommand(cmd);
        ui->txtMdiInput->clear();
    };
    connect(ui->btnMdiSend,  &QPushButton::clicked, this, sendMdi);
    connect(ui->txtMdiInput, &QLineEdit::returnPressed, this, sendMdi);
}

// ─────────────────────────────────────────────────────────────
//  DRO WORK — X Y Z cliquables pour modifier l'origine pièce
//  CNC_Wcs_SetWorkpieceLocation(axis, value) :
//  Definit la coordonnee piece courante = value
//  (l'origine se deplace en consequence)
// ─────────────────────────────────────────────────────────────
void MainWindow::setupDroButtons()
{
    // Manuel
    connect(ui->btnWorkX, &QPushButton::clicked, this, [this](){
        bool ok;
        double val = QInputDialog::getDouble(this, "Origine X",
                                             "Definir X piece a :", bridge->getWorkDro(0), -99999, 99999, 4, &ok);
        if (ok) bridge->setWorkOffset(0, val);
    });
    connect(ui->btnWorkY, &QPushButton::clicked, this, [this](){
        bool ok;
        double val = QInputDialog::getDouble(this, "Origine Y",
                                             "Definir Y piece a :", bridge->getWorkDro(1), -99999, 99999, 4, &ok);
        if (ok) bridge->setWorkOffset(1, val);
    });
    connect(ui->btnWorkZ, &QPushButton::clicked, this, [this](){
        bool ok;
        double val = QInputDialog::getDouble(this, "Origine Z",
                                             "Definir Z piece a :", bridge->getWorkDro(2), -99999, 99999, 4, &ok);
        if (ok) bridge->setWorkOffset(2, val);
    });

    // MDI — meme comportement
    connect(ui->btnMdiWorkX, &QPushButton::clicked, this, [this](){
        bool ok;
        double val = QInputDialog::getDouble(this, "Origine X",
                                             "Definir X piece a :", bridge->getWorkDro(0), -99999, 99999, 4, &ok);
        if (ok) bridge->setWorkOffset(0, val);
    });
    connect(ui->btnMdiWorkY, &QPushButton::clicked, this, [this](){
        bool ok;
        double val = QInputDialog::getDouble(this, "Origine Y",
                                             "Definir Y piece a :", bridge->getWorkDro(1), -99999, 99999, 4, &ok);
        if (ok) bridge->setWorkOffset(1, val);
    });
    connect(ui->btnMdiWorkZ, &QPushButton::clicked, this, [this](){
        bool ok;
        double val = QInputDialog::getDouble(this, "Origine Z",
                                             "Definir Z piece a :", bridge->getWorkDro(2), -99999, 99999, 4, &ok);
        if (ok) bridge->setWorkOffset(2, val);
    });
}

// ─────────────────────────────────────────────────────────────
//  SOFTKEYS PAR PAGE
// ─────────────────────────────────────────────────────────────
void MainWindow::setupSoftkeys()
{
    // Fleches < > inactives pour l'instant (pagination future)
    // ui->sk_prev_0 etc. pas connectees

    // ── Manuel : M / S / F ───────────────────────────────────
    // Ces commandes utilisent CNC_Job_RunCommand
    // Format: "M8" / "S1000" / "F500"
    connect(ui->sk_M_0, &QToolButton::clicked, this, [this](){
        bool ok;
        QString cmd = QInputDialog::getText(this, "Commande M",
                                            "Ex: M8   M9   M3 S1000   M5", QLineEdit::Normal, "M", &ok);
        if (ok && !cmd.isEmpty()) sendCommand(cmd.toUpper());
    });
    connect(ui->sk_S_0, &QToolButton::clicked, this, [this](){
        bool ok;
        double rpm = QInputDialog::getDouble(this, "Vitesse broche S",
                                             "Vitesse (tr/min) :", 0, 0, 99999, 0, &ok);
        if (ok) sendCommand(QString("S%1").arg((int)rpm));
    });
    connect(ui->sk_F_0, &QToolButton::clicked, this, [this](){
        bool ok;
        double f = QInputDialog::getDouble(this, "Avance F",
                                           "Avance (mm/min) :", 0, 0, 99999, 0, &ok);
        if (ok) sendCommand(QString("F%1").arg((int)f));
    });
    connect(ui->sk_orig_0, &QToolButton::clicked, this, [this](){
        // TODO: gestion origines
    });
    connect(ui->sk_tbl_0, &QToolButton::clicked, this, [this](){
        // Tableau outil depuis Mode Manuel — pas de callback UnitEditor
        // On ouvre juste pour consulter/modifier les offsets
        openToolTableDialog(false, nullptr);
    });
    connect(ui->sk_quit_0, &QToolButton::clicked, this, [this](){ close(); });

    // ── Programme — Charger / Enregistrer ───────────────────────
    // Fix texte invisible au clic : surcharger le :pressed du style global
    ui->sk_charger_1->setStyleSheet(
        "QToolButton{"
        "  background:#EBEBEB; color:#000000; font-size:9pt; font-weight:bold;"
        "  border:1px solid #808080;"
        "  border-bottom:2px solid #606060;"
        "  border-radius:4px; padding:2px 4px; margin:3px 2px;"
        "}"
        "QToolButton:hover{ background:#DBDBDB; color:#000000; border-color:#404040; }"
        "QToolButton:pressed{ background:#C8C8C8; color:#000000; }"
        "QToolButton::menu-indicator{ width:0; }"
        );
    // sk_charger_1 : menu Nouveau / Charger
    {
        auto *menu = new QMenu(this);
        menu->addAction("Nouveau programme", this, [this](){
            m_programmePage->newProgram();
        });
        menu->addAction("Charger (.enc)...", this, [this](){
            // Ouvrir le gestionnaire de fichiers
            switchPage(5);
        });
        ui->sk_charger_1->setMenu(menu);
        ui->sk_charger_1->setPopupMode(QToolButton::InstantPopup);
    }
    connect(ui->sk_enregistrer_1, &QToolButton::clicked, this, [this](){
        m_programmePage->saveCurrentBlock();   // flush bloc actif avant écriture
        m_programmePage->saveEncFile();
    });

    // ── Page 1A : BlkForm | Tous usinages | Sélect. ─────────
    connect(ui->sk_blkform_1, &QToolButton::clicked, this, [this](){ m_programmePage->insertBlkForm(); });
    connect(ui->sk_poche_1,   &QToolButton::clicked, this, [this](){ showCycleNav(m_skIdx_familles); });
    connect(ui->sk_select_1,  &QToolButton::clicked, this, [this](){ m_selReturnPage = 0; enterSelectionMode(); });
    ui->sk_select_1->hide();                      // doublon : Sélect. bloc est sur la barre ▷
    connect(ui->sk_next_1a,   &QToolButton::clicked, this, [this](){ showProgPage(1); });
    connect(ui->sk_prev_1a,   &QToolButton::clicked, this, [this](){ /* page 0 */ });

    // Icônes page principale
    setSoftkeyIcon(ui->sk_blkform_1, ":/unit/blk_form.png",
                   "Définir le brut");
    setSoftkeyIcon(ui->sk_poche_1,   ":/unit/tous les cycle usinage.png",
                   "Tous les usinages");

    // sk_trou_1 et sk_contour_1 inutilisés sur la page principale — masqués
    ui->sk_trou_1->hide();
    ui->sk_contour_1->hide();

    // ── Navigation hiérarchique des cycles ────────────────────
    setupProgSoftkeys();

    // ── Programme — page 1B (edition) ────────────────────────
    // Barre « ▷ » (edition) facon TNC :
    //   ◁ | SÉLECT. BLOC | COUPER BLOC | INSÉRER BLOC | COPIER BLOC | SUPPRIMER BLOC
    ui->sk_copier_1->setText("SÉLECT.\nBLOC");
    ui->sk_coller_1->setText("COUPER\nBLOC");
    ui->sk_couper_1->setText("INSÉRER\nBLOC");
    ui->sk_suppr_1->setText("COPIER\nBLOC");
    ui->sk_ins_1->setText("SUPPRIMER\nBLOC");
    for (QToolButton *b : {ui->sk_copier_1, ui->sk_coller_1, ui->sk_couper_1, ui->sk_suppr_1, ui->sk_ins_1}) {
        b->setIcon(QIcon()); b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
    ui->sk_couper_1->setEnabled(false);          // Insérer : grise tant que rien n'est copie
    ui->sk_next_1b->hide();                       // l'enregistrement est sur la barre principale
    connect(ui->sk_copier_1, &QToolButton::clicked, this, [this](){ m_selReturnPage = 1; enterSelectionMode(); });
    connect(ui->sk_coller_1, &QToolButton::clicked, this, [this](){ m_programmePage->cutBlock(); });
    connect(ui->sk_couper_1, &QToolButton::clicked, this, [this](){ m_programmePage->pasteBlock(); });
    connect(ui->sk_suppr_1,  &QToolButton::clicked, this, [this](){ m_programmePage->copyBlock(); });
    connect(ui->sk_ins_1,    &QToolButton::clicked, this, [this](){ m_programmePage->deleteBlock(); });
    connect(ui->sk_prev_1b,  &QToolButton::clicked, this, [this](){ showProgPage(0); });

    // ── Programme — page 1C (sélection) style iTNC 530 ───────
    // sk_sel_up     = Quitter sélection
    // sk_sel_down   = Marquer bloc suivant
    // sk_sel_copier = Copier  (→ clipboard, quitte sélection)
    // sk_sel_couper = Découper (→ clipboard + delete, quitte sélection)
    connect(ui->sk_sel_up,     &QToolButton::clicked, this, &MainWindow::exitSelectionMode);
    connect(ui->sk_sel_down,   &QToolButton::clicked, this, [this](){ m_programmePage->selectNextBlock(); });
    connect(ui->sk_sel_copier, &QToolButton::clicked, this, [this](){
        m_programmePage->copyBlock();
        exitSelectionMode();
    });
    connect(ui->sk_sel_couper, &QToolButton::clicked, this, [this](){
        m_programmePage->cutBlock();
        exitSelectionMode();
    });
    // ── Barre de selection facon TNC :
    //    Quitter sélection | Couper | Insérer | Copier | Bloc suivant
    //    Insérer est grise tant que rien n'a ete copie ou coupe.
    {
        auto *lay = qobject_cast<QBoxLayout*>(ui->sk_sel_up->parentWidget()->layout());
        if (lay) {
            const QList<QWidget*> order{ ui->sk_sel_up, ui->sk_sel_couper, ui->sk_sel_ins,
                                         ui->sk_sel_copier, ui->sk_sel_down };
            int idx = lay->indexOf(ui->sk_sel_up);
            for (QWidget *w : order) { lay->removeWidget(w); lay->insertWidget(idx++, w); }
        }
        ui->sk_sel_up->setText("QUITTER\nSÉLECTION");
        ui->sk_sel_couper->setText("COUPER\nBLOC");
        ui->sk_sel_ins->setText("INSÉRER\nBLOC");
        ui->sk_sel_copier->setText("COPIER\nBLOC");
        ui->sk_sel_down->setText("BLOC\nSUIVANT ▼");
        ui->sk_sel_ins->setEnabled(false);
        ui->sk_sel_ins->hide();                   // doublon : Insérer est sur la barre ▷
        connect(ui->sk_sel_ins, &QToolButton::clicked, this, [this](){
            m_programmePage->pasteBlock();
            exitSelectionMode();
        });
    }
    // ── « Insérer dernier bloc » : un seul bouton, tout a droite avant ▷
    {
        auto *lay = qobject_cast<QBoxLayout*>(ui->sk_next_1a->parentWidget()->layout());
        if (lay) {
            lay->removeWidget(ui->sk_contour_1);
            lay->insertWidget(lay->indexOf(ui->sk_next_1a), ui->sk_contour_1);
        }
        ui->sk_contour_1->setIcon(QIcon());
        ui->sk_contour_1->setText("INSÉRER\nDERNIER\nBLOC");
        ui->sk_contour_1->setToolButtonStyle(Qt::ToolButtonTextOnly);
        ui->sk_contour_1->setEnabled(false);
        ui->sk_contour_1->hide();                 // doublon : Insérer est sur la barre ▷
        connect(ui->sk_contour_1, &QToolButton::clicked, this, [this](){ m_programmePage->pasteBlock(); });
    }
    // Etat grise / actif des boutons d'insertion
    connect(m_programmePage, &ProgrammePage::clipboardChanged, this, [this](bool has){
        ui->sk_sel_ins->setEnabled(has);
        ui->sk_contour_1->setEnabled(has);
        ui->sk_couper_1->setEnabled(has);         // INSÉRER BLOC de la barre ▷
    });

    // ── Softkeys page Exécution : Tableau outil + GOTO seulement ──
    connect(ui->sk_exec_tableau, &QToolButton::clicked, this,
            [this](){ openToolTableDialog(false, nullptr); });
    connect(ui->sk_exec_goto, &QToolButton::clicked, this, [this](){
        // TODO : dialogue GOTO ligne/bloc
        // Pour l'instant : placeholder
        bool ok;
        int line = QInputDialog::getInt(this, "GOTO", "Aller à la ligne :", 1, 1, 99999, 1, &ok);
        Q_UNUSED(line)
    });

    // ── Softkeys page Simulation — 2 pages (flèches < >) ─────
    //
    // PAGE 1 (défaut) : vue et actions fréquentes
    //   e1=Play  e2=Pause  e3=Reset  e4=Goto  e5=Fit All  e6=Zoom+  e7=Zoom-  e8=Brut
    // PAGE 2 : chargement, affichage, mesure
    //   e1=Charger .enc  e2=Charger .nc  e3=Plein/Transparent  e4=Rapides G0
    //   e4=Chemin outil  e5=Rapides G0  e6=Plan 2D/3D  e7=Mesure  e8=Effacer mesure
    //
    m_simSkPage = 0;  // page courante

    // ── Bouton bi-état Transparent/Plein (remplace l'ancien curseur %) ──
    // L'ancien widget custom "− Brut + / XX%" est supprimé : il avait des
    // défauts d'affichage à certains pourcentages. À la place, sk_e3_3 en
    // page 2 devient un bouton qui bascule entre 30% (transparent, sans
    // défaut) et opaque.
    m_skStockAlpha = nullptr;

    auto applySimSoftkeys = std::make_shared<std::function<void()>>();
    *applySimSoftkeys = [this, applySimSoftkeys]{
        auto setSk = [this](QToolButton *b, const QString &txt, std::function<void()> fn){
            if(!b) return;
            b->setText(txt);
            b->setEnabled(!txt.isEmpty());
            b->disconnect();
            if(fn) connect(b, &QToolButton::clicked, this, fn);
        };
        if (m_simSkPage == 0) {
            // PAGE 1
            setSk(ui->sk_e1_3, "▶ Play",   [this]{ if(m_simPage) m_simPage->skPlay();   });
            setSk(ui->sk_e2_3, "⏸ Pause",  [this]{ if(m_simPage) m_simPage->skPause();  });
            setSk(ui->sk_e3_3, "↺ Reset",  [this]{ if(m_simPage) m_simPage->skReset();  });
            setSk(ui->sk_e4_3, "Goto ligne",[this]{ if(m_simPage) m_simPage->skGoto();   });
            setSk(ui->sk_e5_3, "Fit All",  [this]{ if(m_simPage) m_simPage->fitAll();   });
            setSk(ui->sk_e6_3, "Zoom +",   [this]{ if(m_simPage) m_simPage->view()->zoomIn();  });
            setSk(ui->sk_e7_3, "Zoom -",   [this]{ if(m_simPage) m_simPage->view()->zoomOut(); });
            setSk(ui->sk_e8_3, "Brut",     [this]{ if(m_simPage) m_simPage->toggleStock(); });
            if (ui->sk_e3_3) ui->sk_e3_3->show();
        } else {
            // PAGE 2
            setSk(ui->sk_e1_3, "Charger .enc",[this]{ if(m_simPage) m_simPage->skLoadEnc(); });
            setSk(ui->sk_e2_3, "Charger .nc", [this]{ if(m_simPage) m_simPage->skLoadNc();  });
            setSk(ui->sk_e4_3, "Chemin outil",[this]{ if(m_simPage) m_simPage->togglePath(); });
            setSk(ui->sk_e5_3, "Rapides G0",  [this]{ if(m_simPage) m_simPage->toggleRapids(); });
            setSk(ui->sk_e6_3, "Plan 2D/3D",  [this]{ if(m_simPage) m_simPage->togglePlanView(); });
            setSk(ui->sk_e7_3, "Mesure",      [this]{ if(m_simPage) m_simPage->skMesure();     });
            setSk(ui->sk_e8_3, "Effacer mes.",[this]{ if(m_simPage) m_simPage->skClearMesure(); });
            // Matière pleine ↔ transparente : le libellé montre le mode actuel
            auto stockLabel = [this]{
                return (m_simPage && m_simPage->isStockOpaque())
                       ? QString("Matière\nPleine") : QString("Matière\nTransp.");
            };
            setSk(ui->sk_e3_3, stockLabel(), [this, stockLabel]{
                if(!m_simPage) return;
                m_simPage->toggleStockMode();
                if(ui->sk_e3_3) ui->sk_e3_3->setText(stockLabel());
            });
            if (ui->sk_e3_3) ui->sk_e3_3->show();
        }
    };

    (*applySimSoftkeys)();

    // Flèches prev/next : bascule entre page 0 et page 1
    if (ui->sk_prev_3) {
        connect(ui->sk_prev_3, &QToolButton::clicked, this, [this, applySimSoftkeys]{
            m_simSkPage = (m_simSkPage == 0) ? 1 : 0;
            (*applySimSoftkeys)();
        });
    }
    if (ui->sk_next_3) {
        connect(ui->sk_next_3, &QToolButton::clicked, this, [this, applySimSoftkeys]{
            m_simSkPage = (m_simSkPage == 0) ? 1 : 0;
            (*applySimSoftkeys)();
        });
    }

    // ── Signaux VCP depuis ExecutionPage ─────────────────────
    connect(m_executionPage, &ExecutionPage::jogXPlus,  this, [this](bool p){ vcp->jogXPlus(p); });
    connect(m_executionPage, &ExecutionPage::jogXMinus, this, [this](bool p){ vcp->jogXMinus(p); });
    connect(m_executionPage, &ExecutionPage::jogYPlus,  this, [this](bool p){ vcp->jogYPlus(p); });
    connect(m_executionPage, &ExecutionPage::jogYMinus, this, [this](bool p){ vcp->jogYMinus(p); });
    connect(m_executionPage, &ExecutionPage::jogZPlus,  this, [this](bool p){ vcp->jogZPlus(p); });
    connect(m_executionPage, &ExecutionPage::jogZMinus, this, [this](bool p){ vcp->jogZMinus(p); });
    connect(m_executionPage, &ExecutionPage::feedPlusPressed,    this, [this](bool p){ vcp->feedPlus(p); });
    connect(m_executionPage, &ExecutionPage::feedMinusPressed,   this, [this](bool p){ vcp->feedMinus(p); });
    connect(m_executionPage, &ExecutionPage::spindlePlusPressed, this, [this](bool p){ vcp->spindlePlus(p); });
    connect(m_executionPage, &ExecutionPage::spindleMinusPressed,this, [this](bool p){ vcp->spindleMinus(p); });
    connect(m_executionPage, &ExecutionPage::incContToggled, this, [this](){ vcp->toggleIncCont(); });
    connect(m_executionPage, &ExecutionPage::rapidToggled,   this, [this](){ vcp->toggleRapid(); });
    connect(m_executionPage, &ExecutionPage::setX1,   this, [this](){ vcp->setX1(); });
    connect(m_executionPage, &ExecutionPage::setX10,  this, [this](){ vcp->setX10(); });
    connect(m_executionPage, &ExecutionPage::setX100, this, [this](){ vcp->setX100(); });
    connect(m_executionPage, &ExecutionPage::spindleCWRequested,   this, [this](){ vcp->spindleCW(); });
    connect(m_executionPage, &ExecutionPage::spindleCCWRequested,  this, [this](){ vcp->spindleCCW(); });
    connect(m_executionPage, &ExecutionPage::spindleStopRequested, this, [this](){ vcp->spindleStop(); });
    connect(m_executionPage, &ExecutionPage::feedHoldRequested,    this, [this](){ vcp->feedHold(); });
    connect(m_executionPage, &ExecutionPage::cycleStopRequested,   this, [this](){ vcp->cycleStop(); });
    connect(m_executionPage, &ExecutionPage::resetRequested,       this, [this](){ vcp->reset(); });
    connect(m_executionPage, &ExecutionPage::toolTableRequested,
            this, [this](){ openToolTableDialog(false, nullptr); });

    // CYCLE START depuis bouton interne ExecutionPage (avec loadFile)
    connect(m_executionPage, &ExecutionPage::cycleStartRequested, this, [this](){
        const QString path = m_executionPage->currentNcPath();
        if (!path.isEmpty() && bridge && bridge->isLoaded()) {
            bridge->loadFile(path);
            QTimer::singleShot(200, this, [this](){ vcp->cycleStart(); });
        } else {
            vcp->cycleStart();
        }
    });
}

// ─────────────────────────────────────────────────────────────
//  NAVIGATION SOUS-PAGES PROGRAMME
// ─────────────────────────────────────────────────────────────
// Indices softkeyStack : 0=Manuel 1=skPage_programme 2=skPage_prog_edit
//                        3=skPage_prog_sel 4=MDI 5=Simulation 6=Fichiers
// (les 3 pages prog sont aux indices 1, 2, 3 du softkeyStack)
// ─────────────────────────────────────────────────────────────
//  TABLEAU OUTIL — ouvre le dialogue, callback optionnel
// ─────────────────────────────────────────────────────────────
void MainWindow::openToolTableDialog(bool byName,
                                     std::function<void(const ToolData&)> callback)
{
    auto *dlg = new ToolTableDialog(this);
    dlg->setByName(byName);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    if (callback) {
        connect(dlg, &ToolTableDialog::toolSelected,
                this, [callback](const ToolData &tool) {
                    callback(tool);
                });
    }

    dlg->exec();
}

void MainWindow::onGCodeReady(const QVector<BlockData>  &blocks,
                              const QVector<GCodeLine>  &gcode,
                              const QString             &name)
{
    // Transmet à ExecutionPage — qui gère lui-même la logique
    // "machine à l'arrêt → charger, machine running → mettre en attente"
    m_executionPage->offerProgram(blocks, gcode, name);

    // ── Mise à jour simulateur 3D ─────────────────────────────
    if (m_simPage) {
        // Le dossier où chercher les sous-fichiers .cnc = dossier du .enc
        QString encPath = m_programmePage ? m_programmePage->currentEncPath() : QString();
        QString ncDir = encPath.isEmpty() ? QString() : QFileInfo(encPath).absolutePath();
        m_simPage->load(blocks, gcode, ncDir);
    }
}

void MainWindow::showProgPage(int subPage)
{
    // subPage 0→index 1, 1→index 2, 2→index 3
    m_progSkPage = subPage;
    ui->softkeyStack->setCurrentIndex(1 + subPage);
}

// ─────────────────────────────────────────────────────────────
//  ENTRÉE / SORTIE DU MODE SÉLECTION
// ─────────────────────────────────────────────────────────────
void MainWindow::enterSelectionMode()
{
    m_programmePage->beginSelection();
    showProgPage(2); // affiche les softkeys de sélection
}

void MainWindow::exitSelectionMode()
{
    m_programmePage->cancelSelection();
    showProgPage(m_selReturnPage); // retour a la barre d'ou on venait
}

// ─────────────────────────────────────────────────────────────
//  SWITCH PAGE
// ─────────────────────────────────────────────────────────────
void MainWindow::switchPage(int index)
{
    // stackedPages : 0=Manuel 1=Programme 2=Exécution 3=MDI 4=Simulation
    // softkeyStack : 0=Manuel 1=skProg 2=skProgEdit 3=skProgSel 4=Exec 5=MDI 6=Sim

    // Sérialise le bloc courant en mémoire en quittant la page Programme
    if (m_currentPage == 1 && index != 1 && m_programmePage)
        m_programmePage->saveCurrentBlock();  // mémoire seulement, pas de disque

    m_currentPage = index;
    ui->stackedPages->setCurrentIndex(index);

    if (index == 0) {
        ui->softkeyStack->setCurrentIndex(0);
    } else if (index == 1) {
        // Programme → sous-page courante (0=insertion 1=edit 2=sélection)
        ui->softkeyStack->setCurrentIndex(1 + m_progSkPage);
    } else if (index == 2) {
        // Exécution → skPage_execution (index 4)
        ui->softkeyStack->setCurrentIndex(4);
    } else {
        // MDI(3)→5  Sim(4)→6  Fichiers(5)→7
        ui->softkeyStack->setCurrentIndex(index + 2);
        // Pour la page fichiers, les softkeys sont dans FileBrowserPage elle-meme
    }

    // Quand on quitte la page Programme, revenir à la sous-page insertion
    // pour qu'au retour on ne soit pas bloqué sur la page édition
    if (m_currentPage != 1)
        m_progSkPage = 0;

    updateTabStyles(index);

    const QStringList modes = {"MODE MANUEL","PROGRAMME","EXECUTION","MDI","SIMULATION","FICHIERS"};
    // lblMode supprime du UI


}

void MainWindow::updateTabStyles(int active)
{
    // Onglet actif - languette fusionnee avec la page
    const QString styleOn =
        "QToolButton{"
        "background-color:#D6D6D6;"
        "color:#000000;"
        "border-left:1px solid #606060;"
        "border-top:1px solid #606060;"
        "border-right:1px solid #606060;"
        "border-bottom:none;"
        "border-radius:6px 6px 0px 0px;"
        "font-size:10pt;"
        "font-weight:bold;"
        ""
        "padding:8px 20px;"
        "}";

    // Onglet inactif - plus sombre, texte noir visible
    const QString styleOff =
        "QToolButton{"
        "background-color:#AAAAAA;"
        "color:#000000;"
        "border-left:1px solid #808080;"
        "border-top:1px solid #808080;"
        "border-right:1px solid #808080;"
        "border-bottom:1px solid #606060;"
        "border-radius:6px 6px 0px 0px;"
        "font-size:10pt;"
        "font-weight:bold;"
        ""
        "padding:6px 20px;"
        "margin-top:4px;"
        "}"
        "QToolButton:hover{background-color:#BEBEBE;color:#000000;}";

    QList<QToolButton*> tabs = {
        ui->tabModeManuel, ui->tabProgrammation, ui->tabExecution,
        ui->tabMDI, ui->tabSimulation, ui->tabFichiers
    };
    for (int i = 0; i < tabs.size(); ++i)
        tabs[i]->setStyleSheet(i == active ? styleOn : styleOff);
}

// ─────────────────────────────────────────────────────────────
//  BOUTONS BI-ÉTAT
// ─────────────────────────────────────────────────────────────
void MainWindow::refreshIncCont()
{
    ui->btnJogIncCont->setText(modeInc ? "INC" : "CONT");
    ui->btnJogIncCont->setStyleSheet(modeInc ? S_INC : S_CONT);
}
void MainWindow::refreshLentRapide()
{
    ui->btnJogRapid->setText(modeRapid ? "RAPIDE" : "LENT");
    ui->btnJogRapid->setStyleSheet(modeRapid ? S_RAPID : S_LENT);
}
void MainWindow::refreshManAuto()
{
    ui->btnSpindleAutoMan->setText(modeMan ? "MAN" : "AUTO");
    ui->btnSpindleAutoMan->setStyleSheet(modeMan ? S_MAN : S_AUTO);
}
void MainWindow::refreshJogSteps()
{
    ui->btnJogX1  ->setStyleSheet(jogStep==1   ? S_ACTIVE : S_NORMAL);
    ui->btnJogX10 ->setStyleSheet(jogStep==10  ? S_ACTIVE : S_NORMAL);
    ui->btnJogX100->setStyleSheet(jogStep==100 ? S_ACTIVE : S_NORMAL);
}

// ─────────────────────────────────────────────────────────────
//  DRO — rafraichissement 100ms
// ─────────────────────────────────────────────────────────────
void MainWindow::refreshWorkDro()
{
    double x = bridge->getWorkDro(0);
    double y = bridge->getWorkDro(1);
    double z = bridge->getWorkDro(2);
    // Manuel
    ui->lblWorkX->setText(QString::asprintf("%+.4f", x));
    ui->lblWorkY->setText(QString::asprintf("%+.4f", y));
    ui->lblWorkZ->setText(QString::asprintf("%+.4f", z));
    // MDI — meme valeurs
    ui->lblMdiWorkX->setText(QString::asprintf("%+.4f", x));
    ui->lblMdiWorkYDro->setText(QString::asprintf("%+.4f", y));
    ui->lblMdiWorkZDro->setText(QString::asprintf("%+.4f", z));
}
void MainWindow::refreshMachineDro()
{
    ui->lblMachineX->setText(QString::asprintf("%+.4f", bridge->getMachineDro(0)));
    ui->lblMachineY->setText(QString::asprintf("%+.4f", bridge->getMachineDro(1)));
    ui->lblMachineZ->setText(QString::asprintf("%+.4f", bridge->getMachineDro(2)));
}
void MainWindow::refreshDTG()
{
    ui->lblDTGX->setText(QString::asprintf("%+.4f", bridge->getDroDTG(0)));
    ui->lblDTGY->setText(QString::asprintf("%+.4f", bridge->getDroDTG(1)));
    ui->lblDTGZ->setText(QString::asprintf("%+.4f", bridge->getDroDTG(2)));
}

// ─────────────────────────────────────────────────────────────
//  STATE
// ─────────────────────────────────────────────────────────────
void MainWindow::refreshTool()
{
    QString t = QString("T%1 H%2").arg(state.getToolNumber())
    .arg(state.getCurrentHeightOffsetNumber());
    ui->lblTool->setText(t);
    ui->lblTopTool->setText(QString("T%1").arg(state.getToolNumber()));
    ui->lblMdiTool2->setText(t);
}
void MainWindow::refreshFeedrate()
{
    int f = qRound(state.getFeedrate());
    QString fs = QString("%1 mm/min").arg(f);
    ui->lblFeedrate->setText(fs);
    ui->lblTopFeed->setText(QString("F: %1 mm/m").arg(f));
    ui->lblMdiFeedrate2->setText(fs);
}
void MainWindow::refreshFeedOverride()
{
    int v = state.getFeedOverride();
    ui->dialFeedOverride->blockSignals(true);
    ui->dialFeedOverride->setValue(v);
    ui->dialFeedOverride->blockSignals(false);
    QString c = (v<=50)?"#d06010":(v<=100)?"#3a8f34":"#c02020";
    QString lbl = QString("%1%").arg(v);
    ui->lblDialFeed->setText(lbl);
    ui->lblDialFeed->setStyleSheet(QString("QLabel{color:%1;font-size:10pt;font-weight:900;background:transparent;}").arg(c));
    ui->lblMdiDialFeed->setText(lbl);
    if (ui->gaugeFeedOverride) ui->gaugeFeedOverride->setValue(v);
    if (ui->gaugeMdiFeedOverride) ui->gaugeMdiFeedOverride->setValue(v);
}
void MainWindow::refreshSpindle()
{
    int rpm = qRound(state.getSpindleSpeed());
    QString ss = QString("%1 tr/min").arg(rpm);
    ui->lblSpindle->setText(ss);
    ui->lblTopSpindle->setText(QString("S: %1 rpm").arg(rpm));
    ui->lblMdiSpindle2->setText(ss);
    int ovr = state.getFeedOverride();
    ui->dialSpindleOverride->blockSignals(true);
    ui->dialSpindleOverride->setValue(qBound(0, ovr, 200));
    ui->dialSpindleOverride->blockSignals(false);
    QString c = (ovr<=50)?"#d06010":(ovr<=100)?"#3a8f34":"#c02020";
    QString lo = QString("%1%").arg(ovr);
    ui->lblDialSpindle->setText(lo);
    ui->lblDialSpindle->setStyleSheet(QString("QLabel{color:%1;font-size:10pt;font-weight:900;background:transparent;}").arg(c));
    ui->lblMdiDialSpindle->setText(lo);
    if (!ui->gaugeSpindleSpeed) return;
    double maxRPM = state.getHighRangeSpindleSpeedMax();
    if (maxRPM <= 0) maxRPM = 10000.0;
    int pct = (rpm>0) ? qBound(2,(int)(rpm*100.0/maxRPM),200) : 2;
    ui->gaugeSpindleSpeed->setValue(pct);
    QString bc = (rpm==0)?"#EBEBEB":(pct<=100)?"#A6CE39":(pct<=150)?"#FFFF77":"#FF4444";
    ui->gaugeSpindleSpeed->setStyleSheet(
        QString("QProgressBar{background:#FFFFFF;border:1px solid #808080;border-radius:3px;}"
                "QProgressBar::chunk{background:%1;border-radius:3px;}").arg(bc));
}
void MainWindow::refreshMessages()
{
    static QString last;
    QString msg = state.getLastMessage();
    if (!msg.isEmpty() && msg != last) {
        last = msg;
        ui->txtMessages->append(QString("<span style='color:#000080;'>%1</span>").arg(msg));
    }
}

// ─────────────────────────────────────────────────────────────
//  SOFTKEYS CONTEXTUELS
// ─────────────────────────────────────────────────────────────
void MainWindow::onUnitEditorSoftkeyContext(
    UnitEditor::SoftkeyContext ctx,
    QStringList labels,
    int activeIndex,
    QString activeValue)
{
    // Vider la page contextuelle — destruction IMMÉDIATE (pas deleteLater)
    // pour éviter qu'un ancien bouton encore vivant (détruit seulement au
    // prochain passage de la boucle d'événements) intercepte un clic
    // destiné au nouveau bouton reconstruit à la même position → décalage
    // de clic ressenti ("je clique et ça active le bouton de droite").
    while (m_ctxSoftkeyLayout->count() > 0) {
        QLayoutItem *item = m_ctxSoftkeyLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->setParent(nullptr);
            delete item->widget();
        }
        delete item;
    }

    if (ctx == UnitEditor::SK_NONE || labels.isEmpty()) {
        // Retour à la page normale programme
        if (m_currentPage == 1)
            ui->softkeyStack->setCurrentIndex(1 + m_progSkPage);
        return;
    }

    // Icônes pour les modes modèle de points (SK_POINTS, index 4-8)
    static const QStringList ptIcons = {
        "", "", "", "",                              // Nouveau Fichier Éditer DXF — pas d'icône
        ":/unit/point.png",
        ":/unit/ligne.png",
        ":/unit/grille.png",
        ":/unit/cercle.png",
        ":/unit/smartnc_unit_pattern.png"           // Tableau
    };

    // Remplir la page contextuelle avec les boutons
    for (int i = 0; i < labels.size(); ++i) {
        auto *btn = new QToolButton(m_ctxSoftkeyPage);
        btn->setAutoRaise(true);
        btn->setFixedSize(116, 68);
        btn->setFixedSize(116, 68);

        bool isActive = (i == activeIndex);

        // Essayer de mettre une icône si disponible
        QString iconPath = (ctx == UnitEditor::SK_POINTS && i < ptIcons.size())
                               ? ptIcons[i] : "";
        QPixmap px(iconPath);
        if (!px.isNull()) {
            btn->setIcon(QIcon(px));
            btn->setIconSize(QSize(112, 64));
            btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
            btn->setToolTip(labels[i]);
        } else {
            btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
            QString label = labels[i];
            if (isActive && !activeValue.isEmpty())
                label += "\n" + activeValue;
            btn->setText(label);
        }

        btn->setStyleSheet(isActive ?
                               "QToolButton {"
                               "  background:#D0EED0; color:#000000;"
                               "  font-size:9pt; font-weight:bold;"
                               "  border:2px solid #00AA00;"
                               "  border-radius:4px; padding:0px; margin:2px;"
                               "}"
                               "QToolButton:hover { background:#BBDDBB; border-color:#007700; }"
                                    :
                               "QToolButton {"
                               "  background:#EBEBEB; color:#000000;"
                               "  font-size:9pt; font-weight:bold;"
                               "  border:1px solid #808080;"
                               "  border-radius:4px; padding:0px; margin:2px;"
                               "}"
                               "QToolButton:hover { background:#DBDBDB; border-color:#404040; }");

        const int idx = i;
        connect(btn, &QToolButton::clicked, this, [this, idx](){
            if (m_programmePage && m_programmePage->unitEditor())
                m_programmePage->unitEditor()->onSoftkeyPressed(idx);
        });

        // Survol → afficher dans le cadre AIDE
        struct SKHover : QObject {
            ProgrammePage *pp; QString label; QString icon;
            SKHover(ProgrammePage *p, const QString &l, const QString &ic, QObject *parent)
                : QObject(parent), pp(p), label(l), icon(ic) {}
            bool eventFilter(QObject*, QEvent *e) override {
                if (e->type() == QEvent::Enter)
                    pp->showHelp(icon.isEmpty()
                        ? "sk_" + label.simplified().replace(" ","_").toLower()
                        : "_png_" + icon);
                if (e->type() == QEvent::Leave)
                    pp->showHelp(QString());
                return false;
            }
        };
        btn->installEventFilter(new SKHover(m_programmePage, labels[i], iconPath, btn));

        m_ctxSoftkeyLayout->addWidget(btn);
    }

    // Afficher la page contextuelle
    ui->softkeyStack->setCurrentIndex(m_ctxSoftkeyPageIndex);
}

// ─────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────
//  showSaveOverlay — overlay "Sauvegarde..." plein écran
// ─────────────────────────────────────────────────────────────
void MainWindow::showSaveOverlay()
{
    if (!m_saveOverlay) {
        m_saveOverlay = new QLabel(this);
        m_saveOverlay->setAlignment(Qt::AlignCenter);
        m_saveOverlay->setText("  Sauvegarde...");
        m_saveOverlay->setStyleSheet(
            "QLabel{"
            "  background:rgba(14,18,22,0.82);"
            "  color:#56d44e;"
            "  font-family:'IBM Plex Mono',monospace;"
            "  font-size:11pt; font-weight:700;"
            "  border:1px solid #3a8f34; border-radius:8px;"
            "  padding:10px 28px;"
            "}");
        m_saveOverlay->setFixedSize(240, 52);
        m_saveOverlay->raise();
    }
    m_saveOverlay->move((width()-240)/2, (height()-52)/2);
    m_saveOverlay->show();
    m_saveOverlay->raise();
    QTimer::singleShot(1200, this, [this](){
        if (m_saveOverlay) m_saveOverlay->hide();
    });
}

void MainWindow::updateDisplay()
{
    refreshWorkDro();
    refreshMachineDro();
    refreshDTG();
    if (!state.isLoaded()) return;
    refreshFeedOverride();
    refreshSpindle();
    refreshFeedrate();
    refreshTool();
    refreshMessages();
    QString job = state.getJobName();
    setWindowTitle(job.isEmpty() ? "EazyNC" : QString("EazyNC - %1").arg(job));

    // ── Mise à jour DRO page Exécution ───────────────────────
    if (m_executionPage) {
        m_executionPage->updateDro(
            bridge->getWorkDro(0),    bridge->getWorkDro(1),    bridge->getWorkDro(2),
            bridge->getMachineDro(0), bridge->getMachineDro(1), bridge->getMachineDro(2),
            bridge->getDroDTG(0),     bridge->getDroDTG(1),     bridge->getDroDTG(2));
        m_executionPage->updateSpindle(
            qRound(state.getSpindleSpeed()), state.getFeedOverride());
        m_executionPage->updateFeed(
            state.getFeedrate(), state.getFeedOverride());
        m_executionPage->updateTool(
            state.getToolNumber(), state.getCurrentHeightOffsetNumber());
        m_executionPage->updateMessages(state.getLastMessage());

        // Surligner le bloc/ligne en cours selon le mode
        int line=0, prog=0, stack=0;
        if (state.getCurrentLineInfo(line, prog, stack)) {
            if (m_executionPage->viewMode() == ExecutionPage::ModeRaw)
                m_executionPage->highlightLine(line);
            else
                m_executionPage->highlightBlock(prog);
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  showCycleNav — afficher une page de navigation cycles
// ─────────────────────────────────────────────────────────────
void MainWindow::showCycleNav(int pageIdx)
{
    if (pageIdx < 0) return;
    m_navHistory.append(ui->softkeyStack->currentIndex());
    ui->softkeyStack->setCurrentIndex(pageIdx);
}

// ─────────────────────────────────────────────────────────────
//  Helper : créer une page softkey dynamique avec boutons icônes
// ─────────────────────────────────────────────────────────────
static QWidget* makeSKPage(QStackedWidget *stack,
                           const QVector<QPair<QString,QString>> &btns,
                           QVector<QToolButton*> &out,
                           ProgrammePage *pp = nullptr)
{
    auto *page = new QWidget;
    auto *lay  = new QHBoxLayout(page);
    lay->setContentsMargins(2,2,2,2);
    lay->setSpacing(3);
    out.clear();
    for (auto &[icon, tip] : btns) {
        auto *btn = new QToolButton;
        btn->setToolTip(tip);
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setFixedSize(116, 68);
        QPixmap px(icon);
        if (!px.isNull()) {
            btn->setIcon(QIcon(px));
            btn->setIconSize(QSize(1000,1000));
        } else {
            btn->setText(tip);
            btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
            btn->setStyleSheet("font-size:7pt;");
        }
        btn->setStyleSheet(btn->styleSheet() +
                           "QToolButton{border:1px solid #909090;border-radius:3px;"
                           "padding:0px;margin:2px;}"
                           "QToolButton:hover{border:2px solid #0000CC;}"
                           "QToolButton:pressed{border:3px solid #0000FF;background:rgba(0,0,200,0.1);}");

        // Survol → cadre AIDE
        if (pp) {
            struct BtnHover : QObject {
                ProgrammePage *pp; QString icon; QString label;
                BtnHover(ProgrammePage *p, const QString &ic, const QString &lbl, QObject *parent)
                    : QObject(parent), pp(p), icon(ic), label(lbl) {}
                bool eventFilter(QObject*, QEvent *e) override {
                    if (e->type() == QEvent::Enter)
                        pp->showHelp(icon.isEmpty()
                            ? "sk_" + label.simplified().replace(" ","_").toLower()
                            : "_png_" + icon);
                    if (e->type() == QEvent::Leave)
                        pp->showHelp(QString());
                    return false;
                }
            };
            btn->installEventFilter(new BtnHover(pp, icon, tip, btn));
        }

        lay->addWidget(btn);
        out.append(btn);
    }
    stack->addWidget(page);
    return page;
}

// ─────────────────────────────────────────────────────────────
//  setupProgSoftkeys — 4 familles + sous-pages
// ─────────────────────────────────────────────────────────────
void MainWindow::setupProgSoftkeys()
{
    auto *stack = ui->softkeyStack;
    QVector<QToolButton*> btns;

    // ── Helper retour ─────────────────────────────────────────
    auto addBack = [&](QWidget *pg, bool toFam = false) {
        auto *b = new QToolButton; b->setText("<");
        qobject_cast<QHBoxLayout*>(pg->layout())->insertWidget(0, b);
        if (toFam)
            connect(b, &QToolButton::clicked, this, [this]{ showCycleNav(m_skIdx_familles); });
        else
            connect(b, &QToolButton::clicked, this, [this]{ m_navHistory.clear(); showProgPage(0); });
    };

    // ── Page FAMILLES ─────────────────────────────────────────
    auto *pgFam = makeSKPage(stack, {
                                     {":/unit/tout les cycles poches tenons.png", "Poche / Tenon"},
                                     {":/unit/tous les cycle percage.png",         "Perçage"},
                                     {":/unit/tous les cycle filet.png",           "Filetage"},
                                     {":/unit/tous les cycle de contour.png",      "Contour"},
                                     }, btns, m_programmePage);
    m_skIdx_familles = stack->indexOf(pgFam);
    addBack(pgFam, false);


    connect(btns[0], &QToolButton::clicked, this, [this]{ showCycleNav(m_skIdx_poches); });
    connect(btns[1], &QToolButton::clicked, this, [this]{
        // Perçage — ouvre la sous-page de la famille perçage
        showCycleNav(m_skIdx_percage);
    });
    connect(btns[2], &QToolButton::clicked, this, [this]{
        // Filetage — non opérationnel, accès futur
        showCycleNav(m_skIdx_percage); // même page pour l'instant
    });
    connect(btns[3], &QToolButton::clicked, this, [this]{
        // Contour — ouvre la famille des usinages de contour
        showCycleNav(m_skIdx_contour);
    });

    // ── Page POCHE / TENON ────────────────────────────────────
    auto *pgPoch = makeSKPage(stack, {
                                      {":/unit/fraisage de poche rectangulaire.png", "Poche rect."},
                                      {":/unit/fraisage de poche circulaire.png",    "Poche circ."},
                                      {":/unit/fraisage de trou.png",                "Fraisage trou"},
                                      }, btns, m_programmePage);
    m_skIdx_poches = stack->indexOf(pgPoch);
    addBack(pgPoch, true);

    connect(btns[0], &QToolButton::clicked, this, [this]{
        m_navHistory.clear(); showProgPage(0);
        m_programmePage->insertPocheRect();
    });
    connect(btns[1], &QToolButton::clicked, this, [this]{
        m_navHistory.clear(); showProgPage(0);
        m_programmePage->insertPocheCirc();
    });
    connect(btns[2], &QToolButton::clicked, this, [this]{
        m_navHistory.clear(); showProgPage(0);
        m_programmePage->insertFraisageTrou();
    });

    // ── Page CONTOUR (famille usinages de contour) ────────────
    //    Contournage pour l'instant ; la poche de forme viendra ici.
    auto *pgCont = makeSKPage(stack, {
                                      {":/unit/cycle de contournage.png", "Contournage"},
                                      {":/unit/fraisage de poche contour.png", "Poche contour"},
                                      {":/unit/fraisage de poche contour.png", "Usinage 3D"},
                                      }, btns, m_programmePage);
    m_skIdx_contour = stack->indexOf(pgCont);
    addBack(pgCont, true);
    connect(btns[0], &QToolButton::clicked, this, [this]{
        m_navHistory.clear(); showProgPage(0);
        m_programmePage->insertContour();
    });
    connect(btns[1], &QToolButton::clicked, this, [this]{
        m_navHistory.clear(); showProgPage(0);
        m_programmePage->insertPocheContour();
    });
    connect(btns[2], &QToolButton::clicked, this, [this]{
        m_navHistory.clear(); showProgPage(0);
        m_programmePage->insertUsinage3D();
    });

    // ── Page PERÇAGE (famille perçage) ────────────────────────
    auto *pgPerc = makeSKPage(stack, {
                                      {":/unit/percage.png", "Perçage"},
                                      }, btns, m_programmePage);
    m_skIdx_percage = stack->indexOf(pgPerc);
    addBack(pgPerc, true);
    connect(btns[0], &QToolButton::clicked, this, [this]{
        m_navHistory.clear(); showProgPage(0);
        m_programmePage->insertPercage();
    });
}