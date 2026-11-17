#pragma execution_character_set("utf-8")
#include "usinage3deditor.h"
#include "modeusin_widget.h"
#include "tooldatabase.h"
#include "cadimporter.h"
#include "cadview3d.h"
#include "decoupe3d.h"
#include "cycles/usinage3dgenerator.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFileDialog>
#include <QTimer>
#include <QComboBox>
#include <QtMath>
#include <QLocale>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QCoreApplication>
#include <QValidator>
#include "tooltabledialog.h"
#include <QFileInfo>
#include <QFrame>
#include <QDialog>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <functional>
#include <QVector3D>
#include <QButtonGroup>
#include <QToolButton>
#include <QGridLayout>
#include <QMap>
#include <cmath>
#include <QApplication>
#include <QEvent>
#include "eazyfiledialog.h"

namespace {
QLabel *section(const QString &t)
{
    auto *l = new QLabel(t);
    l->setStyleSheet("color:#3a8f34;font-size:15px;font-weight:700;letter-spacing:2px;padding-top:6px;");
    return l;
}
QFrame *ligne()
{
    auto *f = new QFrame; f->setFrameShape(QFrame::HLine);
    f->setStyleSheet("QFrame{background:#c8ccd8;}");
    return f;
}
// Survol d'un champ → clé d'aide
struct Survol : QObject {
    Usinage3DEditor *ed; QString cle;
    Survol(Usinage3DEditor *e, const QString &k, QObject *p) : QObject(p), ed(e), cle(k) {}
    bool eventFilter(QObject *, QEvent *ev) override {
        if (ev->type() == QEvent::Enter) emit ed->helpRequested(cle);
        if (ev->type() == QEvent::Leave) emit ed->helpRequested(QString());
        return false;
    }
};
} // namespace

// Molette sur un champ ou une liste : ne change PAS la valeur,
// elle fait défiler la page comme ailleurs.
struct SansMolette : QObject {
    using QObject::QObject;
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() != QEvent::Wheel) return false;
        for (QWidget *w = qobject_cast<QWidget*>(o); w; w = w->parentWidget())
            if (auto *sa = qobject_cast<QAbstractScrollArea*>(w)) {
                QCoreApplication::sendEvent(sa->verticalScrollBar(), e);
                break;
            }
        return true;
    }
};

// Champ numérique qui accepte le point ET la virgule (comme les autres
// units) : affichage avec un point, la virgule tapée devient un point.
struct SpinPoint : QDoubleSpinBox {
    SpinPoint() { setLocale(QLocale::c()); }
    QValidator::State validate(QString &txt, int &pos) const override {
        txt.replace(',', '.');
        return QDoubleSpinBox::validate(txt, pos);
    }
};

QDoubleSpinBox *Usinage3DEditor::champ(double val, double mini, double maxi, int dec, const QString &suffixe)
{
    auto *s = new SpinPoint;
    s->setRange(mini, maxi);
    s->setDecimals(dec);
    s->setValue(val);
    s->setSuffix(suffixe);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connect(s, &QDoubleSpinBox::valueChanged, this, [this]{ if (!m_chargement) emit dataChanged(); });
    return s;
}

