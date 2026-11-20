#pragma execution_character_set("utf-8")
#include "calculdialog.h"
#include "progression.h"
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <cmath>

// ── Scène : brut L×W×H, NC couches, îlot qui s'élargit en descendant ──
namespace {
const int    NC = 6;
const double L = 180, W = 110, HT = 60, DZ = HT / NC;
struct Ile { double x0, x1, y0, y1; };
Ile ile(int k)
{
    const double t = double(k + 1) / NC;
    return { L * 0.5 - L * 0.42 * t - 6, L * 0.5 + L * 0.42 * t + 6,
             W * 0.5 - W * 0.38 * t - 4, W * 0.5 + W * 0.38 * t + 4 };
}
QPointF isoBrut(double x, double y, double z)     // projection isométrique (unités scène)
{
    const double cs = std::cos(M_PI / 6), sn = std::sin(M_PI / 6);
    return QPointF((x - y) * cs, (x + y) * sn - z * 1.2);
}
}

AnimFraise3D::AnimFraise3D(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(360, 240);
    nouvelleCouche();
}

void AnimFraise3D::nouvelleCouche()
{
    const Ile r = ile(m_k);
    const double m = 8, pas = 14;
    QVector<QPointF> p;
    for (int s = 0; s < 6; ++s) {
        const double x0 = m + s * pas, x1 = L - m - s * pas, y0 = m + s * pas, y1 = W - m - s * pas;
        if (x0 > r.x0 - 10 || y0 > r.y0 - 10) break;
        p << QPointF(x0, y0) << QPointF(x1, y0) << QPointF(x1, y1) << QPointF(x0, y1) << QPointF(x0, y0 + pas);
    }
    p << QPointF(r.x0 - 9, r.y0 - 9) << QPointF(r.x1 + 9, r.y0 - 9) << QPointF(r.x1 + 9, r.y1 + 9)
      << QPointF(r.x0 - 9, r.y1 + 9) << QPointF(r.x0 - 9, r.y0 - 9);
    m_chemin.clear();
    for (int i = 0; i + 1 < p.size(); ++i) {
        const QPointF a = p[i], b = p[i + 1];
        const int n = qMax(1, int(std::hypot(b.x() - a.x(), b.y() - a.y()) / 2.5));
        for (int q = 0; q < n; ++q) m_chemin << a + (b - a) * (double(q) / n);
    }
    m_trace.clear();
    m_i = 0;
}

void AnimFraise3D::avancer()
{
    if (m_chemin.isEmpty()) return;
    m_trace << m_chemin[m_i];
    m_i += 1; m_ang += 0.3;
    if (m_i >= m_chemin.size()) { m_k = (m_k + 1) % NC; nouvelleCouche(); }
    update();
}

