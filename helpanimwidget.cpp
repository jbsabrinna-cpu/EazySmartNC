// ─────────────────────────────────────────────────────────────
//  HelpAnimWidget — dessins d'aide animés (voir helpanimwidget.h)
//  Repère virtuel 400 x 280, mis à l'échelle dans le cadre AIDE.
// ─────────────────────────────────────────────────────────────
#include "helpanimwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QFontMetricsF>
#include <QSet>
#include <QFile>
#include <QMovie>
#include <QtMath>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace {
const QColor kFond   (0xEB, 0xEB, 0xEB);
const QColor kMat    (0xA9, 0xAF, 0xB6);
const QColor kHach   (0x8E, 0x94, 0x9C);
const QColor kBord   (0x5E, 0x64, 0x6B);
const QColor kOutil  (0xE3, 0xB5, 0x05);
const QColor kOutilB (0x7A, 0x62, 0x00);
const QColor kQueue  (0x9C, 0x9C, 0x9C);
const QColor kAvance (0x00, 0x9A, 0x00);
const QColor kRapide (0xD0, 0x28, 0x28);
const QColor kCote   (0x50, 0x50, 0x50);
const QColor kActif  (0x00, 0x58, 0xC8);
const QColor kSurep  (0xF0, 0x8A, 0x24);
const QColor kBride  (0x4A, 0x4F, 0x55);

constexpr double W = 400.0, H = 280.0;

double seg(double t, double a, double b)
{
    if (t <= a) return 0.0;
    if (t >= b) return 1.0;
    return (t - a) / (b - a);
}

void setPx(QPainter &p, int px, bool bold = false)
{
    QFont f = p.font();
    f.setPixelSize(px);
    f.setBold(bold);
    p.setFont(f);
}

void texteFond(QPainter &p, const QPointF &pos, const QString &txt,
               const QColor &c, bool bold, Qt::Alignment al = Qt::AlignLeft)
{
    if (txt.isEmpty()) return;
    setPx(p, 12, bold);
    const QFontMetricsF fm(p.font());
    const double w = fm.horizontalAdvance(txt), h = fm.height();
    double x = pos.x();
    if (al & Qt::AlignRight)   x -= w;
    if (al & Qt::AlignHCenter) x -= w / 2;
    const QRectF r(x - 2, pos.y() - h / 2 - 1, w + 4, h + 2);
    p.setPen(Qt::NoPen);
    QColor bg = kFond; bg.setAlpha(215);
    p.setBrush(bg);
    p.drawRoundedRect(r, 2, 2);
    p.setPen(c);
    p.drawText(r, Qt::AlignCenter, txt);
}

// Trajet partiel le long d'un chemin (0..u), échantillonné
QPainterPath cheminPartiel(const QPainterPath &path, double u, int n = 160)
{
    QPainterPath out;
    if (u <= 0.0) return out;
    out.moveTo(path.pointAtPercent(0.0));
    const int steps = std::max(2, int(n * u));
    for (int i = 1; i <= steps; ++i)
        out.lineTo(path.pointAtPercent(std::min(1.0, u * i / steps)));
    return out;
}
} // namespace

// ─────────────────────────────────────────────────────────────
HelpAnimWidget::HelpAnimWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(160, 112);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
}

QString HelpAnimWidget::cheminGif(const QString &key)
{
    return QStringLiteral(":/help_anim/help_anim/%1.gif").arg(key);
}

bool HelpAnimWidget::hasScene(const QString &key)
{
    if (!key.isEmpty() && QFile::exists(cheminGif(key))) return true;
    if (key.startsWith("posref_") || key.startsWith("posang_")) return true;
    static const QSet<QString> keys = {
        "mode_plongee", "plongee_0", "plongee_1", "plongee_2",
        "prof_passe", "profondeur", "approche", "recouvrement",
        "surep_lat", "surep_fond", "fraisage_0", "fraisage_1",
        "rayon_angle", "longueur_largeur", "diametre", "surep_lat_circ"
    };
    return keys.contains(key);
}

bool HelpAnimWidget::setScene(const QString &key)
{
    if (!hasScene(key)) { stop(); return false; }
    if (key != m_key) {
        stop();
        m_key = key;
        m_clock.restart();
        // Animation 3D pré-calculée (GIF) prioritaire sur le dessin
        if (QFile::exists(cheminGif(key))) {
            m_movie = new QMovie(cheminGif(key), QByteArray(), this);
            m_movie->setCacheMode(QMovie::CacheAll);
            connect(m_movie, &QMovie::frameChanged, this, QOverload<>::of(&QWidget::update));
            m_movie->start();
        }
    }
    if (m_movie) { update(); return true; }
    if (!m_clock.isValid()) m_clock.start();
    if (!m_timer.isActive()) m_timer.start();
    update();
    return true;
}

void HelpAnimWidget::stop()
{
    m_timer.stop();
    if (m_movie) {
        m_movie->stop();
        m_movie->deleteLater();
        m_movie = nullptr;
    }
    m_key.clear();
}

void HelpAnimWidget::hideEvent(QHideEvent *)
{
    m_timer.stop();
    if (m_movie) m_movie->setPaused(true);
}

void HelpAnimWidget::showEvent(QShowEvent *)
{
    if (m_movie) { m_movie->setPaused(false); return; }
    if (!m_key.isEmpty()) m_timer.start();
}

double HelpAnimWidget::phase() const
{
    if (!m_clock.isValid()) return 0.0;
    const double s = m_clock.elapsed() / 1000.0;
    return std::fmod(s, m_period) / m_period;
}

void HelpAnimWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), kFond);
    if (m_key.isEmpty()) return;

    // Animation 3D : image courante mise à l'échelle du cadre
    if (m_movie) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QPixmap px = m_movie->currentPixmap();
        if (!px.isNull()) {
            const QSize sz = px.size().scaled(size(), Qt::KeepAspectRatio);
            const QRect r(QPoint((width() - sz.width()) / 2, (height() - sz.height()) / 2), sz);
            p.drawPixmap(r, px);
        }
        return;
    }

    const double s = std::min(width() / W, height() / H);
    p.translate((width() - W * s) / 2.0, (height() - H * s) / 2.0);
    p.scale(s, s);

    const double t = phase();
    if (m_key == "plongee_0" || m_key == "mode_plongee") scenePlongee(p, t, 0);
    else if (m_key == "plongee_1")        scenePlongee(p, t, 1);
    else if (m_key == "plongee_2")        scenePlongee(p, t, 2);
    else if (m_key == "prof_passe")       scenePasses(p, t, true);
    else if (m_key == "profondeur")       scenePasses(p, t, false);
    else if (m_key == "approche")         sceneApproche(p, t);
    else if (m_key == "recouvrement")     sceneRecouvrement(p, t);
    else if (m_key == "surep_lat")        sceneSurepLat(p, t);
    else if (m_key == "surep_fond")       sceneSurepFond(p, t);
    else if (m_key == "fraisage_0")       sceneFraisage(p, t, true);
    else if (m_key == "fraisage_1")       sceneFraisage(p, t, false);
    else if (m_key == "rayon_angle")      sceneRayon(p, t);
    else if (m_key == "longueur_largeur") sceneLongueur(p, t);
    else if (m_key == "diametre")         sceneDiametre(p, t);
    else if (m_key == "surep_lat_circ")   sceneSurepLatCirc(p, t);
    else if (m_key.startsWith("posref_") || m_key.startsWith("posang_"))
        scenePosition(p, t, m_key.mid(7).toInt(), m_key.startsWith("posang_"));
}

// ═════════════════════════════════════════════════════════════
//  Outils de dessin
// ═════════════════════════════════════════════════════════════
void HelpAnimWidget::drawMatiere(QPainter &p, const QRectF &r)
{
    QPainterPath path; path.addRect(r);
    drawMatierePath(p, path);
}

