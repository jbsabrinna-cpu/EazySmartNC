#pragma execution_character_set("utf-8")
#include "blkmodele3d.h"
#include "cadimporter.h"
#include "cadview3d.h"
#include "cycles/usinage3dgenerator.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QStackedWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QDialog>
#include <QApplication>
#include <QJsonArray>
#include <QLocale>
#include <QVector3D>
#include <QWheelEvent>
#include <memory>
#include <array>
#include <cmath>
#include "eazyfiledialog.h"

namespace {
// Champ qui accepte le point et la virgule, molette sans effet
struct ChampPoint : QDoubleSpinBox {
    ChampPoint() { setLocale(QLocale::c()); setFocusPolicy(Qt::StrongFocus); }
    QValidator::State validate(QString &t, int &p) const override { t.replace(',', '.'); return QDoubleSpinBox::validate(t, p); }
    void wheelEvent(QWheelEvent *e) override { e->ignore(); }
};
QLabel *titre(const QString &t)
{
    auto *l = new QLabel(t);
    l->setStyleSheet("color:#2a6a2a;font-weight:bold;font-size:12pt;padding-top:8px;");
    return l;
}
QLabel *aide(const QString &t)
{
    auto *l = new QLabel(t);
    l->setWordWrap(true);
    l->setStyleSheet("color:#606060;font-size:10pt;");
    return l;
}
// boîte (repère machine) -> boîte alignée dans le repère du STEP
void boiteVersModele(const Orientation3D &o, const double *mn, const double *mx, QVector3D &a, QVector3D &b)
{
    a = QVector3D(1e30f, 1e30f, 1e30f); b = QVector3D(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < 8; ++i) {
        const QVector3D q = o.versModele(QVector3D(i & 1 ? mx[0] : mn[0], i & 2 ? mx[1] : mn[1], i & 4 ? mx[2] : mn[2]));
        for (int k = 0; k < 3; ++k) { a[k] = qMin(a[k], q[k]); b[k] = qMax(b[k], q[k]); }
    }
}
// axe du STEP porté par l'axe machine k, et son signe
void axeModele(const Orientation3D &o, int k, int &a, int &s)
{
    a = 0; s = 1;
    for (int j = 0; j < 3; ++j) if (o.m[k][j] != 0) { a = j; s = o.m[k][j] > 0 ? 1 : -1; }
}
double r3(double v) { return qRound(v * 1000.0) / 1000.0; }
const char *kCouleurs[3] = {"#E04040", "#30B030", "#4070F0"};
}

BlkModele3DWidget::BlkModele3DWidget(QWidget *parent) : QWidget(parent)
{
    setStyleSheet("BlkModele3DWidget{background:#D6D6D6;} QLabel{font-size:12pt;}"
                  "QPushButton{font-size:12pt;padding:6px 14px;}");
    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(30, 20, 30, 20);
    vl->setSpacing(6);

    auto *t = new QLabel("MODÈLE 3D — MISE EN PLACE DU PROGRAMME");
    t->setStyleSheet("color:#303030;font-size:12pt;font-weight:bold;letter-spacing:2px;");
    vl->addWidget(t);
    vl->addWidget(aide("Choisissez le STEP puis « Mise en place 3D » : sens des axes, origine et brut. "
                       "« Calculer et figer » remplit le BLK FORM et l'origine ne bouge plus."));

    {
        auto *h = new QHBoxLayout;
        m_etat = new QLabel;
        m_btnFiger = new QPushButton("Calculer et figer");
        m_btnFiger->setMinimumHeight(40);
        m_btnFiger->setStyleSheet("QPushButton{background:#2a7a2a;color:white;font-weight:bold;font-size:12pt;padding:6px 16px;}");
        m_btnDebloquer = new QPushButton("Débloquer (modifier)");
        m_btnDebloquer->setMinimumHeight(40);
        h->addWidget(m_etat, 1); h->addWidget(m_btnDebloquer); h->addWidget(m_btnFiger);
        vl->addLayout(h);
        connect(m_btnFiger, &QPushButton::clicked, this, [this]{ calculerEtFiger(); });
        connect(m_btnDebloquer, &QPushButton::clicked, this, &BlkModele3DWidget::debloquer);
    }

    vl->addWidget(titre("MODÈLE"));
    {
        auto *h = new QHBoxLayout;
        m_fichier = new QLineEdit; m_fichier->setReadOnly(true);
        m_fichier->setPlaceholderText("fichier STEP ou IGES");
        m_fichier->setStyleSheet("font-size:12pt;background:#FFFFFF;color:#0000CC;padding:3px;");
        m_btnChoisir = new QPushButton("Choisir…");
        m_btnMep = new QPushButton("Mise en place 3D…");
        m_btnMep->setStyleSheet("QPushButton{font-weight:bold;}");
        h->addWidget(m_fichier, 1); h->addWidget(m_btnChoisir); h->addWidget(m_btnMep);
        vl->addLayout(h);
        connect(m_btnChoisir, &QPushButton::clicked, this, &BlkModele3DWidget::choisirFichier);
        connect(m_btnMep, &QPushButton::clicked, this, &BlkModele3DWidget::miseEnPlace);
    }
    m_info = new QLabel; m_info->setStyleSheet("color:#505050;font-size:10pt;");
    vl->addWidget(m_info);

    vl->addWidget(titre("RÉGLAGES"));
    m_resume = new QLabel; m_resume->setWordWrap(true);
    m_resume->setStyleSheet("color:#303030;font-size:11pt;");
    vl->addWidget(m_resume);
    m_resultat = new QLabel; m_resultat->setWordWrap(true);
    m_resultat->setStyleSheet("color:#205080;font-size:11pt;padding-top:6px;");
    vl->addWidget(m_resultat);
    vl->addStretch();

    charger(QJsonObject());
}

