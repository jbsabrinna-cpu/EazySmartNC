#pragma execution_character_set("utf-8")
#include "decoupe3d.h"
#include "clipper2/clipper.h"

#include <QMap>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

struct P3 { double x, y, z; };

// Sutherland–Hodgman : garde la partie du polygone où sens*(z - zc) >= 0
QVector<P3> couperZ(const QVector<P3> &poly, double zc, int sens)
{
    QVector<P3> out;
    const int n = poly.size();
    for (int i = 0; i < n; ++i) {
        const P3 &a = poly[i], &b = poly[(i + 1) % n];
        const double da = sens * (a.z - zc), db = sens * (b.z - zc);
        if (da >= 0) out << a;
        if ((da >= 0) != (db >= 0)) {
            const double t = da / (da - db);
            out << P3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, zc};
        }
    }
    return out;
}

double aire(const Clipper2Lib::PathD &p)
{
    double s = 0;
    for (size_t i = 0, n = p.size(); i < n; ++i) {
        const auto &a = p[i], &b = p[(i + 1) % n];
        s += a.x * b.y - b.x * a.y;
    }
    return 0.5 * s;
}

Clipper2Lib::PathsD versClip(const Contours2D &cs)
{
    Clipper2Lib::PathsD r;
    for (const Contour2D &c : cs) {
        Clipper2Lib::PathD p;
        for (const QPointF &q : c) p.push_back(Clipper2Lib::PointD(q.x(), q.y()));
        if (p.size() >= 3) r.push_back(p);
    }
    return r;
}

Contours2D versQt(const Clipper2Lib::PathsD &ps)
{
    Contours2D r;
    for (const auto &p : ps) {
        Contour2D c;
        c.reserve(int(p.size()));
        for (const auto &q : p) c << QPointF(q.x, q.y);
        if (c.size() >= 3) r << c;
    }
    return r;
}

} // namespace

QVector<double> Decoupe3D::zFacesPlanes(const QVector<float> &mesh)
{
    QMap<qint64, double> aireParZ;          // z en 1/100 mm -> aire cumulée
    for (int i = 0; i + 17 < mesh.size(); i += 18) {
        const float nz = mesh[i + 5];
        if (std::fabs(nz) < 0.999f) continue;
        const double ax = mesh[i], ay = mesh[i + 1], az = mesh[i + 2];
        const double bx = mesh[i + 6], by = mesh[i + 7];
        const double cx = mesh[i + 12], cy = mesh[i + 13];
        const double a = std::fabs((bx - ax) * (cy - ay) - (cx - ax) * (by - ay)) * 0.5;
        aireParZ[qRound64(az * 100.0)] += a;
    }
    QVector<double> zs;
    for (auto it = aireParZ.begin(); it != aireParZ.end(); ++it)
        if (it.value() > 0.5) zs << it.key() / 100.0;   // au moins 0,5 mm²
    std::sort(zs.begin(), zs.end(), std::greater<double>());
    return zs;
}

QVector<Couche3D> Decoupe3D::couches(const QVector<float> &mesh,
                                     double zHaut, double zBas, double passe)
{
    QVector<Couche3D> res;
    if (mesh.size() < 18 || passe <= 0 || zHaut <= zBas) return res;

    // ── Niveaux : pas de passe + faces planes ─────────────────
    const QVector<double> plans = zFacesPlanes(mesh);
    QVector<QPair<double, bool>> niv;                 // (z, facePlane)
    for (double z = zHaut - passe; z > zBas + 1e-6; z -= passe) niv << qMakePair(z, false);
    niv << qMakePair(zBas, false);
    for (double zp : plans)
        if (zp < zHaut - 1e-6 && zp >= zBas - 1e-6) {
            // une face plane remplace un niveau de passe trop proche
            for (int k = niv.size() - 1; k >= 0; --k)
                if (!niv[k].second && std::fabs(niv[k].first - zp) < 0.05) niv.remove(k);
            niv << qMakePair(zp, true);
        }
    std::sort(niv.begin(), niv.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    return couchesAuxNiveaux(mesh, niv);
}

QVector<Couche3D> Decoupe3D::couchesAuxNiveaux(const QVector<float> &mesh,
                                               const QVector<QPair<double, bool>> &niv)
{
    QVector<Couche3D> res;
    // ── Ombre cumulée du haut vers le bas ─────────────────────
    Clipper2Lib::PathsD ombre;
    double zPrec = std::numeric_limits<double>::infinity();
    for (const auto &nv : niv) {
        const double zLo = nv.first - 1e-6;
        Clipper2Lib::PathsD morceaux;
        for (int i = 0; i + 17 < mesh.size(); i += 18) {
            QVector<P3> t = { {mesh[i], mesh[i + 1], mesh[i + 2]},
                              {mesh[i + 6], mesh[i + 7], mesh[i + 8]},
                              {mesh[i + 12], mesh[i + 13], mesh[i + 14]} };
            const double zmin = std::min({t[0].z, t[1].z, t[2].z});
            const double zmax = std::max({t[0].z, t[1].z, t[2].z});
            if (zmax < zLo || zmin > zPrec) continue;      // hors de la tranche
            t = couperZ(t, zLo, +1);                       // garder z >= zLo
            if (std::isfinite(zPrec)) t = couperZ(t, zPrec, -1);   // garder z <= zPrec
            if (t.size() < 3) continue;
            Clipper2Lib::PathD p;
            for (const P3 &q : t) p.push_back(Clipper2Lib::PointD(q.x, q.y));
            double a = aire(p);
            if (std::fabs(a) < 1e-6) continue;             // paroi verticale : rien à projeter
            if (a < 0) std::reverse(p.begin(), p.end());   // tout en CCW
            morceaux.push_back(p);
        }
        if (!morceaux.empty()) {
            morceaux.insert(morceaux.end(), ombre.begin(), ombre.end());
            ombre = Clipper2Lib::Union(morceaux, Clipper2Lib::FillRule::NonZero, 4);
            ombre = Clipper2Lib::SimplifyPaths(ombre, 0.005);
        }
        Couche3D c;
        c.z = nv.first;
        c.facePlane = nv.second;
        c.ombre = versQt(ombre);
        res << c;
        zPrec = nv.first;
    }
    return res;
}

Contours2D Decoupe3D::decaler(const Contours2D &c, double delta)
{
    if (std::fabs(delta) < 1e-9) return c;
    return versQt(Clipper2Lib::InflatePaths(versClip(c), delta, Clipper2Lib::JoinType::Round,
                                            Clipper2Lib::EndType::Polygon, 2.0, 4));
}

Contours2D Decoupe3D::intersection(const Contours2D &a, const Contours2D &b)
{
    return versQt(Clipper2Lib::Intersect(versClip(a), versClip(b), Clipper2Lib::FillRule::NonZero, 4));
}

Contours2D Decoupe3D::zoneAVider(const Contours2D &limite, const Contours2D &ombre, double surep)
{
    const Clipper2Lib::PathsD o = Clipper2Lib::InflatePaths(versClip(ombre), surep, Clipper2Lib::JoinType::Round,
                                                            Clipper2Lib::EndType::Polygon, 2.0, 4);
    Clipper2Lib::PathsD r = Clipper2Lib::Difference(versClip(limite), o, Clipper2Lib::FillRule::NonZero, 4);
    // enlever les miettes (< 0,01 mm²)
    Clipper2Lib::PathsD propre;
    for (const auto &p : r) if (std::fabs(aire(p)) > 0.01) propre.push_back(p);
    return versQt(propre);
}