Usinage3DEditor::Usinage3DEditor(QWidget *parent) : QWidget(parent)
{
    setStyleSheet(
        "Usinage3DEditor { background:#D6D6D6; }"
        "QLabel { font-size:13pt; }"
        "QLineEdit, QDoubleSpinBox, QSpinBox { font-size:14pt; padding:3px 6px; background:#FFFFFF;"
        "  color:#0000CC; border:1px solid #909090; border-radius:3px; }"
        "QLineEdit:focus, QDoubleSpinBox:focus, QSpinBox:focus { border:2px solid #0000FF; }"
        "QPushButton { font-size:12pt; padding:6px 14px; }");

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(10, 8, 10, 8);
    vl->setSpacing(6);

    auto *titre = new QLabel("Usinage 3D");
    titre->setStyleSheet("font-size:15pt;font-weight:bold;color:#202020;");
    vl->addWidget(titre);

    m_mode = new ModeUsinWidget;
    vl->addWidget(m_mode);
    connect(m_mode, &ModeUsinWidget::modeChanged, this, [this]{ if (!m_chargement) emit dataChanged(); });
    connect(m_mode, &ModeUsinWidget::helpHover, this, &Usinage3DEditor::helpRequested);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    vl->addLayout(form);

    // ── MODÈLE 3D ─────────────────────────────────────────────
    form->addRow(section("MODÈLE 3D"));
    {
        auto *w = new QWidget; auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        m_fichier = new QLineEdit;
        m_fichier->setPlaceholderText("fichier STEP ou IGES");
        m_fichier->setReadOnly(true);
        auto *bParc = new QPushButton("Choisir…");
        m_bParc = bParc;
        h->addWidget(m_fichier, 1); h->addWidget(bParc);
        form->addRow("Fichier", w);
        connect(bParc, &QPushButton::clicked, this, &Usinage3DEditor::choisirFichier);
    }
    m_infoFichier = new QLabel;
    m_infoFichier->setStyleSheet("color:#505050;font-size:11pt;");
    form->addRow("", m_infoFichier);
    {
        auto *b3d = new QPushButton("Vue 3D — choisir la zone (arêtes / faces)");
        b3d->setMinimumHeight(40);
        form->addRow("", b3d);
        connect(b3d, &QPushButton::clicked, this, &Usinage3DEditor::ouvrirVue3D);
    }
    m_zBas = champ(0.0, -1000.0, 1000.0, 3, " mm");
    m_decoupe = champ(0.5, 0.0, 20.0, 2, " mm");
    form->addRow("Z bas de l'usinage", m_zBas);
    form->addRow("Découper plus bas de", m_decoupe);
    connect(m_zBas, &QDoubleSpinBox::valueChanged, this, [this](double v){
        if (m_chargement) return;
        const QJsonArray o = m_d["origine"].toArray();
        m_zBasModele = v + (o.size() == 3 ? o[2].toDouble() : 0.0);
    });

    // ── ORIGINE PIÈCE ET BRUT ─────────────────────────────────
    { QWidget *lg = ligne(); form->addRow(lg); m_lignesOrig << lg; }
    form->addRow(section("ORIGINE PIÈCE ET BRUT"));
    {
        auto *w = new QWidget; auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        auto *pave = new QWidget; auto *g = new QGridLayout(pave);
        g->setSpacing(3); g->setContentsMargins(0, 0, 0, 0);
        m_grpOrig = new QButtonGroup(this);
        m_grpOrig->setExclusive(true);
        static const char *sym[9] = {"↙", "↓", "↘", "←", "●", "→", "↖", "↑", "↗"};
        for (int p = 1; p <= 9; ++p) {
            auto *b = new QToolButton;
            b->setText(QString::fromUtf8(sym[p - 1]));
            b->setCheckable(true);
            b->setFixedSize(46, 40);
            b->setStyleSheet("QToolButton{font-size:16pt;background:#F0F0F0;border:1px solid #909090;}"
                             "QToolButton:checked{background:#3070E0;color:white;}");
            m_grpOrig->addButton(b, p);
            g->addWidget(b, 2 - (p - 1) / 3, (p - 1) % 3);      // 7 8 9 en haut
        }
        h->addWidget(pave);
        auto *col = new QFormLayout;
        m_refOrig = new QComboBox; m_refOrig->addItems({"du brut", "de la pièce"});
        m_zOrig = new QComboBox;
        m_zOrig->addItems({"dessus du brut", "dessus de la pièce", "dessous de la pièce"});
        col->addRow("X0 Y0 : coin / milieu", m_refOrig);
        col->addRow("Z0 sur le", m_zOrig);
        h->addLayout(col, 1);
        form->addRow("Origine", w);
        m_lignesOrig << w;
        connect(m_grpOrig, &QButtonGroup::idClicked, this, [this]{ majOrigine(); });
        connect(m_refOrig, &QComboBox::currentIndexChanged, this, [this]{ majOrigine(); });
        connect(m_zOrig, &QComboBox::currentIndexChanged, this, [this]{ majOrigine(); });
    }
    m_modeBrut = new QComboBox;
    m_modeBrut->addItems({"autour de la pièce (surépaisseurs)", "dimensions fixes (planche)"});
    form->addRow("Brut", m_modeBrut);
    m_bSx = champ(5.0, 0, 500, 2, " mm");  m_bSy = champ(5.0, 0, 500, 2, " mm");
    m_bZp = champ(1.0, 0, 500, 2, " mm");  m_bZm = champ(3.0, 0, 500, 2, " mm");
    m_bL = champ(220.0, 0, 5000, 2, " mm"); m_bW = champ(70.0, 0, 5000, 2, " mm");
    m_bH = champ(15.0, 0, 1000, 2, " mm");
    form->addRow("Surép. en X (chaque côté)", m_bSx);
    form->addRow("Surép. en Y (chaque côté)", m_bSy);
    form->addRow("Surép. au-dessus", m_bZp);
    form->addRow("Surép. en dessous", m_bZm);
    form->addRow("Longueur X", m_bL);
    form->addRow("Largeur Y", m_bW);
    form->addRow("Épaisseur Z", m_bH);
    m_formBrut = form;
    m_lignesOrig << m_modeBrut << m_bSx << m_bSy << m_bZp << m_bZm << m_bL << m_bW << m_bH;
    for (QDoubleSpinBox *s : {m_bSx, m_bSy, m_bZp, m_bZm, m_bL, m_bW, m_bH})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this]{ if (!m_chargement) majOrigine(); });
    connect(m_modeBrut, &QComboBox::currentIndexChanged, this, [this]{ majOrigine(); });
    m_infoBrut = new QLabel;
    m_infoBrut->setStyleSheet("color:#205080;font-size:11pt;");
    m_infoBrut->setWordWrap(true);
    form->addRow("", m_infoBrut);
    {
        auto *bBlk = new QPushButton("Remplir le BLK FORM avec ce brut");
        bBlk->setMinimumHeight(38);
        form->addRow("", bBlk);
        m_lignesOrig << bBlk;
        connect(bBlk, &QPushButton::clicked, this, [this]{
            if (!assurerBBox()) return;
            majOrigine();
            const QJsonArray o = m_d["origine"].toArray(), a = m_d["brutMin"].toArray(), b = m_d["brutMax"].toArray();
            if (o.size() != 3 || a.size() != 3 || b.size() != 3) return;
            auto r = [](double v){ return qRound(v * 1000.0) / 1000.0; };
            QJsonObject lim;
            lim["xMinus"] = r(a[0].toDouble() - o[0].toDouble()); lim["xPlus"] = r(b[0].toDouble() - o[0].toDouble());
            lim["yMinus"] = r(a[1].toDouble() - o[1].toDouble()); lim["yPlus"] = r(b[1].toDouble() - o[1].toDouble());
            lim["zMinus"] = r(a[2].toDouble() - o[2].toDouble()); lim["zPlus"] = r(b[2].toDouble() - o[2].toDouble());
            emit remplirBlkForm(lim);
            m_infoBrut->setText(m_infoBrut->text() + QString::fromUtf8("\n✔ BLK FORM rempli."));
        });
    }

    auto outil = [this, form](const QString &lib, QLineEdit *&t, QLabel *&nom) {
        auto *w = new QWidget; auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        t = new QLineEdit; t->setMaximumWidth(90); t->setPlaceholderText("T");
        nom = new QLabel;
        nom->setStyleSheet("background:#FFFFFF;color:#204060;font-weight:bold;font-size:12pt;"
                           "border:1px solid #909090;border-radius:3px;padding:2px 6px;");
        auto *bTab = new QPushButton("Tableau outils…");
        bTab->setMinimumHeight(32);
        h->addWidget(t); h->addWidget(nom, 1); h->addWidget(bTab);
        form->addRow(lib, w);
        QLineEdit *tt = t; QLabel *nn = nom;
        // Sélection dans le tableau d'outils (comme les autres units)
        connect(bTab, &QPushButton::clicked, this, [this, tt]{
            auto *dlg = new ToolTableDialog(this);
            dlg->setByName(false);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            connect(dlg, &ToolTableDialog::toolSelected, this, [tt](const ToolData &td){
                tt->setText(QString::number(td.T));
            });
            dlg->exec();
        });
        connect(t, &QLineEdit::textChanged, this, [this, tt, nn]{
            majNomOutil(tt, nn);
            if (!m_chargement) emit dataChanged();
        });
    };

    // Paramètres de coupe d'un outil — comme les autres units :
    //   broche  : S tr/min  ou  Vc m/min
    //   avances : F mm/min, Fu mm/tr ou Fz mm/dent
    //   + ligne de calcul : S et F réels selon Ø et nb de dents de l'outil
    auto coupe = [this, form](Coupe3D &c, QLineEdit *t, double sv, double fv, double fpv) {
        c.t = t;
        auto ligneMode = [this, form](const QString &lib, QDoubleSpinBox *&v, QComboBox *&m,
                                      const QStringList &modes, double val) {
            auto *w = new QWidget; auto *h = new QHBoxLayout(w);
            h->setContentsMargins(0, 0, 0, 0);
            v = champ(val, 0, 60000, 3, "");
            m = new QComboBox; m->addItems(modes); m->setMinimumWidth(150);
            h->addWidget(v, 1); h->addWidget(m);
            form->addRow(lib, w);
            connect(m, &QComboBox::currentIndexChanged, this, [this]{
                majCalculs(); if (!m_chargement) emit dataChanged(); });
        };
        const QStringList mS = {"S  tr/min", "Vc  m/min"};
        const QStringList mF = {"F  mm/min", "Fu  mm/tr", "Fz  mm/dent"};
        ligneMode("Broche", c.s, c.ms, mS, sv);
        ligneMode("Avance", c.f, c.mf, mF, fv);
        ligneMode("Avance de plongée", c.fp, c.mfp, mF, fpv);
        c.calc = new QLabel;
        c.calc->setStyleSheet("color:#205080;font-size:11pt;");
        form->addRow("", c.calc);
        for (QDoubleSpinBox *x : {c.s, c.f, c.fp})
            connect(x, &QDoubleSpinBox::valueChanged, this, &Usinage3DEditor::majCalculs);
        connect(t, &QLineEdit::textChanged, this, &Usinage3DEditor::majCalculs);
        c.f->installEventFilter(new Survol(this, "avance", c.f));
        c.fp->installEventFilter(new Survol(this, "avance_plongee", c.fp));
    };

    // ── ÉBAUCHE ───────────────────────────────────────────────
    form->addRow(ligne());
    form->addRow(section("ÉBAUCHE"));
    outil("Outil d'ébauche", m_outilEb, m_nomEb);
    coupe(m_cEb, m_outilEb, 8000, 1200, 400);
    m_passeEb   = champ(2.0, 0.05, 50.0, 2, " mm");
    m_recouvrement = new QSpinBox;
    m_recouvrement->setRange(5, 95); m_recouvrement->setValue(40); m_recouvrement->setSuffix(" %");
    m_recouvrement->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connect(m_recouvrement, &QSpinBox::valueChanged, this, [this]{ if (!m_chargement) emit dataChanged(); });
    m_surepLat  = champ(0.3, 0.0, 10.0, 2, " mm");
    m_surepFond = champ(0.2, 0.0, 10.0, 2, " mm");
    form->addRow("Profondeur de passe", m_passeEb);
    form->addRow("Recouvrement", m_recouvrement);
    form->addRow("Surépaisseur latérale", m_surepLat);
    form->addRow("Surépaisseur de fond", m_surepFond);
    m_passeEb->installEventFilter(new Survol(this, "prof_passe", m_passeEb));
    m_recouvrement->installEventFilter(new Survol(this, "recouvrement", m_recouvrement));
    m_surepLat->installEventFilter(new Survol(this, "surep_lat", m_surepLat));
    m_surepFond->installEventFilter(new Survol(this, "surep_fond", m_surepFond));
    {
        auto *bC = new QPushButton("Voir les couches d'ébauche");
        bC->setMinimumHeight(38);
        form->addRow("", bC);
        m_infoCouches = new QLabel;
        m_infoCouches->setStyleSheet("color:#205080;font-size:11pt;");
        m_infoCouches->setWordWrap(true);
        form->addRow("", m_infoCouches);
        connect(bC, &QPushButton::clicked, this, &Usinage3DEditor::voirCouches);
    }

    // ── FINITION ──────────────────────────────────────────────
    form->addRow(ligne());
    form->addRow(section("FINITION"));
    outil("Outil de finition", m_outilFin, m_nomFin);
    coupe(m_cFin, m_outilFin, 10000, 1500, 400);
    m_passeFin = champ(0.5, 0.01, 20.0, 2, " mm");
    m_pasFin   = champ(0.3, 0.01, 20.0, 2, " mm");
    form->addRow("Profondeur de passe", m_passeFin);
    form->addRow("Écart max entre passes", m_pasFin);   // le pas en Z s'affine tout seul sur les zones peu pentues

    auto *note = new QLabel(QString::fromUtf8(
        "Là où l'outil d'ébauche est trop gros pour passer, l'ébauche laisse plus de matière ;\n"
        "la finition en tient compte."));
    note->setStyleSheet("color:#6a7090;font-size:10pt;font-style:italic;");
    vl->addWidget(note);
    vl->addStretch();

    auto *sm = new SansMolette(this);
    for (auto *w : findChildren<QAbstractSpinBox*>()) { w->installEventFilter(sm); w->setFocusPolicy(Qt::StrongFocus); }
    for (auto *w : findChildren<QComboBox*>())        { w->installEventFilter(sm); w->setFocusPolicy(Qt::StrongFocus); }

    charger(QJsonObject());
}

