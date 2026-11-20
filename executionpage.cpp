#pragma execution_character_set("utf-8")
#include "executionpage.h"
#include "sim3dwidget.h"
#include "simulationpage.h"
#include "Ncfileparser.h"

#include <QBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QListView>
#include <QStringListModel>
#include <QStackedWidget>
#include <QProgressBar>
#include <QRegularExpression>
#include <QFileInfo>
#include <QMessageBox>
#include <QShowEvent>
#include <QTimer>
#include <QDateTime>
#include <cmath>
#include <QDebug>

// ── Couleurs du site ────────────────────────────────────────────
static const char *VERT  = "#A6CE39";
static const char *VERT2 = "#00B400";

static QString styleCarte()
{
    return QString(
        "QFrame[carte=\"true\"]{background:#F7F8F7;border:1px solid %1;border-radius:6px;}"
        "QLabel{background:transparent;border:none;color:#1E241E;}"
        "QLabel[titre=\"true\"]{font-size:9pt;font-weight:bold;letter-spacing:2px;color:#6B746B;}"
        "QLabel[sous=\"true\"]{font-size:9pt;color:#6B746B;}"
        "QProgressBar{background:#DDE1DD;border:none;border-radius:3px;max-height:6px;min-height:6px;}"
        "QProgressBar::chunk{background:%1;border-radius:3px;}").arg(VERT);
}

static QFrame *carte(QWidget *parent = nullptr)
{
    auto *f = new QFrame(parent);
    f->setProperty("carte", true);
    return f;
}

static QLabel *titre(const QString &t)
{
    auto *l = new QLabel(t);
    l->setProperty("titre", true);
    return l;
}

static QString fmt(double v) { return QString::asprintf("%+.3f", v); }

static QString duree(qint64 s)
{
    if (s < 0) s = 0;
    return QString::asprintf("%d:%02d:%02d", int(s / 3600), int(s / 60 % 60), int(s % 60));
}

// ═══════════════════════════════════════════════════════════════
ExecutionPage::ExecutionPage(QWidget *parent) : QWidget(parent)
{
    construire();
}

