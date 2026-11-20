#pragma execution_character_set("utf-8")
#include "usinage3deditor.h"
#include "progression.h"
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
#include <QCheckBox>
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
#include <QListWidget>
#include <QMenu>
#include <QAction>
#include <QPainter>
#include <QFont>
#include <QPixmap>
#include <QIcon>

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
    m_infoFichier->setWordWrap(true);
    form->addRow("", m_infoFichier);
    {
        auto *b3d = new QPushButton("Vue 3D — choisir la zone (arêtes / faces)");
        b3d->setMinimumHeight(40);
        form->addRow("", b3d);
        connect(b3d, &QPushButton::clicked, this, &Usinage3DEditor::ouvrirVue3D);
    }
    {   // Clic dans la vue 3D de droite : zone (vert) ou cavités exclues (rouge)
        auto *w = new QWidget; auto *hm = new QHBoxLayout(w); hm->setContentsMargins(0, 0, 0, 0);
        m_bModeZone = new QPushButton(QString::fromUtf8("Zone (vert)"));
        m_bModeExcl = new QPushButton(QString::fromUtf8("Exclure cavités (rouge)"));
        for (QPushButton *b : {m_bModeZone, m_bModeExcl}) { b->setCheckable(true); b->setMinimumHeight(34); hm->addWidget(b); }
        m_bModeZone->setStyleSheet("QPushButton{font-size:11pt;} QPushButton:checked{background:#2a9a3a;color:white;font-weight:bold;}");
        m_bModeExcl->setStyleSheet("QPushButton{font-size:11pt;} QPushButton:checked{background:#C83228;color:white;font-weight:bold;}");
        m_bModeZone->setChecked(true);
        auto *grpM = new QButtonGroup(w); grpM->addButton(m_bModeZone, 0); grpM->addButton(m_bModeExcl, 1); grpM->setExclusive(true);
        form->addRow(QString::fromUtf8("Clic en 3D"), w);
        connect(grpM, &QButtonGroup::idClicked, this, [this](int id){
            if (m_vue) m_vue->setModeExclure(id == 1);
            cacherCouches();
        });
        m_bRecalc = new QPushButton(QString::fromUtf8("Recalculer les couches"));
        m_bRecalc->setMinimumHeight(34);
        m_bRecalc->setStyleSheet("QPushButton{font-size:11pt;color:#3070E0;font-weight:bold;}");
        form->addRow("", m_bRecalc);
        connect(m_bRecalc, &QPushButton::clicked, this, &Usinage3DEditor::majVueMaintenant);
    }
    {   // Cases « Afficher » de la vue 3D : couches d'ébauche, de finition, pavé de surfaçage
        auto *w = new QWidget; auto *h = new QHBoxLayout(w); h->setContentsMargins(0, 0, 0, 0);
        m_cbEb   = new QCheckBox(QString::fromUtf8("Ébauche (orange)"));
        m_cbFin  = new QCheckBox(QString::fromUtf8("Finition (vert)"));
        m_cbPave = new QCheckBox(QString::fromUtf8("Surfaçage (bleu)"));
        for (QCheckBox *c : {m_cbEb, m_cbFin, m_cbPave}) { c->setChecked(true); c->setStyleSheet("font-size:11pt;"); h->addWidget(c); }
        h->addStretch();
        form->addRow(QString::fromUtf8("Afficher"), w);
        for (QCheckBox *c : {m_cbEb, m_cbFin, m_cbPave})
            connect(c, &QCheckBox::toggled, this, [this]{
                if (m_vue) m_vue->setAffichage(m_cbEb->isChecked(), m_cbFin->isChecked(), m_cbPave->isChecked());
            });
    }
    m_zBas = champ(0.0, -1000.0, 1000.0, 3, " mm");
    m_decoupe = champ(0.5, 0.0, 20.0, 2, " mm");
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

    // ── OPÉRATIONS ────────────────────────────────────────────
    //  Une opération = un cycle + un outil + SA zone (faces cliquées).
    //  On ajoute un cycle par la barre des familles au-dessus de la vue 3D.
    form->addRow(ligne());
    form->addRow(section(QString::fromUtf8("OPÉRATIONS")));
    m_listeOps = new QListWidget;
    m_listeOps->setIconSize(QSize(34, 34));
    m_listeOps->setMinimumHeight(4 * 50 + 6);
    m_listeOps->setStyleSheet(
        "QListWidget{background:#FFFFFF;border:1px solid #909090;font-size:11pt;}"
        "QListWidget::item{padding:4px;border-bottom:1px solid #E0E0E0;color:#202020;}"
        "QListWidget::item:selected{background:#EEFAEE;color:#101010;border:2px solid #1F9E1F;}");
    form->addRow(m_listeOps);
    connect(m_listeOps, &QListWidget::currentRowChanged, this, [this](int r){
        if (m_majListe || r < 0) return;
        choisirOperation(r);
    });
    {
        auto *w = new QWidget; auto *h = new QHBoxLayout(w); h->setContentsMargins(0, 0, 0, 0);
        m_bHaut  = new QPushButton(QString::fromUtf8("▲ Monter"));
        m_bBas   = new QPushButton(QString::fromUtf8("▼ Descendre"));
        m_bDup   = new QPushButton(QString::fromUtf8("Dupliquer"));
        m_bSuppr = new QPushButton(QString::fromUtf8("Supprimer"));
        auto *bEF = new QPushButton(QString::fromUtf8("+ Ébauche + Finition"));
        for (QPushButton *b : {m_bHaut, m_bBas, m_bDup, m_bSuppr, bEF}) {
            b->setMinimumHeight(32); b->setStyleSheet("QPushButton{font-size:10pt;padding:4px 8px;}"); h->addWidget(b);
        }
        form->addRow(w);
        auto deplacer = [this](int de, int vers) {
            if (de < 0 || vers < 0 || de >= m_ops.size() || vers >= m_ops.size()) return;
            sauverOpCourante();
            const QJsonValue v = m_ops[de];
            m_ops.removeAt(de); m_ops.insert(vers, v);
            m_opCour = -1;
            majListeOps();
            choisirOperation(vers);
            emit dataChanged();
        };
        connect(m_bHaut, &QPushButton::clicked, this, [this, deplacer]{ deplacer(m_opCour, m_opCour - 1); });
        connect(m_bBas,  &QPushButton::clicked, this, [this, deplacer]{ deplacer(m_opCour, m_opCour + 1); });
        connect(m_bDup, &QPushButton::clicked, this, [this]{
            if (m_opCour < 0) return;
            sauverOpCourante();
            const QJsonValue copie = m_ops.at(m_opCour);
            m_ops.insert(m_opCour + 1, copie);
            const int n = m_opCour + 1;
            m_opCour = -1;
            majListeOps(); choisirOperation(n);
            emit dataChanged();
        });
        connect(m_bSuppr, &QPushButton::clicked, this, [this]{
            if (m_opCour < 0 || m_opCour >= m_ops.size()) return;
            m_ops.removeAt(m_opCour);
            const int n = qMin(m_opCour, int(m_ops.size()) - 1);
            m_opCour = -1;
            majListeOps();
            if (n >= 0) choisirOperation(n); else { m_grpEb->setVisible(false); m_grpFin->setVisible(false); m_grpCt->setVisible(false); majInfoFonds(); }
            emit dataChanged();
        });
        connect(bEF, &QPushButton::clicked, this, &Usinage3DEditor::ajouterEbaucheFinition);
    }

    // ── RÉGLAGES DE L'OPÉRATION CHOISIE ───────────────────────
    form->addRow(ligne());
    m_titreOp = section(QString::fromUtf8("RÉGLAGES DE L'OPÉRATION"));
    form->addRow(m_titreOp);
    m_infoZoneOp = new QLabel;
    m_infoZoneOp->setStyleSheet("color:#205080;font-size:11pt;");
    m_infoZoneOp->setWordWrap(true);
    {
        auto *w = new QWidget; auto *h = new QHBoxLayout(w); h->setContentsMargins(0, 0, 0, 0);
        auto *bEff = new QPushButton(QString::fromUtf8("Toute la pièce"));
        bEff->setToolTip(QString::fromUtf8("Efface la zone de cette opération : elle usine toute la pièce"));
        bEff->setStyleSheet("QPushButton{font-size:10pt;padding:4px 8px;}");
        h->addWidget(m_infoZoneOp, 1); h->addWidget(bEff);
        form->addRow("Zone", w);
        connect(bEff, &QPushButton::clicked, this, [this]{
            if (m_opCour < 0 || m_opCour >= m_ops.size()) return;
            QJsonObject o = m_ops[m_opCour].toObject();
            o.remove("facesZone"); o.remove("contoursZone");
            m_ops[m_opCour] = o;
            m_zBasModele = 0.0;
            m_selAJour = true;
            majInfoFonds(); majListeOps();
            emit dataChanged();
            if (m_tmrVue) m_tmrVue->start(50);
        });
    }
    form->addRow("Z bas de l'usinage", m_zBas);
    form->addRow("Découper plus bas de", m_decoupe);

    // Outil + paramètres de coupe, dans le formulaire du groupe (ébauche ou finition)
    auto outil = [this](QFormLayout *f, const QString &lib, QLineEdit *&t, QLabel *&nom) {
        auto *w = new QWidget; auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        t = new QLineEdit; t->setMaximumWidth(90); t->setPlaceholderText("T");
        nom = new QLabel;
        nom->setStyleSheet("background:#FFFFFF;color:#204060;font-weight:bold;font-size:12pt;"
                           "border:1px solid #909090;border-radius:3px;padding:2px 6px;");
        auto *bTab = new QPushButton("Tableau outils…");
        bTab->setMinimumHeight(32);
        h->addWidget(t); h->addWidget(nom, 1); h->addWidget(bTab);
        f->addRow(lib, w);
        QLineEdit *tt = t; QLabel *nn = nom;
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
            if (!m_chargement) { majListeOps(); emit dataChanged(); }
        });
    };
    auto coupe = [this](QFormLayout *f, Coupe3D &c, QLineEdit *t, double sv, double fv, double fpv) {
        c.t = t;
        auto ligneMode = [this, f](const QString &lib, QDoubleSpinBox *&v, QComboBox *&m,
                                   const QStringList &modes, double val) {
            auto *w = new QWidget; auto *h = new QHBoxLayout(w);
            h->setContentsMargins(0, 0, 0, 0);
            v = champ(val, 0, 60000, 3, "");
            m = new QComboBox; m->addItems(modes); m->setMinimumWidth(150);
            h->addWidget(v, 1); h->addWidget(m);
            f->addRow(lib, w);
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
        f->addRow("", c.calc);
        for (QDoubleSpinBox *x : {c.s, c.f, c.fp})
            connect(x, &QDoubleSpinBox::valueChanged, this, &Usinage3DEditor::majCalculs);
        connect(t, &QLineEdit::textChanged, this, &Usinage3DEditor::majCalculs);
        c.f->installEventFilter(new Survol(this, "avance", c.f));
        c.fp->installEventFilter(new Survol(this, "avance_plongee", c.fp));
    };
    auto groupe = [form](QWidget *&g) {
        g = new QWidget;
        auto *f = new QFormLayout(g);
        f->setContentsMargins(0, 0, 0, 0);
        f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->addRow(g);
        return f;
    };

    // ── Opération d'ÉBAUCHE ──
    {
        QFormLayout *f = groupe(m_grpEb);
        m_formEb = f;
        outil(f, "Outil d'ébauche", m_outilEb, m_nomEb);
        coupe(f, m_cEb, m_outilEb, 8000, 1200, 400);
        m_passeEb   = champ(2.0, 0.05, 50.0, 2, " mm");
        m_recouvrement = new QSpinBox;
        m_recouvrement->setRange(5, 95); m_recouvrement->setValue(40); m_recouvrement->setSuffix(" %");
        m_recouvrement->setButtonSymbols(QAbstractSpinBox::NoButtons);
        connect(m_recouvrement, &QSpinBox::valueChanged, this, [this]{ if (!m_chargement) emit dataChanged(); });
        m_surepLat  = champ(0.3, 0.0, 10.0, 2, " mm");
        m_surepFond = champ(0.2, 0.0, 10.0, 2, " mm");
        f->addRow("Profondeur de passe", m_passeEb);
        f->addRow("Recouvrement", m_recouvrement);
        f->addRow("Surépaisseur latérale", m_surepLat);
        f->addRow("Surépaisseur de fond", m_surepFond);
        m_passeEb->installEventFilter(new Survol(this, "prof_passe", m_passeEb));
        m_recouvrement->installEventFilter(new Survol(this, "recouvrement", m_recouvrement));
        m_surepLat->installEventFilter(new Survol(this, "surep_lat", m_surepLat));
        m_surepFond->installEventFilter(new Survol(this, "surep_fond", m_surepFond));
        auto *bC = new QPushButton("Voir les couches d'ébauche");
        m_bCouches = bC;
        bC->setMinimumHeight(38);
        f->addRow("", bC);
        m_infoCouches = new QLabel;
        m_infoCouches->setStyleSheet("color:#205080;font-size:11pt;");
        m_infoCouches->setWordWrap(true);
        f->addRow("", m_infoCouches);
        connect(bC, &QPushButton::clicked, this, &Usinage3DEditor::voirCouches);
    }

    // ── Contournage : réglages en plus de l'outil / passe / surépaisseur (groupe ébauche) ──
    {
        QFormLayout *f = groupe(m_grpCt);
        m_cbFinCt = new QCheckBox(QString::fromUtf8("Finition de contour : une passe à 0 mm sur toute la hauteur"));
        m_cbFinCt->setStyleSheet("font-size:12pt;");
        m_cbFinCt->setChecked(true);
        f->addRow("", m_cbFinCt);
        m_avFinCt = champ(0.0, 0.0, 20000.0, 3, "");
        m_avFinCt->setToolTip(QString::fromUtf8("Même unité que l'avance (mm/min, mm/tr ou mm/dent). 0 = même avance que l'ébauche."));
        f->addRow(QString::fromUtf8("Avance de finition"), m_avFinCt);
        m_sensCt = new QComboBox;
        m_sensCt->addItems({QString::fromUtf8("Avalant (concordance)"), QString::fromUtf8("Opposition")});
        m_sensCt->setStyleSheet("font-size:12pt;");
        f->addRow(QString::fromUtf8("Mode"), m_sensCt);
        m_rEntreeCt = champ(3.0, 0.5, 50.0, 1, " mm");
        f->addRow(QString::fromUtf8("Entrée / sortie en arc, rayon"), m_rEntreeCt);
        auto *n = new QLabel(QString::fromUtf8("Contour pris dans le modèle 3D à Z bas (la pièce, ou les faces choisies) — même origine et même brut que l'ébauche."));
        n->setWordWrap(true);
        n->setStyleSheet("color:#6a7090;font-size:10pt;font-style:italic;");
        f->addRow("", n);
        connect(m_cbFinCt, &QCheckBox::toggled, this, [this](bool on){
            m_avFinCt->setEnabled(on);
            if (!m_chargement) emit dataChanged();
        });
        connect(m_sensCt, &QComboBox::currentIndexChanged, this, [this]{ if (!m_chargement) emit dataChanged(); });
    }

    // ── Opération de FINITION ──
    {
        QFormLayout *f = groupe(m_grpFin);
        m_stratFin = new QComboBox;
        m_stratFin->addItems({QString::fromUtf8("Finition Z constant (parois raides)"),
                              QString::fromUtf8("Finition balayage (dessus peu pentus)"),
                              QString::fromUtf8("Finition Z constant puis balayage"),
                              QString::fromUtf8("Finition mixte (Z constant sur les parois, balayage ailleurs)")});
        m_stratFin->setStyleSheet("font-size:12pt;");
        f->addRow(QString::fromUtf8("Cycle"), m_stratFin);
        outil(f, "Outil de finition", m_outilFin, m_nomFin);
        coupe(f, m_cFin, m_outilFin, 10000, 1500, 400);
        m_passeFin = champ(0.5, 0.01, 20.0, 2, " mm");
        m_pasFin   = champ(0.3, 0.01, 20.0, 2, " mm");
        f->addRow("Profondeur de passe", m_passeFin);
        f->addRow("Écart max entre passes", m_pasFin);
        m_angleSep = champ(35.0, 5.0, 85.0, 0, QString::fromUtf8(" °"));
        m_angleSep->setToolTip(QString::fromUtf8("Pente au-dessus de laquelle on passe en Z constant (les deux finitions se recouvrent d'environ 1 mm)"));
        f->addRow(QString::fromUtf8("Angle de séparation"), m_angleSep);
        {
            auto *w = new QWidget; auto *hp = new QHBoxLayout(w); hp->setContentsMargins(0, 0, 0, 0);
            m_cbProf = new QCheckBox(QString::fromUtf8("Passes en profondeur, max"));
            m_cbProf->setStyleSheet("font-size:12pt;");
            m_cbProf->setToolTip(QString::fromUtf8("Le balayage est refait de haut en bas par couches (à plat sous la couche),\n"
                                                   "pour ne pas prendre toute la matière d'un coup sans ébauche avant."));
            m_passeProf = champ(1.0, 0.1, 20.0, 2, " mm");
            hp->addWidget(m_cbProf); hp->addWidget(m_passeProf, 1);
            f->addRow(QString::fromUtf8("Balayage"), w);
            connect(m_cbProf, &QCheckBox::toggled, this, [this](bool on){
                m_passeProf->setEnabled(on);
                if (!m_chargement) { majListeOps(); emit dataChanged(); }
            });
        }
        auto *bord = new QLabel(QString::fromUtf8("Bord de zone : arrêt au bord des faces choisies (point de contact), sortie dans le prolongement de la surface"));
        bord->setWordWrap(true);
        bord->setStyleSheet("color:#6a7090;font-size:10pt;font-style:italic;");
        f->addRow("", bord);
        connect(m_stratFin, &QComboBox::currentIndexChanged, this, [this](int i){
            m_angleSep->setEnabled(i == 3);
            m_cbProf->setEnabled(i != 0);                  // Z constant : déjà par couches
            m_passeProf->setEnabled(i != 0 && m_cbProf->isChecked());
            if (m_chargement) return;
            majListeOps();
            if (m_opCour >= 0)
                m_titreOp->setText(QString::fromUtf8("RÉGLAGES DE L'OPÉRATION %1 — %2").arg(m_opCour + 1)
                    .arg(Usinage3DGenerator::nomCycle(opCourante()["type"].toString()).toUpper()));
            emit dataChanged();
        });
    }
    m_grpEb->setVisible(false); m_grpFin->setVisible(false); m_grpCt->setVisible(false);

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
    if (f != m_fichier->text()) {
        for (const char *k : {"facesFonds", "zFonds", "facesZone", "contoursZone", "zFondsAuto", "bbMin", "bbMax",
                              "origine", "brutMin", "brutMax", "facesExclues"}) m_d.remove(QString::fromLatin1(k));
        sauverOpCourante();
        for (int i = 0; i < m_ops.size(); ++i) {           // autre pièce : les zones ne valent plus rien
            QJsonObject o = m_ops[i].toObject();
            o.remove("facesZone"); o.remove("contoursZone"); o.remove("zBasModele"); o.remove("zBas");
            m_ops[i] = o;
        }
        m_zBasModele = 0.0;
        majListeOps();
    }
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
        if (opCourante()["facesZone"].toArray().isEmpty() && opCourante()["contoursZone"].toArray().isEmpty())
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
    {   // sélection déjà faite (opération courante)
        const QJsonObject opc = opCourante();
        QSet<int> faces;
        for (const QJsonValue &x : opc["facesZone"].toArray()) faces.insert(x.toInt());
        v->setFacesChoisies(faces);
        QVector<QVector<QVector3D>> cts;
        for (const QJsonValue &ct : opc["contoursZone"].toArray()) {
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
    if (m_opCour >= 0 && m_opCour < m_ops.size()) {
        QJsonObject o = m_ops[m_opCour].toObject();
        o["facesZone"] = fa; o["contoursZone"] = ca;
        m_ops[m_opCour] = o;
    }
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
    majListeOps();
    m_selAJour = true;
    if (!m_chargement) emit dataChanged();
}

void Usinage3DEditor::majInfoFonds()
{
    QString s = m_fichier->text().isEmpty() ? QString("aucun fichier")
                                            : QFileInfo(m_fichier->text()).fileName();
    if (!m_fichier->text().isEmpty()) {
        const QJsonObject opc = opCourante();
        const int nc = opc["contoursZone"].toArray().size(), nf = opc["facesZone"].toArray().size();
        if (m_infoZoneOp)
            m_infoZoneOp->setText(nc + nf == 0 ? QString::fromUtf8("toute la pièce — cliquez des faces dans la vue 3D pour la limiter")
                                               : QString::fromUtf8("%1 face(s), %2 contour(s) — clic dans la vue 3D : ajouter / retirer").arg(nf).arg(nc));
        s += QString::fromUtf8("   —   %1 opération(s)").arg(m_ops.size());
        const QJsonArray zfa = m_d["zFondsAuto"].toArray();
        if (!zfa.isEmpty()) {
            const QJsonArray o = m_d["origine"].toArray();
            const double oz = o.size() == 3 ? o[2].toDouble() : 0.0;
            QStringList zl;
            for (const QJsonValue &v : zfa) zl << QString::number(v.toDouble() - oz, 'f', 2);
            s += QString::fromUtf8("\n%1 fond(s) plat(s) trouvé(s), en orange dans la vue — Z %2 (vidés entièrement en finition)")
                     .arg(zfa.size()).arg(zl.join(" / "));
        }
        const int nex = m_d["facesExclues"].toArray().size();
        if (nex > 0) s += QString::fromUtf8("\n%1 face(s) de cavité exclue(s) (rouge) : pas usinées ici").arg(nex);
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
    // opérations (ancien bloc : converti) ; les anciennes clés d'outil sortent du bloc
    m_ops = Usinage3DGenerator::operations(d);
    const QStringList anciennes = QStringLiteral("modeUsin outilEb outilFin passeEb recouvrement surepLat surepFond "
        "passeFin pasFin stratFin angleSep facesZone contoursZone zBas zBasModele "
        "sEb modeSEb avanceEb modeAvanceEb avPlongeeEb modeAvPlongeeEb "
        "sFin modeSFin avanceFin modeAvanceFin avPlongeeFin modeAvPlongeeFin").split(QLatin1Char(' '));
    for (const QString &k : anciennes) m_d.remove(k);
    m_fichier->setText(blkFige() ? m_blk["fichier"].toString() : d["fichier"].toString());
    if (auto *b = m_grpOrig->button(d["origPos"].toInt(7))) b->setChecked(true);
    m_refOrig->setCurrentIndex(d["origRef"].toInt(0));
    m_zOrig->setCurrentIndex(d["origZ"].toInt(0));
    m_modeBrut->setCurrentIndex(d["brutMode"].toInt(0));
    m_bSx->setValue(d["brutSx"].toDouble(5.0)); m_bSy->setValue(d["brutSy"].toDouble(5.0));
    m_bZp->setValue(d["brutZp"].toDouble(1.0)); m_bZm->setValue(d["brutZm"].toDouble(3.0));
    m_bL->setValue(d["brutL"].toDouble(220.0)); m_bW->setValue(d["brutW"].toDouble(70.0));
    m_bH->setValue(d["brutH"].toDouble(15.0));
    m_opCour = -1;
    m_chargement = false;
    majListeOps();
    if (!m_ops.isEmpty()) choisirOperation(0);
    else { m_grpEb->setVisible(false); m_grpFin->setVisible(false); m_grpCt->setVisible(false); majInfoFonds(); }
    majOrigine();
    m_selAJour = true;
    if (m_tmrVue) m_tmrVue->start(50);
}

QJsonObject Usinage3DEditor::donnees() const
{
    QJsonObject d = m_d;
    d["fichier"]      = m_fichier->text();
    QJsonArray ops = m_ops;
    if (m_opCour >= 0 && m_opCour < ops.size()) ops[m_opCour] = opDepuisChamps(ops[m_opCour].toObject());
    d["operations"]   = ops;
    d["origPos"]      = m_grpOrig->checkedId() > 0 ? m_grpOrig->checkedId() : 7;
    d["origRef"]      = m_refOrig->currentIndex();
    d["origZ"]        = m_zOrig->currentIndex();
    d["brutMode"]     = m_modeBrut->currentIndex();
    d["brutSx"] = m_bSx->value(); d["brutSy"] = m_bSy->value();
    d["brutZp"] = m_bZp->value(); d["brutZm"] = m_bZm->value();
    d["brutL"]  = m_bL->value();  d["brutW"]  = m_bW->value(); d["brutH"] = m_bH->value();
    if (blkFige()) {                      // origine et brut : ceux du BLK FORM figé
        d["fichier"] = m_blk["fichier"];
        for (const char *k : {"orient", "origMode", "origPoint", "origRef3", "origPos", "origRef", "origZ", "brutMode",
                              "brutSx", "brutSy", "brutZp", "brutZm", "brutL", "brutW", "brutH", "brutZal"})
            if (m_blk.contains(QString::fromLatin1(k))) d[QString::fromLatin1(k)] = m_blk[QString::fromLatin1(k)];
    }
    return d;
}

// ═══════════════════════════════════════════════════════════════
//  Opérations
// ═══════════════════════════════════════════════════════════════
namespace {
QPixmap iconeOp(const QString &type)
{
    const bool eb = (type == "ebauche"), ct = (type == "contour");
    QPixmap p(34, 34); p.fill(Qt::transparent);
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setPen(Qt::NoPen);
    g.setBrush(eb ? QColor(232, 137, 42) : ct ? QColor(40, 100, 200) : QColor(47, 158, 72));
    g.drawRoundedRect(QRectF(0, 0, 34, 34), 5, 5);
    g.setPen(Qt::white);
    QFont f; f.setBold(true); f.setPointSize(15); g.setFont(f);
    g.drawText(QRectF(0, 0, 34, 34), Qt::AlignCenter, eb ? QString::fromUtf8("É") : ct ? QString("C") : QString("F"));
    return p;
}
// Bulle d'aide d'un cycle : dessin + texte
QString bulleCycle(const QString &type)
{
    struct B { const char *img, *titre, *texte, *oui, *non; };
    B b{"", "", "", "", ""};
    if (type == "ebauche")  b = {"ebauche", "Ébauche par couches", "Enlève la matière par couches à Z fixe, du bord du brut vers la pièce. Laisse une surépaisseur et des marches pour la finition.", "Toujours en premier, avec une fraise plate assez grosse", "Ne finit pas les surfaces courbes (marches)"};
    if (type == "balayage") b = {"balayage", "Finition balayage", "Allers-retours en lignes parallèles ; l'outil suit la surface en montant et en descendant.", "Dessus peu pentus, galbes doux", "Parois raides : les passes s'écartent sur la pente"};
    if (type == "zconst")   b = {"zconst", "Finition Z constant", "L'outil fait le tour de la pièce à une hauteur fixe, puis descend d'un pas et recommence.", "Parois raides, flancs", "Dessus plats : les tours s'écartent sur le plat"};
    if (type == "mixte")    b = {"mixte", "Finition mixte", "Z constant sur les parois raides, balayage sur les dessus ; les deux se recouvrent d'environ 1 mm. Réglage : angle de séparation (35°).", "Pièces avec dessus galbé et flancs (chevalet)", ""};
    if (type == "zbal")     b = {"zbal", "Finition Z constant puis balayage", "Z constant sur toute la pièce, puis un balayage sur toute la pièce.", "Sûr : tout est repassé deux fois", "Plus long que la finition mixte"};
    if (type == "contour")  b = {"contour", "Contournage", "Fait le tour du contour de la pièce avec une fraise plate : passes en Z avec une surépaisseur, puis une passe de finition à 0 sur toute la hauteur. Entrée et sortie en arc tangent.", "Flancs verticaux, bord de la pièce net jusqu'en bas", "Ne finit pas les dessus galbés (boule)"};
    if (type == "crayon")   b = {"crayon", "Reprise crayon (plus tard)", "Passe seulement au fond des angles rentrants, là où la grosse boule n'a pas pu descendre.", "Après une finition, avec une boule plus petite", ""};
    if (!*b.img) return QString();
    QString t = QString("<table cellspacing=6><tr><td><img src=':/aide3d/%1.png'></td><td width=270>"
                        "<b style='color:#0030A0;font-size:12pt;'>%2</b><br>%3").arg(b.img, QString::fromUtf8(b.titre), QString::fromUtf8(b.texte));
    if (*b.oui) t += QString::fromUtf8("<br><span style='color:#1D7A2E;'>✔ %1</span>").arg(QString::fromUtf8(b.oui));
    if (*b.non) t += QString::fromUtf8("<br><span style='color:#B02020;'>✘ %1</span>").arg(QString::fromUtf8(b.non));
    return t + "</td></tr></table>";
}
}

QJsonObject Usinage3DEditor::opCourante() const
{
    if (m_opCour < 0 || m_opCour >= m_ops.size()) return QJsonObject();
    return opDepuisChamps(m_ops[m_opCour].toObject());
}

int Usinage3DEditor::premiereOp(bool ebauche) const
{
    for (int i = 0; i < m_ops.size(); ++i)
        if (Usinage3DGenerator::estEbauche(m_ops[i].toObject()) == ebauche) return i;
    return -1;
}

// Nouvelle opération : outil et vitesses repris de la dernière opération de la même famille
QJsonObject Usinage3DEditor::opParDefaut(const QString &type) const
{
    const bool ct = (type == "contour");
    const bool eb = (type == "ebauche") || ct;           // contournage : fraise plate comme l'ébauche
    QJsonObject o;
    for (int i = m_ops.size() - 1; i >= 0; --i) {
        const QJsonObject p = (i == m_opCour) ? opCourante() : m_ops[i].toObject();
        const bool pEb = Usinage3DGenerator::estEbauche(p) || Usinage3DGenerator::estContour(p);
        if (pEb == eb) {
            for (const char *k : {"outil", "s", "modeS", "avance", "modeAvance", "avPlongee", "modeAvPlongee",
                                  "passe", "pas", "angleSep", "recouvrement", "surepLat", "surepFond"})
                if (p.contains(QString::fromLatin1(k))) o[QString::fromLatin1(k)] = p[QString::fromLatin1(k)];
            break;
        }
    }
    o["type"] = type;
    if (!o.contains("passe")) o["passe"] = eb ? 2.0 : 0.5;
    if (ct) {
        o["surepLat"] = 0.2;
        o["finCt"] = true; o["sensCt"] = 0; o["rEntreeCt"] = 3.0; o["avanceFinCt"] = 0.0;
        o["passe"] = 2.0;
        o.remove("recouvrement"); o.remove("surepFond");
    }
    if (eb) {
        if (!o.contains("s")) { o["s"] = 8000; o["avance"] = 1200; o["avPlongee"] = 400; }
        if (!o.contains("recouvrement")) o["recouvrement"] = 40;
        if (!o.contains("surepLat")) o["surepLat"] = 0.3;
        if (!o.contains("surepFond")) o["surepFond"] = 0.2;
    } else {
        if (!o.contains("s")) { o["s"] = 10000; o["avance"] = 1500; o["avPlongee"] = 400; }
        if (!o.contains("pas")) o["pas"] = 0.3;
        if (!o.contains("angleSep")) o["angleSep"] = 35.0;
    }
    return o;
}

QJsonObject Usinage3DEditor::opDepuisChamps(const QJsonObject &base) const
{
    QJsonObject o = base;
    const bool ct = Usinage3DGenerator::estContour(base);
    const bool eb = Usinage3DGenerator::estEbauche(base) || ct;
    const Coupe3D &c = eb ? m_cEb : m_cFin;
    if (ct) {
        o["outil"] = m_outilEb->text().trimmed();
        o["passe"] = m_passeEb->value();
        o["surepLat"] = m_surepLat->value();
        o["finCt"] = m_cbFinCt->isChecked();
        o["avanceFinCt"] = m_avFinCt->value();
        o["sensCt"] = m_sensCt->currentIndex();
        o["rEntreeCt"] = m_rEntreeCt->value();
    } else if (eb) {
        o["outil"] = m_outilEb->text().trimmed();
        o["passe"] = m_passeEb->value();
        o["recouvrement"] = m_recouvrement->value();
        o["surepLat"] = m_surepLat->value();
        o["surepFond"] = m_surepFond->value();
    } else {
        const QStringList types = {QStringLiteral("zconst"), QStringLiteral("balayage"), QStringLiteral("zbal"), QStringLiteral("mixte")};
        o["type"] = types.at(qBound(0, m_stratFin->currentIndex(), 3));
        o["outil"] = m_outilFin->text().trimmed();
        o["passe"] = m_passeFin->value();
        o["pas"] = m_pasFin->value();
        o["angleSep"] = m_angleSep->value();
        o["profondeurs"] = m_cbProf->isChecked() && m_stratFin->currentIndex() != 0;
        o["passeProf"] = m_passeProf->value();
    }
    o["s"] = c.s->value();             o["modeS"] = c.ms->currentIndex();
    o["avance"] = c.f->value();        o["modeAvance"] = c.mf->currentIndex();
    o["avPlongee"] = c.fp->value();    o["modeAvPlongee"] = c.mfp->currentIndex();
    o["zBas"] = m_zBas->value();
    o["zBasModele"] = m_zBasModele;
    o["decoupeSous"] = m_decoupe->value();
    return o;
}

void Usinage3DEditor::sauverOpCourante()
{
    if (m_opCour >= 0 && m_opCour < m_ops.size())
        m_ops[m_opCour] = opDepuisChamps(m_ops[m_opCour].toObject());
}

void Usinage3DEditor::chargerOp(const QJsonObject &op)
{
    m_chargement = true;
    const bool ct = Usinage3DGenerator::estContour(op);
    const bool eb = Usinage3DGenerator::estEbauche(op) || ct;
    Coupe3D &c = eb ? m_cEb : m_cFin;
    if (ct) {
        m_outilEb->setText(op["outil"].toString());
        m_passeEb->setValue(op["passe"].toDouble(2.0));
        m_surepLat->setValue(op["surepLat"].toDouble(0.2));
        m_cbFinCt->setChecked(op["finCt"].toBool(true));
        m_avFinCt->setValue(op["avanceFinCt"].toDouble(0.0));
        m_avFinCt->setEnabled(m_cbFinCt->isChecked());
        m_sensCt->setCurrentIndex(qBound(0, op["sensCt"].toInt(0), 1));
        m_rEntreeCt->setValue(op["rEntreeCt"].toDouble(3.0));
    } else if (eb) {
        m_outilEb->setText(op["outil"].toString());
        m_passeEb->setValue(op["passe"].toDouble(2.0));
        m_recouvrement->setValue(op["recouvrement"].toInt(40));
        m_surepLat->setValue(op["surepLat"].toDouble(0.3));
        m_surepFond->setValue(op["surepFond"].toDouble(0.2));
    } else {
        const QString t = op["type"].toString();
        m_stratFin->setCurrentIndex(t == "zconst" ? 0 : t == "balayage" ? 1 : t == "zbal" ? 2 : 3);
        m_outilFin->setText(op["outil"].toString());
        m_passeFin->setValue(op["passe"].toDouble(0.5));
        m_pasFin->setValue(op["pas"].toDouble(0.3));
        m_angleSep->setValue(op["angleSep"].toDouble(35.0));
        m_angleSep->setEnabled(m_stratFin->currentIndex() == 3);
        m_cbProf->setChecked(op["profondeurs"].toBool(false));
        m_passeProf->setValue(op["passeProf"].toDouble(1.0));
        m_cbProf->setEnabled(m_stratFin->currentIndex() != 0);
        m_passeProf->setEnabled(m_cbProf->isEnabled() && m_cbProf->isChecked());
    }
    c.ms->setCurrentIndex(op["modeS"].toInt(0));
    c.mf->setCurrentIndex(op["modeAvance"].toInt(0));
    c.mfp->setCurrentIndex(op["modeAvPlongee"].toInt(0));
    c.s->setValue(op["s"].toDouble(eb ? 8000 : 10000));
    c.f->setValue(op["avance"].toDouble(eb ? 1200 : 1500));
    c.fp->setValue(op["avPlongee"].toDouble(400));
    m_zBasModele = op["zBasModele"].toDouble(op["zBas"].toDouble(0.0));
    m_zBas->setValue(op["zBas"].toDouble(0.0));
    m_decoupe->setValue(op["decoupeSous"].toDouble(m_d["decoupeSous"].toDouble(0.5)));
    m_grpEb->setVisible(eb);
    m_grpFin->setVisible(!eb);
    m_grpCt->setVisible(ct);
    // lignes propres à l'ébauche par couches : cachées pour le contournage
    if (m_formEb) {
        m_formEb->setRowVisible(m_recouvrement, !ct);
        m_formEb->setRowVisible(m_surepFond, !ct);
        if (m_bCouches) m_formEb->setRowVisible(m_bCouches, !ct);
        if (m_infoCouches) m_formEb->setRowVisible(m_infoCouches, !ct);
        if (auto *lb = qobject_cast<QLabel*>(m_formEb->labelForField(m_outilEb->parentWidget())))
            lb->setText(ct ? QString::fromUtf8("Outil (fraise plate)") : QString::fromUtf8("Outil d'ébauche"));
    }
    m_chargement = false;
    majCalculs();
}

void Usinage3DEditor::choisirOperation(int i)
{
    if (i < 0 || i >= m_ops.size()) return;
    if (i != m_opCour) sauverOpCourante();
    m_opCour = i;
    const QJsonObject op = m_ops[i].toObject();
    chargerOp(op);
    m_titreOp->setText(QString::fromUtf8("RÉGLAGES DE L'OPÉRATION %1 — %2").arg(i + 1)
                       .arg(Usinage3DGenerator::nomCycle(op["type"].toString()).toUpper()));
    if (m_listeOps && m_listeOps->currentRow() != i) {
        m_majListe = true; m_listeOps->setCurrentRow(i); m_majListe = false;
    }
    if (m_bHaut)  m_bHaut->setEnabled(i > 0);
    if (m_bBas)   m_bBas->setEnabled(i < m_ops.size() - 1);
    if (m_bDup)   m_bDup->setEnabled(true);
    if (m_bSuppr) m_bSuppr->setEnabled(true);
    majOrigine();
    majInfoFonds();
    m_selAJour = true;                    // faces de cette opération dans la vue
    if (m_tmrVue) m_tmrVue->start(50);
}

void Usinage3DEditor::majListeOps()
{
    if (!m_listeOps) return;
    m_majListe = true;
    const int cour = m_opCour;
    // avertissements (matière non ébauchée) : gardés dans chaque opération
    {
        QJsonArray tous = m_ops;
        if (cour >= 0 && cour < tous.size()) tous[cour] = opCourante();
        for (int i = 0; i < m_ops.size(); ++i) {
            QJsonObject o = m_ops[i].toObject();
            const QString a = (m_doc && !m_doc->mesh.isEmpty()) ? Usinage3DGenerator::avertissement(tous, i, *m_doc)
                                                                : o["avertissement"].toString();
            if (a.isEmpty()) o.remove("avertissement"); else o["avertissement"] = a;
            m_ops[i] = o;
        }
    }
    m_listeOps->clear();
    for (int i = 0; i < m_ops.size(); ++i) {
        QJsonObject op = (i == cour) ? opCourante() : m_ops[i].toObject();
        const QString avert = m_ops[i].toObject()["avertissement"].toString();
        const int nf = op["facesZone"].toArray().size(), nc = op["contoursZone"].toArray().size();
        QString zone = (nf + nc == 0) ? QString::fromUtf8("toute la pièce")
                     : QString::fromUtf8("%1 face(s)%2").arg(nf).arg(nc ? QString::fromUtf8(", %1 contour(s)").arg(nc) : QString());
        QString outil = op["outil"].toString();
        if (!outil.isEmpty()) {
            bool ok = false; const int t = outil.toInt(&ok);
            QString nom;
            if (ok) if (const ToolData *td = ToolDatabase::instance().toolByT(t)) nom = td->description;
            outil = "T" + outil + (nom.isEmpty() ? QString() : "  " + nom);
        } else outil = QString::fromUtf8("outil ?");
        auto *it = new QListWidgetItem(QIcon(iconeOp(op["type"].toString())),
            QString::fromUtf8("%1. %2\n     Zone : %3    ·    %4").arg(i + 1)
                .arg(Usinage3DGenerator::nomCycle(op["type"].toString()), zone, outil));
        QString bulle = bulleCycle(op["type"].toString());
        if (!avert.isEmpty()) {
            it->setText(it->text() + QString::fromUtf8("\n     ⚠ ") + avert.section(QString::fromUtf8(" — "), 0, 0));
            it->setForeground(QColor(190, 90, 0));
            bulle = QString::fromUtf8("<b style='color:#B05000;'>⚠ ") + avert.toHtmlEscaped() + "</b><br>" + bulle;
        }
        it->setToolTip(bulle);
        it->setSizeHint(QSize(0, avert.isEmpty() ? 48 : 68));
        m_listeOps->addItem(it);
    }
    if (cour >= 0 && cour < m_ops.size()) m_listeOps->setCurrentRow(cour);
    if (m_bDup)   m_bDup->setEnabled(cour >= 0);
    if (m_bSuppr) m_bSuppr->setEnabled(cour >= 0);
    m_majListe = false;
}

void Usinage3DEditor::ajouterOperation(const QString &type)
{
    sauverOpCourante();
    m_ops.append(opParDefaut(type));
    const int n = int(m_ops.size()) - 1;
    m_opCour = -1;
    majListeOps();
    choisirOperation(n);
    if (!m_chargement) emit dataChanged();
}

void Usinage3DEditor::ajouterEbaucheFinition()
{
    sauverOpCourante();
    m_ops.append(opParDefaut("ebauche"));
    m_ops.append(opParDefaut("mixte"));
    const int n = int(m_ops.size()) - 2;
    m_opCour = -1;
    majListeOps();
    choisirOperation(n);
    emit dataChanged();
}

// Barre des familles de cycles (au-dessus de la vue 3D) : un bouton par
// famille, la liste des cycles se déroule ; bulle d'aide au survol.
QWidget *Usinage3DEditor::barreFamilles()
{
    if (m_barreFam) return m_barreFam;
    m_barreFam = new QWidget;
    auto *h = new QHBoxLayout(m_barreFam);
    h->setContentsMargins(0, 0, 0, 0); h->setSpacing(4);
    const QString st = "QToolButton{font-size:10pt;font-weight:bold;padding:3px 10px;background:#FAFAFA;border:1px solid #909090;border-radius:3px;}"
                       "QToolButton:hover{background:#D6E8FF;border:1px solid #3070E0;}"
                       "QToolButton:disabled{color:#A0A0A0;}"
                       "QToolButton::menu-indicator{subcontrol-position:right center;}";
    auto famille = [&](const QString &txt, const QList<QPair<QString, QString>> &cycles, bool actif) {
        auto *b = new QToolButton;
        b->setText(txt + "  ");
        b->setStyleSheet(st);
        b->setPopupMode(QToolButton::InstantPopup);
        b->setEnabled(actif);
        if (!actif) b->setToolTip(QString::fromUtf8("Plus tard"));
        auto *m = new QMenu(b);
        m->setToolTipsVisible(true);
        m->setStyleSheet("QMenu{font-size:11pt;} QMenu::item{padding:6px 18px;} QMenu::item:selected{background:#D6E8FF;color:#0000C0;}");
        for (const auto &c : cycles) {
            QAction *a = m->addAction(c.first);
            a->setToolTip(bulleCycle(c.second));
            if (c.second == "crayon") a->setEnabled(false);
            else { const QString t = c.second; connect(a, &QAction::triggered, this, [this, t]{ ajouterOperation(t); }); }
        }
        b->setMenu(m);
        h->addWidget(b);
    };
    famille(QString::fromUtf8("▤ Surfaçage"), {}, false);
    famille(QString::fromUtf8("≋ Ébauche"), {{QString::fromUtf8("Ébauche par couches"), "ebauche"}}, true);
    famille(QString::fromUtf8("◠ Finition"), {{QString::fromUtf8("Finition balayage"), "balayage"},
                                             {QString::fromUtf8("Finition Z constant"), "zconst"},
                                             {QString::fromUtf8("Finition mixte"), "mixte"},
                                             {QString::fromUtf8("Finition Z constant puis balayage"), "zbal"},
                                             {QString::fromUtf8("Reprise crayon (plus tard)"), "crayon"}}, true);
    famille(QString::fromUtf8("◯ Perçage"), {}, false);
    famille(QString::fromUtf8("▭ 2D sur 3D"), {{QString::fromUtf8("Contournage + finition de contour"), "contour"}}, true);
    h->addStretch();
    return m_barreFam;
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
    if (opCourante()["facesZone"].toArray().isEmpty() && opCourante()["contoursZone"].toArray().isEmpty()
        && std::fabs(m_zBasModele) < 1e-9)
        m_zBasModele = doc.bbMin[2];
    majOrigine();
    const QJsonArray o = m_d["origine"].toArray(), bmi = m_d["brutMin"].toArray(), bma = m_d["brutMax"].toArray();
    const double oz = o[2].toDouble();

    // couches de l'opération d'ébauche (la courante, sinon la première)
    const QJsonObject tout = donnees();
    const QJsonArray ops = tout["operations"].toArray();
    int iEb = (m_opCour >= 0 && Usinage3DGenerator::estEbauche(ops[m_opCour].toObject())) ? m_opCour : premiereOp(true);
    if (iEb < 0) { QApplication::restoreOverrideCursor(); m_infoCouches->setText(QString::fromUtf8("Aucune opération d'ébauche.")); return; }
    const QJsonObject opEb = ops[iEb].toObject();
    double R = 0.0;
    {
        bool okT = false; const int t = opEb["outil"].toString().trimmed().toInt(&okT);
        if (okT && t > 0) if (const ToolData *td = ToolDatabase::instance().toolByT(t)) R = td->Routil;
    }
    QJsonObject dd = Usinage3DGenerator::pourOperation(tout, opEb);
    const Usinage3DPlan pl = Usinage3DGenerator::plan(dd, doc, R);
    QVector<Couche3D> couches = pl.couches;
    const double passe = opEb["passe"].toDouble(2.0);

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

// ═══════════════════════════════════════════════════════════════
//  Vue 3D intégrée (colonne de droite de la page Programme)
// ═══════════════════════════════════════════════════════════════
void Usinage3DEditor::setVue(CadView3D *v)
{
    m_vue = v;
    if (!m_tmrVue) {
        m_tmrVue = new QTimer(this);
        m_tmrVue->setSingleShot(true);
        m_tmrVue->setInterval(500);
        connect(m_tmrVue, &QTimer::timeout, this, &Usinage3DEditor::majVue);
        // tout changement de réglage -> couches recalculées (après une pause)
        connect(this, &Usinage3DEditor::dataChanged, m_tmrVue, [this]{ if (!m_chargement) m_tmrVue->start(); });
    }
    if (v) connect(v, &CadView3D::facesChanged, this, &Usinage3DEditor::zoneDepuisVue, Qt::UniqueConnection);
}

// Pendant la sélection (zone / cavités) : on ne voit que la pièce
void Usinage3DEditor::cacherCouches()
{
    m_couchesCachees = true;
    if (m_vue) { m_vue->setCouches({}); m_vue->setPave({}, 0, 0); }
    if (m_infoCouches)
        m_infoCouches->setText(QString::fromUtf8("Couches cachées pendant la sélection — « Recalculer les couches » pour les voir"));
}

void Usinage3DEditor::majVueMaintenant()
{
    m_couchesCachees = false;
    m_selAJour = true;
    if (m_tmrVue) m_tmrVue->stop();
    majVue();
}

void Usinage3DEditor::majVue()
{
    if (!m_vue || !isVisible()) return;
    if (Progression::enCours) { if (m_tmrVue) m_tmrVue->start(); return; }   // calcul du G-code : plus tard
    const QString f = m_fichier->text();
    if (f.isEmpty() || !blkFige()) { m_vue->setCouches({}); m_vue->setPave({}, 0, 0); return; }
    if (!m_doc || m_docChemin != f) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_doc = std::make_shared<CadDocument>(CadImporter::load(f));
        QApplication::restoreOverrideCursor();
        m_docChemin = f;
        if (m_doc->mesh.isEmpty()) return;
        m_vue->setDocument(*m_doc);
        m_selAJour = true;
    }
    const CadDocument &doc = *m_doc;
    m_d["bbMin"] = QJsonArray{doc.bbMin[0], doc.bbMin[1], doc.bbMin[2]};
    m_d["bbMax"] = QJsonArray{doc.bbMax[0], doc.bbMax[1], doc.bbMax[2]};
    const bool av = m_chargement; m_chargement = true;
    majOrigine();
    m_chargement = av;
    const QJsonArray o = m_d["origine"].toArray(), a = m_d["brutMin"].toArray(), b = m_d["brutMax"].toArray();
    if (o.size() == 3 && a.size() == 3 && b.size() == 3)
        m_vue->setRepere(QVector3D(o[0].toDouble(), o[1].toDouble(), o[2].toDouble()),
                         QVector3D(a[0].toDouble(), a[1].toDouble(), a[2].toDouble()),
                         QVector3D(b[0].toDouble(), b[1].toDouble(), b[2].toDouble()));
    if (m_selAJour) {                                    // zone enregistrée du bloc
        m_selAJour = false;
        const QJsonObject opc = opCourante();
        QSet<int> faces;
        for (const QJsonValue &x : opc["facesZone"].toArray()) faces.insert(x.toInt());
        m_vue->setFacesChoisies(faces);
        QVector<QVector<QVector3D>> cts;
        for (const QJsonValue &ct : opc["contoursZone"].toArray()) {
            QVector<QVector3D> pts;
            for (const QJsonValue &p : ct.toArray()) {
                const QJsonArray q = p.toArray();
                pts << QVector3D(float(q[0].toDouble()), float(q[1].toDouble()), float(q[2].toDouble()));
            }
            cts << pts;
        }
        m_vue->setContoursChoisis(cts);
        QSet<int> ex;
        for (const QJsonValue &x : m_d["facesExclues"].toArray()) ex.insert(x.toInt());
        m_vue->setFacesExclues(ex);
        m_vue->setSelectionFaces(true);
        m_vue->setModeExclure(m_bModeExcl && m_bModeExcl->isChecked());
    }
    if (m_couchesCachees) { cacherCouches(); return; }
    // Couches en direct : ébauche (orange) de l'opération d'ébauche (la courante,
    // sinon la première) ; finition (vert) de l'opération de finition (la
    // courante, sinon la première), dans sa zone ; pavé de surfaçage (bleu).
    const QJsonObject tout = donnees();
    const QJsonArray ops = tout["operations"].toArray();
    const bool courEb = m_opCour >= 0 && m_opCour < ops.size() && Usinage3DGenerator::estEbauche(ops[m_opCour].toObject());
    const int iEb = courEb ? m_opCour : premiereOp(true);
    const int iFin = (!courEb && m_opCour >= 0 && m_opCour < ops.size()) ? m_opCour : premiereOp(false);
    if (std::fabs(m_zBasModele) < 1e-9 && opCourante()["facesZone"].toArray().isEmpty() && opCourante()["contoursZone"].toArray().isEmpty())
        m_zBasModele = doc.bbMin[2];
    auto rayon = [](const QJsonObject &op) -> double {
        bool okT = false; const int t = op["outil"].toString().trimmed().toInt(&okT);
        if (okT && t > 0) if (const ToolData *td = ToolDatabase::instance().toolByT(t)) return double(td->Routil);
        return 0.0;
    };
    QApplication::setOverrideCursor(Qt::WaitCursor);
    Usinage3DPlan plEb, plFin;
    if (iEb >= 0) plEb = Usinage3DGenerator::plan(Usinage3DGenerator::pourOperation(tout, ops[iEb].toObject()), doc, rayon(ops[iEb].toObject()));
    QVector<Couche3D> fin;
    if (iFin >= 0) {
        const QJsonObject opF = ops[iFin].toObject();
        plFin = Usinage3DGenerator::plan(Usinage3DGenerator::pourOperation(tout, opF), doc, 0.0);
        double pas = qMax(0.1, opF["passe"].toDouble(0.5));
        const double zTop = doc.bbMax[2];
        while ((zTop - plFin.zFin) / pas > 80) pas *= 2;     // lisible : 80 lignes au plus
        QVector<QPair<double, bool>> niv;
        for (double z = zTop - pas * 0.5; z > plFin.zFin + 1e-6; z -= pas) niv << qMakePair(z, false);
        fin = Decoupe3D::couchesAuxNiveaux(doc.mesh, niv);
        const Contours2D lim = plFin.zone.isEmpty() ? plFin.limite
                             : Decoupe3D::intersection(plFin.limite, Decoupe3D::decaler(plFin.zone, rayon(opF)));
        for (Couche3D &c : fin) c.ombre = Decoupe3D::intersection(c.ombre, lim);
    }
    QApplication::restoreOverrideCursor();
    m_vue->setCouches(iEb >= 0 ? plEb.couches : QVector<Couche3D>(), fin);
    const double zSurf = doc.bbMax[2] + (iEb >= 0 ? ops[iEb].toObject()["surepFond"].toDouble(0.2) : 0.0);
    if (iEb >= 0 && plEb.brutMax[2] > zSurf + 1e-3 && !plEb.limite.isEmpty())
        m_vue->setPave(plEb.limite.first(), zSurf, plEb.brutMax[2]);
    else
        m_vue->setPave({}, 0, 0);
    m_vue->setAffichage(m_cbEb->isChecked(), m_cbFin->isChecked(), m_cbPave->isChecked());
    majListeOps();                                       // avertissements (le modèle est chargé)
    const double oz = o.size() == 3 ? o[2].toDouble() : 0.0;
    if (iEb >= 0)
        m_infoCouches->setText(QString::fromUtf8("%1 couches, de Z %2 à Z %3 (vue 3D à droite)")
            .arg(plEb.couches.size())
            .arg(plEb.couches.isEmpty() ? 0.0 : plEb.couches.first().z - oz, 0, 'f', 2)
            .arg(plEb.couches.isEmpty() ? 0.0 : plEb.couches.last().z - oz, 0, 'f', 2));
}

// Zone cliquée dans la vue intégrée -> bloc (faces, contours, fonds, Z bas)
void Usinage3DEditor::zoneDepuisVue()
{
    if (!m_vue || m_chargement) return;
    QJsonArray fa;
    for (int f : m_vue->facesChoisies()) fa.append(f);
    QJsonArray ca;
    for (const auto &ct : m_vue->contoursChoisis()) {
        QJsonArray pa;
        for (const QVector3D &p : ct)
            pa.append(QJsonArray{qRound(p.x() * 1000.0) / 1000.0, qRound(p.y() * 1000.0) / 1000.0,
                                 qRound(p.z() * 1000.0) / 1000.0});
        ca.append(pa);
    }
    if (m_opCour >= 0 && m_opCour < m_ops.size()) {       // zone de l'opération choisie
        QJsonObject o = m_ops[m_opCour].toObject();
        o["facesZone"] = fa; o["contoursZone"] = ca;
        m_ops[m_opCour] = o;
    }
    QJsonArray ex;
    for (int f : m_vue->facesExclues()) ex.append(f);
    m_d["facesExclues"] = ex;
    m_d.remove("facesFonds"); m_d.remove("zFonds");
    QList<double> zf = m_vue->zFondsPlats();
    std::sort(zf.begin(), zf.end(), std::greater<double>());
    QJsonArray za;
    for (double z : zf) za.append(qRound(z * 1000.0) / 1000.0);
    m_d["zFondsAuto"] = za;
    if (!m_vue->selectionVide()) m_zBasModele = m_vue->zBasSelection();   // Z bas rempli tout seul
    majOrigine();
    majInfoFonds();
    majListeOps();
    cacherCouches();
    emit dataChanged();
}