// ── Calcul : pièce, brut et origine dans le repère machine ───────
void BlkModele3DWidget::calculer(const CadDocument &doc, double pMin[3], double pMax[3],
                                 double o[3], double bMin[3], double bMax[3])
{
    const Orientation3D or_ = Orientation3D::depuis(m_d["orient"].toArray());
    for (int k = 0; k < 3; ++k) { pMin[k] = 1e30; pMax[k] = -1e30; }
    for (int i = 0; i < 8; ++i) {
        const QVector3D q = or_.versMachine(QVector3D(i & 1 ? doc.bbMax[0] : doc.bbMin[0],
                                                      i & 2 ? doc.bbMax[1] : doc.bbMin[1],
                                                      i & 4 ? doc.bbMax[2] : doc.bbMin[2]));
        for (int k = 0; k < 3; ++k) { pMin[k] = qMin(pMin[k], double(q[k])); pMax[k] = qMax(pMax[k], double(q[k])); }
    }
    QJsonObject tmp = m_d; tmp["origMode"] = 0;
    Usinage3DGenerator::origineEtBrut(tmp, pMin, pMax, o, bMin, bMax);     // brut seulement

    // Origine gardée dans le repère du STEP : elle suit la pièce quand les axes tournent
    QVector3D om;
    const QJsonArray pt = m_d["origPtModele"].toArray();
    if (m_d["origSrc"].toString() == "piece" && pt.size() == 3) {
        om = or_.versMachine(QVector3D(pt[0].toDouble(), pt[1].toDouble(), pt[2].toDouble()));
    } else {
        QJsonArray id = m_d["origBoite"].toArray();
        if (id.size() != 3) id = QJsonArray{0, 2, 2};
        QVector3D a, b; boiteVersModele(or_, bMin, bMax, a, b);
        if (m_d["origSrc"].toString() == "pboite") {               // encombrement de la pièce
            a = QVector3D(doc.bbMin[0], doc.bbMin[1], doc.bbMin[2]);
            b = QVector3D(doc.bbMax[0], doc.bbMax[1], doc.bbMax[2]);
        }
        QVector3D q;
        for (int k = 0; k < 3; ++k) q[k] = a[k] + 0.5f * id[k].toInt() * (b[k] - a[k]);
        om = or_.versMachine(q);
    }
    for (int k = 0; k < 3; ++k) o[k] = r3(om[k]);
    // Pour les blocs (Usinage 3D…) : origine machine directe
    m_d["origMode"] = 1;
    m_d["origPoint"] = QJsonArray{o[0], o[1], o[2]};
    m_d["origRef3"] = QJsonArray{"valeur", "valeur", "valeur"};
}

QString BlkModele3DWidget::texteAxes() const
{
    const Orientation3D or_ = Orientation3D::depuis(m_d["orient"].toArray());
    if (or_.identite()) return QString::fromUtf8("comme le STEP");
    QStringList t;
    for (int k = 0; k < 3; ++k) {
        int a, s; axeModele(or_, k, a, s);
        t << QString("%1+ = %2%3").arg(QChar("XYZ"[k])).arg(s > 0 ? "+" : "−").arg(QChar("XYZ"[a]));
    }
    return t.join("   ") + QString::fromUtf8(" du STEP");
}

QString BlkModele3DWidget::texteOrigine() const
{
    const QJsonArray pt = m_d["origPtModele"].toArray();
    if (m_d["origSrc"].toString() == "piece" && pt.size() == 3)
        return QString::fromUtf8("point de la pièce (STEP  X %1  Y %2  Z %3)")
            .arg(pt[0].toDouble(), 0, 'f', 3).arg(pt[1].toDouble(), 0, 'f', 3).arg(pt[2].toDouble(), 0, 'f', 3);
    QJsonArray id = m_d["origBoite"].toArray();
    if (id.size() != 3) id = QJsonArray{0, 2, 2};
    const Orientation3D or_ = Orientation3D::depuis(m_d["orient"].toArray());
    QStringList t;
    for (int k = 0; k < 3; ++k) {
        int a, s; axeModele(or_, k, a, s);
        const int i = s > 0 ? id[a].toInt() : 2 - id[a].toInt();
        if (k < 2) t << QString("%1%2").arg(QChar("XY"[k])).arg(i == 0 ? "−" : i == 2 ? "+" : " milieu");
        else       t << (i == 0 ? QString::fromUtf8("Z dessous") : i == 2 ? QString::fromUtf8("Z dessus") : QString::fromUtf8("Z milieu"));
    }
    return (m_d["origSrc"].toString() == "pboite" ? QString::fromUtf8("boîte de la pièce : ")
                                                   : QString::fromUtf8("boîte du brut : ")) + t.join("  ");
}