void ExecutionPage::construire()
{
    setObjectName("execPage");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("#execPage{background:#E4E6E4;}" + styleCarte());

    auto *racine = new QHBoxLayout(this);
    racine->setContentsMargins(8, 8, 8, 8);
    racine->setSpacing(8);

    // ════ Colonne gauche : position, outil, pupitre ════════════════
    m_colG = new QWidget;
    m_colG->setFixedWidth(300);
    auto *g = new QVBoxLayout(m_colG);
    g->setContentsMargins(0, 0, 0, 0);
    g->setSpacing(8);

    {   // Position
        auto *c = carte();
        auto *v = new QVBoxLayout(c);
        v->setContentsMargins(12, 10, 12, 10);
        v->setSpacing(0);
        m_titrePos = titre(QString::fromUtf8("POSITION · PIÈCE"));
        v->addWidget(m_titrePos);
        const char *noms[3] = {"X", "Y", "Z"};
        const char *coul[3] = {"#C82020", "#1D8A2A", "#2050C8"};
        for (int a = 0; a < 3; ++a) {
            auto *h = new QHBoxLayout;
            auto *n = new QLabel(noms[a]);
            n->setStyleSheet(QString("font-family:Consolas,monospace;font-size:24pt;font-weight:bold;color:%1;").arg(coul[a]));
            m_ax[a] = new QLabel("+0.000");
            m_ax[a]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_ax[a]->setStyleSheet("font-family:Consolas,monospace;font-size:26pt;font-weight:bold;");
            h->addWidget(n);
            h->addWidget(m_ax[a], 1);
            v->addLayout(h);
        }
        auto *seg = new QHBoxLayout;
        seg->setSpacing(4);
        const char *modes[3] = {"Machine", "Pièce", "Reste"};
        for (int i = 0; i < 3; ++i) {
            m_seg[i] = new QPushButton(QString::fromUtf8(modes[i]));
            m_seg[i]->setCheckable(true);
            m_seg[i]->setFocusPolicy(Qt::NoFocus);
            m_seg[i]->setStyleSheet(QString(
                "QPushButton{background:#fff;border:1px solid #C9CEC9;border-radius:4px;padding:5px 0;color:#6B746B;font-size:9pt;}"
                "QPushButton:checked{border:1px solid %1;border-bottom:3px solid %1;color:#1E241E;font-weight:bold;}").arg(VERT));
            connect(m_seg[i], &QPushButton::clicked, this, [this, i]{ m_modePos = i; majPositions(); });
            seg->addWidget(m_seg[i]);
        }
        v->addSpacing(8);
        v->addLayout(seg);
        g->addWidget(c);
    }
    {   // Outil + avance / broche
        auto *c = carte();
        auto *v = new QVBoxLayout(c);
        v->setContentsMargins(12, 10, 12, 10);
        v->setSpacing(4);
        v->addWidget(titre("OUTIL"));
        m_lblOutil = new QLabel("T-");
        m_lblOutil->setStyleSheet("font-size:12pt;font-weight:bold;");
        v->addWidget(m_lblOutil);
        auto *hf = new QHBoxLayout;
        m_lblF = new QLabel("F 0 mm/min");
        m_lblFo = new QLabel("100 %");
        hf->addWidget(m_lblF, 1);
        hf->addWidget(m_lblFo);
        v->addLayout(hf);
        m_barF = new QProgressBar;
        m_barF->setRange(0, 150);
        m_barF->setTextVisible(false);
        v->addWidget(m_barF);
        m_lblS = new QLabel("S 0 tr/min");
        v->addWidget(m_lblS);
        g->addWidget(c);
    }
    {   // Pupitre
        auto *c = carte();
        auto *v = new QVBoxLayout(c);
        v->setContentsMargins(12, 10, 12, 10);
        v->setSpacing(6);
        v->addWidget(titre("PUPITRE"));
        auto *gr = new QHBoxLayout;
        gr->setSpacing(6);
        auto bouton = [](const QString &ic, const QString &txt, const char *coul, const char *bord) {
            auto *b = new QPushButton(ic + "\n" + txt);
            b->setFocusPolicy(Qt::NoFocus);
            b->setMinimumHeight(62);
            b->setStyleSheet(QString(
                "QPushButton{background:#fff;border:2px solid %2;border-radius:6px;color:%1;font-weight:bold;font-size:10pt;}"
                "QPushButton:pressed{background:#EEF6DC;}").arg(coul, bord));
            return b;
        };
        auto *depart = bouton(QString::fromUtf8("▶"), QString::fromUtf8("Départ"), "#007A00", VERT);
        auto *arret  = bouton(QString::fromUtf8("❚❚"), QString::fromUtf8("Arrêt av."), "#B07000", "#E0B040");
        auto *stop   = bouton(QString::fromUtf8("■"), "Stop", "#C02020", "#E06060");
        depart->setToolTip(QString::fromUtf8("Départ cycle : lance le programme chargé dans CNC12"));
        arret->setToolTip(QString::fromUtf8("Arrêt d'avance (feed hold)"));
        stop->setToolTip(QString::fromUtf8("Arrêt du cycle"));
        connect(depart, &QPushButton::clicked, this, &ExecutionPage::cycleStartRequested);
        connect(arret,  &QPushButton::clicked, this, &ExecutionPage::feedHoldRequested);
        connect(stop,   &QPushButton::clicked, this, &ExecutionPage::cycleStopRequested);
        gr->addWidget(depart);
        gr->addWidget(arret);
        gr->addWidget(stop);
        v->addLayout(gr);
        auto *raz = new QPushButton("Reset");
        raz->setFocusPolicy(Qt::NoFocus);
        raz->setToolTip(QString::fromUtf8("Reset CNC12 (efface l'alarme)"));
        raz->setStyleSheet(QString("QPushButton{background:#fff;border:1px solid %1;border-radius:5px;padding:6px;font-size:9pt;}"
                                   "QPushButton:pressed{background:#EEF6DC;}").arg(VERT));
        connect(raz, &QPushButton::clicked, this, &ExecutionPage::resetRequested);
        v->addWidget(raz);
        g->addWidget(c);
    }
    g->addStretch(1);
    racine->addWidget(m_colG);

    // ════ Colonne centrale : programme ═════════════════════════════
    m_colC = new QWidget;
    m_colC->setFixedWidth(430);
    auto *cc = new QVBoxLayout(m_colC);
    cc->setContentsMargins(0, 0, 0, 0);
    cc->setSpacing(8);
    {
        auto *h = new QHBoxLayout;
        h->setSpacing(6);
        const QString st = QString(
            "QPushButton{background:#fff;border:1px solid %1;border-radius:6px;padding:7px 0;font-weight:bold;font-size:10pt;color:#1E241E;}"
            "QPushButton:checked{background:%1;color:#000;}").arg(VERT);
        m_bBlocs = new QPushButton(QString::fromUtf8("▦  BLOCS"));
        m_bGcode = new QPushButton(QString::fromUtf8("≡  G-CODE"));
        for (QPushButton *b : {m_bBlocs, m_bGcode}) {
            b->setCheckable(true);
            b->setFocusPolicy(Qt::NoFocus);
            b->setStyleSheet(st);
            h->addWidget(b);
        }
        connect(m_bBlocs, &QPushButton::clicked, this, [this]{ choisirVue(0); });
        connect(m_bGcode, &QPushButton::clicked, this, [this]{ choisirVue(1); });
        cc->addLayout(h);
    }
    m_lblProg = new QLabel(QString::fromUtf8("Aucun programme — touche « Charger programme »"));
    m_lblProg->setStyleSheet("color:#6B746B;font-size:9pt;");
    cc->addWidget(m_lblProg);
    m_lblMsg = new QLabel;
    m_lblMsg->setWordWrap(true);
    m_lblMsg->setStyleSheet("color:#B07000;font-size:9pt;font-weight:bold;");
    m_lblMsg->hide();
    cc->addWidget(m_lblMsg);

    m_pile = new QStackedWidget;
    m_listeBlocs = new QListWidget;
    m_listeBlocs->setFrameShape(QFrame::NoFrame);
    m_listeBlocs->setSelectionMode(QAbstractItemView::NoSelection);
    m_listeBlocs->setFocusPolicy(Qt::NoFocus);
    m_listeBlocs->setSpacing(3);
    m_listeBlocs->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listeBlocs->setStyleSheet("QListWidget{background:transparent;}");
    m_pile->addWidget(m_listeBlocs);

    m_listeGcode = new QListView;
    m_modeleGcode = new QStringListModel(this);
    m_listeGcode->setModel(m_modeleGcode);
    m_listeGcode->setUniformItemSizes(true);
    m_listeGcode->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listeGcode->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listeGcode->setStyleSheet(QString(
        "QListView{background:#F7F8F7;border:1px solid %1;border-radius:6px;font-family:Consolas,monospace;font-size:10pt;color:#1E241E;}"
        "QListView::item{padding:1px 6px;}"
        "QListView::item:selected{background:#EEF6DC;color:#000;border-left:4px solid %2;}").arg(VERT, VERT2));
    m_pile->addWidget(m_listeGcode);
    cc->addWidget(m_pile, 1);

    {   // Avancement
        auto *c = carte();
        auto *v = new QVBoxLayout(c);
        v->setContentsMargins(12, 8, 12, 8);
        v->setSpacing(4);
        auto *h = new QHBoxLayout;
        h->addWidget(titre("PROGRAMME"), 1);
        m_lblPct = new QLabel("0 %");
        m_lblPct->setStyleSheet("font-weight:bold;");
        h->addWidget(m_lblPct);
        v->addLayout(h);
        m_barProg = new QProgressBar;
        m_barProg->setRange(0, 1000);
        m_barProg->setTextVisible(false);
        v->addWidget(m_barProg);
        m_lblTemps = new QLabel(QString::fromUtf8("0:00:00 écoulé"));
        m_lblTemps->setProperty("sous", true);
        v->addWidget(m_lblTemps);
        cc->addWidget(c);
    }
    racine->addWidget(m_colC);

    // ════ Droite : vue 3D en direct ════════════════════════════════
    auto *v3 = new QFrame;
    v3->setObjectName("vue3d");
    v3->setStyleSheet(QString("#vue3d{background:#121512;border:1px solid %1;border-radius:6px;}").arg(VERT));
    auto *vv = new QVBoxLayout(v3);
    vv->setContentsMargins(1, 1, 1, 1);
    vv->setSpacing(0);
    {
        auto *bar = new QWidget;
        bar->setFixedHeight(36);
        bar->setStyleSheet("background:transparent;");
        auto *h = new QHBoxLayout(bar);
        h->setContentsMargins(10, 0, 10, 0);
        h->setSpacing(8);
        auto *t = new QLabel(QString::fromUtf8("VUE 3D — EN DIRECT"));
        t->setStyleSheet("color:#E6EBE6;font-size:9pt;font-weight:bold;letter-spacing:2px;background:transparent;");
        h->addWidget(t, 1);
        const QString st = QString(
            "QPushButton{color:#9AA59A;border:1px solid #3A453A;border-radius:11px;padding:2px 10px;font-size:9pt;background:transparent;}"
            "QPushButton:checked{color:%1;border-color:%1;}").arg(VERT);
        auto puce = [&](const QString &txt) {
            auto *b = new QPushButton(txt);
            b->setCheckable(true);
            b->setChecked(true);
            b->setFocusPolicy(Qt::NoFocus);
            b->setStyleSheet(st);
            h->addWidget(b);
            return b;
        };
        m_chMat   = puce(QString::fromUtf8("Matière"));
        m_chOutil = puce("Outil");
        m_chParc  = puce(QString::fromUtf8("Parcours"));
        m_chMat->setToolTip(QString::fromUtf8("Matière pleine / transparente"));
        connect(m_chMat,   &QPushButton::toggled, this, [this](bool on){ if (m_sim) m_sim->setStockOpaque(on); });
        connect(m_chOutil, &QPushButton::toggled, this, [this](bool on){ if (m_sim) m_sim->setShowTool(on); });
        connect(m_chParc,  &QPushButton::toggled, this, [this](bool on){ if (m_sim) m_sim->setShowToolPath(on); });
        vv->addWidget(bar);
        auto *sep = new QFrame;
        sep->setFixedHeight(1);
        sep->setStyleSheet("background:#263026;border:none;");
        vv->addWidget(sep);
    }
    m_sim = new Sim3DWidget;
    m_sim->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Les commandes de lecture appartiennent à la page Simulation : ici la vue suit la machine.
    if (QWidget *tp = m_sim->findChild<QWidget *>("tpBar")) tp->hide();
    vv->addWidget(m_sim, 1);
    m_lblPrep = new QLabel(QString::fromUtf8("Préparation de la vue 3D…"), m_sim);
    m_lblPrep->setStyleSheet(QString("color:%1;background:rgba(18,21,18,200);border:1px solid %1;border-radius:6px;padding:6px 12px;font-weight:bold;").arg(VERT));
    m_lblPrep->move(16, 16);
    m_lblPrep->adjustSize();
    m_lblPrep->hide();
    connect(m_sim, &Sim3DWidget::parcoursPret, this, [this]{
        m_lblPrep->hide();
        if (m_ligne > 1) m_sim->seekToLine(m_ligne - 1);
        m_dernier3D.start();
        qDebug() << "[EXEC] vue 3D prete";
    });
    racine->addWidget(v3, 1);

    m_seg[m_modePos]->setChecked(true);
    choisirVue(0);
}