void Usinage3DEditor::majNomOutil(QLineEdit *t, QLabel *nom)
{
    bool ok = false;
    const int n = t->text().trimmed().toInt(&ok);
    QString s;
    if (ok && n > 0)
        if (const ToolData *td = ToolDatabase::instance().toolByT(n)) s = td->description;
    nom->setText(s);
}

void Usinage3DEditor::choisirFichier()
{
    const QString dep = m_fichier->text().isEmpty() ? QStringLiteral("C:/cncm/ncfiles")
                                                    : QFileInfo(m_fichier->text()).absolutePath();
    const QString f = EazyFileDialog::ouvrir(this, "Modèle 3D", dep, {"step", "stp", "iges", "igs"}, "STEP");
    if (f.isEmpty()) return;
    if (f != m_fichier->text())
        for (const char *k : {"facesFonds", "zFonds", "facesZone", "contoursZone", "zFondsAuto", "bbMin", "bbMax",
                              "origine", "brutMin", "brutMax"}) m_d.remove(QString::fromLatin1(k));
    m_fichier->setText(f);
    majInfoFonds();
    if (!m_chargement) emit dataChanged();
    // le fichier choisi s'ouvre tout de suite dans la visionneuse 3D
    QTimer::singleShot(0, this, &Usinage3DEditor::ouvrirVue3D);
}

