#pragma execution_character_set("utf-8")
#include "clipper2utils.h"

// Clipper2 — include unique
#include "clipper2/clipper.h"

#include <QtMath>
#include <QDebug>

// ─────────────────────────────────────────────────────────────
//  Constante d'échelle : mm → microns (×1000)
//  Clipper2 travaille en entiers 64-bit
// ─────────────────────────────────────────────────────────────
static constexpr double SCALE = 1000.0;

// ─────────────────────────────────────────────────────────────
//  Conversions EazyNC ↔ Clipper2
// ─────────────────────────────────────────────────────────────
static Clipper2Lib::Path64 toClipperPath(const Path2D &p)
{
    Clipper2Lib::Path64 cp;
    cp.reserve(p.size());
    for (const QPointF &pt : p)
        cp.emplace_back(
            static_cast<int64_t>(std::round(pt.x() * SCALE)),
            static_cast<int64_t>(std::round(pt.y() * SCALE)));
    return cp;
}

static Clipper2Lib::Paths64 toClipperPaths(const Paths2D &paths)
{
    Clipper2Lib::Paths64 cp;
    cp.reserve(paths.size());
    for (const Path2D &p : paths)
        cp.push_back(toClipperPath(p));
    return cp;
}

static Path2D fromClipperPath(const Clipper2Lib::Path64 &cp)
{
    Path2D p;
    p.reserve((int)cp.size());
    for (const auto &pt : cp)
        p.append(QPointF(pt.x / SCALE, pt.y / SCALE));
    return p;
}

static Paths2D fromClipperPaths(const Clipper2Lib::Paths64 &cps)
{
    Paths2D paths;
    paths.reserve((int)cps.size());
    for (const auto &cp : cps)
        if (!cp.empty()) paths.append(fromClipperPath(cp));
    return paths;
}

// ─────────────────────────────────────────────────────────────
//  offset — profil unique
// ─────────────────────────────────────────────────────────────
Paths2D Clipper2Utils::offset(const Path2D &profil, double delta, int joinType)
{
    if (profil.size() < 3) return {};
    return offset(Paths2D{profil}, delta, joinType);
}

// ─────────────────────────────────────────────────────────────
//  offset — plusieurs profils
// ─────────────────────────────────────────────────────────────
Paths2D Clipper2Utils::offset(const Paths2D &profils, double delta, int joinType, double arcTol)
{
    if (profils.isEmpty()) return {};

    using namespace Clipper2Lib;

    // Type de joint
    JoinType jt = JoinType::Round;
    if      (joinType == 1) jt = JoinType::Miter;
    else if (joinType == 2) jt = JoinType::Square;

    ClipperOffset co;
    co.MiterLimit(4.0);
    co.ArcTolerance(qMax(0.001, arcTol) * SCALE);  // précision arcs (0.1mm par défaut)

    Paths64 input = toClipperPaths(profils);
    co.AddPaths(input, jt, EndType::Polygon);

    Paths64 result;
    co.Execute(delta * SCALE, result);

    return fromClipperPaths(result);
}

// ─────────────────────────────────────────────────────────────
//  concentricPasses — passes du centre vers la paroi
// ─────────────────────────────────────────────────────────────
Paths2D Clipper2Utils::concentricPasses(const Path2D &profil,
                                         double routil,
                                         double dr,
                                         double recouvrement)
{
    if (profil.size() < 3 || routil < 0.1) return {};

    const double rEffectif = routil + dr;  // rayon avec correcteur
    if (rEffectif < 0.01) {
        qWarning() << "[Clipper2] rEffectif trop petit:" << rEffectif;
        return {};
    }

    const double pas = 2.0 * routil * (recouvrement / 100.0);
    const double pasMin = routil * 0.1;
    const double pasEffectif = qMax(pas, pasMin);

    Paths2D passes;

    // Démarrer depuis l'offset maximal (centre) vers 0 (paroi)
    // L'offset initial = -(rEffectif) depuis la paroi intérieure
    // Puis on agrandit progressivement par pas jusqu'à toucher la paroi

    // 1. Calculer combien de passes rentrent
    // On fait des offsets de -(rEffectif), -(rEffectif + pas), ...
    // jusqu'à ce que le résultat soit vide

    double delta = -rEffectif;  // négatif = réduction vers l'intérieur
    int safety = 0;

    // Collecter depuis l'intérieur (plus petit) vers l'extérieur (plus grand)
    QVector<Paths2D> accumulator;
    while (safety++ < 500) {
        // Miter (joinType=1) : côtés droits restent parfaitement droits.
        // Les coins arrondis viennent déjà du profil makeRect.
        // (Round bombait les côtés → faux arcs en sortie G-code.)
        Paths2D pass = offset(profil, delta, 1);
        if (pass.isEmpty()) break;
        // Vérifier que le résultat est non dégénéré
        bool valid = false;
        for (const Path2D &p : pass)
            if (p.size() >= 3) { valid = true; break; }
        if (!valid) break;
        accumulator.prepend(pass);  // centre d'abord
        delta -= pasEffectif;
    }

    // Aplatir en liste de passes (chaque passe = un Paths2D)
    for (const Paths2D &p : accumulator)
        passes.append(p.isEmpty() ? Path2D() : p.first());

    return passes;
}