QString BlkModele3DWidget::texteBrut() const
{
    auto f = [](double v){ return QString::number(v, 'f', 2); };
    if (m_d["brutMode"].toInt(0) == 0)
        return QString::fromUtf8("autour de la pièce : X ±%1  Y ±%2  dessus %3  dessous %4")
            .arg(f(m_d["brutSx"].toDouble()), f(m_d["brutSy"].toDouble()), f(m_d["brutZp"].toDouble()), f(m_d["brutZm"].toDouble()));
    return QString::fromUtf8("fixe %1 × %2 × %3, pièce %4")
        .arg(f(m_d["brutL"].toDouble()), f(m_d["brutW"].toDouble()), f(m_d["brutH"].toDouble()))
        .arg(m_d["brutZal"].toInt(0) == 1 ? QString::fromUtf8("en bas") : QString::fromUtf8("en haut"));
}

void BlkModele3DWidget::majEtat()
{
    m_btnFiger->setVisible(!m_verrou);
    m_btnDebloquer->setVisible(m_verrou);
    m_btnChoisir->setEnabled(!m_verrou);
    m_btnMep->setText(m_verrou ? QString::fromUtf8("Voir en 3D…") : QString::fromUtf8("Mise en place 3D…"));
    m_etat->setText(m_verrou ? QString::fromUtf8("🔒  Origine figée")
                             : QString::fromUtf8("Origine libre — pas encore figée"));
    m_etat->setStyleSheet(m_verrou ? "color:#2a7a2a;font-weight:bold;font-size:12pt;"
                                   : "color:#a06000;font-weight:bold;font-size:12pt;");
    const QJsonArray mi = m_d["bbMin"].toArray(), ma = m_d["bbMax"].toArray();
    m_info->setText(mi.size() == 3 ? QString::fromUtf8("Pièce (machine) %1 × %2 × %3 mm")
                        .arg(ma[0].toDouble() - mi[0].toDouble(), 0, 'f', 2)
                        .arg(ma[1].toDouble() - mi[1].toDouble(), 0, 'f', 2)
                        .arg(ma[2].toDouble() - mi[2].toDouble(), 0, 'f', 2)
                    : (m_fichier->text().isEmpty() ? QString("aucun fichier") : QString::fromUtf8("pas encore calculé")));
    m_resume->setText(QString::fromUtf8("<b>Axes :</b> %1<br><b>Origine :</b> %2<br><b>Brut :</b> %3")
                      .arg(texteAxes(), texteOrigine(), texteBrut()));
    const QJsonArray o = m_d["origine"].toArray(), a = m_d["brutMin"].toArray(), b = m_d["brutMax"].toArray();
    if (m_verrou && o.size() == 3 && a.size() == 3 && b.size() == 3) {
        auto f = [](double v){ return QString::number(v, 'f', 3); };
        m_resultat->setText(QString::fromUtf8("Brut dans le repère pièce :  X %1 / %2   Y %3 / %4   Z %5 / %6")
            .arg(f(a[0].toDouble() - o[0].toDouble()), f(b[0].toDouble() - o[0].toDouble()),
                 f(a[1].toDouble() - o[1].toDouble()), f(b[1].toDouble() - o[1].toDouble()),
                 f(a[2].toDouble() - o[2].toDouble()), f(b[2].toDouble() - o[2].toDouble())));
    } else m_resultat->clear();
}

void BlkModele3DWidget::choisirFichier()
{
    const QString dep = m_fichier->text().isEmpty() ? QStringLiteral("C:/cncm/ncfiles")
                                                    : QFileInfo(m_fichier->text()).absolutePath();
    const QString f = EazyFileDialog::ouvrir(this, "Modèle 3D", dep, {"step", "stp", "iges", "igs"}, "STEP");
    if (f.isEmpty()) return;
    m_fichier->setText(f);
    for (const char *k : {"bbMin", "bbMax", "origine", "brutMin", "brutMax", "origPtModele"}) m_d.remove(QString::fromLatin1(k));
    m_d["origSrc"] = "boite";
    majEtat();
    emit dataChanged();
    miseEnPlace();                 // le STEP choisi s'ouvre directement
}

void BlkModele3DWidget::calculerEtFiger(const CadDocument *docDonne)
{
    if (m_fichier->text().isEmpty()) {
        QMessageBox::information(this, "Modèle 3D", "Choisissez d'abord le fichier STEP ou IGES.");
        return;
    }
    CadDocument docLu;
    if (!docDonne) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        docLu = CadImporter::load(m_fichier->text());
        QApplication::restoreOverrideCursor();
        docDonne = &docLu;
    }
    if (docDonne->mesh.isEmpty()) {
        QMessageBox::warning(this, "Modèle 3D", "Impossible de lire le modèle 3D.");
        return;
    }
    double pMin[3], pMax[3], o[3], bMin[3], bMax[3];
    calculer(*docDonne, pMin, pMax, o, bMin, bMax);
    auto arr = [](const double *v){ return QJsonArray{v[0], v[1], v[2]}; };
    m_d["bbMin"] = arr(pMin); m_d["bbMax"] = arr(pMax);
    m_d["origine"] = arr(o); m_d["brutMin"] = arr(bMin); m_d["brutMax"] = arr(bMax);
    m_verrou = true;
    majEtat();
    emit brutCalcule(r3(bMin[0] - o[0]), r3(bMax[0] - o[0]), r3(bMin[1] - o[1]), r3(bMax[1] - o[1]),
                     r3(bMin[2] - o[2]), r3(bMax[2] - o[2]));
    emit verrouChange(true);
    emit dataChanged();
}