void Usinage3DEditor::ouvrirVue3D()
{
    if (m_fichier->text().isEmpty()) { choisirFichier(); return; }
    QDialog dlg(this);
    dlg.setWindowTitle("Usinage 3D — " + QFileInfo(m_fichier->text()).fileName()
                       + QString::fromUtf8(" — choix de la zone"));
    auto *l = new QVBoxLayout(&dlg);
    l->setContentsMargins(2, 2, 2, 2);
    auto *v = new CadView3D(&dlg);
    l->addWidget(v, 1);
    auto *bas = new QHBoxLayout;
    auto *bVal = new QPushButton("Valider la zone");
    auto *bAnn = new QPushButton("Annuler");
    bVal->setMinimumHeight(40); bAnn->setMinimumHeight(40);
    bas->addStretch(); bas->addWidget(bAnn); bas->addWidget(bVal);
    l->addLayout(bas);
    connect(bVal, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(bAnn, &QPushButton::clicked, &dlg, &QDialog::reject);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    {
        const CadDocument doc = CadImporter::load(m_fichier->text());
        v->setDocument(doc);
        m_d["bbMin"] = QJsonArray{doc.bbMin[0], doc.bbMin[1], doc.bbMin[2]};
        m_d["bbMax"] = QJsonArray{doc.bbMax[0], doc.bbMax[1], doc.bbMax[2]};
        // pas encore de zone : Z bas = dessous de la pièce
        if (m_d["facesZone"].toArray().isEmpty() && m_d["contoursZone"].toArray().isEmpty())
            m_zBasModele = doc.bbMin[2];
    }
    QApplication::restoreOverrideCursor();
    majOrigine();
    {
        const QJsonArray o = m_d["origine"].toArray(), a = m_d["brutMin"].toArray(), b = m_d["brutMax"].toArray();
        if (o.size() == 3 && a.size() == 3 && b.size() == 3)
            v->setRepere(QVector3D(o[0].toDouble(), o[1].toDouble(), o[2].toDouble()),
                         QVector3D(a[0].toDouble(), a[1].toDouble(), a[2].toDouble()),
                         QVector3D(b[0].toDouble(), b[1].toDouble(), b[2].toDouble()));
    }
    {   // sélection déjà faite
        QSet<int> faces;
        for (const QJsonValue &x : m_d["facesZone"].toArray()) faces.insert(x.toInt());
        v->setFacesChoisies(faces);
        QVector<QVector<QVector3D>> cts;
        for (const QJsonValue &ct : m_d["contoursZone"].toArray()) {
            QVector<QVector3D> pts;
            for (const QJsonValue &p : ct.toArray()) {
                const QJsonArray a = p.toArray();
                pts << QVector3D(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()));
            }
            cts << pts;
        }
        v->setContoursChoisis(cts);
    }
    v->setSelectionFaces(true);
    dlg.resize(1200, 760);
    dlg.setWindowState(Qt::WindowMaximized);
    if (dlg.exec() != QDialog::Accepted) return;

    // Zone : faces + contours (points 3D arrondis au µm)
    QJsonArray fa;
    for (int f : v->facesChoisies()) fa.append(f);
    QJsonArray ca;
    for (const auto &ct : v->contoursChoisis()) {
        QJsonArray pa;
        for (const QVector3D &p : ct)
            pa.append(QJsonArray{qRound(p.x() * 1000.0) / 1000.0, qRound(p.y() * 1000.0) / 1000.0,
                                 qRound(p.z() * 1000.0) / 1000.0});
        ca.append(pa);
    }
    m_d["facesZone"] = fa;
    m_d["contoursZone"] = ca;
    m_d.remove("facesFonds"); m_d.remove("zFonds");
    // fonds plats trouvés automatiquement (faces planes horizontales)
    QList<double> zf = v->zFondsPlats();
    std::sort(zf.begin(), zf.end(), std::greater<double>());
    QJsonArray za;
    for (double z : zf) za.append(qRound(z * 1000.0) / 1000.0);
    m_d["zFondsAuto"] = za;
    if (!v->selectionVide()) {
        m_zBasModele = v->zBasSelection();          // Z bas rempli tout seul, modifiable
        majOrigine();                               // -> affiché dans le repère pièce
    }
    majInfoFonds();
    if (!m_chargement) emit dataChanged();
}