// ─────────────────────────────────────────────────────────────
//  makeRect — rectangle avec coins arrondis optionnels
// ─────────────────────────────────────────────────────────────
Path2D Clipper2Utils::makeRect(double cx, double cy,
                                double longueur, double largeur,
                                double rayonCoin)
{
    Path2D p;
    double hx = longueur / 2.0;
    double hy = largeur  / 2.0;
    double r  = qMin(rayonCoin, qMin(hx, hy));

    if (r < 0.01) {
        // Rectangle simple
        p << QPointF(cx - hx, cy - hy)
          << QPointF(cx + hx, cy - hy)
          << QPointF(cx + hx, cy + hy)
          << QPointF(cx - hx, cy + hy);
        return p;
    }

    // Rectangle avec coins arrondis — approximation par arcs
    const int arcSegs = 16;
    auto addArc = [&](double ocx, double ocy, double startAngle) {
        for (int i = 0; i <= arcSegs; ++i) {
            double a = qDegreesToRadians(startAngle + i * 90.0 / arcSegs);
            p << QPointF(ocx + r * qCos(a), ocy + r * qSin(a));
        }
    };

    // 4 coins : BD, BD, HG, HD
    addArc(cx + hx - r, cy - hy + r, -90.0);  // coin BD  → bas-droit
    addArc(cx + hx - r, cy + hy - r,   0.0);  // coin HD  → haut-droit
    addArc(cx - hx + r, cy + hy - r,  90.0);  // coin HG  → haut-gauche
    addArc(cx - hx + r, cy - hy + r, 180.0);  // coin BG  → bas-gauche

    return p;
}

// ─────────────────────────────────────────────────────────────
//  difference — booleen A \ B (zones dans A mais pas dans B)
// ─────────────────────────────────────────────────────────────
Paths2D Clipper2Utils::difference(const Paths2D &subjects, const Paths2D &clips)
{
    using namespace Clipper2Lib;
    Paths64 subj = toClipperPaths(subjects);
    Paths64 clip = toClipperPaths(clips);
    Paths64 res  = Difference(subj, clip, FillRule::NonZero);
    return fromClipperPaths(res);
}

Paths2D Clipper2Utils::difference(const Path2D &subject, const Path2D &clip)
{
    Paths2D s; s << subject;
    Paths2D c; c << clip;
    return difference(s, c);
}

// ─────────────────────────────────────────────────────────────
//  intersect — booleen A ∩ B (zones dans A ET dans B)
// ─────────────────────────────────────────────────────────────
Paths2D Clipper2Utils::intersect(const Paths2D &subjects, const Paths2D &clips)
{
    using namespace Clipper2Lib;
    Paths64 subj = toClipperPaths(subjects);
    Paths64 clip = toClipperPaths(clips);
    Paths64 res  = Intersect(subj, clip, FillRule::NonZero);
    return fromClipperPaths(res);
}

Paths2D Clipper2Utils::intersect(const Path2D &subject, const Path2D &clip)
{
    Paths2D s; s << subject;
    Paths2D c; c << clip;
    return intersect(s, c);
}

// ─────────────────────────────────────────────────────────────
//  makeCircle
// ─────────────────────────────────────────────────────────────
Path2D Clipper2Utils::makeCircle(double cx, double cy, double rayon, int segments)
{
    Path2D p;
    p.reserve(segments);
    for (int i = 0; i < segments; ++i) {
        double a = 2.0 * M_PI * i / segments;
        p << QPointF(cx + rayon * qCos(a), cy + rayon * qSin(a));
    }
    return p;
}

// ─────────────────────────────────────────────────────────────
//  isValid
// ─────────────────────────────────────────────────────────────
bool Clipper2Utils::isValid(const Path2D &p)
{
    if (p.size() < 3) return false;
    // Vérifier que l'aire n'est pas nulle
    return qAbs(signedArea(p)) > 1e-6;
}

// ─────────────────────────────────────────────────────────────
//  signedArea (formule du lacet)
// ─────────────────────────────────────────────────────────────
double Clipper2Utils::signedArea(const Path2D &p)
{
    double area = 0.0;
    const int n = p.size();
    for (int i = 0, j = n - 1; i < n; j = i++) {
        area += (p[j].x() + p[i].x()) * (p[j].y() - p[i].y());
    }
    return area / 2.0;
}

bool Clipper2Utils::isCCW(const Path2D &p)
{
    return signedArea(p) > 0;
}

// ─────────────────────────────────────────────────────────────
//  ensureCCW / ensureCW
// ─────────────────────────────────────────────────────────────
Path2D Clipper2Utils::ensureCCW(const Path2D &p)
{
    if (isCCW(p)) return p;
    Path2D r = p;
    std::reverse(r.begin(), r.end());
    return r;
}

Path2D Clipper2Utils::ensureCW(const Path2D &p)
{
    if (!isCCW(p)) return p;
    Path2D r = p;
    std::reverse(r.begin(), r.end());
    return r;
}