// ── Vue blocs / G-code ─────────────────────────────────────────
void ExecutionPage::choisirVue(int v)
{
    m_pile->setCurrentIndex(v);
    m_bBlocs->setChecked(v == 0);
    m_bGcode->setChecked(v == 1);
    if (v == 1 && m_ligne > 0) allerALigne(m_ligne);
}

void ExecutionPage::basculerBlocsGcode() { choisirVue(m_pile->currentIndex() == 0 ? 1 : 0); }

void ExecutionPage::basculerAgrandir3D()
{
    const bool grand = m_colG->isVisible();
    m_colG->setVisible(!grand);
    m_colC->setVisible(!grand);
}

void ExecutionPage::montrerMessages()
{
    QMessageBox::information(this, "Messages CNC12",
        m_messages.isEmpty() ? QString::fromUtf8("Aucun message de CNC12.")
                             : m_messages.join("\n"));
}

void ExecutionPage::chercherLigne(int ligne)
{
    m_pile->setCurrentIndex(1);
    m_bBlocs->setChecked(false);
    m_bGcode->setChecked(true);
    allerALigne(ligne);
}

void ExecutionPage::allerALigne(int ligne)
{
    if (!m_modeleGcode || m_modeleGcode->rowCount() == 0) return;
    const QModelIndex i = m_modeleGcode->index(qBound(0, ligne - 1, m_modeleGcode->rowCount() - 1));
    m_listeGcode->setCurrentIndex(i);
    m_listeGcode->scrollTo(i, QAbstractItemView::PositionAtCenter);
}