void Usinage3DEditor::majInfoFonds()
{
    QString s = m_fichier->text().isEmpty() ? QString("aucun fichier")
                                            : QFileInfo(m_fichier->text()).fileName();
    if (!m_fichier->text().isEmpty()) {
        const int nc = m_d["contoursZone"].toArray().size(), nf = m_d["facesZone"].toArray().size();
        if (nc + nf == 0) s += QString::fromUtf8("   —   zone non choisie");
        else s += QString::fromUtf8("   —   zone : %1 contour(s), %2 face(s)").arg(nc).arg(nf);
        const int nfond = m_d["zFondsAuto"].toArray().size();
        if (nfond > 0) s += QString::fromUtf8("   —   %1 fond(s) plat(s) trouvé(s)").arg(nfond);
    }
    m_infoFichier->setText(s);
}

void Usinage3DEditor::setModeleBlk(const QJsonObject &m)
{
    m_blk = m;
    const bool fige = blkFige();
    if (m_formBrut)
        for (QWidget *w : std::as_const(m_lignesOrig)) m_formBrut->setRowVisible(w, !fige && false);
    if (m_bParc) m_bParc->setVisible(!fige);
}

void Usinage3DEditor::charger(const QJsonObject &d)
{
    m_chargement = true;
    m_d = d;
    m_mode->setMode(d["modeUsin"].toInt(0));
    m_fichier->setText(blkFige() ? m_blk["fichier"].toString() : d["fichier"].toString());
    majInfoFonds();
    m_outilEb->setText(d["outilEb"].toString());
    m_outilFin->setText(d["outilFin"].toString());
    m_passeEb->setValue(d["passeEb"].toDouble(2.0));
    m_recouvrement->setValue(d["recouvrement"].toInt(40));
    m_surepLat->setValue(d["surepLat"].toDouble(0.3));
    m_surepFond->setValue(d["surepFond"].toDouble(0.2));
    m_passeFin->setValue(d["passeFin"].toDouble(0.5));
    m_pasFin->setValue(d["pasFin"].toDouble(0.3));
    m_zBas->setValue(d["zBas"].toDouble(0.0));
    m_zBasModele = d["zBasModele"].toDouble(d["zBas"].toDouble(0.0));
    if (auto *b = m_grpOrig->button(d["origPos"].toInt(7))) b->setChecked(true);
    m_refOrig->setCurrentIndex(d["origRef"].toInt(0));
    m_zOrig->setCurrentIndex(d["origZ"].toInt(0));
    m_modeBrut->setCurrentIndex(d["brutMode"].toInt(0));
    m_bSx->setValue(d["brutSx"].toDouble(5.0)); m_bSy->setValue(d["brutSy"].toDouble(5.0));
    m_bZp->setValue(d["brutZp"].toDouble(1.0)); m_bZm->setValue(d["brutZm"].toDouble(3.0));
    m_bL->setValue(d["brutL"].toDouble(220.0)); m_bW->setValue(d["brutW"].toDouble(70.0));
    m_bH->setValue(d["brutH"].toDouble(15.0));
    m_decoupe->setValue(d["decoupeSous"].toDouble(0.5));
    auto lire = [&d](Coupe3D &c, const QString &suf, double sv, double fv, double fpv) {
        c.ms->setCurrentIndex(d["modeS" + suf].toInt(0));
        c.mf->setCurrentIndex(d["modeAvance" + suf].toInt(0));
        c.mfp->setCurrentIndex(d["modeAvPlongee" + suf].toInt(0));
        c.s->setValue(d["s" + suf].toDouble(sv));
        c.f->setValue(d["avance" + suf].toDouble(fv));
        c.fp->setValue(d["avPlongee" + suf].toDouble(fpv));
    };
    lire(m_cEb, "Eb", 8000, 1200, 400);
    lire(m_cFin, "Fin", 10000, 1500, 400);
    m_chargement = false;
    majCalculs();
    majOrigine();
}