void BlkModele3DWidget::debloquer()
{
    if (QMessageBox::question(this, "Origine figée",
            "L'origine est figée : les blocs du programme sont calculés à partir d'elle.\n"
            "La débloquer pour la modifier ?") != QMessageBox::Yes) return;
    m_verrou = false;
    majEtat();
    emit verrouChange(false);
    emit dataChanged();
}

// ═══════════════════════════════════════════════════════════════
//  Page de mise en place (vue 3D du STEP + 3 étapes)
// ═══════════════════════════════════════════════════════════════
void BlkModele3DWidget::miseEnPlace()
{
    if (m_fichier->text().isEmpty()) { choisirFichier(); return; }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const CadDocument doc = CadImporter::load(m_fichier->text());     // repère du STEP
    QApplication::restoreOverrideCursor();
    if (doc.mesh.isEmpty()) { QMessageBox::warning(this, "Modèle 3D", "Impossible de lire le modèle 3D."); return; }

    QDialog dlg(this);
    dlg.setWindowTitle("Mise en place — " + QFileInfo(m_fichier->text()).fileName());
    dlg.setStyleSheet("QLabel{font-size:11pt;} QPushButton{font-size:11pt;padding:5px 10px;}"
                      "QRadioButton{font-size:11pt;} QComboBox{font-size:11pt;}");
    auto *hl = new QHBoxLayout(&dlg); hl->setContentsMargins(4, 4, 4, 4);
    auto *gauche = new QWidget; gauche->setFixedWidth(400);
    auto *gl = new QVBoxLayout(gauche); gl->setContentsMargins(6, 6, 6, 6); gl->setSpacing(6);
    auto *v = new CadView3D(&dlg);
    hl->addWidget(gauche); hl->addWidget(v, 1);
    v->setDocument(doc);

    auto *etat = new QLabel; etat->setStyleSheet("font-size:12pt;font-weight:bold;");
    gl->addWidget(etat);

    // Étapes
    auto *grpEt = new QButtonGroup(&dlg);
    {
        auto *h = new QHBoxLayout;
        const char *noms[3] = {"1  Axes", "2  Origine", "3  Brut"};
        for (int i = 0; i < 3; ++i) {
            auto *b = new QPushButton(QString::fromUtf8(noms[i]));
            b->setCheckable(true); b->setMinimumHeight(40);
            b->setStyleSheet("QPushButton{font-size:12pt;font-weight:bold;background:#E8E8E8;}"
                             "QPushButton:checked{background:#3070E0;color:white;}");
            grpEt->addButton(b, i); h->addWidget(b);
        }
        grpEt->setExclusive(true);
        gl->addLayout(h);
    }
    auto *pile = new QStackedWidget;
    gl->addWidget(pile);

    // ── 1. Axes ──
    auto *msgAxes = new QLabel; msgAxes->setWordWrap(true);
    auto *grpAx = new QButtonGroup(&dlg);
    QPushButton *bInvX = nullptr, *bInvZ = nullptr, *bRaz = nullptr;
    {
        auto *w = new QWidget; auto *l = new QVBoxLayout(w); l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(aide("Cliquez X+, Y+ ou Z+ puis la face de la boîte orange vers laquelle l'axe doit aller. "
                          "Le 3e axe est déduit. L'origine ne bouge pas."));
        auto *h = new QHBoxLayout;
        const char *noms[3] = {"X+", "Y+", "Z+"};
        for (int k = 0; k < 3; ++k) {
            auto *b = new QPushButton(QString::fromLatin1(noms[k]));
            b->setCheckable(true); b->setMinimumHeight(38);
            b->setStyleSheet(QString("QPushButton{color:%1;font-weight:bold;font-size:13pt;}"
                                     "QPushButton:checked{background:%1;color:white;}").arg(kCouleurs[k]));
            grpAx->addButton(b, k); h->addWidget(b);
        }
        grpAx->setExclusive(true);
        l->addLayout(h);
        auto *h2 = new QHBoxLayout;
        bInvX = new QPushButton("Inverser X"); bInvZ = new QPushButton("Inverser Z"); bRaz = new QPushButton("Remettre");
        h2->addWidget(bInvX); h2->addWidget(bInvZ); h2->addWidget(bRaz);
        l->addLayout(h2);
        l->addWidget(msgAxes);
        l->addStretch();
        pile->addWidget(w);
    }
    // ── 2. Origine ──
    auto *rBoite = new QRadioButton("Boîte du brut");
    auto *rPBoite = new QRadioButton("Boîte de la pièce");
    auto *rPiece = new QRadioButton("Point sur la pièce");
    auto *lblOrig = new QLabel; lblOrig->setWordWrap(true); lblOrig->setStyleSheet("color:#205080;font-weight:bold;");
    auto *grpPave = new QButtonGroup(&dlg);
    auto *grpZ = new QButtonGroup(&dlg);
    auto *pave = new QWidget;
    {
        auto *w = new QWidget; auto *l = new QVBoxLayout(w); l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(rBoite);
        l->addWidget(aide("     croix cyan sur le brut, ou le pavé"));
        l->addWidget(rPBoite);
        l->addWidget(aide("     croix orange sur l'encombrement de la pièce, ou le pavé"));
        l->addWidget(rPiece);
        l->addWidget(aide("     sommet, centre de rayon (croix jaune) ou point d'une face"));
        // Pavé : X/Y vus de dessus en sens machine + Z
        auto *hp = new QHBoxLayout(pave); hp->setContentsMargins(0, 6, 0, 6);
        auto *g = new QGridLayout; g->setSpacing(3);
        static const char *sym[9] = {"↙", "↓", "↘", "←", "●", "→", "↖", "↑", "↗"};
        for (int p = 0; p < 9; ++p) {
            auto *bt = new QPushButton(QString::fromUtf8(sym[p]));
            bt->setCheckable(true); bt->setFixedSize(48, 42);
            bt->setStyleSheet("QPushButton{font-size:16pt;background:#F0F0F0;border:1px solid #909090;padding:0;}"
                              "QPushButton:checked{background:#3070E0;color:white;}");
            grpPave->addButton(bt, p);
            g->addWidget(bt, 2 - p / 3, p % 3);
        }
        grpPave->setExclusive(true);
        hp->addLayout(g);
        auto *vz = new QVBoxLayout; vz->setSpacing(3);
        vz->addWidget(new QLabel("Z :"));
        const char *nz[3] = {"Dessus", "Milieu", "Dessous"};
        for (int i = 0; i < 3; ++i) {
            auto *bt = new QPushButton(QString::fromUtf8(nz[i]));
            bt->setCheckable(true); bt->setMinimumHeight(30);
            bt->setStyleSheet("QPushButton{background:#F0F0F0;border:1px solid #909090;}"
                              "QPushButton:checked{background:#3070E0;color:white;}");
            grpZ->addButton(bt, 2 - i);              // id = indice Z machine (2 dessus)
            vz->addWidget(bt);
        }
        grpZ->setExclusive(true);
        vz->addStretch();
        hp->addLayout(vz); hp->addStretch();
        l->addWidget(pave);
        l->addWidget(lblOrig);
        l->addStretch();
        pile->addWidget(w);
        auto *gr = new QButtonGroup(w); gr->addButton(rBoite); gr->addButton(rPBoite); gr->addButton(rPiece);
        const QString src = m_d["origSrc"].toString();
        (src == "piece" ? rPiece : src == "pboite" ? rPBoite : rBoite)->setChecked(true);
    }
    // Pavé <-> indices de boîte (repère du STEP) via le sens des axes
    auto majPave = [=]{
        QJsonArray id = m_d["origBoite"].toArray();
        if (id.size() != 3) id = QJsonArray{0, 2, 2};
        const Orientation3D or_ = Orientation3D::depuis(m_d["orient"].toArray());
        int im[3];
        for (int k = 0; k < 3; ++k) { int a, s; axeModele(or_, k, a, s); im[k] = s > 0 ? id[a].toInt() : 2 - id[a].toInt(); }
        if (auto *bt = grpPave->button(im[1] * 3 + im[0])) bt->setChecked(true);
        if (auto *bt = grpZ->button(im[2])) bt->setChecked(true);
        pave->setEnabled(!rPiece->isChecked());
    };
    // ── 3. Brut ──
    auto *rRel = new QRadioButton("Autour de la pièce");
    auto *rFix = new QRadioButton("Dimensions fixes");
    auto *form = new QFormLayout;
    auto champ = [this, &dlg](const char *cle, double defaut, double maxi) {
        auto *s = new ChampPoint;
        s->setRange(0, maxi); s->setDecimals(2); s->setSuffix(" mm");
        s->setValue(m_d[QString::fromLatin1(cle)].toDouble(defaut));
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setStyleSheet("QDoubleSpinBox{font-size:12pt;color:#0000CC;background:#FFFFFF;padding:2px 6px;"
                         "border:1px solid #909090;border-radius:3px;}");
        s->setParent(&dlg);
        return s;
    };
    auto *sx = champ("brutSx", 2, 500), *sy = champ("brutSy", 2, 500), *zp = champ("brutZp", 2, 500), *zm = champ("brutZm", 2, 500);
    auto *sL = champ("brutL", 220, 5000), *sW = champ("brutW", 70, 5000), *sH = champ("brutH", 15, 1000);
    auto *lblDims = new QLabel;
    lblDims->setStyleSheet("font-size:12pt;color:#205080;padding-top:8px;");
    auto *cZal = new QComboBox; cZal->addItems({"pièce en haut du brut", "pièce en bas du brut"});
    cZal->setCurrentIndex(m_d["brutZal"].toInt(0));
    {
        auto *w = new QWidget; auto *l = new QVBoxLayout(w); l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(rRel); l->addWidget(rFix);
        form->addRow("Surép. X (chaque côté)", sx);
        form->addRow("Surép. Y (chaque côté)", sy);
        form->addRow("Longueur X", sL);
        form->addRow("Largeur Y", sW);
        form->addRow("Épaisseur Z", sH);
        form->addRow("Position Z", cZal);
        form->addRow("Surép. au-dessus", zp);
        form->addRow("Surép. en dessous", zm);
        l->addLayout(form);
        l->addWidget(lblDims);
        l->addStretch();
        pile->addWidget(w);
        auto *g = new QButtonGroup(w); g->addButton(rRel); g->addButton(rFix);
        (m_d["brutMode"].toInt(0) == 1 ? rFix : rRel)->setChecked(true);
    }
    auto majBrutVisible = [=]{
        const bool fixe = rFix->isChecked();
        for (QWidget *w : {static_cast<QWidget*>(sx), static_cast<QWidget*>(sy), static_cast<QWidget*>(zm)}) form->setRowVisible(w, !fixe);
        for (QWidget *w : {static_cast<QWidget*>(sL), static_cast<QWidget*>(sW), static_cast<QWidget*>(sH), static_cast<QWidget*>(cZal)}) form->setRowVisible(w, fixe);
        form->setRowVisible(zp, !fixe || cZal->currentIndex() == 0);
    };
    majBrutVisible();

    auto *bVoir = new QPushButton("Voir les axes");
    bVoir->setCheckable(true);
    bVoir->setStyleSheet("QPushButton{background:#E8E8E8;color:#202020;}"
                         "QPushButton:checked{background:#3070E0;color:white;font-weight:bold;}");
    gl->addWidget(bVoir);
    auto *resume = new QLabel; resume->setWordWrap(true); resume->setStyleSheet("color:#303030;font-size:10pt;");
    gl->addWidget(resume);
    gl->addStretch();
    auto *bFiger = new QPushButton("Calculer et figer");
    bFiger->setMinimumHeight(44);
    bFiger->setStyleSheet("QPushButton{background:#2a7a2a;color:white;font-weight:bold;font-size:12pt;}");
    auto *bDeb = new QPushButton("Débloquer (modifier)");
    auto *bFermer = new QPushButton("Fermer");
    gl->addWidget(bFiger); gl->addWidget(bDeb); gl->addWidget(bFermer);
    connect(bFermer, &QPushButton::clicked, &dlg, &QDialog::accept);

    // ── Mise à jour de la vue + des textes ──
    auto maj = [=, &doc]{
        double pMin[3], pMax[3], o[3], bMin[3], bMax[3];
        calculer(doc, pMin, pMax, o, bMin, bMax);
        const Orientation3D or_ = Orientation3D::depuis(m_d["orient"].toArray());
        QVector3D ba, bb; boiteVersModele(or_, bMin, bMax, ba, bb);
        v->setAxesMachine(or_.axe(0), or_.axe(1), or_.axe(2));
        v->setRepere(or_.versModele(QVector3D(o[0], o[1], o[2])), ba, bb,
                     QVector3D(doc.bbMin[0], doc.bbMin[1], doc.bbMin[2]), QVector3D(doc.bbMax[0], doc.bbMax[1], doc.bbMax[2]));
        lblOrig->setText(QString::fromUtf8("Origine : ") + texteOrigine());
        majPave();
        lblDims->setText(QString::fromUtf8("Pièce :  <b>%1 × %2 × %3</b> mm<br>Brut total :  <b>%4 × %5 × %6</b> mm")
            .arg(pMax[0] - pMin[0], 0, 'f', 2).arg(pMax[1] - pMin[1], 0, 'f', 2).arg(pMax[2] - pMin[2], 0, 'f', 2)
            .arg(bMax[0] - bMin[0], 0, 'f', 2).arg(bMax[1] - bMin[1], 0, 'f', 2).arg(bMax[2] - bMin[2], 0, 'f', 2));
        resume->setText(QString::fromUtf8("<b>Axes :</b> %1<br><b>Origine :</b> %2<br><b>Brut :</b> %3<br>"
                                          "Pièce %4 × %5 × %6   Brut %7 × %8 × %9 mm")
            .arg(texteAxes(), texteOrigine(), texteBrut())
            .arg(pMax[0] - pMin[0], 0, 'f', 2).arg(pMax[1] - pMin[1], 0, 'f', 2).arg(pMax[2] - pMin[2], 0, 'f', 2)
            .arg(bMax[0] - bMin[0], 0, 'f', 2).arg(bMax[1] - bMin[1], 0, 'f', 2).arg(bMax[2] - bMin[2], 0, 'f', 2));
        majEtat();
    };
    auto change = [this, maj]{ maj(); if (!m_chargement) emit dataChanged(); };

    // Étape -> mode de la vue
    auto etape = [=](int i){
        pile->setCurrentIndex(i);
        if (m_verrou) { v->setModeMiseEnPlace(0); v->setVoirAxes(true); bVoir->setVisible(false); return; }
        v->setModeMiseEnPlace(i == 0 ? 1 : i == 1 ? (rPiece->isChecked() ? 3 : rPBoite->isChecked() ? 4 : 2) : 0);
        bVoir->setVisible(i != 0);
        v->setVoirAxes(i == 0 || bVoir->isChecked());
    };
    auto majVerrou = [=]{
        etat->setText(m_verrou ? QString::fromUtf8("🔒  Origine figée") : QString::fromUtf8("Origine libre"));
        etat->setStyleSheet(m_verrou ? "color:#2a7a2a;font-size:12pt;font-weight:bold;" : "color:#a06000;font-size:12pt;font-weight:bold;");
        pile->setEnabled(!m_verrou);
        bFiger->setVisible(!m_verrou); bDeb->setVisible(m_verrou);
        etape(grpEt->checkedId() < 0 ? 0 : grpEt->checkedId());
    };
    connect(grpEt, &QButtonGroup::idClicked, &dlg, etape);
    connect(bVoir, &QPushButton::toggled, &dlg, [v](bool on){ v->setVoirAxes(on); });

    // Nouveau sens des axes : les cotes du brut tournent avec la pièce
    // (longueur / largeur / épaisseur fixes et surép. X / Y suivent leur axe du STEP)
    auto poserOrient = [=](const Orientation3D &nouv){
        const Orientation3D anc = Orientation3D::depuis(m_d["orient"].toArray());
        const char *cf[3] = {"brutL", "brutW", "brutH"}, *cs[2] = {"brutSx", "brutSy"};
        double f[3], s[2];
        for (int k = 0; k < 3; ++k) f[k] = m_d[QString::fromLatin1(cf[k])].toDouble();
        for (int k = 0; k < 2; ++k) s[k] = m_d[QString::fromLatin1(cs[k])].toDouble();
        QDoubleSpinBox *bf[3] = {sL, sW, sH}, *bs[2] = {sx, sy};
        for (int k = 0; k < 3; ++k) {
            int a, sg; axeModele(nouv, k, a, sg);
            int ka = k; for (int j = 0; j < 3; ++j) if (anc.m[j][a] != 0) ka = j;
            m_d[QString::fromLatin1(cf[k])] = f[ka];
            bf[k]->blockSignals(true); bf[k]->setValue(f[ka]); bf[k]->blockSignals(false);
            if (k < 2 && ka < 2) {
                m_d[QString::fromLatin1(cs[k])] = s[ka];
                bs[k]->blockSignals(true); bs[k]->setValue(s[ka]); bs[k]->blockSignals(false);
            }
        }
        m_d["orient"] = nouv.json();
    };
    // Sens des axes : bouton armé puis face
    auto arme = std::make_shared<int>(-1);
    auto choisi = std::make_shared<std::array<bool, 3>>(std::array<bool, 3>{false, false, false});
    auto dernier = std::make_shared<int>(-1);
    auto desarmer = [grpAx, arme]{
        grpAx->setExclusive(false);
        for (QAbstractButton *b : grpAx->buttons()) b->setChecked(false);
        grpAx->setExclusive(true);
        *arme = -1;
    };
    connect(grpAx, &QButtonGroup::idClicked, &dlg, [arme, msgAxes](int id){
        *arme = id;
        msgAxes->setStyleSheet(QString("color:%1;font-weight:bold;").arg(kCouleurs[id]));
        msgAxes->setText(QString::fromUtf8("%1+ : cliquez une face de la boîte orange").arg(QChar("XYZ"[id])));
    });
    connect(v, &CadView3D::faceBoiteChoisie, &dlg, [=](int, int axeModele, bool plus){
        if (*arme < 0) {
            msgAxes->setStyleSheet("color:#a06000;font-weight:bold;");
            msgAxes->setText(QString::fromUtf8("Choisissez d'abord X+, Y+ ou Z+."));
            return;
        }
        const int j = *arme;
        QVector3D n; n[axeModele] = plus ? 1.0f : -1.0f;
        Orientation3D o = Orientation3D::depuis(m_d["orient"].toArray());
        auto paral = [](const QVector3D &a, const QVector3D &b){ return QVector3D::crossProduct(a, b).lengthSquared() < 0.5f; };
        // axe gardé : le dernier choisi, sinon un autre non parallèle
        QVector<int> cand;
        if (*dernier >= 0 && *dernier != j) cand << *dernier;
        for (int k = 2; k >= 0; --k) if (k != j && !cand.contains(k)) cand << k;
        int garde = -1;
        for (int kk : cand) {
            if (!paral(n, o.axe(kk))) { garde = kk; break; }
            if ((*choisi)[kk]) {
                msgAxes->setStyleSheet("color:#C02020;font-weight:bold;");
                msgAxes->setText(QString::fromUtf8("Refusé : %1+ ne peut pas être dans la même direction que %2.")
                                 .arg(QChar("XYZ"[j])).arg(QChar("XYZ"[kk])));
                return;
            }
        }
        if (garde < 0) return;
        auto pose = [&](int k, const QVector3D &q){ o.m[k][0] = qRound(q.x()); o.m[k][1] = qRound(q.y()); o.m[k][2] = qRound(q.z()); };
        pose(j, n);
        const int t = 3 - j - garde;
        pose(t, QVector3D::crossProduct(o.axe((t + 1) % 3), o.axe((t + 2) % 3)));
        poserOrient(o);
        (*choisi)[j] = true; *dernier = j;
        desarmer();
        msgAxes->setStyleSheet("color:#2a7a2a;font-weight:bold;");
        msgAxes->setText(QString::fromUtf8("%1+ placé, %2+ déduit.").arg(QChar("XYZ"[j])).arg(QChar("XYZ"[t])));
        change();
    });
    auto inverser = [=](int k){                   // demi-tour autour de l'autre axe
        Orientation3D o = Orientation3D::depuis(m_d["orient"].toArray());
        for (int c = 0; c < 3; ++c) { o.m[k][c] = -o.m[k][c]; o.m[1][c] = -o.m[1][c]; }
        m_d["orient"] = o.json();
        desarmer(); msgAxes->clear();
        change();
    };
    connect(bInvX, &QPushButton::clicked, &dlg, [=]{ inverser(0); });
    connect(bInvZ, &QPushButton::clicked, &dlg, [=]{ inverser(2); });
    connect(bRaz, &QPushButton::clicked, &dlg, [=]{
        poserOrient(Orientation3D());
        *choisi = {false, false, false}; *dernier = -1;
        desarmer(); msgAxes->clear();
        change();
    });

    // Origine
    auto srcRadio = [=]{
        m_d["origSrc"] = rPiece->isChecked() ? "piece" : rPBoite->isChecked() ? "pboite" : "boite";
        etape(1); change();
    };
    for (QRadioButton *r : {rBoite, rPBoite, rPiece})
        connect(r, &QRadioButton::toggled, &dlg, [=](bool on){ if (on) srcRadio(); });
    connect(v, &CadView3D::pointBoiteChoisi, &dlg, [=](int ix, int iy, int iz){
        m_d["origBoite"] = QJsonArray{ix, iy, iz};
        change();
    });
    auto depuisPave = [=]{
        const int p = grpPave->checkedId(), z = grpZ->checkedId();
        if (p < 0 || z < 0) return;
        const int im[3] = {p % 3, p / 3, z};
        const Orientation3D or_ = Orientation3D::depuis(m_d["orient"].toArray());
        QJsonArray id{0, 0, 0};
        for (int k = 0; k < 3; ++k) { int a, s; axeModele(or_, k, a, s); id[a] = s > 0 ? im[k] : 2 - im[k]; }
        m_d["origBoite"] = id;
        change();
    };
    connect(grpPave, &QButtonGroup::idClicked, &dlg, [=]{ depuisPave(); });
    connect(grpZ, &QButtonGroup::idClicked, &dlg, [=]{ depuisPave(); });
    connect(v, &CadView3D::pointChoisi, &dlg, [=](const QVector3D &p){
        if (!rPiece->isChecked()) return;
        m_d["origSrc"] = "piece";
        m_d["origPtModele"] = QJsonArray{r3(p.x()), r3(p.y()), r3(p.z())};
        change();
    });

    // Brut
    auto lier = [=, &dlg](QDoubleSpinBox *s, const char *cle){
        connect(s, &QDoubleSpinBox::valueChanged, &dlg, [=](double x){ m_d[QString::fromLatin1(cle)] = x; change(); });
    };
    lier(sx, "brutSx"); lier(sy, "brutSy"); lier(zp, "brutZp"); lier(zm, "brutZm");
    lier(sL, "brutL"); lier(sW, "brutW"); lier(sH, "brutH");
    connect(rFix, &QRadioButton::toggled, &dlg, [=, &doc](bool f){
        if (f && !m_d.contains("brutFixeInit")) {          // 1re fois : dimensions du brut actuel
            double pMin[3], pMax[3], o[3], bMin[3], bMax[3];
            m_d["brutMode"] = 0;
            calculer(doc, pMin, pMax, o, bMin, bMax);
            m_d["brutFixeInit"] = true;
            sL->setValue(std::ceil(bMax[0] - bMin[0])); sW->setValue(std::ceil(bMax[1] - bMin[1]));
            sH->setValue(std::ceil(bMax[2] - bMin[2]));
        }
        m_d["brutMode"] = f ? 1 : 0; majBrutVisible(); change();
    });
    connect(cZal, &QComboBox::currentIndexChanged, &dlg, [=](int i){ m_d["brutZal"] = i; majBrutVisible(); change(); });

    // Figer / débloquer
    connect(bFiger, &QPushButton::clicked, &dlg, [=, &doc]{ calculerEtFiger(&doc); majVerrou(); });
    connect(bDeb, &QPushButton::clicked, &dlg, [=]{ debloquer(); majVerrou(); });

    grpEt->button(0)->setChecked(true);
    maj();
    majVerrou();
    dlg.resize(1300, 800);
    dlg.setWindowState(Qt::WindowMaximized);
    dlg.exec();
    majEtat();
}