// ── Chargement ─────────────────────────────────────────────────
void ExecutionPage::offerProgram(const QVector<BlockData> &blocks, const QVector<GCodeLine> &gcode, const QString &name)
{
    m_blocks = blocks;
    m_nomProg = QFileInfo(name).completeBaseName();
    if (m_nomProg.isEmpty()) m_nomProg = "Programme";
    m_infoCnc.clear();
    chargerLignes(gcode);
}

void ExecutionPage::loadRawFile(const QString &path)
{
    const QVector<GCodeLine> l = NcFileParser::parse(path);
    if (l.isEmpty()) return;
    m_ncPath = path;
    m_blocks.clear();
    m_nomProg = QFileInfo(path).completeBaseName();
    m_infoCnc.clear();
    chargerLignes(l);
}

void ExecutionPage::setInfoCnc12(const QString &txt)
{
    m_infoCnc = txt;
    m_lblProg->setText(m_infoCnc.isEmpty() ? m_nomProg : m_nomProg + QString::fromUtf8("  ·  ") + m_infoCnc);
}

// Découpe le G-code en blocs à partir des repères écrits par EazyNC :
//   ( == BLOC n : TYPE : nom == )        un bloc du programme
//   ( --- Operation i/N : cycle --- )    une opération d'un bloc 3D
//   ( --- Material Size --- )            le BLK FORM
// Ancien .nc sans repère : un bloc par appel d'outil.
void ExecutionPage::chargerLignes(const QVector<GCodeLine> &lignes)
{
    QElapsedTimer chrono; chrono.start();
    m_lignes = lignes;
    m_sections.clear();
    m_ligne = 0;
    m_enCours = false;
    m_tempsAvant = 0;
    m_simCharge = false;

    static const QRegularExpression reBloc(R"(^==\s*BLOC\s+\d+\s*:\s*([^:]*?)\s*:\s*(.*?)\s*==$)");
    static const QRegularExpression reOp(R"(^---\s*Operation\s+(\d+)/(\d+)\s*:\s*(.*?)\s*---$)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reOutil(R"(Appel outil\s+T(\d+))", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reT(R"(\bT(\d+)\b)");

    bool reperes = false;
    for (const GCodeLine &l : lignes)
        if (l.type == GCodeLine::Comment && reBloc.match(l.comment.trimmed()).hasMatch()) { reperes = true; break; }

    QVector<int> niveaux;                     // 0 = bloc, 1 = opération
    // Icône : celle du bloc dans la page Programme, celle du cycle pour une opération 3D
    auto icone = [](const QString &t) -> QIcon {
        const QString u = t.toUpper();
        if (u.contains("BLK"))                  return blockIcon(BlockType::BlkForm);
        if (u.contains("USINAGE 3D"))           return blockIcon(BlockType::Usinage3D);
        if (u.contains("EBAUCHE") || u.contains(QString::fromUtf8("ÉBAUCHE"))) return QIcon(":/aide3d/ebauche.png");
        if (u.contains("Z CONSTANT") && u.contains("BALAYAGE")) return QIcon(":/aide3d/zbal.png");
        if (u.contains("Z CONSTANT"))           return QIcon(":/aide3d/zconst.png");
        if (u.contains("BALAYAGE"))             return QIcon(":/aide3d/balayage.png");
        if (u.contains("MIXTE"))                return QIcon(":/aide3d/mixte.png");
        if (u.contains("POCHE CONTOUR"))        return blockIcon(BlockType::PocheContour);
        if (u.contains("POCHE CIRC"))           return blockIcon(BlockType::PocheCirc);
        if (u.contains("POCHE"))                return blockIcon(BlockType::PocheRect);
        if (u.contains("FRAISAGE TROU"))        return blockIcon(BlockType::FraisageTrou);
        if (u.contains("PERCAGE"))              return blockIcon(BlockType::Percage);
        if (u.contains("POINTAGE"))             return blockIcon(BlockType::Pointage);
        if (u.contains("TARAUDAGE"))            return blockIcon(BlockType::Taraudage);
        if (u.contains("ALESAGE"))              return blockIcon(BlockType::Alesage);
        if (u.contains("SURFACAGE"))            return blockIcon(BlockType::Surfacage);
        if (u.contains("RAINURE CIRC"))         return blockIcon(BlockType::RainureCirc);
        if (u.contains("RAINURE"))              return blockIcon(BlockType::Rainure);
        if (u.contains("TENON CIRC"))           return blockIcon(BlockType::TenonCirc);
        if (u.contains("TENON"))                return blockIcon(BlockType::TenonRect);
        if (u.contains("CONTOUR"))              return blockIcon(BlockType::Contour);
        return QIcon();
    };
    for (int i = 0; i < lignes.size(); ++i) {
        const GCodeLine &l = lignes[i];
        if (l.type != GCodeLine::Comment) continue;
        const QString c = l.comment.trimmed();
        Section s;
        s.debut = i;
        int niv = 0;
        if (c.startsWith("--- Material Size", Qt::CaseInsensitive)) {
            s.nom = "BLK FORM"; s.icone = icone("BLK");
        } else if (reperes) {
            QRegularExpressionMatch m = reBloc.match(c);
            if (m.hasMatch()) {
                s.nom = m.captured(2).isEmpty() ? m.captured(1) : m.captured(2);
                if (s.nom.compare(m.captured(1), Qt::CaseInsensitive) != 0) s.detail = m.captured(1);
                s.icone = icone(m.captured(1));
            } else if ((m = reOp.match(c)).hasMatch()) {
                s.nom = m.captured(1) + ". " + m.captured(3);
                s.icone = icone(m.captured(3));
                s.op = true;
                niv = 1;
            } else continue;
        } else {
            QRegularExpressionMatch m = reOutil.match(c);
            if (!m.hasMatch()) continue;
            s.nom = "Outil T" + m.captured(1);
        }
        m_sections << s;
        niveaux << niv;
    }
    // Fin de chaque bloc : début du suivant de même niveau ou plus haut
    for (int k = 0; k < m_sections.size(); ++k) {
        int fin = lignes.size() - 1;
        for (int j = k + 1; j < m_sections.size(); ++j)
            if (niveaux[j] <= niveaux[k]) { fin = m_sections[j].debut - 1; break; }
        m_sections[k].fin = fin;
        const bool aDesOps = k + 1 < m_sections.size() && niveaux[k + 1] > niveaux[k];
        // Outil utilisé dans le bloc
        if (m_sections[k].nom != "BLK FORM" && !m_sections[k].nom.startsWith("Outil T") && !aDesOps) {
            for (int i = m_sections[k].debut; i <= qMin(fin, m_sections[k].debut + 400); ++i) {
                const GCodeLine &l = lignes[i];
                if (l.type == GCodeLine::Comment) continue;
                QRegularExpressionMatch m = reT.match(l.code.toUpper());
                if (m.hasMatch()) {
                    const QString t = "T" + m.captured(1);
                    m_sections[k].detail = m_sections[k].detail.isEmpty() ? t : m_sections[k].detail + QString::fromUtf8(" · ") + t;
                    break;
                }
            }
        }
    }

    qDebug() << "[EXEC] blocs" << m_sections.size() << "en" << chrono.elapsed() << "ms";
    // Temps estimé ligne par ligne (avance programmée, rapide à 5000 mm/min).
    // Lecture caractère par caractère : 180 000 lignes en quelques dizaines de ms.
    m_tCumul.resize(lignes.size());
    {
        double x = 0, y = 0, z = 0, f = 1000, t = 0;
        int mode = 0;
        for (int i = 0; i < lignes.size(); ++i) {
            const GCodeLine &l = lignes[i];
            if (l.type != GCodeLine::Comment && l.type != GCodeLine::Blank && !l.code.isEmpty()
                && !l.code.contains(QLatin1String("::::"))) {
                const QChar *c = l.code.constData();
                const int n = int(l.code.size());
                double nx = x, ny = y, nz = z;
                bool bouge = false, g28 = false;
                for (int k = 0; k < n; ) {
                    const char ch = char(c[k].toUpper().toLatin1());
                    if (ch == ';' || ch == '(') break;
                    if (ch < 'A' || ch > 'Z') { ++k; continue; }
                    int a = k + 1;
                    while (a < n && c[a] == ' ') ++a;
                    int b = a;
                    if (b < n && (c[b] == '-' || c[b] == '+')) ++b;
                    while (b < n && (c[b].isDigit() || c[b] == '.')) ++b;
                    if (b == a) { k = a; continue; }
                    bool ok = false;
                    const double v = QStringView(c + a, b - a).toDouble(&ok);
                    k = b;
                    if (!ok) continue;
                    switch (ch) {
                    case 'G': if (v == 28) g28 = true; else if (v >= 0 && v <= 3 && v == int(v)) mode = int(v); break;
                    case 'X': nx = v; bouge = true; break;
                    case 'Y': ny = v; bouge = true; break;
                    case 'Z': nz = v; bouge = true; break;
                    case 'F': if (v > 0) f = v; break;
                    default: break;
                    }
                }
                if (bouge && !g28) {
                    const double d = std::sqrt((nx - x) * (nx - x) + (ny - y) * (ny - y) + (nz - z) * (nz - z));
                    t += d / (mode == 0 ? 5000.0 : f) * 60.0;
                    x = nx; y = ny; z = nz;
                }
            }
            m_tCumul[i] = float(t);
        }
    }
    qDebug() << "[EXEC] temps estime" << chrono.elapsed() << "ms";
    // Liste G-code
    {
        QStringList txt;
        txt.reserve(lignes.size());
        const int larg = QString::number(lignes.size()).size();
        for (int i = 0; i < lignes.size(); ++i) {
            const GCodeLine &l = lignes[i];
            const QString code = l.type == GCodeLine::Comment ? "( " + l.comment + " )" : l.code;
            txt << QString("%1  %2").arg(i + 1, larg).arg(code);
        }
        m_modeleGcode->setStringList(txt);
    }

    // Liste des blocs
    m_listeBlocs->clear();
    m_rangs.clear();
    for (const Section &s : std::as_const(m_sections)) {
        auto *f = new QFrame;
        auto *h = new QHBoxLayout(f);
        h->setContentsMargins(s.op ? 28 : 10, 6, 10, 6);
        h->setSpacing(10);
        auto *ic = new QLabel;
        const int cote = s.op ? 34 : 42;
        ic->setFixedSize(cote, cote);
        ic->setAlignment(Qt::AlignCenter);
        ic->setStyleSheet("background:#fff;border:1px solid #C9CEC9;border-radius:5px;");
        if (!s.icone.isNull()) ic->setPixmap(s.icone.pixmap(cote - 6, cote - 6));
        h->addWidget(ic);
        auto *col = new QVBoxLayout;
        col->setSpacing(1);
        auto *n = new QLabel(s.nom);
        n->setStyleSheet("font-weight:bold;font-size:10pt;background:transparent;border:none;");
        col->addWidget(n);
        if (!s.detail.isEmpty()) {
            auto *d = new QLabel(s.detail);
            d->setStyleSheet("font-size:9pt;color:#6B746B;background:transparent;border:none;");
            col->addWidget(d);
        }
        Ligne r;
        r.f = f;
        r.pb = new QProgressBar;
        r.pb->setRange(0, 1000);
        r.pb->setTextVisible(false);
        r.pb->setStyleSheet(QString("QProgressBar{background:#DDE1DD;border:none;border-radius:2px;max-height:4px;min-height:4px;}"
                                    "QProgressBar::chunk{background:%1;border-radius:2px;}").arg(VERT2));
        r.pb->hide();
        col->addWidget(r.pb);
        h->addLayout(col, 1);
        r.st = new QLabel;
        r.st->setStyleSheet(QString("font-size:9pt;font-weight:bold;color:%1;background:transparent;border:none;").arg(VERT2));
        h->addWidget(r.st);
        auto *it = new QListWidgetItem(m_listeBlocs);
        it->setSizeHint(QSize(0, s.op ? 48 : 58));
        m_listeBlocs->setItemWidget(it, f);
        m_rangs << r;
    }
    if (m_sections.isEmpty()) {
        auto *it = new QListWidgetItem(QString::fromUtf8("  Programme sans découpage en blocs — voir G-CODE"), m_listeBlocs);
        it->setForeground(QColor("#6B746B"));
    }

    qDebug() << "[EXEC] listes" << chrono.elapsed() << "ms";
    setInfoCnc12(m_infoCnc);
    majBlocs();
    if (isVisible()) construire3D();
}

// Vue 3D : construite à l'affichage de la page (pas de calcul pour rien en arrière-plan)
void ExecutionPage::construire3D()
{
    if (m_simCharge || m_lignes.isEmpty() || !m_sim) return;
    m_simCharge = true;
    QElapsedTimer chrono; chrono.start();
    const StockData brut = SimulationPage::stockDepuisGcode(m_lignes);
    if (brut.valid) {
        m_sim->setStock(brut);
        m_sim->onRebuildHeightmap();
    }
    m_sim->setStockOpaque(m_chMat->isChecked());
    m_sim->setShowTool(m_chOutil->isChecked());
    m_sim->setShowToolPath(m_chParc->isChecked());
    m_sim->fitAll();
    m_lblPrep->show();
    m_lblPrep->raise();
    m_sim->buildMovesOnlyAsync(m_lignes, SimulationPage::rayonOutil(m_lignes));   // la page reste utilisable
    qDebug() << "[EXEC] vue 3D" << chrono.elapsed() << "ms";
}

void ExecutionPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (!m_simCharge && !m_lignes.isEmpty())
        QTimer::singleShot(0, this, [this]{ construire3D(); });
}