QJsonObject Usinage3DEditor::donnees() const
{
    QJsonObject d = m_d;
    d["modeUsin"]     = m_mode->mode();
    d["fichier"]      = m_fichier->text();
    d["outilEb"]      = m_outilEb->text().trimmed();
    d["outilFin"]     = m_outilFin->text().trimmed();
    d["passeEb"]      = m_passeEb->value();
    d["recouvrement"] = m_recouvrement->value();
    d["surepLat"]     = m_surepLat->value();
    d["surepFond"]    = m_surepFond->value();
    d["passeFin"]     = m_passeFin->value();
    d["pasFin"]       = m_pasFin->value();
    d["zBas"]         = m_zBas->value();
    d["zBasModele"]   = m_zBasModele;
    d["origPos"]      = m_grpOrig->checkedId() > 0 ? m_grpOrig->checkedId() : 7;
    d["origRef"]      = m_refOrig->currentIndex();
    d["origZ"]        = m_zOrig->currentIndex();
    d["brutMode"]     = m_modeBrut->currentIndex();
    d["brutSx"] = m_bSx->value(); d["brutSy"] = m_bSy->value();
    d["brutZp"] = m_bZp->value(); d["brutZm"] = m_bZm->value();
    d["brutL"]  = m_bL->value();  d["brutW"]  = m_bW->value(); d["brutH"] = m_bH->value();
    d["decoupeSous"]  = m_decoupe->value();
    auto ecrire = [&d](const Coupe3D &c, const QString &suf) {
        d["s" + suf]             = c.s->value();
        d["modeS" + suf]         = c.ms->currentIndex();
        d["avance" + suf]        = c.f->value();
        d["modeAvance" + suf]    = c.mf->currentIndex();
        d["avPlongee" + suf]     = c.fp->value();
        d["modeAvPlongee" + suf] = c.mfp->currentIndex();
    };
    ecrire(m_cEb, "Eb");
    ecrire(m_cFin, "Fin");
    if (blkFige()) {                      // origine et brut : ceux du BLK FORM figé
        d["fichier"] = m_blk["fichier"];
        for (const char *k : {"orient", "origMode", "origPoint", "origRef3", "origPos", "origRef", "origZ", "brutMode",
                              "brutSx", "brutSy", "brutZp", "brutZm", "brutL", "brutW", "brutH", "brutZal"})
            if (m_blk.contains(QString::fromLatin1(k))) d[QString::fromLatin1(k)] = m_blk[QString::fromLatin1(k)];
    }
    return d;
}

// S et F réels (tr/min, mm/min) à partir des modes Vc / Fu / Fz,
// avec le Ø et le nombre de dents de l'outil du tableau.
void Usinage3DEditor::majCalculs()
{
    for (Coupe3D *c : {&m_cEb, &m_cFin}) {
        if (!c->calc) continue;
        const ToolData *td = nullptr;
        bool ok = false; const int n = c->t->text().trimmed().toInt(&ok);
        if (ok && n > 0) td = ToolDatabase::instance().toolByT(n);
        const double D = td ? td->Routil * 2.0 : 0.0;
        const int z = td ? td->nbDents : 0;
        double S = c->s->value();
        if (c->ms->currentIndex() == 1) S = D > 0 ? c->s->value() * 1000.0 / (M_PI * D) : 0.0;
        auto F = [&](QDoubleSpinBox *v, QComboBox *m) -> double {
            switch (m->currentIndex()) {
            case 1: return v->value() * S;                       // Fu mm/tr
            case 2: return z > 0 ? v->value() * z * S : 0.0;      // Fz mm/dent
            default: return v->value();                           // F mm/min
            }
        };
        QString txt;
        if (S <= 0) txt = "→ choisir l'outil (Ø) pour calculer S";
        else {
            const double f = F(c->f, c->mf), fp = F(c->fp, c->mfp);
            txt = QString("→ S %1 tr/min   F %2 mm/min   F plongée %3 mm/min")
                      .arg(qRound(S)).arg(f > 0 ? QString::number(qRound(f)) : "?")
                      .arg(fp > 0 ? QString::number(qRound(fp)) : "?");
            if ((c->mf->currentIndex() == 2 || c->mfp->currentIndex() == 2) && z <= 0)
                txt += "   (nb de dents manquant)";
        }
        c->calc->setText(txt);
    }
}

// Encombrement du STEP (chargé une fois, gardé dans le bloc)
bool Usinage3DEditor::assurerBBox()
{
    if (m_d["bbMin"].toArray().size() == 3 && m_d["bbMax"].toArray().size() == 3) return true;
    if (m_fichier->text().isEmpty()) return false;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const CadDocument doc = CadImporter::load(m_fichier->text());
    QApplication::restoreOverrideCursor();
    if (doc.mesh.isEmpty()) return false;
    m_d["bbMin"] = QJsonArray{doc.bbMin[0], doc.bbMin[1], doc.bbMin[2]};
    m_d["bbMax"] = QJsonArray{doc.bbMax[0], doc.bbMax[1], doc.bbMax[2]};
    return true;
}