void AnimFraise3D::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#f4f6f9"));
    p.setPen(QColor("#d0d6de"));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    // Centrage : encombrement projeté du brut + de la fraise (70 au-dessus)
    double xmin = 1e9, xmax = -1e9, ymin = 1e9, ymax = -1e9;
    for (double x : {0.0, L}) for (double y : {0.0, W}) for (double z : {0.0, HT + 70.0}) {
        const QPointF q = isoBrut(x, y, z);
        xmin = qMin(xmin, q.x()); xmax = qMax(xmax, q.x()); ymin = qMin(ymin, q.y()); ymax = qMax(ymax, q.y());
    }
    const double marge = 14;
    const double s = qMin((width() - 2 * marge) / (xmax - xmin), (height() - 2 * marge) / (ymax - ymin));
    const double ox = width() / 2.0 - s * (xmin + xmax) / 2, oy = height() / 2.0 - s * (ymin + ymax) / 2;
    auto iso = [&](double x, double y, double z) { const QPointF q = isoBrut(x, y, z); return QPointF(ox + s * q.x(), oy + s * q.y()); };
    auto poly = [&](std::initializer_list<QPointF> pts, const QColor &c) {
        QPolygonF pg; for (const QPointF &q : pts) pg << q;
        p.setPen(QPen(QColor("#6b7a8c"), 1)); p.setBrush(c); p.drawPolygon(pg);
    };
    auto boite = [&](double x0, double x1, double y0, double y1, double z0, double z1, const QColor &dessus) {
        poly({iso(x0, y1, z0), iso(x1, y1, z0), iso(x1, y1, z1), iso(x0, y1, z1)}, QColor("#9fb0c3"));
        poly({iso(x1, y0, z0), iso(x1, y1, z0), iso(x1, y1, z1), iso(x1, y0, z1)}, QColor("#b4c2d2"));
        poly({iso(x0, y0, z1), iso(x1, y0, z1), iso(x1, y1, z1), iso(x0, y1, z1)}, dessus);
    };

    const double zc = HT - (m_k + 1) * DZ;
    boite(0, L, 0, W, 0, zc, QColor("#c9d3df"));
    for (int j = m_k; j >= 0; --j) {
        const Ile r = ile(j);
        boite(r.x0, r.x1, r.y0, r.y1, HT - (j + 1) * DZ, HT - j * DZ, j == 0 ? QColor("#e7d3f2") : QColor("#c9d3df"));
    }
    // chemin outil parcouru (rouge)
    if (m_trace.size() > 1) {
        QPainterPath ch; ch.moveTo(iso(m_trace[0].x(), m_trace[0].y(), zc));
        for (int n = 1; n < m_trace.size(); ++n) ch.lineTo(iso(m_trace[n].x(), m_trace[n].y(), zc));
        p.setPen(QPen(QColor("#e0402a"), 1.3)); p.setBrush(Qt::NoBrush); p.drawPath(ch);
    }
    // fraise : corps, goujures qui défilent, bout
    const QPointF pt = m_chemin.value(m_i, QPointF(0, 0));
    const QPointF b = iso(pt.x(), pt.y(), zc), t = iso(pt.x(), pt.y(), zc + 70);
    const double rr = 8 * s / 1.25;
    p.setPen(Qt::NoPen); p.setBrush(QColor("#8a929c"));
    p.drawRect(QRectF(b.x() - rr, t.y(), 2 * rr, b.y() - t.y()));
    p.setBrush(QColor("#b9c0c8")); p.drawEllipse(QPointF(b.x(), t.y()), rr, rr / 2);
    p.setPen(QPen(Qt::white, 1.5));
    const double hGouj = (b.y() - t.y()) * 0.5;
    for (int d = 0; d < 3; ++d) {
        const double yy = t.y() + std::fmod(m_ang * 6 * s / 1.25 + d * hGouj / 3, hGouj) + (b.y() - t.y()) * 0.45;
        p.drawLine(QPointF(b.x() - rr, yy), QPointF(b.x() + rr, yy + rr * 0.75));
    }
    p.setPen(Qt::NoPen); p.setBrush(QColor("#5b636c")); p.drawEllipse(b, rr, rr / 2);
    p.setPen(QColor("#2a9a3a"));
    p.setFont(QFont("Arial", 9));
    p.drawText(QPointF(10, 18), QString("Z %1").arg(-(m_k + 1) * 2.0, 0, 'f', 1));
}