// ── État des blocs selon la ligne exécutée ─────────────────────
void ExecutionPage::majBlocs()
{
    const int cur = m_ligne - 1;                  // index 0
    for (int k = 0; k < m_sections.size() && k < m_rangs.size(); ++k) {
        const Section &s = m_sections[k];
        Ligne &r = m_rangs[k];
        int etat = 0;                             // 0 à venir, 1 en cours, 2 fait
        if (cur >= 0 && cur > s.fin) etat = 2;
        else if (cur >= s.debut && cur <= s.fin) etat = 1;
        if (etat == 1) {
            const int n = qMax(1, s.fin - s.debut);
            r.pb->setValue(qBound(0, (cur - s.debut) * 1000 / n, 1000));
        }
        if (etat == r.etat) continue;
        r.etat = etat;
        QString st;
        if (etat == 2)
            st = QString("QFrame{background:#F7F8F7;border:1px solid #C9CEC9;border-left:4px solid %1;border-radius:6px;}").arg(VERT);
        else if (etat == 1)
            st = QString("QFrame{background:#fff;border:2px solid %1;border-left:5px solid %2;border-radius:6px;}").arg(VERT, VERT2);
        else
            st = "QFrame{background:#F7F8F7;border:1px solid #C9CEC9;border-left:4px solid #C9CEC9;border-radius:6px;}";
        r.f->setStyleSheet(st + "QLabel{border:none;}");
        r.st->setText(etat == 2 ? QString::fromUtf8("✓") : etat == 1 ? QString::fromUtf8("▶ en cours") : QString());
        r.pb->setVisible(etat == 1);
        if (etat == 1) m_listeBlocs->scrollToItem(m_listeBlocs->item(k));
    }

    // Avancement global (au temps estimé, plus juste qu'au nombre de lignes)
    const float tot = m_tCumul.isEmpty() ? 0.f : m_tCumul.last();
    const float fait = (cur >= 0 && cur < m_tCumul.size()) ? m_tCumul[cur] : 0.f;
    const double frac = tot > 0 ? double(fait) / tot : 0.0;
    m_barProg->setValue(int(frac * 1000));
    m_lblPct->setText(QString("%1 %").arg(int(frac * 100)));
    const qint64 ecoule = m_tempsAvant + (m_enCours && m_chrono.isValid() ? m_chrono.elapsed() / 1000 : 0);
    double reste = tot - fait;
    if (fait > 30 && ecoule > 30) reste *= qBound(0.5, double(ecoule) / fait, 3.0);   // corrige selon la vitesse réelle
    m_lblTemps->setText(QString::fromUtf8("%1 écoulé   ·   ≈ %2 restant").arg(duree(ecoule), duree(qint64(reste))));
}