// Origine pièce + brut, dans le repère du STEP ; affichage dans le repère pièce
void Usinage3DEditor::majOrigine()
{
    if (m_formBrut)
        for (QWidget *w : std::as_const(m_lignesOrig)) m_formBrut->setRowVisible(w, false);
    if (!blkFige()) {
        m_infoBrut->setText(QString::fromUtf8("<span style='color:#C02020;font-weight:bold;'>Origine et brut : le BLK FORM "
            "n'est pas figé.</span><br>Faites la mise en place dans BLK FORM → Modèle 3D, puis « Calculer et figer »."));
        if (!m_chargement) emit dataChanged();
        return;
    }
    {
        // Origine et brut du BLK FORM (repère machine) -> repère du STEP pour ce bloc
        const QJsonArray o = m_blk["origine"].toArray(), a = m_blk["brutMin"].toArray(), b = m_blk["brutMax"].toArray();
        const QJsonArray pa = m_blk["bbMin"].toArray(), pb = m_blk["bbMax"].toArray();
        if (o.size() == 3 && a.size() == 3 && b.size() == 3) {
            const Orientation3D or_ = Orientation3D::depuis(m_blk["orient"].toArray());
            const QVector3D om = or_.versModele(QVector3D(o[0].toDouble(), o[1].toDouble(), o[2].toDouble()));
            QVector3D mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
            for (int i = 0; i < 8; ++i) {
                const QVector3D q = or_.versModele(QVector3D((i & 1 ? b : a)[0].toDouble(), (i & 2 ? b : a)[1].toDouble(),
                                                             (i & 4 ? b : a)[2].toDouble()));
                for (int k = 0; k < 3; ++k) { mn[k] = qMin(mn[k], q[k]); mx[k] = qMax(mx[k], q[k]); }
            }
            m_d["origine"] = QJsonArray{om.x(), om.y(), om.z()};
            m_d["brutMin"] = QJsonArray{mn.x(), mn.y(), mn.z()};
            m_d["brutMax"] = QJsonArray{mx.x(), mx.y(), mx.z()};
            const bool av = m_chargement; m_chargement = true;
            m_zBas->setValue(m_zBasModele - om.z());
            m_chargement = av;
            auto f = [](double v){ return QString::number(v, 'f', 2); };
            QString piece;
            if (pa.size() == 3 && pb.size() == 3)
                piece = QString::fromUtf8("   Pièce %1 × %2 × %3 mm").arg(f(pb[0].toDouble() - pa[0].toDouble()),
                            f(pb[1].toDouble() - pa[1].toDouble()), f(pb[2].toDouble() - pa[2].toDouble()));
            m_infoBrut->setText(QString::fromUtf8("<b style='color:#2a7a2a;'>🔒 Origine et brut : BLK FORM (figé)</b><br>"
                "Brut %1 × %2 × %3 mm%4<br>Dans le repère pièce :  X %5 / %6   Y %7 / %8   Z %9 / %10")
                .arg(f(b[0].toDouble() - a[0].toDouble()), f(b[1].toDouble() - a[1].toDouble()), f(b[2].toDouble() - a[2].toDouble()), piece)
                .arg(f(a[0].toDouble() - o[0].toDouble()), f(b[0].toDouble() - o[0].toDouble()),
                     f(a[1].toDouble() - o[1].toDouble()), f(b[1].toDouble() - o[1].toDouble()),
                     f(a[2].toDouble() - o[2].toDouble()), f(b[2].toDouble() - o[2].toDouble())));
        }
        if (!m_chargement) emit dataChanged();
        return;
    }
    const bool fixe = m_modeBrut->currentIndex() == 1;
    if (m_formBrut) {
        for (QWidget *w : {static_cast<QWidget*>(m_bSx), static_cast<QWidget*>(m_bSy)}) m_formBrut->setRowVisible(w, !fixe);
        m_formBrut->setRowVisible(m_bZm, !fixe);
        for (QWidget *w : {static_cast<QWidget*>(m_bL), static_cast<QWidget*>(m_bW), static_cast<QWidget*>(m_bH)})
            m_formBrut->setRowVisible(w, fixe);
    }
    const QJsonArray mi = m_d["bbMin"].toArray(), ma = m_d["bbMax"].toArray();
    if (mi.size() != 3 || ma.size() != 3) {
        m_infoBrut->setText(m_fichier->text().isEmpty()
            ? QString::fromUtf8("Choisissez d'abord le fichier 3D.")
            : QString::fromUtf8("Ouvrez la vue 3D une fois pour lire l'encombrement de la pièce."));
        if (!m_chargement) emit dataChanged();
        return;
    }
    double pMin[3], pMax[3], bMin[3], bMax[3];
    for (int i = 0; i < 3; ++i) { pMin[i] = mi[i].toDouble(); pMax[i] = ma[i].toDouble(); }
    if (!fixe) {
        const double s[3] = {m_bSx->value(), m_bSy->value(), 0.0};
        for (int i = 0; i < 2; ++i) { bMin[i] = pMin[i] - s[i]; bMax[i] = pMax[i] + s[i]; }
        bMin[2] = pMin[2] - m_bZm->value(); bMax[2] = pMax[2] + m_bZp->value();
    } else {
        const double cx = (pMin[0] + pMax[0]) / 2, cy = (pMin[1] + pMax[1]) / 2;
        bMin[0] = cx - m_bL->value() / 2; bMax[0] = cx + m_bL->value() / 2;
        bMin[1] = cy - m_bW->value() / 2; bMax[1] = cy + m_bW->value() / 2;
        bMax[2] = pMax[2] + m_bZp->value(); bMin[2] = bMax[2] - m_bH->value();
    }
    const int pos = m_grpOrig->checkedId() > 0 ? m_grpOrig->checkedId() : 7;
    const double *rMin = m_refOrig->currentIndex() == 0 ? bMin : pMin;
    const double *rMax = m_refOrig->currentIndex() == 0 ? bMax : pMax;
    const int colX = (pos - 1) % 3, rowY = (pos - 1) / 3;
    double o[3];
    o[0] = rMin[0] + colX * 0.5 * (rMax[0] - rMin[0]);
    o[1] = rMin[1] + rowY * 0.5 * (rMax[1] - rMin[1]);
    o[2] = m_zOrig->currentIndex() == 0 ? bMax[2] : (m_zOrig->currentIndex() == 1 ? pMax[2] : pMin[2]);
    auto arr = [](const double *v){ return QJsonArray{v[0], v[1], v[2]}; };
    m_d["origine"] = arr(o); m_d["brutMin"] = arr(bMin); m_d["brutMax"] = arr(bMax);

    // Z bas affiché dans le repère pièce
    const bool av = m_chargement; m_chargement = true;
    m_zBas->setValue(m_zBasModele - o[2]);
    m_chargement = av;

    auto f = [](double v){ return QString::number(v, 'f', 2); };
    m_infoBrut->setText(QString::fromUtf8(
        "Brut %1 × %2 × %3 mm   —   dans le repère pièce :  X %4 / %5   Y %6 / %7   Z %8 / %9\n"
        "Pièce :  X %10 / %11   Y %12 / %13   Z %14 / %15")
        .arg(f(bMax[0] - bMin[0]), f(bMax[1] - bMin[1]), f(bMax[2] - bMin[2]),
             f(bMin[0] - o[0]), f(bMax[0] - o[0]), f(bMin[1] - o[1]), f(bMax[1] - o[1]))
        .arg(f(bMin[2] - o[2]), f(bMax[2] - o[2]),
             f(pMin[0] - o[0]), f(pMax[0] - o[0]), f(pMin[1] - o[1]), f(pMax[1] - o[1]),
             f(pMin[2] - o[2]), f(pMax[2] - o[2])));
    if (!m_chargement) emit dataChanged();
}