void HelpAnimWidget::drawMatierePath(QPainter &p, const QPainterPath &path)
{
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(kMat);
    p.drawPath(path);
    p.setClipPath(path, Qt::IntersectClip);
    p.setPen(QPen(kHach, 1.0));
    const QRectF b = path.boundingRect();
    for (double x = b.left() - b.height(); x < b.right(); x += 8.0)
        p.drawLine(QPointF(x, b.bottom()), QPointF(x + b.height(), b.top()));
    p.restore();
    p.save();
    p.setPen(QPen(kBord, 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.restore();
}

void HelpAnimWidget::drawOutilProfil(QPainter &p, double x, double yBas, double d)
{
    const double h = 40.0;
    const QRectF coupe(x - d / 2, yBas - h, d, h);
    const QRectF queue(x - d * 0.38, yBas - h - 80.0, d * 0.76, 80.0);
    p.save();
    p.setPen(QPen(QColor(0x60, 0x60, 0x60), 1.0));
    p.setBrush(kQueue);
    p.drawRect(queue);
    p.setPen(QPen(kOutilB, 1.2));
    p.setBrush(kOutil);
    p.drawRect(coupe);
    // goujures
    p.setClipRect(coupe);
    p.setPen(QPen(kOutilB, 1.0));
    for (double k = -h; k < d + h; k += 12.0)
        p.drawLine(QPointF(coupe.left() + k, coupe.bottom()),
                   QPointF(coupe.left() + k + h * 0.6, coupe.top()));
    p.restore();
    // axe
    p.save();
    QPen ax(QColor(0x30, 0x30, 0x30), 0.8, Qt::DashDotLine);
    p.setPen(ax);
    p.drawLine(QPointF(x, yBas - h - 86), QPointF(x, yBas + 6));
    p.restore();
}

void HelpAnimWidget::drawOutilDessus(QPainter &p, const QPointF &c, double r, double angleDeg)
{
    p.save();
    QColor f = kOutil; f.setAlpha(230);
    p.setPen(QPen(kOutilB, 1.2));
    p.setBrush(f);
    p.drawEllipse(c, r, r);
    p.translate(c);
    p.rotate(angleDeg);              // positif = sens horaire à l'écran
    p.setPen(QPen(kOutilB, 1.6));
    for (int k = 0; k < 4; ++k) {
        p.drawLine(QPointF(0, 0), QPointF(r * 0.92, 0));
        // petite dent
        QPolygonF dent;
        dent << QPointF(r * 0.92, 0) << QPointF(r * 0.70, r * 0.16) << QPointF(r * 0.70, 0);
        p.setBrush(kOutilB);
        p.drawPolygon(dent);
        p.rotate(90);
    }
    p.setBrush(kOutilB);
    p.drawEllipse(QPointF(0, 0), 2.2, 2.2);
    p.restore();
}

void HelpAnimWidget::drawFleche(QPainter &p, const QPointF &a, const QPointF &b, const QColor &c)
{
    p.save();
    p.setPen(QPen(c, 1.6));
    p.drawLine(a, b);
    const QPointF d = b - a;
    const double L = std::hypot(d.x(), d.y());
    if (L > 0.1) {
        const QPointF u = d / L, n(-u.y(), u.x());
        QPolygonF tri;
        tri << b << b - u * 8 + n * 3.5 << b - u * 8 - n * 3.5;
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPolygon(tri);
    }
    p.restore();
}

void HelpAnimWidget::drawCote(QPainter &p, const QPointF &a, const QPointF &b,
                              const QString &txt, bool actif, bool texteADroite)
{
    const QColor c = actif ? kActif : kCote;
    p.save();
    p.setPen(QPen(c, actif ? 1.5 : 1.0));
    p.drawLine(a, b);
    const QPointF d = b - a;
    const double L = std::hypot(d.x(), d.y());
    if (L > 0.1) {
        const QPointF u = d / L, n(-u.y(), u.x());
        const double s = std::min(7.0, L / 2.5);
        QPolygonF t1, t2;
        t1 << a << a + u * s + n * 3 << a + u * s - n * 3;
        t2 << b << b - u * s + n * 3 << b - u * s - n * 3;
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPolygon(t1);
        p.drawPolygon(t2);
    }
    const QPointF m = (a + b) / 2.0;
    const bool vertical = std::abs(d.x()) < std::abs(d.y());
    if (vertical)
        texteFond(p, m + QPointF(texteADroite ? 6 : -6, 0), txt, c, actif,
                  texteADroite ? Qt::AlignLeft : Qt::AlignRight);
    else
        texteFond(p, m + QPointF(0, texteADroite ? 11 : -11), txt, c, actif, Qt::AlignHCenter);
    p.restore();
}

void HelpAnimWidget::drawNiveau(QPainter &p, double y, double x0, double x1,
                                const QString &txt, const QColor &c)
{
    p.save();
    p.setPen(QPen(c, 1.0, Qt::DashLine));
    p.drawLine(QPointF(x0, y), QPointF(x1, y));
    p.restore();
    texteFond(p, QPointF(x1 + 4, y), txt, c, false);
}

void HelpAnimWidget::drawTitre(QPainter &p, const QString &txt)
{
    p.save();
    setPx(p, 14, true);
    p.setPen(QColor(0x30, 0x30, 0x30));
    p.drawText(QPointF(10, 20), txt);
    p.restore();
}

// ═════════════════════════════════════════════════════════════
//  Scènes
// ═════════════════════════════════════════════════════════════

// ── Plongée : hélice / rampe / directe (vue de profil) ────────
void HelpAnimWidget::scenePlongee(QPainter &p, double t, int mode)
{
    static const char *titres[3] = { "Plongée hélicoïdale", "Plongée en rampe", "Plongée directe" };
    static const char *legendes[3] = {
        "L'outil descend en tournant sur un cercle",
        "L'outil descend en aller-retour sur une ligne",
        "L'outil descend tout droit : avance réduite" };
    drawTitre(p, QString::fromUtf8(titres[mode]));

    const double yTop = 120, yBot = 255, x0 = 30, x1 = 330;
    const double d = 26, yA = 104, yF = 212;
    drawMatiere(p, QRectF(x0, yTop, x1 - x0, yBot - yTop));

    const double u = seg(t, 0.08, 0.86);

    auto pos = [&](double v, double &x, double &yb, double &pz) {
        yb = yA + (yF - yA) * v;
        pz = 0;
        if (mode == 0) {
            const double th = 2 * M_PI * 4.0 * v;
            x = 180 + 34 * std::cos(th);
            pz = -9 * std::sin(th);
        } else if (mode == 1) {
            const double L = 110, s = v * 4.0;
            int k = std::min(3, int(s));
            double f = s - k;
            x = (k % 2 == 0) ? 125 + L * f : 235 - L * f;
        } else {
            x = 180;
        }
    };

    // Matière enlevée : fond le plus bas atteint sous l'outil
    const int N = 240;
    QVector<double> fondX;
    QVector<double> fond;
    for (double x = x0; x <= x1; x += 2) { fondX << x; fond << yTop; }
    QVector<QPointF> trace;
    QVector<bool> arriere;
    for (int i = 0; i <= N; ++i) {
        const double v = u * i / N;
        double x, yb, pz; pos(v, x, yb, pz);
        trace << QPointF(x, yb + pz);
        arriere << (pz < 0);
        for (int j = 0; j < fondX.size(); ++j)
            if (std::abs(fondX[j] - x) <= d / 2 && yb > fond[j]) fond[j] = yb;
    }
    QPainterPath vide;
    bool open = false;
    for (int j = 0; j < fondX.size(); ++j) {
        if (fond[j] > yTop + 0.1) {
            if (!open) { vide.moveTo(fondX[j], yTop - 1); open = true; }
            vide.lineTo(fondX[j], fond[j]);
        } else if (open) {
            vide.lineTo(fondX[j - 1], yTop - 1); vide.closeSubpath(); open = false;
        }
    }
    if (open) { vide.lineTo(fondX.last(), yTop - 1); vide.closeSubpath(); }
    p.setPen(Qt::NoPen);
    p.setBrush(kFond);
    p.drawPath(vide);
    p.setPen(QPen(kBord, 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(vide);

    drawNiveau(p, yTop, x0, x1, "Z0", kCote);

    // Approche en rapide
    double xs, ybs, pzs; pos(0, xs, ybs, pzs);
    p.setPen(QPen(kRapide, 1.4, Qt::DashLine));
    p.drawLine(QPointF(xs, 46), QPointF(xs, yA));

    // Trajectoire en avance
    for (int i = 1; i < trace.size(); ++i) {
        QColor c = kAvance;
        if (arriere[i]) c.setAlpha(90);
        p.setPen(QPen(c, 1.8));
        p.drawLine(trace[i - 1], trace[i]);
    }

    double x, yb, pz; pos(u, x, yb, pz);
    drawOutilProfil(p, x, yb, d);

    texteFond(p, QPointF(200, 270), QString::fromUtf8(legendes[mode]), kCote, false, Qt::AlignHCenter);
}

// ── Profondeur de passe / profondeur totale (vue de profil) ───
void HelpAnimWidget::scenePasses(QPainter &p, double t, bool highlightPasse)
{
    drawTitre(p, highlightPasse ? QString::fromUtf8("Profondeur de passe")
                                : QString::fromUtf8("Profondeur totale"));
    const double yTop = 92, yBot = 262, x0 = 30, x1 = 330;
    const double pl = 110, pr = 270, d = 30, passe = 40;
    const int n = 3;
    drawMatiere(p, QRectF(x0, yTop, x1 - x0, yBot - yTop));

    const double u = seg(t, 0.05, 0.88);
    const double s = u * n;
    const int k = std::min(n - 1, int(s));
    const double f = (u >= 1.0) ? 1.0 : s - k;
    const double xl = pl + d / 2, xr = pr - d / 2;
    const double yPrev = yTop + passe * k, yk = yTop + passe * (k + 1);
    const bool gauche = (k % 2 == 0);
    const double xDep = gauche ? xl : xr;

    double x, yb;
    QRectF enCours;
    if (f < 0.2) {
        const double g = f / 0.2;
        x = xDep; yb = yPrev + (yk - yPrev) * g;
        enCours = QRectF(x - d / 2, yTop - 1, d, yb - yTop + 1);
    } else {
        const double g = (f - 0.2) / 0.8;
        x = gauche ? xl + (xr - xl) * g : xr - (xr - xl) * g;
        yb = yk;
        const double a = std::min(xDep, x) - d / 2, b = std::max(xDep, x) + d / 2;
        enCours = QRectF(a, yTop - 1, b - a, yk - yTop + 1);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(kFond);
    if (k > 0) p.drawRect(QRectF(pl, yTop - 1, pr - pl, passe * k + 1));
    p.drawRect(enCours);

    // fonds de passe en pointillés
    p.setPen(QPen(kCote, 0.8, Qt::DotLine));
    for (int i = 1; i <= n; ++i)
        p.drawLine(QPointF(pl, yTop + passe * i), QPointF(pr, yTop + passe * i));

    drawNiveau(p, yTop, x0, x1, "Z0", kCote);
    drawOutilProfil(p, x, yb, d);

    drawCote(p, QPointF(300, yTop), QPointF(300, yTop + passe),
             "Passe", highlightPasse, true);
    drawCote(p, QPointF(80, yTop), QPointF(80, yTop + passe * n),
             "Profondeur", !highlightPasse, false);
}

// ── Z approche / saut de bride (vue de profil) ────────────────
void HelpAnimWidget::sceneApproche(QPainter &p, double t)
{
    drawTitre(p, QString::fromUtf8("Z approche et saut de bride"));
    const double yTop = 172, yBot = 262, x0 = 40, x1 = 298;
    const double ySB = 52, yAp = 150, yFin = 236, xo = 220, d = 26;

    drawMatiere(p, QRectF(x0, yTop, x1 - x0, yBot - yTop));
    // bride
    p.setPen(QPen(QColor(0x20, 0x20, 0x20), 1.0));
    p.setBrush(kBride);
    p.drawRect(QRectF(52, yTop - 20, 70, 20));
    p.drawRect(QRectF(82, yTop - 52, 10, 32));
    p.drawRect(QRectF(74, yTop - 58, 26, 7));

    double yb;
    if (t < 0.08)       yb = ySB;
    else if (t < 0.30)  yb = ySB + (yAp - ySB) * seg(t, 0.08, 0.30);
    else if (t < 0.68)  yb = yAp + (yFin - yAp) * seg(t, 0.30, 0.68);
    else if (t < 0.76)  yb = yFin;
    else if (t < 0.92)  yb = yFin + (ySB - yFin) * seg(t, 0.76, 0.92);
    else                yb = ySB;

    const double prof = (t < 0.30) ? yTop : (t < 0.68 ? std::max(yTop, yb) : yFin);
    if (prof > yTop) {
        p.setPen(Qt::NoPen);
        p.setBrush(kFond);
        p.drawRect(QRectF(xo - d / 2, yTop - 1, d, prof - yTop + 1));
    }

    drawNiveau(p, ySB, x0, x1, "Saut de bride", kRapide);
    drawNiveau(p, yAp, x0, x1, "Z approche", kActif);
    drawNiveau(p, yTop, x0, x1, "Z0", kCote);

    // traces : rapide en rouge pointillé, avance en vert
    const double xt = xo + d / 2 + 12;
    p.setPen(QPen(kRapide, 1.6, Qt::DashLine));
    if (t >= 0.08) p.drawLine(QPointF(xt, ySB), QPointF(xt, std::min(yb, yAp)));
    if (t >= 0.30) {
        p.setPen(QPen(kAvance, 2.0));
        p.drawLine(QPointF(xt, yAp), QPointF(xt, (t < 0.68) ? yb : yFin));
    }
    if (t >= 0.76) {
        p.setPen(QPen(kRapide, 1.6, Qt::DashLine));
        p.drawLine(QPointF(xt + 10, yFin), QPointF(xt + 10, yb));
    }

    drawOutilProfil(p, xo, yb, d);
    drawCote(p, QPointF(150, yAp), QPointF(150, yTop), "approche", true, false);

    const QString leg = (t < 0.30) ? QString::fromUtf8("Rapide jusqu'au Z approche")
                      : (t < 0.76) ? QString::fromUtf8("Avance travail sous le Z approche")
                                   : QString::fromUtf8("Retour en rapide au saut de bride");
    texteFond(p, QPointF(200, 272), leg, kCote, false, Qt::AlignHCenter);
}

// ── Recouvrement (vue de dessus) ──────────────────────────────
void HelpAnimWidget::sceneRecouvrement(QPainter &p, double t)
{
    drawTitre(p, QString::fromUtf8("Recouvrement entre passes"));
    const double x0 = 20, x1 = 276, y0 = 34, y1 = 256;
    drawMatiere(p, QRectF(x0, y0, x1 - x0, y1 - y0));

    const double r = 22, pas = 0.6 * 2 * r;        // 40 % de recouvrement
    const double xa = 62, xb = 232;
    const int n = 6;
    const double u = seg(t, 0.03, 0.90);
    const double s = u * n;
    const int k = std::min(n - 1, int(s));
    const double f = (u >= 1.0) ? 1.0 : s - k;
    auto yc = [&](int i) { return 64 + i * pas; };

    auto bande = [&](int i, double xd, double xf) {
        const double a = std::min(xd, xf) - r, b = std::max(xd, xf) + r;
        return QRectF(a, yc(i) - r, b - a, 2 * r);
    };

    p.setPen(Qt::NoPen);
    p.setBrush(kFond);
    QVector<QRectF> faites;
    for (int i = 0; i < k; ++i) faites << bande(i, xa, xb);
    const double xCur = (k % 2 == 0) ? xa + (xb - xa) * f : xb - (xb - xa) * f;
    faites << bande(k, (k % 2 == 0) ? xa : xb, xCur);
    for (const QRectF &b : faites) p.drawRoundedRect(b, r, r);

    // zones de recouvrement
    QColor rc = kActif; rc.setAlpha(70);
    p.setBrush(rc);
    for (int i = 1; i < faites.size(); ++i) {
        const QRectF &b = faites[i];
        const double top = yc(i) - r, bot = yc(i - 1) + r;
        p.drawRect(QRectF(b.left() + r, top, std::max(0.0, b.width() - 2 * r), bot - top));
    }

    // trajectoire
    p.setPen(QPen(kAvance, 1.2, Qt::DashLine));
    for (int i = 0; i < k; ++i) p.drawLine(QPointF(xa, yc(i)), QPointF(xb, yc(i)));
    p.drawLine(QPointF((k % 2 == 0) ? xa : xb, yc(k)), QPointF(xCur, yc(k)));

    drawOutilDessus(p, QPointF(xCur, yc(k)), r, t * 360 * 10);

    drawCote(p, QPointF(288, yc(0) - r), QPointF(288, yc(0) + r), QString::fromUtf8("Ø outil"), false, true);
    if (k >= 1)
        drawCote(p, QPointF(302, yc(1) - r), QPointF(302, yc(0) + r), "recouvrement", true, true);
}

// ── Surépaisseur latérale (vue de dessus) ─────────────────────
void HelpAnimWidget::sceneSurepLat(QPainter &p, double t)
{
    drawTitre(p, QString::fromUtf8("Surépaisseur latérale"));
    const QRectF matiere(20, 34, 360, 222);
    const QRectF paroi(96, 66, 220, 156);
    const double se = 14, r = 18;
    drawMatiere(p, matiere);

    // surépaisseur laissée par l'ébauche
    p.setPen(Qt::NoPen);
    p.setBrush(kSurep);
    p.drawRect(paroi);
    p.setBrush(kFond);
    p.drawRect(paroi.adjusted(se, se, -se, -se));

    // finition : l'outil suit la paroi et enlève la surépaisseur
    const QRectF centre = paroi.adjusted(r, r, -r, -r);
    QPainterPath trajet;
    trajet.moveTo(centre.topLeft());
    trajet.lineTo(centre.topRight());
    trajet.lineTo(centre.bottomRight());
    trajet.lineTo(centre.bottomLeft());
    trajet.lineTo(centre.topLeft());
    const double u = seg(t, 0.20, 0.90);
    const QPainterPath fait = cheminPartiel(trajet, u);
    if (u > 0) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kFond, 2 * r, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(fait);
        p.setPen(QPen(kAvance, 1.4));
        p.drawPath(fait);
    }
    p.setPen(QPen(kBord, 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(paroi);

    const QPointF c = (u > 0) ? trajet.pointAtPercent(u) : centre.topLeft();
    drawOutilDessus(p, c, r, t * 360 * 10);

    drawCote(p, QPointF(paroi.left(), 150), QPointF(paroi.left() + se, 150), "", true, true);
    texteFond(p, QPointF(paroi.left() - 6, 150), QString::fromUtf8("surép."), kActif, true, Qt::AlignRight);

    texteFond(p, QPointF(200, 270),
              u <= 0 ? QString::fromUtf8("Après ébauche : matière laissée sur la paroi")
                     : QString::fromUtf8("Finition : l'outil enlève la surépaisseur"),
              kCote, false, Qt::AlignHCenter);
}

// ── Surépaisseur de fond (vue de profil) ──────────────────────
void HelpAnimWidget::sceneSurepFond(QPainter &p, double t)
{
    drawTitre(p, QString::fromUtf8("Surépaisseur de fond"));
    const double yTop = 92, yBot = 258, x0 = 30, x1 = 330;
    const double pl = 100, pr = 260, yR = 188, yF = 212, d = 36;
    drawMatiere(p, QRectF(x0, yTop, x1 - x0, yBot - yTop));
    p.setPen(Qt::NoPen);
    p.setBrush(kFond);
    p.drawRect(QRectF(pl, yTop - 1, pr - pl, yR - yTop + 1));
    p.setBrush(kSurep);
    p.drawRect(QRectF(pl, yR, pr - pl, yF - yR));

    const double xl = pl + d / 2, xr = pr - d / 2;
    double x = xl, yb = yTop - 14;
    if (t >= 0.15 && t < 0.30) yb = (yTop - 14) + (yF - yTop + 14) * seg(t, 0.15, 0.30);
    else if (t >= 0.30) { yb = yF; x = xl + (xr - xl) * seg(t, 0.30, 0.90); }

    if (yb > yR) {
        p.setBrush(kFond);
        p.drawRect(QRectF(pl, yR - 1, (x + d / 2) - pl, std::min(yb, yF) - yR + 1));
    }
    drawNiveau(p, yTop, x0, x1, "Z0", kCote);
    drawOutilProfil(p, x, yb, d);
    drawCote(p, QPointF(300, yR), QPointF(300, yF), QString::fromUtf8("surép. fond"), true, true);
    texteFond(p, QPointF(200, 272),
              t < 0.30 ? QString::fromUtf8("Après ébauche : matière laissée au fond")
                       : QString::fromUtf8("Finition du fond à la cote"),
              kCote, false, Qt::AlignHCenter);
}

// ── Avalant / opposition (vue de dessus, broche M3) ───────────
void HelpAnimWidget::sceneFraisage(QPainter &p, double t, bool avalant)
{
    drawTitre(p, avalant ? QString::fromUtf8("Fraisage en avalant (M3)")
                         : QString::fromUtf8("Fraisage en opposition (M3)"));
    const double r = 34, yc = 146, engage = 22;
    const double u = seg(t, 0.04, 0.92);
    const double x = 70 + 260 * u;

    // Avalant : matière à droite du sens d'avance (dessous à l'écran)
    QRectF mat, enleve;
    if (avalant) {
        const double yMat = yc + r - engage;
        mat    = QRectF(20, yMat, 360, 256 - yMat);
        enleve = QRectF(20, yMat - 1, x - 20, engage + 1);
    } else {
        const double yMat = yc - r + engage;
        mat    = QRectF(20, 36, 360, yMat - 36);
        enleve = QRectF(20, yc - r, x - 20, engage + 1);
    }
    drawMatiere(p, mat);
    p.setPen(Qt::NoPen);
    p.setBrush(kFond);
    p.drawRect(enleve);
    p.drawEllipse(QPointF(x, yc), r, r);

    drawOutilDessus(p, QPointF(x, yc), r, t * 360 * 8);

    // flèche de rotation (sens horaire vu de dessus)
    {
        const double rr = r + 9;
        QPainterPath arc;
        const QRectF br(x - rr, yc - rr, 2 * rr, 2 * rr);
        const double a0 = avalant ? 150 : 330, sweep = -80;
        arc.arcMoveTo(br, a0);
        arc.arcTo(br, a0, sweep);
        p.setPen(QPen(kCote, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawPath(arc);
        const double phi = qDegreesToRadians(a0 + sweep);
        const QPointF end(x + rr * std::cos(phi), yc - rr * std::sin(phi));
        const QPointF tan(std::sin(phi), std::cos(phi));      // sens horaire
        drawFleche(p, end - tan * 9, end, kCote);
        texteFond(p, QPointF(avalant ? x - rr - 4 : x + rr + 4,
                             avalant ? yc - rr + 4 : yc + rr - 4),
                  "M3", kCote, true, avalant ? Qt::AlignRight : Qt::AlignLeft);
    }

    // sens d'avance
    const double yf = avalant ? yc - r - 26 : yc + r + 26;
    drawFleche(p, QPointF(x - 28, yf), QPointF(x + 28, yf), kAvance);
    texteFond(p, QPointF(x, yf + (avalant ? -12 : 12)), "avance", kAvance, false, Qt::AlignHCenter);

    texteFond(p, QPointF(200, 270),
              avalant ? QString::fromUtf8("Matière à droite du sens d'avance : copeau épais vers mince")
                      : QString::fromUtf8("Matière à gauche du sens d'avance : copeau mince vers épais"),
              kActif, false, Qt::AlignHCenter);
}

// ── Rayon d'angle (vue de dessus) ─────────────────────────────
void HelpAnimWidget::sceneRayon(QPainter &p, double t)
{
    drawTitre(p, QString::fromUtf8("Rayon d'angle de la poche"));
    const QRectF matiere(20, 34, 360, 222);
    const double rc = 52, ro = 15;
    const QRectF poche(-80, 70, 360, 300);       // seul le coin haut-droit est visible

    QPainterPath m; m.addRect(matiere);
    QPainterPath pc; pc.addRoundedRect(poche, rc, rc);
    drawMatierePath(p, m.subtracted(pc));

    // trajectoire du centre outil
    const QPointF ca(poche.right() - rc, poche.top() + rc);   // centre de l'arc
    QPainterPath traj;
    traj.moveTo(40, poche.top() + ro);
    traj.lineTo(ca.x(), poche.top() + ro);
    const double ri = rc - ro;
    traj.arcTo(QRectF(ca.x() - ri, ca.y() - ri, 2 * ri, 2 * ri), 90, -90);
    traj.lineTo(ca.x() + ri, 250);

    const double u = seg(t, 0.05, 0.90);
    p.setPen(QPen(kAvance, 1.6));
    p.setBrush(Qt::NoBrush);
    p.drawPath(cheminPartiel(traj, u));

    // arc de paroi en couleur
    QPainterPath arcP;
    const QRectF br(ca.x() - rc, ca.y() - rc, 2 * rc, 2 * rc);
    arcP.arcMoveTo(br, 90);
    arcP.arcTo(br, 90, -90);
    p.setPen(QPen(kActif, 2.6));
    p.drawPath(arcP);

    // centre + cote R
    p.setPen(QPen(kActif, 1.0));
    p.drawLine(ca + QPointF(-5, 0), ca + QPointF(5, 0));
    p.drawLine(ca + QPointF(0, -5), ca + QPointF(0, 5));
    const double a = qDegreesToRadians(45.0);
    const QPointF pa(ca.x() + rc * std::cos(a), ca.y() - rc * std::sin(a));
    drawFleche(p, ca, pa, kActif);
    texteFond(p, ca + QPointF(-8, 16), "R", kActif, true, Qt::AlignRight);

    drawOutilDessus(p, traj.pointAtPercent(u), ro, t * 360 * 10);
    texteFond(p, QPointF(200, 270), QString::fromUtf8("0 = angle vif (le rayon de l'outil reste)"),
              kCote, false, Qt::AlignHCenter);
}

// ── Longueur x largeur (vue de dessus) ────────────────────────
void HelpAnimWidget::sceneLongueur(QPainter &p, double t)
{
    drawTitre(p, QString::fromUtf8("Longueur x largeur"));
    const QRectF matiere(20, 34, 300, 196);
    const QRectF poche(60, 62, 210, 136);
    const double ro = 14;

    QPainterPath m; m.addRect(matiere);
    QPainterPath pc; pc.addRoundedRect(poche, ro, ro);
    drawMatierePath(p, m.subtracted(pc));

    QPainterPath traj;
    const QRectF c = poche.adjusted(ro, ro, -ro, -ro);
    traj.moveTo(c.topLeft());
    traj.lineTo(c.topRight());
    traj.lineTo(c.bottomRight());
    traj.lineTo(c.bottomLeft());
    traj.lineTo(c.topLeft());
    const double u = seg(t, 0.05, 0.90);
    p.setPen(QPen(kAvance, 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawPath(cheminPartiel(traj, u));
    drawOutilDessus(p, traj.pointAtPercent(u), ro, t * 360 * 10);

    // lignes d'attache + cotes
    p.setPen(QPen(kCote, 0.8));
    p.drawLine(QPointF(poche.left(), poche.bottom() + 4), QPointF(poche.left(), 252));
    p.drawLine(QPointF(poche.right(), poche.bottom() + 4), QPointF(poche.right(), 252));
    p.drawLine(QPointF(poche.right() + 4, poche.top()), QPointF(346, poche.top()));
    p.drawLine(QPointF(poche.right() + 4, poche.bottom()), QPointF(346, poche.bottom()));
    drawCote(p, QPointF(poche.left(), 246), QPointF(poche.right(), 246), "Longueur", true, false);
    drawCote(p, QPointF(340, poche.top()), QPointF(340, poche.bottom()), "Largeur", true, true);
}

// ── Diamètre de poche circulaire (vue de dessus) ──────────────
void HelpAnimWidget::sceneDiametre(QPainter &p, double t)
{
    drawTitre(p, QString::fromUtf8("Diamètre de la poche"));
    const QRectF matiere(20, 34, 330, 222);
    const QPointF c(185, 145);
    const double rp = 92, ro = 14, rc = rp - ro;
    drawMatiere(p, matiere);

    // trajet : spirale du centre vers la paroi, puis un tour complet
    QPainterPath traj;
    traj.moveTo(c);
    const int N = 240;
    const double tours = 3.0;
    for (int i = 1; i <= N; ++i) {
        const double a = double(i) / N, th = 2 * M_PI * tours * a, r = rc * a;
        traj.lineTo(c.x() + r * std::cos(th), c.y() - r * std::sin(th));
    }
    for (int i = 1; i <= 90; ++i) {
        const double th = 2 * M_PI * tours + 2 * M_PI * i / 90.0;
        traj.lineTo(c.x() + rc * std::cos(th), c.y() - rc * std::sin(th));
    }

    const double u = seg(t, 0.05, 0.88);
    const QPainterPath fait = cheminPartiel(traj, u, 400);
    if (u > 0) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kFond, 2 * ro, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(fait);
        p.setPen(QPen(kAvance, 1.2));
        p.drawPath(fait);
    }
    // contour final en trait fin
    p.setPen(QPen(kBord, 1.0, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, rp, rp);

    drawOutilDessus(p, u > 0 ? traj.pointAtPercent(u) : c, ro, t * 360 * 10);

    const double a = qDegreesToRadians(35.0);
    const QPointF d(rp * std::cos(a), -rp * std::sin(a));
    drawCote(p, c - d, c + d, QString::fromUtf8("Diamètre"), true, true);
}

// ── Surépaisseur latérale, poche circulaire (vue de dessus) ───
void HelpAnimWidget::sceneSurepLatCirc(QPainter &p, double t)
{
    drawTitre(p, QString::fromUtf8("Surépaisseur latérale"));
    const QRectF matiere(20, 34, 330, 222);
    const QPointF c(185, 145);
    const double rp = 92, se = 14, ro = 16, rc = rp - ro;
    drawMatiere(p, matiere);

    // surépaisseur laissée par l'ébauche
    p.setPen(Qt::NoPen);
    p.setBrush(kSurep);
    p.drawEllipse(c, rp, rp);
    p.setBrush(kFond);
    p.drawEllipse(c, rp - se, rp - se);

    // finition : un tour à la cote
    QPainterPath traj;
    traj.moveTo(c.x() - rc, c.y());
    for (int i = 1; i <= 120; ++i) {
        const double th = M_PI - 2 * M_PI * i / 120.0;
        traj.lineTo(c.x() + rc * std::cos(th), c.y() - rc * std::sin(th));
    }
    const double u = seg(t, 0.20, 0.90);
    const QPainterPath fait = cheminPartiel(traj, u, 240);
    if (u > 0) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kFond, 2 * ro, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(fait);
        p.setPen(QPen(kAvance, 1.4));
        p.drawPath(fait);
    }
    p.setPen(QPen(kBord, 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, rp, rp);

    drawOutilDessus(p, u > 0 ? traj.pointAtPercent(u) : traj.pointAtPercent(0), ro, t * 360 * 10);

    drawCote(p, QPointF(c.x() + rp - se, c.y()), QPointF(c.x() + rp, c.y()), "", true, true);
    texteFond(p, QPointF(c.x() + rp + 6, c.y()), QString::fromUtf8("surép."), kActif, true, Qt::AlignLeft);

    texteFond(p, QPointF(200, 270),
              u <= 0 ? QString::fromUtf8("Après ébauche : matière laissée sur la paroi")
                     : QString::fromUtf8("Finition : l'outil enlève la surépaisseur"),
              kCote, false, Qt::AlignHCenter);
}

// ── Position de la poche (4 coins + centre) et position angulaire ──
//  Points proposés : 7 9 (coins haut), 5 (centre), 1 3 (coins bas).
//  Les coins d'une poche à rayons sont les coins FICTIFS, à
//  l'intersection des côtés prolongés (fuyantes en pointillé).
//  L'angle tourne la poche autour du point choisi (pivot du cycle).
void HelpAnimWidget::scenePosition(QPainter &p, double t, int point, bool angle)
{
    if (point != 1 && point != 3 && point != 7 && point != 9) point = 5;
    drawTitre(p, angle ? QString::fromUtf8("Position angulaire")
                       : QString::fromUtf8("Position de la poche"));
    const double L = 170, H = 100, R = 22;
    const double cx = 200, cy = 150;
    auto coin = [&](int k) {
        const int c = (k - 1) % 3, l = (k - 1) / 3;            // l = 0 en bas
        return QPointF(cx - L / 2 + c * L / 2, cy + H / 2 - l * H / 2);
    };
    const QPointF pivot = coin(point);

    drawMatiere(p, QRectF(20, 34, 360, 222));

    const double aMax = 20.0;
    const double a = angle ? aMax * (0.5 - 0.5 * std::cos(M_PI * seg(t, 0.1, 0.7))) : 0.0;
    QTransform tr;
    tr.translate(pivot.x(), pivot.y()); tr.rotate(-a); tr.translate(-pivot.x(), -pivot.y());

    QPainterPath pp; pp.addRoundedRect(QRectF(cx - L / 2, cy - H / 2, L, H), R, R);

    if (angle) {                                              // position d'origine
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kCote, 1.0, Qt::DashLine));
        p.drawPath(pp);
    }
    p.setPen(QPen(kBord, 1.4));
    p.setBrush(kFond);
    p.drawPath(tr.map(pp));

    // fuyantes : côtés prolongés jusqu'aux coins fictifs
    p.setPen(QPen(kActif, 1.0, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    for (int k : {1, 3, 7, 9}) {
        const QPointF c = coin(k);
        const double sx = (k == 1 || k == 7) ? 1 : -1;        // vers l'intérieur en X
        const double sy = (k == 1 || k == 3) ? -1 : 1;        // vers l'intérieur en Y (écran)
        p.drawLine(tr.map(c), tr.map(c + QPointF(sx * (R + 6), 0)));
        p.drawLine(tr.map(c), tr.map(c + QPointF(0, sy * (R + 6))));
    }

    if (angle) {                                              // arc d'angle au pivot
        p.setPen(QPen(kActif, 1.6));
        const double Ra = 58;
        QPainterPath arc;
        const QRectF br(pivot.x() - Ra, pivot.y() - Ra, 2 * Ra, 2 * Ra);
        arc.arcMoveTo(br, 0);
        arc.arcTo(br, 0, a);
        p.drawPath(arc);
        p.drawLine(pivot, pivot + QPointF(Ra + 12, 0));
        const double ra = qDegreesToRadians(a);
        p.drawLine(pivot, pivot + QPointF((Ra + 12) * std::cos(ra), -(Ra + 12) * std::sin(ra)));
        texteFond(p, pivot + QPointF((Ra + 20) * std::cos(ra / 2), -(Ra + 20) * std::sin(ra / 2)),
                  QString::number(qRound(a)) + QString::fromUtf8("°"), kActif, true);
    }

    // les 5 points proposés
    for (int k : {1, 3, 5, 7, 9}) {
        const QPointF q = tr.map(coin(k));
        const bool sel = (k == point);
        p.setPen(Qt::NoPen);
        p.setBrush(sel ? kRapide : QColor(0x70, 0x70, 0x70));
        p.drawEllipse(q, sel ? 6.0 : 4.0, sel ? 6.0 : 4.0);
        setPx(p, 12, sel);
        p.setPen(sel ? kRapide : kCote);
        p.drawText(q + QPointF(7, -7), QString::number(k));
    }

    // point choisi = position X / Y programmée
    const double puls = 0.5 + 0.5 * std::sin(t * 2 * M_PI * 2);
    QColor halo = kRapide; halo.setAlpha(int(60 + 80 * puls));
    p.setPen(QPen(halo, 3));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(pivot, 12 + 4 * puls, 12 + 4 * puls);
    p.setPen(QPen(kRapide, 1.6));
    p.drawLine(pivot + QPointF(-20, 0), pivot + QPointF(20, 0));
    p.drawLine(pivot + QPointF(0, -20), pivot + QPointF(0, 20));
    texteFond(p, pivot + QPointF(24, 16), "X / Y", kRapide, true);

    const QString nom = point == 5 ? QString::fromUtf8("le centre")
                                   : QString::fromUtf8("le coin %1").arg(point);
    texteFond(p, QPointF(200, 270),
              angle ? (point == 5 ? QString::fromUtf8("La poche tourne autour du centre, placé sur le X / Y")
                                  : QString::fromUtf8("La poche tourne autour du coin %1, placé sur le X / Y").arg(point))
                    : QString::fromUtf8("%1 de la poche se place sur le X / Y").arg(
                          nom.left(1).toUpper() + nom.mid(1)),
              kCote, false, Qt::AlignHCenter);
}