void ExecutionPage::setLigneCourante(int ligne, bool enCours)
{
    if (enCours && !m_enCours) m_chrono.start();
    if (!enCours && m_enCours && m_chrono.isValid()) m_tempsAvant += m_chrono.elapsed() / 1000;
    if (enCours && ligne > 0 && ligne < m_ligne - 50) { m_tempsAvant = 0; m_chrono.start(); }   // reparti du début
    m_enCours = enCours;
    const bool change = ligne != m_ligne;
    m_ligne = ligne;
    majBlocs();
    if (!change) return;
    if (m_pile->currentIndex() == 1) allerALigne(m_ligne);
    if (m_simCharge && enCours && (!m_dernier3D.isValid() || m_dernier3D.elapsed() > 1500)) {
        m_sim->seekToLine(qMax(0, m_ligne - 1));
        m_dernier3D.start();
    }
}

// ── Position, outil, avance ────────────────────────────────────
void ExecutionPage::updateDro(double wx, double wy, double wz, double mx, double my, double mz,
                              double dtgx, double dtgy, double dtgz)
{
    const double v[3][3] = {{mx, my, mz}, {wx, wy, wz}, {dtgx, dtgy, dtgz}};
    for (int i = 0; i < 3; ++i)
        for (int a = 0; a < 3; ++a) m_val[i][a] = v[i][a];
    majPositions();
}