// ── Couches d'ébauche ───────────────────────────────────────────
//  niveaux : dessus du brut − passe, − 2 passes, ... jusqu'à Z bas − découpe
//            + un niveau sur chaque fond plat (Z fond + surép. de fond)
//  zone à vider à chaque niveau : limite − (ombre de la pièce + surép. latérale)
//  limite : le brut, ou la zone choisie agrandie du Ø outil (dans le brut)
void Usinage3DEditor::voirCouches()
{
    if (m_fichier->text().isEmpty()) { m_infoCouches->setText(QString::fromUtf8("Choisissez d'abord le fichier 3D.")); return; }
    if (!blkFige()) {
        m_infoCouches->setText(QString::fromUtf8("<span style='color:#C02020;font-weight:bold;'>Figez d'abord l'origine "
                                                 "dans le BLK FORM (Modèle 3D).</span>"));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const CadDocument doc = CadImporter::load(m_fichier->text());
    if (doc.mesh.isEmpty()) { QApplication::restoreOverrideCursor(); return; }
    m_d["bbMin"] = QJsonArray{doc.bbMin[0], doc.bbMin[1], doc.bbMin[2]};
    m_d["bbMax"] = QJsonArray{doc.bbMax[0], doc.bbMax[1], doc.bbMax[2]};
    if (m_d["facesZone"].toArray().isEmpty() && m_d["contoursZone"].toArray().isEmpty()
        && std::fabs(m_zBasModele) < 1e-9)
        m_zBasModele = doc.bbMin[2];
    majOrigine();
    const QJsonArray o = m_d["origine"].toArray(), bmi = m_d["brutMin"].toArray(), bma = m_d["brutMax"].toArray();
    const double oz = o[2].toDouble();

    double R = 0.0;
    {
        bool okT = false; const int t = m_outilEb->text().trimmed().toInt(&okT);
        if (okT && t > 0) if (const ToolData *td = ToolDatabase::instance().toolByT(t)) R = td->Routil;
    }
    QJsonObject dd = donnees();
    const Usinage3DPlan pl = Usinage3DGenerator::plan(dd, doc, R);
    QVector<Couche3D> couches = pl.couches;
    const double passe = m_passeEb->value();

    QApplication::restoreOverrideCursor();

    // Résumé (Z dans le repère pièce)
    int nPlats = 0, nVides = 0;
    QStringList liste;
    for (const Couche3D &c : couches) {
        if (c.facePlane) ++nPlats;
        if (c.aVider.isEmpty()) ++nVides;
        liste << QString::number(c.z - oz, 'f', 2) + (c.facePlane ? "*" : "");
    }
    m_infoCouches->setText(QString::fromUtf8("%1 couches, de Z %2 à Z %3 (passe %4 mm)%5\nZ : %6%7")
        .arg(couches.size())
        .arg(couches.isEmpty() ? 0.0 : couches.first().z - oz, 0, 'f', 2)
        .arg(couches.isEmpty() ? 0.0 : couches.last().z - oz, 0, 'f', 2)
        .arg(passe, 0, 'f', 2)
        .arg(nPlats ? QString::fromUtf8(", dont %1 sur fond plat (*)").arg(nPlats) : QString())
        .arg(liste.join("  "))
        .arg(R <= 0 ? QString::fromUtf8("\n(outil d'ébauche sans rayon : zone non agrandie)") : QString()));

    // Vue 3D des couches
    QDialog dlg(this);
    dlg.setWindowTitle(QString::fromUtf8("Couches d'ébauche — ") + QFileInfo(m_fichier->text()).fileName());
    auto *l = new QVBoxLayout(&dlg);
    l->setContentsMargins(2, 2, 2, 2);
    auto *v = new CadView3D(&dlg);
    l->addWidget(v, 1);
    auto *info = new QLabel(m_infoCouches->text());
    info->setWordWrap(true);
    info->setStyleSheet("font-size:11pt;padding:4px;");
    l->addWidget(info);
    auto *bF = new QPushButton("Fermer");
    bF->setMinimumHeight(38);
    l->addWidget(bF, 0, Qt::AlignRight);
    connect(bF, &QPushButton::clicked, &dlg, &QDialog::accept);
    v->setDocument(doc);
    v->setRepere(QVector3D(o[0].toDouble(), o[1].toDouble(), o[2].toDouble()),
                 QVector3D(bmi[0].toDouble(), bmi[1].toDouble(), bmi[2].toDouble()),
                 QVector3D(bma[0].toDouble(), bma[1].toDouble(), bma[2].toDouble()));
    v->setCouches(couches);
    dlg.resize(1200, 760);
    dlg.setWindowState(Qt::WindowMaximized);
    dlg.exec();
}