// ═══════════════════════════════════════════════════════════════
CalculDialog::CalculDialog(const QStringList &noms, const QVector<double> &poids, QWidget *parent)
    : QDialog(parent), m_noms(noms), m_poids(poids)
{
    setWindowTitle(QString::fromUtf8("EazySmartNC — Calcul du G-code"));
    setModal(true);
    setStyleSheet("QDialog{background:#ffffff;} QLabel{background:transparent;}");
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(14, 14, 14, 14);
    m_anim = new AnimFraise3D;
    v->addWidget(m_anim, 1);
    m_msg = new QLabel;
    m_msg->setAlignment(Qt::AlignCenter);
    m_msg->setStyleSheet("color:#1a5fb4; font-size:12pt; font-weight:bold;");
    v->addWidget(m_msg);
    m_detail = new QLabel;
    m_detail->setStyleSheet("color:#404040; font-size:10pt;");
    v->addWidget(m_detail);
    m_barre = new QProgressBar;
    m_barre->setRange(0, 1000); m_barre->setTextVisible(false); m_barre->setFixedHeight(12);
    m_barre->setStyleSheet("QProgressBar{background:#e3e3e3; border:none; border-radius:6px;}"
                           "QProgressBar::chunk{background:#2a9a3a; border-radius:6px;}");
    v->addWidget(m_barre);
    auto *h = new QHBoxLayout;
    m_pct = new QLabel("0 %");
    m_pct->setStyleSheet("color:#606060; font-size:10pt;");
    m_annuler = new QPushButton("Annuler");
    m_annuler->setMinimumSize(110, 34);
    m_annuler->setStyleSheet("QPushButton{background:#ffffff; color:#202020; border:1px solid #8a8a8a; font-size:10pt;}"
                             "QPushButton:hover{background:#eeeeee;}");
    h->addWidget(m_pct); h->addStretch(); h->addWidget(m_annuler);
    v->addLayout(h);
    connect(m_annuler, &QPushButton::clicked, this, [this]{ reject(); });
    resize(460, 480);

    m_timer = new QTimer(this);
    m_timer->setInterval(30);
    connect(m_timer, &QTimer::timeout, this, [this]{ m_anim->avancer(); ++m_tic; majTexte(); });
    m_timer->start();
    majTexte();
}

void CalculDialog::majTexte()
{
    const QString pts = QString(".").repeated(1 + (m_tic / 25) % 3);
    m_msg->setText(Progression::annuler ? QString::fromUtf8("Annulation en cours…")
                                         : QString::fromUtf8("Un peu de patience…\nEazySmartNC calcule le G-code") + pts);
    const int nb = Progression::nbBlocs, bl = Progression::bloc;
    const int ph = Progression::phase, et = Progression::etape, ne = Progression::nbEtapes;
    QString d;
    if (nb > 0 && bl > 0) {
        d = QString("Bloc %1/%2").arg(bl).arg(nb);
        const QString nom = m_noms.value(bl - 1);
        if (!nom.isEmpty()) d += QString::fromUtf8(" – ") + nom;
        static const char *phases[] = {"", "ébauche couche", "finition couche", "balayage ligne"};
        if (ph >= 1 && ph <= 3 && ne > 0)
            d += QString::fromUtf8(" · %1 %2/%3").arg(QString::fromUtf8(phases[ph])).arg(et).arg(ne);
    }
    m_detail->setText(d);
    double frac = 0;
    if (ne > 0 && ph >= 1 && ph <= 3) {
        static const double base[] = {0, 0.0, 0.4, 0.7}, poids[] = {0, 0.4, 0.3, 0.3};
        frac = base[ph] + poids[ph] * qBound(0.0, double(et) / ne, 1.0);
    }
    double total = 0, fait = 0;
    for (int i = 0; i < nb; ++i) {
        const double w = m_poids.value(i, 1.0);
        total += w;
        if (i < bl - 1) fait += w;
        else if (i == bl - 1) fait += w * frac;
    }
    double prog = total > 0 ? fait / total : 0;
    m_max = qMax(m_max, prog);                 // la barre ne recule jamais
    m_barre->setValue(int(m_max * 1000));
    m_pct->setText(QString("%1 %").arg(int(m_max * 100)));
}

void CalculDialog::reject()
{
    if (m_fini) { QDialog::reject(); return; }
    Progression::annuler = true;               // le calcul s'arrête au prochain contrôle
    m_annuler->setEnabled(false);
    majTexte();
}

void CalculDialog::terminer()
{
    m_fini = true;
    m_timer->stop();
    QDialog::accept();
}