// ── Chargement (avec reprise des anciens réglages) ──────────────
void BlkModele3DWidget::charger(const QJsonObject &d)
{
    m_chargement = true;
    m_d = d;
    m_fichier->setText(d["fichier"].toString());
    const Orientation3D or_ = Orientation3D::depuis(m_d["orient"].toArray());
    if (!m_d.contains("origSrc")) {
        const QJsonArray p = m_d["origPoint"].toArray();
        if (m_d["origMode"].toInt(0) == 1 && p.size() == 3) {          // ancien point 3D (machine)
            const QVector3D q = or_.versModele(QVector3D(p[0].toDouble(), p[1].toDouble(), p[2].toDouble()));
            m_d["origSrc"] = "piece";
            m_d["origPtModele"] = QJsonArray{r3(q.x()), r3(q.y()), r3(q.z())};
        } else {                                                       // coin du brut, en haut à gauche
            const int pos = m_d["origPos"].toInt(7);
            const int im[3] = {(pos - 1) % 3, (pos - 1) / 3, 2};
            QJsonArray id{0, 0, 0};
            for (int k = 0; k < 3; ++k) { int a, s; axeModele(or_, k, a, s); id[a] = s > 0 ? im[k] : 2 - im[k]; }
            m_d["origSrc"] = "boite";
            m_d["origBoite"] = id;
        }
    }
    if (!m_d.contains("brutSx")) {                                     // valeurs par défaut : 2 mm autour
        m_d["brutMode"] = 0;
        m_d["brutSx"] = 2.0; m_d["brutSy"] = 2.0; m_d["brutZp"] = 2.0; m_d["brutZm"] = 2.0;
        m_d["brutL"] = 220.0; m_d["brutW"] = 70.0; m_d["brutH"] = 15.0;
    }
    m_verrou = d["verrou"].toBool(false);
    majEtat();
    m_chargement = false;
}

QJsonObject BlkModele3DWidget::donnees() const
{
    QJsonObject d = m_d;
    d["fichier"] = m_fichier->text();
    d["verrou"]  = m_verrou;
    return d;
}