void ExecutionPage::majPositions()
{
    static const char *t[3] = {"POSITION · MACHINE", "POSITION · PIÈCE", "POSITION · RESTE À PARCOURIR"};
    m_titrePos->setText(QString::fromUtf8(t[m_modePos]));
    for (int i = 0; i < 3; ++i) m_seg[i]->setChecked(i == m_modePos);
    for (int a = 0; a < 3; ++a) m_ax[a]->setText(fmt(m_val[m_modePos][a]));
}

void ExecutionPage::updateSpindle(int rpm, int)
{
    m_lblS->setText(QString("S %1 tr/min").arg(rpm));
}

void ExecutionPage::updateFeed(double feed, int ovr)
{
    m_feedOvr = ovr;
    m_lblF->setText(QString("F %1 mm/min").arg(qRound(feed)));
    m_lblFo->setText(QString("%1 %").arg(ovr));
    m_barF->setValue(qBound(0, ovr, 150));
}

void ExecutionPage::updateTool(int tNum, int hNum, const QString &nom)
{
    QString s = QString("T%1").arg(tNum);
    if (!nom.trimmed().isEmpty()) s += QString::fromUtf8(" · ") + nom.trimmed();
    if (hNum > 0 && hNum != tNum) s += QString("   H%1").arg(hNum);
    m_lblOutil->setText(s);
}

void ExecutionPage::updateMessages(const QString &msg)
{
    const QString m = msg.trimmed();
    if (m.isEmpty() || (!m_messages.isEmpty() && m_messages.last().endsWith(m))) return;
    m_messages << QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + m;
    while (m_messages.size() > 200) m_messages.removeFirst();
    m_lblMsg->setText(QString::fromUtf8("CNC12 : ") + m);
    m_lblMsg->show();
}
