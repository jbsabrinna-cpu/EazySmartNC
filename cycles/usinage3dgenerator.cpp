#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  usinage3dgenerator.cpp — voir usinage3dgenerator.h
// ═══════════════════════════════════════════════════════════════
#include "usinage3dgenerator.h"
#include "../cadimporter.h"
#include "../tooldatabase.h"
#include "../clipper2/clipper.h"
#include "../contourelement.h"
#include "../uniteditor.h"
#include "contourgenerator.h"
#include "../progression.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QRegularExpression>

#include <QJsonArray>
#include <QSet>
#include <QMap>
#include <QFileInfo>
#include <QtMath>
#include <QDebug>
#include <functional>
#include <algorithm>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

using namespace Clipper2Lib;

PathsD versClip(const Contours2D &cs)
{
    PathsD r;
    for (const Contour2D &c : cs) {
        PathD p;
        for (const QPointF &q : c) p.push_back(PointD(q.x(), q.y()));
        if (p.size() >= 3) r.push_back(p);
    }
    return r;
}

// Bords de contours fermés, coupés par une zone : morceaux ouverts
PathsD bordsDans(const PathsD &fermes, const PathsD &zone)
{
    PathsD ouverts;
    for (PathD p : fermes) {
        if (p.size() < 3) continue;
        p.push_back(p.front());                 // refermer en chemin ouvert
        ouverts.push_back(p);
    }
    ClipperD c(4);
    c.AddOpenSubject(ouverts);
    c.AddClip(zone);
    PathsD ferm, ouv;
    c.Execute(ClipType::Intersection, FillRule::NonZero, ferm, ouv);
    PathsD res;
    for (const PathD &p : ouv) if (p.size() >= 2) res.push_back(p);
    return res;
}

double longueur(const PathD &p)
{
    double L = 0;
    for (size_t i = 1; i < p.size(); ++i) L += std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
    return L;
}

struct Outil {
    int T = 0; double R = 0; int dents = 0; bool boule = false; QString nom;
};

Outil lireOutil(const QString &t)
{
    Outil o;
    bool ok = false;
    o.T = t.trimmed().toInt(&ok);
    if (!ok || o.T <= 0) { o.T = 0; return o; }
    if (const ToolData *td = ToolDatabase::instance().toolByT(o.T)) {
        o.R = td->Routil; o.dents = td->nbDents; o.nom = td->description;
        o.boule = (td->toolType == ToolData::Boule);
    }
    return o;
}

double tr(const QJsonArray &a, int i) { return a.size() > i ? a[i].toDouble() : 0.0; }

// Douglas-Peucker sur une suite de points 3D (garde les extrémités)
void simplifier(const QVector<QVector3D> &in, double tol, QVector<QVector3D> &out)
{
    const int n = in.size();
    if (n <= 2) { out = in; return; }
    QVector<bool> garde(n, false);
    garde[0] = garde[n - 1] = true;
    QVector<QPair<int, int>> pile{{0, n - 1}};
    while (!pile.isEmpty()) {
        auto [a, b] = pile.takeLast();
        const QVector3D A = in[a], B = in[b], AB = B - A;
        const float L2 = AB.lengthSquared();
        double dMax = -1; int iMax = -1;
        for (int i = a + 1; i < b; ++i) {
            const QVector3D AP = in[i] - A;
            const float t = L2 > 1e-12f ? qBound(0.0f, QVector3D::dotProduct(AP, AB) / L2, 1.0f) : 0.0f;
            const double d = (AP - AB * t).length();
            if (d > dMax) { dMax = d; iMax = i; }
        }
        if (dMax > tol && iMax > 0) { garde[iMax] = true; pile << qMakePair(a, iMax) << qMakePair(iMax, b); }
    }
    out.clear();
    for (int i = 0; i < n; ++i) if (garde[i]) out << in[i];
}


// ── Carte des hauteurs de la pièce (plus haut Z de la pièce à chaque nœud) ──
const float VIDE_H = -1e9f;
struct CarteH { double x0 = 0, y0 = 0, res = 0.25; int nx = 0, ny = 0; QVector<float> h; };

CarteH carteHauteurs(const CadDocument &doc, double res, double marge)
{
    CarteH g; g.res = res;
    g.x0 = doc.bbMin[0] - marge; g.y0 = doc.bbMin[1] - marge;
    g.nx = int((doc.bbMax[0] - doc.bbMin[0] + 2 * marge) / res) + 2;
    g.ny = int((doc.bbMax[1] - doc.bbMin[1] + 2 * marge) / res) + 2;
    g.h = QVector<float>(g.nx * g.ny, VIDE_H);
    const int nx = g.nx, ny = g.ny; const double gx0 = g.x0, gy0 = g.y0;
    for (int t = 0; t + 17 < doc.mesh.size(); t += 18) {
        const float *p = doc.mesh.constData() + t;
        const double ax = p[0], ay = p[1], az = p[2], bx = p[6], by = p[7], bz = p[8], cx = p[12], cy = p[13], cz = p[14];
        const double den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        if (std::fabs(den) < 1e-12) continue;
        // Triangle « bombé » : le maillage STEP est fait de facettes plates
        // (quelques µm sous la vraie surface au milieu de chaque triangle) ->
        // plaques visibles sur les dessus. Avec les normales lissées des
        // sommets, chaque arête est remplacée par un arc (flèche estimée), et
        // le triangle par la surface du 2e degré qui passe par ces arcs.
        // Seulement sur les zones lisses et peu raides (sinon facette plate).
        double dq[3][3] = {};                       // flèche des arêtes ab, bc, ca
        bool bombe = false;
        double gxp = 0, gyp = 0;                    // pente du plan du triangle
        {
            auto nrm = [&](int o, double n[3]) {
                n[0] = p[o + 3]; n[1] = p[o + 4]; n[2] = p[o + 5];
                const double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (l < 1e-9) return false;
                n[0] /= l; n[1] /= l; n[2] /= l; return true;
            };
            double na[3], nb[3], nc[3];
            const double ux = bx - ax, uy = by - ay, uz = bz - az, vx = cx - ax, vy = cy - ay, vz = cz - az;
            double fn[3] = {uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
            const double fl = std::sqrt(fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2]);
            if (fl > 1e-12 && nrm(0, na) && nrm(6, nb) && nrm(12, nc)) {
                for (double &v : fn) v /= fl;
                if (fn[2] < 0) for (double &v : fn) v = -v;
                auto haut = [](double n[3]) { if (n[2] < 0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; } };
                haut(na); haut(nb); haut(nc);
                auto dot = [](const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
                if (fn[2] > 0.5 && dot(na, nb) > 0.98 && dot(nb, nc) > 0.98 && dot(na, nc) > 0.98) {
                    const double P[3][3] = {{ax, ay, az}, {bx, by, bz}, {cx, cy, cz}};
                    const double *N[3] = {na, nb, nc};
                    const int ar[3][2] = {{0, 1}, {1, 2}, {2, 0}};
                    for (int e = 0; e < 3; ++e) {
                        const int i = ar[e][0], j = ar[e][1];
                        double d[3], m[3];
                        for (int k = 0; k < 3; ++k) { d[k] = P[j][k] - P[i][k]; m[k] = N[i][k] + N[j][k]; }
                        const double fl2 = -(d[0] * (N[i][0] - N[j][0]) + d[1] * (N[i][1] - N[j][1]) + d[2] * (N[i][2] - N[j][2])) / 8.0;
                        const double ml = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
                        for (int k = 0; k < 3; ++k) dq[e][k] = fl2 * m[k] / ml;
                    }
                    gxp = -fn[0] / fn[2]; gyp = -fn[1] / fn[2];
                    bombe = true;
                }
            }
        }
        const int i0 = qMax(0, int((qMin(ax, qMin(bx, cx)) - gx0) / res)), i1 = qMin(nx - 1, int((qMax(ax, qMax(bx, cx)) - gx0) / res) + 1);
        const int j0 = qMax(0, int((qMin(ay, qMin(by, cy)) - gy0) / res)), j1 = qMin(ny - 1, int((qMax(ay, qMax(by, cy)) - gy0) / res) + 1);
        for (int jj = j0; jj <= j1; ++jj) for (int ii = i0; ii <= i1; ++ii) {
            const double x = gx0 + ii * res, y = gy0 + jj * res;
            const double l1 = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / den;
            const double l2 = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / den;
            const double l3 = 1 - l1 - l2;
            if (l1 < -1e-6 || l2 < -1e-6 || l3 < -1e-6) continue;
            double zz = l1 * az + l2 * bz + l3 * cz;
            if (bombe) {
                double d[3];
                for (int k = 0; k < 3; ++k) d[k] = 4 * (l1 * l2 * dq[0][k] + l2 * l3 * dq[1][k] + l3 * l1 * dq[2][k]);
                zz += qBound(-0.05, d[2] - gxp * d[0] - gyp * d[1], 0.05);
            }
            const float z = float(zz);
            float &hv = g.h[jj * nx + ii];
            if (z > hv) hv = z;
        }
    }
    return g;
}

// Empreinte XY des cavités exclues (faces cliquées en rouge), bouchée pleine.
//  Fonds : triangles projetés. Parois verticales (trou débouchant, alésage) :
//  leur projection est un trait -> trait épaissi ; l'anneau obtenu est
//  rempli (on ne garde que les contours extérieurs).
PathsD empreinteExclue(const CadDocument &doc, const QJsonArray &faces)
{
    if (faces.isEmpty()) return {};
    QSet<int> ex;
    for (const QJsonValue &v : faces) ex.insert(v.toInt());
    PathsD tris, traits;
    for (int t = 0; t < doc.meshFace.size() && (t + 1) * 18 <= doc.mesh.size(); ++t) {
        if (!ex.contains(doc.meshFace[t])) continue;
        const float *p = doc.mesh.constData() + t * 18;
        PathD tr{PointD(p[0], p[1]), PointD(p[6], p[7]), PointD(p[12], p[13])};
        if (std::fabs(Area(tr)) < 1e-4) {                   // paroi : le plus long côté
            int k = 0; double dm = -1;
            for (int i = 0; i < 3; ++i) {
                const double d = std::hypot(tr[(i + 1) % 3].x - tr[i].x, tr[(i + 1) % 3].y - tr[i].y);
                if (d > dm) { dm = d; k = i; }
            }
            if (dm > 1e-4) traits.push_back(PathD{tr[k], tr[(k + 1) % 3]});
            continue;
        }
        if (Area(tr) < 0) std::reverse(tr.begin(), tr.end());
        tris.push_back(tr);
    }
    if (!traits.empty()) {
        const PathsD ep = InflatePaths(traits, 0.06, JoinType::Round, EndType::Round, 2.0, 4);
        tris.insert(tris.end(), ep.begin(), ep.end());
    }
    if (tris.empty()) return {};
    const PathsD emp = InflatePaths(Union(tris, FillRule::NonZero, 4), 0.05, JoinType::Round, EndType::Polygon, 2.0, 4);
    PathsD res;
    for (const PathD &c : emp) if (Area(c) > 0.01) res.push_back(c);   // extérieurs seulement : plein
    return res;
}

// Remplit la carte dans l'empreinte en PROLONGEANT la surface autour
// (« peau tendue » : équation de Laplace, bords = la pièce juste autour).
// masque[i] = 1 pour les nœuds remplis. La surface n'est jamais baissée.
void remplirExclus(CarteH &g, const PathsD &emp, QVector<char> &masque)
{
    masque = QVector<char>(g.nx * g.ny, 0);
    if (emp.empty()) return;
    const int nx = g.nx, ny = g.ny;
    QVector<int> cel;
    for (const PathD &c : emp) {
        const Rect<double> bb = GetBounds(c);
        const int i0 = qMax(1, int((bb.left - g.x0) / g.res)), i1 = qMin(nx - 2, int((bb.right - g.x0) / g.res) + 1);
        const int j0 = qMax(1, int((bb.top - g.y0) / g.res)), j1 = qMin(ny - 2, int((bb.bottom - g.y0) / g.res) + 1);
        for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) {
            const int k = j * nx + i;
            if (masque[k]) continue;
            if (PointInPolygon(PointD(g.x0 + i * g.res, g.y0 + j * g.res), c) == PointInPolygonResult::IsOutside) continue;
            masque[k] = 1; cel << k;
        }
    }
    if (cel.isEmpty()) return;
    const int vois[4] = {1, -1, nx, -nx};
    auto ajouter = [&](int q) {
        const int i = q % nx, j = q / nx;
        if (i < 1 || j < 1 || i > nx - 2 || j > ny - 2 || masque[q]) return false;
        masque[q] = 1; cel << q; return true;
    };
    // bord pris 0,75 mm plus loin : jamais sur l'arête de la cavité
    for (int pas = 0; pas < 3; ++pas) {
        const QVector<int> c0 = cel;
        for (int k : c0) for (int d : vois) if (g.h[k + d] > VIDE_H) ajouter(k + d);   // pas hors de la pièce
    }
    // Par cavité (morceau connexe) : 1) surface du 2e degré ajustée sur la
    // couronne de pièce autour -> la courbure est prolongée (une peau tendue
    // seule reste trop plate : les couches « sautent » au passage) ;
    // 2) peau tendue sur le petit écart restant -> raccord exact au bord.
    QVector<float> f = g.h;
    QVector<int> comp(nx * ny, -1);
    QVector<double> ec(nx * ny, 0.0);
    QVector<char> estCreux(nx * ny, 0);
    int nc = 0;
    for (int k0 : cel) {
        if (comp[k0] >= 0) continue;
        QVector<int> zone{k0}; comp[k0] = nc;
        for (int i = 0; i < zone.size(); ++i)
            for (int d : vois) { const int q = zone[i] + d; if (masque[q] && comp[q] < 0) { comp[q] = nc; zone << q; } }
        // couronne : nœuds de pièce à 1..3 nœuds du morceau
        QSet<int> cour;
        QVector<int> front = zone;
        QSet<int> vu(zone.begin(), zone.end());
        for (int pas = 0; pas < 3; ++pas) {
            QVector<int> nf;
            for (int k : front) for (int d : vois) {
                const int q = k + d;
                if (q < 0 || q >= nx * ny || vu.contains(q)) continue;
                vu.insert(q);
                if (masque[q] || g.h[q] <= VIDE_H) continue;
                cour.insert(q); nf << q;
            }
            front = nf;
        }
        if (!cour.isEmpty()) {                               // pas le bas d'un bord de pièce voisin
            QVector<float> hz; for (int q : cour) hz << g.h[q];
            std::nth_element(hz.begin(), hz.begin() + hz.size() / 2, hz.end());
            const float med = hz[hz.size() / 2];
            for (auto it = cour.begin(); it != cour.end(); ) { if (g.h[*it] < med - 1.0f) it = cour.erase(it); else ++it; }
        }
        if (cour.isEmpty()) { for (int k : zone) masque[k] = 0; ++nc; continue; }
        // moindres carrés z = a + b u + c v + d u² + e uv + f v²  (u, v centrés)
        double cx = 0, cy = 0;
        for (int q : cour) { cx += q % nx; cy += q / nx; }
        cx /= cour.size(); cy /= cour.size();
        const int np = cour.size() >= 3 ? 3 : 1;     // plan : la courbure vient des vrais bords
        double A[6][7] = {};
        for (int q : cour) {
            const double u = (q % nx - cx) * g.res, v = (q / nx - cy) * g.res;
            const double t[6] = {1, u, v, u * u, u * v, v * v};
            for (int r = 0; r < np; ++r) { for (int c2 = 0; c2 < np; ++c2) A[r][c2] += t[r] * t[c2]; A[r][6] += t[r] * g.h[q]; }
        }
        double co[6] = {};
        bool ok = true;
        for (int r = 0; r < np && ok; ++r) {                 // Gauss avec pivot
            int pv = r;
            for (int r2 = r + 1; r2 < np; ++r2) if (std::fabs(A[r2][r]) > std::fabs(A[pv][r])) pv = r2;
            if (std::fabs(A[pv][r]) < 1e-12) { ok = false; break; }
            if (pv != r) for (int c2 = 0; c2 < 7; ++c2) std::swap(A[r][c2], A[pv][c2]);
            for (int r2 = 0; r2 < np; ++r2) if (r2 != r) {
                const double m = A[r2][r] / A[r][r];
                for (int c2 = r; c2 < 7; ++c2) A[r2][c2] -= m * A[r][c2];
            }
        }
        if (ok) for (int r = 0; r < np; ++r) co[r] = A[r][6] / A[r][r];
        else { double sb = 0; for (int q : cour) sb += g.h[q]; co[0] = sb / cour.size(); }
        auto quad = [&](int k) {
            const double u = (k % nx - cx) * g.res, v = (k / nx - cy) * g.res;
            return co[0] + co[1] * u + co[2] * v + co[3] * u * u + co[4] * u * v + co[5] * v * v;
        };
        // Inconnus = vrai creux (vide, ou nettement sous le plan). Tout le reste
        // (y compris la bande autour) garde la VRAIE surface et sert d'appui :
        // la peau part exactement du bord réel -> les couches ne sautent plus.
        QVector<int> creux;
        for (int k : zone) if (g.h[k] <= VIDE_H || g.h[k] < quad(k) - 0.05) creux << k;
        for (int k : creux) estCreux[k] = 1;
        for (int k : creux) ec[k] = 0.0;
        for (int it = 0; it < 20000 && !creux.isEmpty(); ++it) {
            double dmax = 0;
            for (int k : creux) {
                double s2 = 0; int n = 0;
                for (int d : vois) {
                    const int q = k + d;
                    if (estCreux[q]) { s2 += ec[q]; ++n; }
                    else if (g.h[q] > VIDE_H) { s2 += g.h[q] - quad(q); ++n; }
                }
                if (n == 0) continue;
                const double old = ec[k], nv = old + 1.8 * (s2 / n - old);
                dmax = qMax(dmax, std::fabs(nv - old));
                ec[k] = nv;
            }
            if (dmax < 1e-6) break;
        }
        // 2e passe : peau « à courbure continue » (biharmonique), partie de la
        // peau tendue. La peau tendue seule reste sous un dessus bombé (jusqu'à
        // 0,02 mm au milieu d'un trou) : la boule marquait le tour des trous.
        {
            auto val = [&](int q, bool &ok) -> double {
                if (q < 0 || q >= nx * ny) { ok = false; return 0; }
                if (estCreux[q]) return ec[q];
                if (g.h[q] > VIDE_H) return g.h[q] - quad(q);
                ok = false; return 0;
            };
            QVector<char> bih(nx * ny, 0);
            for (int k : creux) {
                bool ok = true;
                const int st[12] = {1, -1, nx, -nx, nx + 1, nx - 1, -nx + 1, -nx - 1, 2, -2, 2 * nx, -2 * nx};
                for (int d : st) { val(k + d, ok); if (!ok) break; }
                bih[k] = ok;
            }
            for (int it = 0; it < 40000; ++it) {
                double dmax = 0;
                for (int k : creux) {
                    if (!bih[k]) continue;
                    bool ok = true;
                    const double s8 = val(k + 1, ok) + val(k - 1, ok) + val(k + nx, ok) + val(k - nx, ok);
                    const double s2 = val(k + nx + 1, ok) + val(k + nx - 1, ok) + val(k - nx + 1, ok) + val(k - nx - 1, ok);
                    const double s1 = val(k + 2, ok) + val(k - 2, ok) + val(k + 2 * nx, ok) + val(k - 2 * nx, ok);
                    if (!ok) continue;
                    const double nv = ec[k] + 1.5 * ((8 * s8 - 2 * s2 - s1) / 20.0 - ec[k]);
                    dmax = qMax(dmax, std::fabs(nv - ec[k]));
                    ec[k] = nv;
                }
                if (dmax < 1e-7) break;
            }
        }
        for (int k : creux) f[k] = float(quad(k) + ec[k]);
        ++nc;
    }
    for (int k : cel) if (masque[k]) g.h[k] = qMax(g.h[k], f[k]);
}

// Partie du bouchon plus haute que z (pour l'ombre d'une couche d'ébauche) :
// courbe de niveau exacte (carrés marchants, interpolée) de la surface
// prolongée, limitée aux nœuds bouchés. Pas de marge : le bouchon recouvre
// déjà la pièce sur 0,75 mm autour de la cavité, il s'y soude tout seul.
PathsD bouchonAuDessus(const CarteH &g, const QVector<char> &masque, double z)
{
    const int nx = g.nx, ny = g.ny;
    const float L = float(z), BAS = -1e9f;
    auto val = [&](int a, int b) { const int k = b * nx + a; return masque[k] ? g.h[k] : BAS; };
    auto dedans = [&](int a, int b) { return val(a, b) > L; };
    auto pt = [&](qint64 e) -> PointD {
        const bool vert = e & 1; const qint64 n = e >> 1;
        const int a = int(n % nx), b = int(n / nx);
        const int a2 = vert ? a : a + 1, b2 = vert ? b + 1 : b;
        const float v1 = val(a, b), v2 = val(a2, b2);
        double t = (std::fabs(v2 - v1) > 1e-9f) ? (L - v1) / double(v2 - v1) : 0.5;
        t = qBound(0.0, t, 1.0);
        return PointD(g.x0 + (a + (a2 - a) * t) * g.res, g.y0 + (b + (b2 - b) * t) * g.res);
    };
    QHash<qint64, qint64> suivant;
    bool rien = true;
    for (int b = 0; b + 1 < ny; ++b) for (int a = 0; a + 1 < nx; ++a) {
        const int k = b * nx + a;
        if (!masque[k] && !masque[k + 1] && !masque[k + nx] && !masque[k + nx + 1]) continue;
        const int cas = (dedans(a, b) ? 1 : 0) | (dedans(a + 1, b) ? 2 : 0)
                      | (dedans(a + 1, b + 1) ? 4 : 0) | (dedans(a, b + 1) ? 8 : 0);
        if (cas == 0 || cas == 15) { if (cas == 15) rien = false; continue; }
        rien = false;
        const qint64 B = 2 * (qint64(b) * nx + a), Tt = 2 * (qint64(b + 1) * nx + a);
        const qint64 Lf = 2 * (qint64(b) * nx + a) + 1, R = 2 * (qint64(b) * nx + a + 1) + 1;
        auto seg = [&](qint64 de, qint64 vers) { suivant.insert(de, vers); };
        switch (cas) {
        case 1: seg(B, Lf); break;   case 2: seg(R, B); break;
        case 3: seg(R, Lf); break;   case 4: seg(Tt, R); break;
        case 5: seg(B, R); seg(Tt, Lf); break;
        case 6: seg(Tt, B); break;   case 7: seg(Tt, Lf); break;
        case 8: seg(Lf, Tt); break;  case 9: seg(B, Tt); break;
        case 10: seg(Lf, B); seg(R, Tt); break;
        case 11: seg(R, Tt); break;  case 12: seg(Lf, R); break;
        case 13: seg(B, R); break;   case 14: seg(Lf, B); break;
        }
    }
    if (rien) return {};
    PathsD boucles;
    while (!suivant.isEmpty()) {
        const qint64 depart = suivant.begin().key();
        PathD boucle;
        qint64 e = depart;
        for (int garde = 0; garde < 10000000; ++garde) {
            auto it = suivant.find(e);
            if (it == suivant.end()) break;
            boucle.push_back(pt(e));
            const qint64 n = it.value();
            suivant.erase(it);
            e = n;
            if (e == depart) break;
        }
        if (boucle.size() >= 3) boucles.push_back(SimplifyPath(boucle, 0.005, true));
    }
    return Union(boucles, FillRule::EvenOdd, 4);
}

} // namespace

// ═══════════════════════════════════════════════════════════════
void Orientation3D::appliquer(CadDocument &doc) const
{
    if (identite()) return;
    auto tourne = [this](float *v) {
        const QVector3D q = versMachine(QVector3D(v[0], v[1], v[2]));
        v[0] = q.x(); v[1] = q.y(); v[2] = q.z();
    };
    for (int i = 0; i + 5 < doc.mesh.size(); i += 6) { tourne(doc.mesh.data() + i); tourne(doc.mesh.data() + i + 3); }
    for (int i = 0; i + 2 < doc.edges3d.size(); i += 3) tourne(doc.edges3d.data() + i);
    for (int i = 0; i + 2 < doc.centres3d.size(); i += 3) tourne(doc.centres3d.data() + i);
    // un quart de tour peut inverser l'ordre des sommets : on garde les
    // normales (déjà tournées), l'ordre n'est pas utilisé par les calculs
    for (int k = 0; k < 3; ++k) { doc.bbMin[k] = 1e30f; doc.bbMax[k] = -1e30f; }
    for (int i = 0; i + 5 < doc.mesh.size(); i += 6)
        for (int k = 0; k < 3; ++k) { doc.bbMin[k] = qMin(doc.bbMin[k], doc.mesh[i + k]); doc.bbMax[k] = qMax(doc.bbMax[k], doc.mesh[i + k]); }
}

// ═══════════════════════════════════════════════════════════════
void Usinage3DGenerator::origineEtBrut(const QJsonObject &d, const double pMin[3], const double pMax[3],
                                       double o[3], double bMin[3], double bMax[3])
{
    if (d["brutMode"].toInt(0) == 0) {
        const double sx = d["brutSx"].toDouble(5.0), sy = d["brutSy"].toDouble(5.0);
        bMin[0] = pMin[0] - sx; bMax[0] = pMax[0] + sx;
        bMin[1] = pMin[1] - sy; bMax[1] = pMax[1] + sy;
        bMin[2] = pMin[2] - d["brutZm"].toDouble(3.0); bMax[2] = pMax[2] + d["brutZp"].toDouble(1.0);
    } else {
        const double cx = (pMin[0] + pMax[0]) / 2, cy = (pMin[1] + pMax[1]) / 2;
        const double L = d["brutL"].toDouble(220.0), W = d["brutW"].toDouble(70.0), H = d["brutH"].toDouble(15.0);
        bMin[0] = cx - L / 2; bMax[0] = cx + L / 2; bMin[1] = cy - W / 2; bMax[1] = cy + W / 2;
        if (d["brutZal"].toInt(0) == 1) { bMin[2] = pMin[2]; bMax[2] = bMin[2] + H; }   // pièce en bas du brut
        else { bMax[2] = pMax[2] + d["brutZp"].toDouble(1.0); bMin[2] = bMax[2] - H; }  // en haut (+ surép.)
    }
    // Méthode « point choisi en 3D » : chaque axe a sa référence
    //  valeur (point cliqué : sommet, centre de rayon, face), bmin/bmax
    //  (faces du brut), pmin/pmax (faces de la pièce), pmil/bmil (milieux)
    if (d["origMode"].toInt(0) == 1) {
        const QJsonArray pt = d["origPoint"].toArray(), rf = d["origRef3"].toArray();
        for (int k = 0; k < 3; ++k) {
            const QString tag = rf.size() == 3 ? rf[k].toString() : QString("valeur");
            if      (tag == "bmin") o[k] = bMin[k];
            else if (tag == "bmax") o[k] = bMax[k];
            else if (tag == "pmin") o[k] = pMin[k];
            else if (tag == "pmax") o[k] = pMax[k];
            else if (tag == "pmil") o[k] = (pMin[k] + pMax[k]) / 2;
            else if (tag == "bmil") o[k] = (bMin[k] + bMax[k]) / 2;
            else o[k] = pt.size() == 3 ? pt[k].toDouble() : (k == 2 ? bMax[2] : (bMin[k] + bMax[k]) / 2);
        }
        return;
    }
    const int pos = d["origPos"].toInt(7);
    const bool refBrut = d["origRef"].toInt(0) == 0;
    const double *rMin = refBrut ? bMin : pMin, *rMax = refBrut ? bMax : pMax;
    const int colX = (pos - 1) % 3, rowY = (pos - 1) / 3;
    o[0] = rMin[0] + colX * 0.5 * (rMax[0] - rMin[0]);
    o[1] = rMin[1] + rowY * 0.5 * (rMax[1] - rMin[1]);
    const int oz = d["origZ"].toInt(0);
    o[2] = oz == 0 ? bMax[2] : (oz == 1 ? pMax[2] : pMin[2]);
}

// ═══════════════════════════════════════════════════════════════
//  Plan d'ébauche : niveaux + ombre + zone à vider
// ═══════════════════════════════════════════════════════════════
Usinage3DPlan Usinage3DGenerator::plan(const QJsonObject &d, const CadDocument &doc, double rayonEb)
{
    Usinage3DPlan pl;
    if (doc.mesh.isEmpty()) { pl.erreur = "modele 3D vide ou introuvable"; return pl; }
    // Origine + brut recalculés depuis l'encombrement réel de la pièce
    {
        const double pMin[3] = {doc.bbMin[0], doc.bbMin[1], doc.bbMin[2]};
        const double pMax[3] = {doc.bbMax[0], doc.bbMax[1], doc.bbMax[2]};
        origineEtBrut(d, pMin, pMax, pl.origine, pl.brutMin, pl.brutMax);
    }

    double zBas = d["zBasModele"].toDouble(0.0);
    const bool zone = !d["contoursZone"].toArray().isEmpty() || !d["facesZone"].toArray().isEmpty();
    if (!zone && std::fabs(zBas) < 1e-9) zBas = doc.bbMin[2];      // défaut : dessous de la pièce
    pl.zFin = zBas - d["decoupeSous"].toDouble(0.5);
    const double zHaut = pl.brutMax[2];
    const double passe = qMax(0.05, d["passeEb"].toDouble(2.0));
    const double sFond = d["surepFond"].toDouble(0.2);
    const double sLat  = d["surepLat"].toDouble(0.3);

    // Fonds plats : faces B-rep dont toutes les normales sont vers +Z
    QMap<int, bool> plane; QMap<int, double> zs; QMap<int, int> nb; QMap<int, double> aireF;
    for (int t = 0; t < doc.meshFace.size() && (t + 1) * 18 <= doc.mesh.size(); ++t) {
        const int f = doc.meshFace[t];
        const float *p = doc.mesh.constData() + t * 18;
        bool ok = plane.value(f, true);
        for (int k = 0; k < 3; ++k) { if (p[k * 6 + 5] < 0.999f) ok = false; zs[f] += p[k * 6 + 2]; nb[f] += 1; }
        aireF[f] += 0.5 * std::fabs((p[6] - p[0]) * (p[13] - p[1]) - (p[12] - p[0]) * (p[7] - p[1]));
        plane[f] = ok;
    }
    // Surfaçage : s'il y a du brut au-dessus de la pièce, on descend
    // jusqu'au dessus de la pièce + surép. de fond (niveau « surfaçage »),
    // puis les couches continuent au pas de passe à partir de là.
    QVector<QPair<double, bool>> niv;
    const double zSurf = doc.bbMax[2] + sFond;
    double zDep = zHaut;
    if (zHaut > zSurf + 1e-3) {
        for (double z = zHaut - passe; z > zSurf + 1e-6; z -= passe) niv << qMakePair(z, false);
        niv << qMakePair(zSurf, false);
        zDep = zSurf;
    }
    for (double z = zDep - passe; z > pl.zFin + 1e-6; z -= passe) niv << qMakePair(z, false);
    niv << qMakePair(pl.zFin, false);
    for (auto it = plane.cbegin(); it != plane.cend(); ++it) {
        if (!it.value() || aireF[it.key()] < 1.0) continue;
        const double zf = zs[it.key()] / nb[it.key()] + sFond;
        if (zf >= zHaut - 1e-6 || zf <= pl.zFin + 1e-6) continue;
        bool deja = false;
        for (const auto &n : niv) if (n.second && std::fabs(n.first - zf) < 1e-3) deja = true;
        if (deja) continue;
        for (int k = niv.size() - 1; k >= 0; --k)
            if (!niv[k].second && std::fabs(niv[k].first - zf) < 0.3 && k != niv.size() - 1) niv.remove(k);
        niv << qMakePair(zf, true);
    }
    std::sort(niv.begin(), niv.end(), [](const auto &a, const auto &b){ return a.first > b.first; });
    pl.couches = Decoupe3D::couchesAuxNiveaux(doc.mesh, niv);
    // cavités exclues : bouchées par la surface prolongée (peau tendue) ->
    // l'ébauche ne tourne plus autour, à aucune hauteur
    {
        const PathsD emp = empreinteExclue(doc, d["facesExclues"].toArray());
        if (!emp.empty()) {
            CarteH g = carteHauteurs(doc, 0.25, 2.0);
            QVector<char> masque;
            remplirExclus(g, emp, masque);
            for (Couche3D &c : pl.couches) {
                const PathsD b = bouchonAuDessus(g, masque, c.z);
                if (b.empty()) continue;
                const PathsD u = Union(versClip(c.ombre), b, FillRule::NonZero, 4);
                Contours2D o;
                for (const PathD &p : u) { Contour2D cc; for (const PointD &q : p) cc << QPointF(q.x, q.y); o << cc; }
                c.ombre = o;
            }
        }
    }

    // Limite : le brut, ou la zone choisie agrandie (dans le brut)
    const Contours2D brut{ Contour2D{ QPointF(pl.brutMin[0], pl.brutMin[1]), QPointF(pl.brutMax[0], pl.brutMin[1]),
                                      QPointF(pl.brutMax[0], pl.brutMax[1]), QPointF(pl.brutMin[0], pl.brutMax[1]) } };
    pl.limite = brut;
    // Zone choisie = contours cliqués + empreinte des faces vertes. Avant, les
    // faces vertes n'étaient pas prises : tout le brut était usiné.
    const QJsonArray cz = d["contoursZone"].toArray();
    {
        Contours2D z2;
        for (const QJsonValue &ct : cz) {
            Contour2D c;
            for (const QJsonValue &p : ct.toArray()) c << QPointF(p.toArray()[0].toDouble(), p.toArray()[1].toDouble());
            if (c.size() >= 3) z2 << c;
        }
        for (const PathD &p : empreinteExclue(doc, d["facesZone"].toArray())) {
            Contour2D c;
            for (const PointD &q : p) c << QPointF(q.x, q.y);
            if (c.size() >= 3) z2 << c;
        }
        if (!z2.isEmpty()) {
            PathsD u = Union(versClip(z2), FillRule::NonZero, 4);
            Contours2D zu;
            for (const PathD &p : u) { Contour2D c; for (const PointD &q : p) c << QPointF(q.x, q.y); if (c.size() >= 3) zu << c; }
            pl.zone = zu;
            pl.limite = Decoupe3D::intersection(brut, Decoupe3D::decaler(zu, 2.0 * rayonEb + sLat + 1.0));
        }
    }
    for (Couche3D &c : pl.couches) c.aVider = Decoupe3D::zoneAVider(pl.limite, c.ombre, sLat);
    return pl;
}

// ═══════════════════════════════════════════════════════════════
Usinage3DGenerator::Usinage3DGenerator(const QJsonObject &d, double approche, double sautBride)
    : m_d(d), m_approche(approche), m_sautBride(sautBride) {}

namespace {

// Enveloppe convexe (chaîne monotone)
Contour2D enveloppe(QVector<QPointF> p)
{
    std::sort(p.begin(), p.end(), [](const QPointF &a, const QPointF &b){
        return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y()); });
    if (p.size() < 3) return p;
    auto cr = [](const QPointF &o, const QPointF &a, const QPointF &b){
        return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x()); };
    QVector<QPointF> h(2 * p.size());
    int k = 0;
    for (int i = 0; i < p.size(); ++i) { while (k >= 2 && cr(h[k - 2], h[k - 1], p[i]) <= 0) --k; h[k++] = p[i]; }
    for (int i = p.size() - 2, t = k + 1; i >= 0; --i) { while (k >= t && cr(h[k - 2], h[k - 1], p[i]) <= 0) --k; h[k++] = p[i]; }
    h.resize(k - 1);
    return h;
}

// Dessin d'une couche : UN contour extérieur (les trous sont ignorés,
// la matière y reste). Plusieurs morceaux -> on les relie (fermeture
// du Ø outil), sinon enveloppe : on laisse plus de matière, jamais moins.
// Ce qu'une série de passes (chemins du centre outil) laisse de « zone »
// sans l'avoir touché (rayon R) : bandes oubliées entre les anneaux.
PathsD nonCouvert(const PathsD &zone, const PathsD &trajets, double R)
{
    if (trajets.empty()) return zone;
    const PathsD bande = InflatePaths(trajets, qMax(0.05, R - 0.05), JoinType::Round, EndType::Joined, 2.0, 3);
    PathsD res;
    for (const PathD &p : Difference(zone, bande, FillRule::NonZero, 3))
        if (std::fabs(Area(p)) > 0.02) res.push_back(p);
    return res;
}

// Morceaux de matière d'une couche (extérieurs, reliés si l'outil ne passe pas entre)
PathsD morceauxCouche(const Contours2D &ombre, double rOutil)
{
    PathsD ext;
    for (const PathD &p : versClip(ombre)) {
        double a = 0;
        for (size_t i = 0, n = p.size(); i < n; ++i) a += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
        if (a > 0) ext.push_back(p);                       // extérieurs (CCW)
    }
    if (ext.empty()) return {};
    if (ext.size() > 1) {
        const double g = rOutil + 0.5;
        PathsD f = InflatePaths(InflatePaths(ext, g, JoinType::Round, EndType::Polygon, 2.0, 4),
                                -g, JoinType::Round, EndType::Polygon, 2.0, 4);
        PathsD fe;
        for (const PathD &p : f) {
            double a = 0;
            for (size_t i = 0, n = p.size(); i < n; ++i) a += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
            if (a > 0) fe.push_back(p);
        }
        ext = fe;
    }
    return ext;
}

Contour2D dessinCouche(const Contours2D &ombre, double rOutil)
{
    const PathsD ext = morceauxCouche(ombre, rOutil);
    if (ext.empty()) return {};
    Contour2D r;
    if (ext.size() == 1) {
        for (const PointD &q : SimplifyPath(ext[0], 0.01, true)) r << QPointF(q.x, q.y);
    } else {
        QVector<QPointF> tous;
        for (const PathD &p : ext) for (const PointD &q : p) tous << QPointF(q.x, q.y);
        r = enveloppe(tous);
    }
    return r;
}

// Garder les mouvements d'un cycle Contour (l'outil est appelé une
// seule fois pour tout le bloc : pas de T M6 / S / M5 / M9 à chaque couche)
void ajouterMouvements(QVector<GCodeLine> &out, const QVector<GCodeLine> &cyc, double decalZ = 0.0)
{
    // Le cycle Contour travaille sous Z0 : il est calculé avec Z0 = dessus
    // du brut, puis tous les Z sont décalés de « decalZ » (repère pièce).
    static const QRegularExpression reZ("Z(-?\\d+(?:\\.\\d*)?)");
    for (GCodeLine l : cyc) {
        if (std::fabs(decalZ) > 1e-9 && !l.code.startsWith("(")) {
            QString code; int pos = 0;
            auto it = reZ.globalMatch(l.code);
            while (it.hasNext()) {
                const auto m = it.next();
                code += l.code.mid(pos, m.capturedStart() - pos);
                code += "Z" + QString::number(m.captured(1).toDouble() + decalZ, 'f', 3);
                pos = m.capturedEnd();
            }
            code += l.code.mid(pos);
            l.code = code;
        }
        const QString c = l.code.trimmed();
        if (l.type == GCodeLine::ToolCall || l.type == GCodeLine::Spindle || l.type == GCodeLine::MFunction) continue;
        if (c.startsWith("G4 P") || c.startsWith("( Appel outil")) continue;
        if (l.type == GCodeLine::Blank) continue;
        out << l;
    }
}

} // namespace

// Mémoire du résultat : le G-code du bloc est gardé sur disque ; s'il n'a
// rien de changé (bloc, fichier STEP, outils), il est repris tel quel.
static const char *U3D_VERSION = "u3d-41";

// ── Opérations du bloc ─────────────────────────────────────────
QString Usinage3DGenerator::nomCycle(const QString &t)
{
    if (t == "ebauche")  return QString::fromUtf8("Ébauche par couches");
    if (t == "zconst")   return QString::fromUtf8("Finition Z constant");
    if (t == "balayage") return QString::fromUtf8("Finition balayage");
    if (t == "zbal")     return QString::fromUtf8("Finition Z constant puis balayage");
    if (t == "mixte")    return QString::fromUtf8("Finition mixte");
    return t;
}

QJsonArray Usinage3DGenerator::operations(const QJsonObject &d)
{
    if (d.contains(QStringLiteral("operations"))) return d.value(QStringLiteral("operations")).toArray();
    // Ancien bloc : modeUsin 0 éb+fin, 1 éb, 2 fin ; une seule zone pour les deux
    const QStringList clesZone = {QStringLiteral("facesZone"), QStringLiteral("contoursZone"),
                                  QStringLiteral("zBasModele"), QStringLiteral("zBas")};
    QJsonObject eb, fi;
    eb.insert(QStringLiteral("type"), QStringLiteral("ebauche"));
    eb.insert(QStringLiteral("outil"), d.value(QStringLiteral("outilEb")).toString());
    eb.insert(QStringLiteral("passe"), d.value(QStringLiteral("passeEb")).toDouble(2.0));
    eb.insert(QStringLiteral("recouvrement"), d.value(QStringLiteral("recouvrement")).toInt(40));
    eb.insert(QStringLiteral("surepLat"), d.value(QStringLiteral("surepLat")).toDouble(0.3));
    eb.insert(QStringLiteral("surepFond"), d.value(QStringLiteral("surepFond")).toDouble(0.2));
    const QStringList types = {QStringLiteral("zconst"), QStringLiteral("balayage"), QStringLiteral("zbal"), QStringLiteral("mixte")};
    int strat = 3;
    if (d.contains(QStringLiteral("stratFin"))) strat = d.value(QStringLiteral("stratFin")).toInt(2);
    strat = qBound(0, strat, 3);
    fi.insert(QStringLiteral("type"), types.at(strat));
    fi.insert(QStringLiteral("outil"), d.value(QStringLiteral("outilFin")).toString());
    fi.insert(QStringLiteral("passe"), d.value(QStringLiteral("passeFin")).toDouble(0.5));
    fi.insert(QStringLiteral("pas"), d.value(QStringLiteral("pasFin")).toDouble(0.3));
    fi.insert(QStringLiteral("angleSep"), d.value(QStringLiteral("angleSep")).toDouble(35.0));
    const QStringList cles = {QStringLiteral("s"), QStringLiteral("modeS"), QStringLiteral("avance"),
                              QStringLiteral("modeAvance"), QStringLiteral("avPlongee"), QStringLiteral("modeAvPlongee")};
    for (const QString &c : cles) {
        if (d.contains(c + QStringLiteral("Eb")))  eb.insert(c, d.value(c + QStringLiteral("Eb")));
        if (d.contains(c + QStringLiteral("Fin"))) fi.insert(c, d.value(c + QStringLiteral("Fin")));
    }
    for (const QString &k : clesZone)
        if (d.contains(k)) { eb.insert(k, d.value(k)); fi.insert(k, d.value(k)); }
    const int mode = d.value(QStringLiteral("modeUsin")).toInt(0);
    QJsonArray ops;
    if (mode != 2) ops.append(eb);
    if (mode != 1) ops.append(fi);
    return ops;
}

QJsonObject Usinage3DGenerator::pourOperation(const QJsonObject &d, const QJsonObject &op)
{
    QJsonObject r = d;
    r.remove(QStringLiteral("operations"));
    const QStringList aRetirer = {QStringLiteral("facesZone"), QStringLiteral("contoursZone"), QStringLiteral("zBasModele"),
                                  QStringLiteral("zBas"), QStringLiteral("outilEb"), QStringLiteral("outilFin")};
    for (const QString &k : aRetirer) r.remove(k);
    const QStringList deLOp = {QStringLiteral("facesZone"), QStringLiteral("contoursZone"), QStringLiteral("zBasModele"),
                               QStringLiteral("zBas"), QStringLiteral("decoupeSous")};
    for (const QString &k : deLOp) if (op.contains(k)) r.insert(k, op.value(k));
    const QString t = op.value(QStringLiteral("type")).toString();
    const bool eb = estEbauche(op);
    const QString suf = eb ? QStringLiteral("Eb") : QStringLiteral("Fin");
    const QStringList cles = {QStringLiteral("s"), QStringLiteral("modeS"), QStringLiteral("avance"),
                              QStringLiteral("modeAvance"), QStringLiteral("avPlongee"), QStringLiteral("modeAvPlongee")};
    for (const QString &c : cles) if (op.contains(c)) r.insert(c + suf, op.value(c));
    if (eb) {
        r.insert(QStringLiteral("modeUsin"), 1);
        r.insert(QStringLiteral("outilEb"), op.value(QStringLiteral("outil")).toString());
        r.insert(QStringLiteral("passeEb"), op.value(QStringLiteral("passe")).toDouble(2.0));
        r.insert(QStringLiteral("recouvrement"), op.value(QStringLiteral("recouvrement")).toInt(40));
        r.insert(QStringLiteral("surepLat"), op.value(QStringLiteral("surepLat")).toDouble(0.3));
        r.insert(QStringLiteral("surepFond"), op.value(QStringLiteral("surepFond")).toDouble(0.2));
    } else {
        r.insert(QStringLiteral("modeUsin"), 2);
        r.insert(QStringLiteral("outilFin"), op.value(QStringLiteral("outil")).toString());
        r.insert(QStringLiteral("passeFin"), op.value(QStringLiteral("passe")).toDouble(0.5));
        r.insert(QStringLiteral("pasFin"), op.value(QStringLiteral("pas")).toDouble(0.3));
        r.insert(QStringLiteral("angleSep"), op.value(QStringLiteral("angleSep")).toDouble(35.0));
        r.insert(QStringLiteral("stratFin"), t == QLatin1String("zconst") ? 0 : t == QLatin1String("balayage") ? 1
                                           : t == QLatin1String("zbal") ? 2 : 3);
        r.insert(QStringLiteral("profFin"), op.value(QStringLiteral("profondeurs")).toBool(false));
        r.insert(QStringLiteral("passeProfFin"), op.value(QStringLiteral("passeProf")).toDouble(1.0));
    }
    return r;
}

Contours2D Usinage3DGenerator::zoneOperation(const QJsonObject &op, const CadDocument &doc)
{
    Contours2D z2;
    for (const QJsonValue &ct : op.value(QStringLiteral("contoursZone")).toArray()) {
        Contour2D c;
        for (const QJsonValue &p : ct.toArray()) c << QPointF(p.toArray()[0].toDouble(), p.toArray()[1].toDouble());
        if (c.size() >= 3) z2 << c;
    }
    for (const PathD &p : empreinteExclue(doc, op.value(QStringLiteral("facesZone")).toArray())) {
        Contour2D c;
        for (const PointD &q : p) c << QPointF(q.x, q.y);
        if (c.size() >= 3) z2 << c;
    }
    if (z2.isEmpty()) return z2;
    Contours2D zu;
    for (const PathD &p : Union(versClip(z2), FillRule::NonZero, 4)) {
        Contour2D c; for (const PointD &q : p) c << QPointF(q.x, q.y);
        if (c.size() >= 3) zu << c;
    }
    return zu;
}

QString Usinage3DGenerator::avertissement(const QJsonArray &ops, int i, const CadDocument &doc)
{
    if (i < 0 || i >= ops.size()) return QString();
    const QJsonObject op = ops[i].toObject();
    if (estEbauche(op) || op.value(QStringLiteral("profondeurs")).toBool(false)) return QString();
    if (doc.mesh.isEmpty()) return QString();
    QVector<QJsonObject> ebs;
    for (int k = 0; k < i; ++k) if (estEbauche(ops[k].toObject())) ebs << ops[k].toObject();
    if (ebs.isEmpty())
        return QString::fromUtf8("pas d'ébauche avant : la boule prend toute la matière — ajouter une ébauche avant, "
                                 "ou cocher « Passes en profondeur »");
    Contours2D couvert;
    for (const QJsonObject &e : ebs) {
        const Contours2D z = zoneOperation(e, doc);
        if (z.isEmpty()) return QString();                    // une ébauche sur toute la pièce : tout est fait
        const double R = lireOutil(e.value(QStringLiteral("outil")).toString()).R;
        couvert << Decoupe3D::decaler(z, 2.0 * R + e.value(QStringLiteral("surepLat")).toDouble(0.3) + 1.0);
    }
    Contours2D fin = zoneOperation(op, doc);
    if (fin.isEmpty())
        fin << Contour2D{QPointF(doc.bbMin[0], doc.bbMin[1]), QPointF(doc.bbMax[0], doc.bbMin[1]),
                         QPointF(doc.bbMax[0], doc.bbMax[1]), QPointF(doc.bbMin[0], doc.bbMax[1])};
    else fin = Decoupe3D::decaler(fin, lireOutil(op.value(QStringLiteral("outil")).toString()).R);
    const PathsD reste = Difference(versClip(fin), Union(versClip(couvert), FillRule::NonZero, 4), FillRule::NonZero, 4);
    double aire = 0; for (const PathD &p : reste) aire += std::fabs(Area(p));
    if (aire < 1.0) return QString();
    return QString::fromUtf8("sort de la zone de l'ébauche : matière non ébauchée — mettre l'ébauche sur « Toute la pièce », "
                             "ou cocher « Passes en profondeur »");
}

QVector<GCodeLine> Usinage3DGenerator::generate(int blkId)
{
    if (!m_d.contains("operations")) return generateUne(blkId);   // ancien bloc : tel quel
    QVector<GCodeLine> out;
    const QJsonArray ops = m_d["operations"].toArray();
    if (ops.isEmpty()) { out << comment("!! Usinage 3D : aucune operation", blkId); return out; }
    int n = 0;
    for (const QJsonValue &v : ops) {
        const QJsonObject op = v.toObject();
        ++n;
        out << comment(QString("--- Operation %1/%2 : %3 ---").arg(n).arg(ops.size()).arg(nomCycle(op["type"].toString())), blkId);
        if (!op.value(QStringLiteral("avertissement")).toString().isEmpty()) {
            QString a = op.value(QStringLiteral("avertissement")).toString().normalized(QString::NormalizationForm_D);
            QString asc; for (const QChar c : a) if (c.unicode() >= 32 && c.unicode() < 127 && c != '(' && c != ')') asc += c;
            out << comment("!! ATTENTION : " + asc.simplified(), blkId);
        }
        Usinage3DGenerator g(pourOperation(m_d, op), m_approche, m_sautBride);
        out << g.generateUne(blkId);
        if (Progression::annuler) break;
    }
    return out;
}

QVector<GCodeLine> Usinage3DGenerator::generateUne(int blkId)
{
    const QString fichier0 = m_d["fichier"].toString();
    QByteArray cle = QJsonDocument(m_d).toJson(QJsonDocument::Compact);
    cle += QByteArray::number(m_approche) + "|" + QByteArray::number(m_sautBride) + "|" + U3D_VERSION;
    cle += "|" + QByteArray::number(QFileInfo(fichier0).lastModified().toMSecsSinceEpoch());
    for (const char *k : {"outilEb", "outilFin"}) {
        const Outil o = lireOutil(m_d[QString::fromLatin1(k)].toString());
        cle += "|" + QByteArray::number(o.T) + ":" + QByteArray::number(o.R, 'f', 4) + ":" +
               QByteArray::number(o.dents) + ":" + QByteArray::number(int(o.boule));
    }
    const QString dosCache = QDir::tempPath() + "/eazync_u3d";
    QDir().mkpath(dosCache);
    const QString fCache = dosCache + "/cache_" +
        QString::fromLatin1(QCryptographicHash::hash(cle, QCryptographicHash::Md5).toHex()) + ".txt";
    {
        QFile f(fCache);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QVector<GCodeLine> out;
            QTextStream ts(&f);
            ts.setEncoding(QStringConverter::Utf8);
            while (!ts.atEnd()) {
                const QStringList p = ts.readLine().split(QChar(0x1F));
                if (p.size() < 3) continue;
                GCodeLine l; l.type = GCodeLine::Type(p[0].toInt()); l.code = p[1]; l.comment = p[2]; l.blockId = blkId;
                out << l;
            }
            if (!out.isEmpty()) { qDebug() << "[U3D] G-code repris de la memoire" << fCache; return out; }
        }
    }
    QVector<GCodeLine> out = calculer(blkId);
    if (Progression::annuler) return out;          // calcul annulé : rien en mémoire
    QFile f(fCache);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        for (const GCodeLine &l : out)
            ts << int(l.type) << QChar(0x1F) << l.code << QChar(0x1F) << l.comment << "\n";
    }
    return out;
}

QVector<GCodeLine> Usinage3DGenerator::calculer(int blkId)
{
    QVector<GCodeLine> out;
    const QString fichier = m_d["fichier"].toString();
    out << blank();
    out << comment("===== USINAGE 3D : " + QFileInfo(fichier).fileName() + " =====", blkId);
    if (fichier.isEmpty()) { out << comment("!! aucun fichier 3D", blkId); return out; }

    CadDocument docBrut = CadImporter::load(fichier);
    // Sens des axes choisi dans le BLK FORM : tout est calculé dans le repère machine
    const Orientation3D orient = Orientation3D::depuis(m_d["orient"].toArray());
    if (!orient.identite()) {
        orient.appliquer(docBrut);
        QJsonArray cz;
        double zMin = 1e30;
        for (const QJsonValue &ct : m_d["contoursZone"].toArray()) {
            QJsonArray pa;
            for (const QJsonValue &p : ct.toArray()) {
                const QJsonArray a = p.toArray();
                const QVector3D q = orient.versMachine(QVector3D(a[0].toDouble(), a[1].toDouble(), a[2].toDouble()));
                pa.append(QJsonArray{q.x(), q.y(), q.z()});
                zMin = qMin(zMin, double(q.z()));
            }
            cz.append(pa);
        }
        m_d["contoursZone"] = cz;
        m_d["zBasModele"] = cz.isEmpty() ? double(docBrut.bbMin[2]) : zMin;
    }
    const CadDocument &doc = docBrut;
    const int mode = m_d["modeUsin"].toInt(0);              // 0 éb+fin, 1 éb, 2 fin
    const Outil oEb = lireOutil(m_d["outilEb"].toString());
    const Outil oFin = lireOutil(m_d["outilFin"].toString());
    const Usinage3DPlan pl = plan(m_d, doc, oEb.R);
    if (!pl.erreur.isEmpty()) { out << comment("!! " + pl.erreur, blkId); return out; }

    const double ox = pl.origine[0], oy = pl.origine[1], oz = pl.origine[2];
    const double zSecu = pl.brutMax[2] - oz + m_approche;       // repère pièce
    const double zDessus = pl.brutMax[2] - oz;                  // dessus du brut, repère pièce
    const QString dossier = QDir::tempPath() + "/eazync_u3d";
    QDir().mkpath(dossier);

    // Contour de base commun aux couches (brut, avances, outil...)
    auto base = [&](const Outil &ot, const QString &suf) {
        ContourData cd;
        cd.toolRef      = QString::number(ot.T);
        cd.spindleVal   = m_d["s" + suf].toDouble();
        cd.spindleByVc  = m_d["modeS" + suf].toInt(0) == 1;
        cd.avanceVal    = m_d["avance" + suf].toDouble();
        cd.avanceMode   = m_d["modeAvance" + suf].toInt(0);
        cd.avanceFinition = cd.avanceVal;
        cd.avanceFinMode  = cd.avanceMode;
        cd.descenteVal  = m_d["avPlongee" + suf].toDouble();
        cd.descenteMode = m_d["modeAvPlongee" + suf].toInt(0);
        cd.approche     = m_approche;      // au-dessus du brut (Z0 du cycle = dessus du brut)
        cd.sautBride    = m_approche;      // pas de remontée à 50 mm entre couches
        cd.positionOutil = 0;              // extérieur auto : ébauche depuis le brut
        cd.blkValid = true;
        cd.blkXmin = pl.brutMin[0] - ox; cd.blkXmax = pl.brutMax[0] - ox;
        cd.blkYmin = pl.brutMin[1] - oy; cd.blkYmax = pl.brutMax[1] - oy;
        cd.surepProf = 0.0;
        return cd;
    };
    // Écrit le dessin d'une couche en .cnt (repère pièce)
    auto ecrireDessin = [&](const Contour2D &ct, const QString &nom) -> QString {
        ContourFile cf;
        cf.name = nom;
        for (int i = 0; i <= ct.size(); ++i) {
            const QPointF &p = ct[i % ct.size()];
            ContourElement e; e.type = ContourElement::L;
            e.x2 = p.x() - ox; e.y2 = p.y() - oy;
            cf.elements << e;
        }
        const QString chemin = dossier + "/" + nom + ".cnt";
        cf.save(chemin);
        return chemin;
    };
    auto appelOutil = [&](const Outil &ot, const QString &suf) {
        out << emitToolBlock(QString::number(ot.T), false, m_d["s" + suf].toDouble(),
                             m_d["modeS" + suf].toInt(0) == 1, 2.0 * ot.R, true, "", "", blkId);
        out << line(GCodeLine::Rapid, "G0 Z" + fc(zSecu), "Hauteur de securite", blkId);
    };

    // ─────────────── ÉBAUCHE : une couche = un dessin = une passe ───────────────
    if (mode == 0 || mode == 1) {
        if (oEb.T <= 0 || oEb.R <= 0) {
            out << comment("!! ebauche : outil d'ebauche absent ou sans rayon dans le tableau", blkId);
        } else {
            const double sLat = m_d["surepLat"].toDouble(0.3);
            out << comment(QString("EBAUCHE  T%1 %2  %3 couches  surep. lat %4 / fond %5")
                           .arg(oEb.T).arg(oEb.nom).arg(pl.couches.size())
                           .arg(fc(sLat), fc(m_d["surepFond"].toDouble())), blkId);
            appelOutil(oEb, "Eb");
            int n = 0;
            double zAvant = zDessus;                         // Z de la couche précédente (déjà vidée)
            for (const Couche3D &c : pl.couches) {
                ++n;
                Progression::phase = 1; Progression::nbEtapes = int(pl.couches.size()); Progression::etape = n;
                if (Progression::annuler) break;
                const double z = c.z - oz;
                out << comment(QString("--- Couche %1/%2  Z%3%4 ---").arg(n).arg(pl.couches.size())
                               .arg(fc(z), c.facePlane ? "  (fond plat)" : ""), blkId);
                Contour2D ct = dessinCouche(c.ombre, oEb.R);
                ContourData cd = base(oEb, "Eb");
                cd.modeUsin    = 1;                          // ébauche seule
                cd.surepLat    = sLat;
                cd.stepOverPct = 100 - m_d["recouvrement"].toInt(40);
                cd.profondeur  = zDessus - z;                // profondeur depuis le dessus du brut
                cd.profPasse   = cd.profondeur;              // une seule passe, à ce Z
                cd.plongeeRapide = true;                     // entrée par le bord du brut (dans le vide)
                if (ct.size() < 3) {
                    // Rien de la pièce au-dessus de ce Z : SURFAÇAGE en escargot
                    // de toute la limite (brut ou zone), du bord vers le centre.
                    double SEb = m_d["sEb"].toDouble();
                    if (m_d["modeSEb"].toInt(0) == 1 && oEb.R > 0) SEb = SEb * 1000.0 / (M_PI * 2.0 * oEb.R);
                    const QString FEb  = avanceStr(m_d["avanceEb"].toDouble(), m_d["modeAvanceEb"].toInt(0), SEb, oEb.dents);
                    const QString FpEb = avanceStr(m_d["avPlongeeEb"].toDouble(), m_d["modeAvPlongeeEb"].toInt(0), SEb, oEb.dents);
                    const double pas = qMax(0.1, 2.0 * oEb.R * (100 - m_d["recouvrement"].toInt(40)) / 100.0);
                    out << comment(QString("    surfacage escargot, pas %1 mm").arg(fc(pas)), blkId);
                    PathsD anneau = versClip(pl.limite);
                    bool premier = true;
                    int garde = 0;
                    PathsD faits;
                    bool reste = false;                       // 2e tour : bandes oubliées
                    while (garde++ < 2000) {
                        if (anneau.empty()) {
                            if (reste) break;
                            reste = true;
                            anneau = nonCouvert(versClip(pl.limite), faits, oEb.R);
                            if (anneau.empty()) break;
                            out << comment("    reprise de la bande restee au milieu", blkId);
                        }
                        for (const PathD &p0 : anneau) {
                            const PathD p = SimplifyPath(p0, 0.01, true);
                            if (p.size() < 3) continue;
                            faits.push_back(p);
                            if (premier) {
                                out << line(GCodeLine::Rapid, "G0 X" + fc(p[0].x - ox) + " Y" + fc(p[0].y - oy), "", blkId);
                                out << line(GCodeLine::Rapid, "G0 Z" + fc(qMin(zSecu, z + 1.0)), "", blkId);
                                out << line(GCodeLine::Linear, "G1 Z" + fc(z) + " " + FpEb, "Plongee au bord du brut", blkId);
                                premier = false;
                            } else {
                                out << line(GCodeLine::Linear, "G1 X" + fc(p[0].x - ox) + " Y" + fc(p[0].y - oy) + " " + FEb, "", blkId);
                            }
                            for (size_t q = 1; q <= p.size(); ++q) {
                                const PointD &pq = p[q % p.size()];
                                out << line(GCodeLine::Linear, "G1 X" + fc(pq.x - ox) + " Y" + fc(pq.y - oy)
                                                               + (q == 1 ? " " + FEb : QString()), "", blkId);
                            }
                        }
                        anneau = reste ? PathsD{} : InflatePaths(anneau, -pas, JoinType::Round, EndType::Polygon, 2.0, 3);
                    }
                    out << line(GCodeLine::Rapid, "G0 Z" + fc(zSecu), "", blkId);
                    zAvant = z;
                    continue;
                }
                const PathsD morc = morceauxCouche(c.ombre, oEb.R);
                if (morc.size() > 1) {
                    // Plusieurs îlots de matière à ce Z : poche autour de CHACUN
                    // (avant : une enveloppe autour de tous -> îlot à bords droits)
                    double SEb = m_d["sEb"].toDouble();
                    if (m_d["modeSEb"].toInt(0) == 1 && oEb.R > 0) SEb = SEb * 1000.0 / (M_PI * 2.0 * oEb.R);
                    const QString FEb  = avanceStr(m_d["avanceEb"].toDouble(), m_d["modeAvanceEb"].toInt(0), SEb, oEb.dents);
                    const QString FpEb = avanceStr(m_d["avPlongeeEb"].toDouble(), m_d["modeAvPlongeeEb"].toInt(0), SEb, oEb.dents);
                    const double pas = qMax(0.1, 2.0 * oEb.R * (100 - m_d["recouvrement"].toInt(40)) / 100.0);
                    const PathsD obst = InflatePaths(morc, oEb.R + sLat, JoinType::Round, EndType::Polygon, 2.0, 4);
                    out << comment(QString("    poche autour de %1 ilots, pas %2 mm").arg(morc.size()).arg(fc(pas)), blkId);
                    // un trajet à fond de couche traverse-t-il un îlot ?
                    auto traverse = [&](const PointD &a, const PointD &b) {
                        ClipperD cl(4);
                        cl.AddOpenSubject(PathsD{PathD{a, b}});
                        cl.AddClip(obst);
                        PathsD f1, o1;
                        cl.Execute(ClipType::Intersection, FillRule::NonZero, f1, o1);
                        for (const PathD &q : o1) if (longueur(q) > 0.01) return true;
                        return false;
                    };
                    const double zAppro = qMin(zSecu, zAvant + 0.5);
                    const PathsD zoneC = Difference(versClip(pl.limite), obst, FillRule::NonZero, 4);
                    PathsD anneau = zoneC;
                    PointD pos(0, 0); bool aPos = false;
                    int garde = 0;
                    PathsD faits;
                    bool reste = false;
                    while (garde++ < 2000) {
                        if (anneau.empty()) {
                            if (reste) break;
                            reste = true;
                            anneau = nonCouvert(zoneC, faits, oEb.R);
                            if (anneau.empty()) break;
                        }
                        for (const PathD &p0 : anneau) {
                            PathD p = SimplifyPath(p0, 0.01, true);
                            if (p.size() < 3) continue;
                            faits.push_back(p);
                            if (aPos) {                                   // départ au plus près
                                size_t im = 0; double dm = 1e30;
                                for (size_t i = 0; i < p.size(); ++i) {
                                    const double dd = std::hypot(p[i].x - pos.x, p[i].y - pos.y);
                                    if (dd < dm) { dm = dd; im = i; }
                                }
                                std::rotate(p.begin(), p.begin() + im, p.end());
                            }
                            if (!aPos || traverse(pos, p[0])) {
                                if (aPos) out << line(GCodeLine::Rapid, "G0 Z" + fc(zSecu), "", blkId);
                                out << line(GCodeLine::Rapid, "G0 X" + fc(p[0].x - ox) + " Y" + fc(p[0].y - oy), "", blkId);
                                out << line(GCodeLine::Rapid, "G0 Z" + fc(zAppro), "", blkId);
                                out << line(GCodeLine::Linear, "G1 Z" + fc(z) + " " + FpEb, "", blkId);
                            } else {
                                out << line(GCodeLine::Linear, "G1 X" + fc(p[0].x - ox) + " Y" + fc(p[0].y - oy) + " " + FEb, "", blkId);
                            }
                            for (size_t q = 1; q <= p.size(); ++q) {
                                const PointD &pq = p[q % p.size()];
                                out << line(GCodeLine::Linear, "G1 X" + fc(pq.x - ox) + " Y" + fc(pq.y - oy)
                                                               + (q == 1 ? " " + FEb : QString()), "", blkId);
                            }
                            pos = p[0]; aPos = true;
                        }
                        anneau = reste ? PathsD{} : InflatePaths(anneau, -pas, JoinType::Round, EndType::Polygon, 2.0, 3);
                    }
                    out << line(GCodeLine::Rapid, "G0 Z" + fc(zSecu), "", blkId);
                    zAvant = z;
                    continue;
                }
                cd.contourName = QString("u3d_eb_%1").arg(n, 2, 10, QChar('0'));
                cd.contourFile = ecrireDessin(ct, cd.contourName);
                ContourGenerator gen(cd);
                ajouterMouvements(out, gen.generate(blkId), zDessus);
                zAvant = z;
            }
            out << line(GCodeLine::Rapid, "G0 Z" + fc(zSecu), "Fin ebauche", blkId);
        }
    }

    // ─────────────── FINITION : un dessin par couche fine, contournage ───────────────
    //  Fraise boule : le centre de la boule doit rester hors de
    //     ∪ ombre(z + h) ⊕ ρ(h),  ρ(h) = √(2rh − h²),  h ∈ [0, r]
    //  (la boule touche une pente plus haut que sa pointe). Le bord de cette
    //  zone est le CHEMIN DU CENTRE : usiné « sur la ligne » (R0).
    //  Fraise plate : ombre(z) ⊕ r.
    if (mode == 0 || mode == 2) {
        if (oFin.T <= 0 || oFin.R <= 0) {
            out << comment("!! finition : outil de finition absent ou sans rayon dans le tableau", blkId);
        } else {
            const double passeFin = qMax(0.05, m_d["passeFin"].toDouble(0.5));
            const double ecartMax = qMax(0.02, m_d["pasFin"].toDouble(0.3));
            const double r = oFin.R;
            // Limite de la finition : zone choisie agrandie du rayon de l'outil
            // (la boule finit tout le bord de la zone), sinon le brut entier.
            Contours2D limFin = pl.limite;
            if (!pl.zone.isEmpty()) limFin = Decoupe3D::intersection(pl.limite, Decoupe3D::decaler(pl.zone, r));
            const bool aZone = !pl.zone.isEmpty();
            const PathsD limFinP = versClip(limFin);
            QVector<double> hs, rho;                             // profil de l'outil
            if (oFin.boule) {
                for (int j = 0; j <= 8; ++j) { const double h = r * j / 8.0; hs << h; rho << std::sqrt(qMax(0.0, 2 * r * h - h * h)); }
            } else { hs << 0.0; rho << r; }

            // ── Carte des hauteurs de la pièce H, puis hauteur de la pointe T ──
            //  T(x,y) = plus haute position de la pointe de l'outil centré en
            //  (x,y) sans toucher la pièce (« drop-cutter »). Le chemin du centre
            //  à une hauteur zt est la courbe de niveau T = zt. Calcul fait UNE fois.
            const double res = 0.25;
            CarteH carte = carteHauteurs(doc, res, r + 2 * res);
            {   // cavités exclues : surface prolongée (la finition passe dessus)
                QVector<char> masque;
                remplirExclus(carte, empreinteExclue(doc, m_d["facesExclues"].toArray()), masque);
            }
            const double gx0 = carte.x0, gy0 = carte.y0;
            const int nx = carte.nx, ny = carte.ny;
            const float VIDE = VIDE_H;
            QVector<float> &H = carte.h;
            QVector<float> T(nx * ny, VIDE);
            QVector<int> src(nx * ny, -1);                             // nœud où l'outil touche la pièce
            {
                struct K { int di, dj; float dz; };
                QVector<K> noyau;
                const int kr = int(r / res) + 1;
                for (int dj = -kr; dj <= kr; ++dj) for (int di = -kr; di <= kr; ++di) {
                    const double dd = std::hypot(di * res, dj * res);
                    if (dd > r) continue;
                    noyau << K{di, dj, float(oFin.boule ? (std::sqrt(r * r - dd * dd) - r) : 0.0)};
                }
                for (int jj = 0; jj < ny; ++jj) for (int ii = 0; ii < nx; ++ii) {
                    const float h = H[jj * nx + ii];
                    if (h <= VIDE) continue;
                    for (const K &k : noyau) {                         // « tampon » de l'outil
                        const int a = ii + k.di, b = jj + k.dj;
                        if (a < 1 || b < 1 || a >= nx - 1 || b >= ny - 1) continue;
                        float &tv = T[b * nx + a];
                        if (h + k.dz > tv) { tv = h + k.dz; src[b * nx + a] = jj * nx + ii; }
                    }
                }
                // Contact entre deux nœuds : la boule ne touche pas forcément PILE
                // un nœud de la grille. Parabole sur les voisins du nœud porteur
                // -> vrai sommet. Sans ça : petits paliers (1-2 µm) en plaques
                // sur les dessus peu pentus, visibles en simulation.
                if (oFin.boule) {
                    auto g = [&](int ta, int tb, int q) -> double {   // hauteur pointe si contact sur le nœud q
                        const float h = H[q];
                        if (h <= VIDE) return -1e30;
                        const double dd = std::hypot((q % nx - ta) * res, (q / nx - tb) * res);
                        if (dd > r) return -1e30;
                        return h + std::sqrt(r * r - dd * dd) - r;
                    };
                    QVector<float> T2 = T;
                    for (int tb = 1; tb + 1 < ny; ++tb) for (int ta = 1; ta + 1 < nx; ++ta) {
                        const int k = tb * nx + ta, q = src[k];
                        if (q < 0) continue;
                        const int qa = q % nx, qb = q / nx;
                        if (qa < 1 || qb < 1 || qa + 1 >= nx || qb + 1 >= ny) continue;
                        const double g0 = g(ta, tb, q);
                        double gain = 0;
                        const double gxm = g(ta, tb, q - 1), gxp = g(ta, tb, q + 1);
                        const double gym = g(ta, tb, q - nx), gyp = g(ta, tb, q + nx);
                        const double cx = gxm - 2 * g0 + gxp, cy = gym - 2 * g0 + gyp;
                        if (gxm > -1e29 && gxp > -1e29 && cx < -1e-9) gain += qMin(0.01, (gxp - gxm) * (gxp - gxm) / (-8 * cx));
                        if (gym > -1e29 && gyp > -1e29 && cy < -1e-9) gain += qMin(0.01, (gyp - gym) * (gyp - gym) / (-8 * cy));
                        if (gain > 0) T2[k] = float(T[k] + gain);
                    }
                    T = T2;
                }
            }
            // ── Bord de zone au POINT DE CONTACT (comme Fusion) ──
            //  Une position de l'outil est gardée si l'endroit où il touche la
            //  pièce est sur les faces choisies. La face est finie jusqu'à son
            //  arête, sans que l'outil descende sur les faces voisines.
            QVector<char> surFaces;                                   // par nœud de H
            if (aZone) {
                surFaces = QVector<char>(nx * ny, 0);
                const PathsD zp = versClip(Decoupe3D::decaler(pl.zone, 0.6 * res));
                for (const PathD &p : zp) {
                    const Rect<double> bb = GetBounds(p);
                    const int i0 = qMax(0, int((bb.left - gx0) / res)), i1 = qMin(nx - 1, int((bb.right - gx0) / res) + 1);
                    const int j0 = qMax(0, int((bb.top - gy0) / res)), j1 = qMin(ny - 1, int((bb.bottom - gy0) / res) + 1);
                    for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) {
                        const int k = j * nx + i;
                        if (surFaces[k]) continue;
                        if (PointInPolygon(PointD(gx0 + i * res, gy0 + j * res), p) != PointInPolygonResult::IsOutside) surFaces[k] = 1;
                    }
                }
            }
            auto contactSurFaces = [&](double x, double y) -> bool {
                if (!aZone) return true;
                const int i = int(std::lround((x - gx0) / res)), j = int(std::lround((y - gy0) / res));
                if (i < 0 || j < 0 || i >= nx || j >= ny) return false;
                const int q = src[j * nx + i];
                return q >= 0 && surFaces[q];
            };
            // ── Finition mixte (stratégie 3) : séparation selon la pente ──
            //  Pente de la surface vue par l'outil (T) : >= angle -> Z constant,
            //  < angle -> balayage. Chaque zone agrandie de ~1 mm pour que les
            //  deux finitions se recouvrent (pas de marche au raccord).
            const int stratM = m_d["stratFin"].toInt(2);
            QVector<char> zoneZ, zoneB;
            if (stratM == 3) {
                const double tanSep = std::tan(qDegreesToRadians(qBound(5.0, m_d["angleSep"].toDouble(35.0), 85.0)));
                QVector<char> raide(nx * ny, 0), doux(nx * ny, 0);
                for (int jj = 1; jj + 1 < ny; ++jj) for (int ii = 1; ii + 1 < nx; ++ii) {
                    const int k = jj * nx + ii;
                    const float c = T[k];
                    if (c <= VIDE) continue;
                    const float a = T[k - 1], b = T[k + 1], u = T[k - nx], v = T[k + nx];
                    if (a <= VIDE || b <= VIDE || u <= VIDE || v <= VIDE) { raide[k] = 1; continue; }   // bord : paroi
                    const double gx = (b - a) / (2 * res), gy = (v - u) / (2 * res);
                    if (std::hypot(gx, gy) >= tanSep) raide[k] = 1; else doux[k] = 1;
                }
                auto dilater = [&](QVector<char> m, int pas) {
                    for (int p = 0; p < pas; ++p) {
                        QVector<char> m2 = m;
                        for (int jj = 1; jj + 1 < ny; ++jj) for (int ii = 1; ii + 1 < nx; ++ii) {
                            const int k = jj * nx + ii;
                            if (m[k]) continue;
                            if (m[k - 1] || m[k + 1] || m[k - nx] || m[k + nx]) m2[k] = 1;
                        }
                        m = m2;
                    }
                    return m;
                };
                const int rec = qMax(1, int(std::lround(1.0 / res)));   // ~1 mm de recouvrement
                zoneZ = dilater(raide, rec);
                zoneB = dilater(doux, rec);
            }
            auto dansMasque = [&](const QVector<char> &m, double x, double y) {
                if (m.isEmpty()) return true;
                const int i = int(std::lround((x - gx0) / res)), j = int(std::lround((y - gy0) / res));
                if (i < 0 || j < 0 || i >= nx || j >= ny) return false;
                return m[j * nx + i] != 0;
            };
            // Courbe de niveau T = zt (carrés marchants) -> contours fermés,
            // zone interdite à gauche (extérieurs dans le sens trigo)
            QMap<qint64, PathsD> cache;
            auto Tval = [&](double x, double y) -> double {
                const double fx = (x - gx0) / res, fy = (y - gy0) / res;
                const int a = int(std::floor(fx)), b = int(std::floor(fy));
                if (a < 0 || b < 0 || a + 1 >= nx || b + 1 >= ny) return VIDE;
                const float t00 = T[b * nx + a], t10 = T[b * nx + a + 1], t01 = T[(b + 1) * nx + a], t11 = T[(b + 1) * nx + a + 1];
                if (t00 <= VIDE || t10 <= VIDE || t01 <= VIDE || t11 <= VIDE)   // bord : le plus haut des voisins (prudent)
                    return qMax(qMax(t00, t10), qMax(t01, t11));
                const double u = fx - a, v = fy - b;
                const double bil = (1 - u) * (1 - v) * t00 + u * (1 - v) * t10 + (1 - u) * v * t01 + u * v * t11;
                // Bicubique (Catmull-Rom) : surface lisse, sans les « cassures »
                // tous les 0,25 mm du bilinéaire (elles marquaient la pièce).
                // (écart avec la vraie surface : très faible à ce pas de grille)
                if (a < 1 || b < 1 || a + 2 >= nx || b + 2 >= ny) return bil;
                auto cr = [](double p0, double p1, double p2, double p3, double t) {
                    return p1 + 0.5 * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));
                };
                double col[4];
                for (int jj = 0; jj < 4; ++jj) {
                    const int row = (b - 1 + jj) * nx;
                    float p[4];
                    for (int ii = 0; ii < 4; ++ii) {
                        p[ii] = T[row + a - 1 + ii];
                        if (p[ii] <= VIDE) return bil;
                    }
                    col[jj] = cr(p[0], p[1], p[2], p[3], u);
                }
                return cr(col[0], col[1], col[2], col[3], v);
            };
            auto chemin = [&](double zt) -> PathsD {
                const qint64 cle = qRound64(zt * 1e4);
                auto itc = cache.constFind(cle);
                if (itc != cache.constEnd()) return itc.value();
                const float L = float(zt);
                auto dedans = [&](int a, int b) { return T[b * nx + a] > L; };
                auto pt = [&](qint64 e) -> PointD {                  // point sur une arête
                    const bool vert = e & 1; const qint64 n = e >> 1;
                    const int a = int(n % nx), b = int(n / nx);
                    const int a2 = vert ? a : a + 1, b2 = vert ? b + 1 : b;
                    const float v1 = T[b * nx + a], v2 = T[b2 * nx + a2];
                    double t = (std::fabs(v2 - v1) > 1e-9f) ? (L - v1) / double(v2 - v1) : 0.5;
                    t = qBound(0.0, t, 1.0);
                    return PointD(gx0 + (a + (a2 - a) * t) * res, gy0 + (b + (b2 - b) * t) * res);
                };
                QHash<qint64, qint64> suivant;
                for (int b = 0; b + 1 < ny; ++b) for (int a = 0; a + 1 < nx; ++a) {
                    const int cas = (dedans(a, b) ? 1 : 0) | (dedans(a + 1, b) ? 2 : 0)
                                  | (dedans(a + 1, b + 1) ? 4 : 0) | (dedans(a, b + 1) ? 8 : 0);
                    if (cas == 0 || cas == 15) continue;
                    const qint64 B = 2 * (qint64(b) * nx + a), Tt = 2 * (qint64(b + 1) * nx + a);
                    const qint64 Lf = 2 * (qint64(b) * nx + a) + 1, R = 2 * (qint64(b) * nx + a + 1) + 1;
                    auto seg = [&](qint64 de, qint64 vers) { suivant.insert(de, vers); };
                    switch (cas) {
                    case 1: seg(B, Lf); break;   case 2: seg(R, B); break;
                    case 3: seg(R, Lf); break;   case 4: seg(Tt, R); break;
                    case 5: seg(B, R); seg(Tt, Lf); break;          // col : régions reliées (prudent)
                    case 6: seg(Tt, B); break;   case 7: seg(Tt, Lf); break;
                    case 8: seg(Lf, Tt); break;  case 9: seg(B, Tt); break;
                    case 10: seg(Lf, B); seg(R, Tt); break;
                    case 11: seg(R, Tt); break;  case 12: seg(Lf, R); break;
                    case 13: seg(B, R); break;   case 14: seg(Lf, B); break;
                    }
                }
                PathsD ext;
                while (!suivant.isEmpty()) {
                    const qint64 depart = suivant.begin().key();
                    PathD boucle;
                    qint64 e = depart;
                    for (int garde = 0; garde < 10000000; ++garde) {
                        auto it = suivant.find(e);
                        if (it == suivant.end()) break;
                        boucle.push_back(pt(e));
                        const qint64 n = it.value();
                        suivant.erase(it);
                        e = n;
                        if (e == depart) break;
                    }
                    if (boucle.size() >= 3 && Area(boucle) > 0) {     // extérieurs ; trous : matière laissée
                        // Lissage : la grille de 0,25 mm donne des zigzags, surtout
                        // sur les pentes douces (chemin « tordu »). Moyenne glissante
                        // légère (déplacement de quelques centièmes au plus).
                        const int nb = int(boucle.size());
                        if (nb >= 8) {
                            for (int it = 0; it < 4; ++it) {
                                PathD l2(boucle.size());
                                for (int i = 0; i < nb; ++i) {
                                    const PointD &a0 = boucle[(i + nb - 1) % nb], &a1 = boucle[i], &a2 = boucle[(i + 1) % nb];
                                    l2[i] = PointD(0.25 * a0.x + 0.5 * a1.x + 0.25 * a2.x, 0.25 * a0.y + 0.5 * a1.y + 0.25 * a2.y);
                                }
                                boucle = l2;
                            }
                        }
                        // Recalage : chaque point remis EXACTEMENT sur T = zt (surface
                        // bicubique, pas de Newton le long de la pente). Sans ça, le
                        // lissage et l'interpolation linéaire font descendre l'outil
                        // de quelques centièmes sous la surface -> traces sur la pièce.
                        for (PointD &q : boucle) {
                            for (int it = 0; it < 4; ++it) {
                                const double v = Tval(q.x, q.y);
                                if (v <= VIDE + 1) break;
                                const double e = 0.02;
                                const double gxp = Tval(q.x + e, q.y), gxm = Tval(q.x - e, q.y);
                                const double gyp = Tval(q.x, q.y + e), gym = Tval(q.x, q.y - e);
                                if (gxp <= VIDE + 1 || gxm <= VIDE + 1 || gyp <= VIDE + 1 || gym <= VIDE + 1) break;
                                const double gx = (gxp - gxm) / (2 * e), gy = (gyp - gym) / (2 * e);
                                const double g2 = gx * gx + gy * gy;
                                if (g2 < 1e-6) break;
                                double dx = -(v - zt) * gx / g2, dy = -(v - zt) * gy / g2;
                                const double d = std::hypot(dx, dy);
                                if (d > 0.1) { dx *= 0.1 / d; dy *= 0.1 / d; }
                                q.x += dx; q.y += dy;
                                if (d < 1e-5) break;
                            }
                        }
                        ext.push_back(SimplifyPath(boucle, 0.001, true));
                    }
                }
                cache.insert(cle, ext);
                return ext;
            };
            // Écart à plat trop grand ? (test rapide Clipper) : la passe basse
            // doit tenir dans la passe haute agrandie de « écart max »
            auto tropEcarte = [&](const PathsD &haut, const PathsD &bas) -> bool {
                if (bas.empty() || haut.empty()) return false;
                const PathsD g = InflatePaths(haut, ecartMax, JoinType::Round, EndType::Polygon, 2.0, 3);
                const PathsD dif = Difference(bas, g, FillRule::NonZero, 3);
                double a = 0;
                for (const PathD &p : dif) a += std::fabs(Area(p));
                return a > 0.05;
            };

            // Couches : pas maximum = passe de finition, puis couches ajoutées
            // tant que l'écart à plat dépasse « écart max » (dessus peu pentu)
            QVector<double> zf;
            for (double z = doc.bbMax[2] - passeFin; z > pl.zFin + 1e-6; z -= passeFin) zf << z;
            zf << pl.zFin;
            {
                QVector<double> res;
                double zHautPrec = doc.bbMax[2];                  // au-dessus : rien
                for (double zb : zf) {
                    QVector<QPair<double, double>> pile{{zHautPrec, zb}};
                    QVector<double> ajout;
                    int garde = 0;
                    while (!pile.isEmpty() && garde++ < 60) {
                        auto [za, zc] = pile.takeLast();
                        if (za - zc < 0.1) { ajout << zc; continue; }    // pas mini ~0,06 mm
                        const PathsD ha = (za >= doc.bbMax[2] - 1e-9) ? PathsD() : chemin(za);
                        if (tropEcarte(ha, chemin(zc))) { const double zm = (za + zc) / 2; pile << qMakePair(zm, zc) << qMakePair(za, zm); }
                        else ajout << zc;
                    }
                    std::sort(ajout.begin(), ajout.end(), std::greater<double>());
                    for (double z : ajout) if (res.isEmpty() || std::fabs(res.last() - z) > 1e-6) res << z;
                    zHautPrec = zb;
                }
                zf = res;
            }

            out << comment(QString("FINITION  T%1 %2 (%3)  %4 couches : pas max %5 mm, ecart max %6 mm, chemin du centre calcule")
                           .arg(oFin.T).arg(oFin.nom, oFin.boule ? "boule" : "plate")
                           .arg(zf.size()).arg(fc(passeFin), fc(ecartMax)), blkId);
            appelOutil(oFin, "Fin");
            double SFin = m_d["sFin"].toDouble();
            if (m_d["modeSFin"].toInt(0) == 1 && oFin.R > 0) SFin = SFin * 1000.0 / (M_PI * 2.0 * oFin.R);
            const QString FFin  = avanceStr(m_d["avanceFin"].toDouble(), m_d["modeAvanceFin"].toInt(0), SFin, oFin.dents);
            const QString FpFin = avanceStr(m_d["avPlongeeFin"].toDouble(), m_d["modeAvPlongeeFin"].toInt(0), SFin, oFin.dents);
            const int strat = m_d["stratFin"].toInt(2);       // 0 Z constant, 1 balayage, 2 les deux, 3 mixte (pente)
            if (strat == 1) zf.clear();                      // balayage seul : pas de niveaux
            int n = 0;
            for (double zt : zf) {
                ++n;
                Progression::phase = 2; Progression::nbEtapes = int(zf.size()); Progression::etape = n;
                if (Progression::annuler) break;
                const double z = zt - oz;
                if (z >= zDessus - 1e-6) continue;
                int k = 0;
                for (const PathD &p : chemin(zt)) {
                    Contour2D ct;
                    for (auto it = p.rbegin(); it != p.rend(); ++it) ct << QPointF(it->x, it->y);   // sens horaire = avalant en M3
                    if (ct.size() < 3) continue;
                    ++k;
                    const GCodeLine titre = comment(QString("--- Finition %1/%2  Z%3%4 ---").arg(n).arg(zf.size()).arg(fc(z))
                                   .arg(k > 1 ? QString("  morceau %1").arg(k) : QString()), blkId);
                    // Passe émise directement : descente PILE sur le chemin (sûr :
                    // l'outil y touche juste la pièce), tour complet, remontée.
                    // Pas d'entrée/sortie tangente (elle peut rentrer dans la pièce).
                    // Finition mixte : seulement les morceaux sur les parois raides.
                    const double zLib = qMin(zSecu, z + m_d["passeEb"].toDouble(2.0) + 1.0);
                    QVector<QVector<QPointF>> morcZ;
                    if (aZone) {
                        // zone choisie : seulement les morceaux du tour dans la zone,
                        // puis (mixte) seulement sur les parois raides
                        PathD fer; for (const QPointF &q : ct) fer.push_back(PointD(q.x(), q.y()));
                        for (const PathD &o : bordsDans(PathsD{fer}, limFinP)) {
                            // points tous les 0,2 mm au plus : le bord tombe au bon endroit
                            QVector<QPointF> pts;
                            for (size_t i = 0; i < o.size(); ++i) {
                                if (i > 0) {
                                    const double dx = o[i].x - o[i - 1].x, dy = o[i].y - o[i - 1].y;
                                    const int nd = int(std::hypot(dx, dy) / 0.2);
                                    for (int k = 1; k <= nd; ++k)
                                        pts << QPointF(o[i - 1].x + dx * k / (nd + 1), o[i - 1].y + dy * k / (nd + 1));
                                }
                                pts << QPointF(o[i].x, o[i].y);
                            }
                            QVector<QPointF> cur;
                            for (const QPointF &q : pts) {
                                if ((zoneZ.isEmpty() || dansMasque(zoneZ, q.x(), q.y())) && contactSurFaces(q.x(), q.y())) cur << q;
                                else { if (cur.size() >= 2) morcZ << cur; cur.clear(); }
                            }
                            if (cur.size() >= 2) morcZ << cur;
                        }
                    } else if (zoneZ.isEmpty()) {
                        QVector<QPointF> tout = ct; tout << ct[0];
                        morcZ << tout;
                    } else {
                        // départ sur un point hors zone pour ne pas couper un morceau en deux
                        int dep = 0;
                        for (int q = 0; q < ct.size(); ++q) if (!dansMasque(zoneZ, ct[q].x(), ct[q].y())) { dep = q; break; }
                        QVector<QPointF> cur;
                        for (int q = 0; q <= ct.size(); ++q) {
                            const QPointF &pq = ct[(dep + q) % ct.size()];
                            if (dansMasque(zoneZ, pq.x(), pq.y())) cur << pq;
                            else { if (cur.size() >= 2) morcZ << cur; cur.clear(); }
                        }
                        if (cur.size() >= 2) morcZ << cur;
                    }
                    if (!morcZ.isEmpty()) out << titre;
                    for (const QVector<QPointF> &m : morcZ) {
                        out << line(GCodeLine::Rapid, "G0 X" + fc(m[0].x() - ox) + " Y" + fc(m[0].y() - oy), "", blkId);
                        out << line(GCodeLine::Rapid, "G0 Z" + fc(zLib), "", blkId);
                        out << line(GCodeLine::Linear, "G1 Z" + fc(z) + " " + FpFin, "Descente sur le chemin", blkId);
                        for (int q = 1; q < m.size(); ++q)
                            out << line(GCodeLine::Linear, "G1 X" + fc(m[q].x() - ox) + " Y" + fc(m[q].y() - oy)
                                                           + (q == 1 ? " " + FFin : QString()), "", blkId);
                        out << line(GCodeLine::Rapid, "G0 Z" + fc(zSecu), "", blkId);
                    }
                }
            }

            // ── Zones plates : toute la surface vidée (plus d'îlot au milieu) ──
            if (strat != 1 && strat != 3)          // mixte : le balayage fait les zones plates
            //  Pour chaque face plane horizontale : zone où la pointe peut
            //  descendre au Z de la face = chemin(z−δ) − chemin(z+δ), vidée
            //  en passes concentriques vers l'intérieur au Z de la face.
            {
                QMap<int, bool> plane; QMap<int, double> zs; QMap<int, int> nb; QMap<int, double> aireF;
                for (int t = 0; t < doc.meshFace.size() && (t + 1) * 18 <= doc.mesh.size(); ++t) {
                    const int f = doc.meshFace[t];
                    const float *p = doc.mesh.constData() + t * 18;
                    bool ok = plane.value(f, true);
                    for (int kk = 0; kk < 3; ++kk) { if (p[kk * 6 + 5] < 0.999f) ok = false; zs[f] += p[kk * 6 + 2]; nb[f] += 1; }
                    aireF[f] += 0.5 * std::fabs((p[6] - p[0]) * (p[13] - p[1]) - (p[12] - p[0]) * (p[7] - p[1]));
                    plane[f] = ok;
                }
                QVector<double> zPlats;
                for (auto it = plane.cbegin(); it != plane.cend(); ++it) {
                    if (!it.value() || aireF[it.key()] < 1.0) continue;
                    const double z0 = zs[it.key()] / nb[it.key()];
                    if (z0 <= pl.zFin + 1e-6 || z0 - oz >= zDessus - 1e-6) continue;
                    bool deja = false;
                    for (double z : zPlats) if (std::fabs(z - z0) < 1e-3) deja = true;
                    if (!deja) zPlats << z0;
                }
                std::sort(zPlats.begin(), zPlats.end(), std::greater<double>());
                // pas entre passes : crête de 0,01 mm pour la boule
                const double pas = oFin.boule ? qBound(0.1, 2.0 * std::sqrt(2.0 * r * 0.01), r) : qMax(0.1, 1.2 * r);
                for (double z0 : zPlats) {
                    PathsD zone = Difference(chemin(z0 - 0.003), chemin(z0 + 0.003), FillRule::NonZero, 3);
                    // limité à la zone choisie (contour agrandi) s'il y en a une
                    if (!limFin.isEmpty()) {
                        PathsD lim;
                        for (const Contour2D &c : limFin) { PathD pp; for (const QPointF &q : c) pp.push_back(PointD(q.x(), q.y())); lim.push_back(pp); }
                        zone = Intersect(zone, lim, FillRule::NonZero, 3);
                    }
                    double aire = 0; for (const PathD &p : zone) aire += std::fabs(Area(p));
                    if (aire < 1.0) continue;
                    const double z = z0 - oz;
                    out << comment(QString("--- Finition zone plate Z%1 : vidage, pas %2 ---").arg(fc(z), fc(pas)), blkId);
                    PathsD anneau = InflatePaths(zone, -pas * 0.5, JoinType::Round, EndType::Polygon, 2.0, 3);
                    int garde = 0;
                    while (!anneau.empty() && garde++ < 2000) {
                        for (const PathD &p0 : anneau) {
                            const PathD p = SimplifyPath(p0, 0.005, true);
                            if (p.size() < 3) continue;
                            Contour2D ct;
                            for (auto it = p.rbegin(); it != p.rend(); ++it) ct << QPointF(it->x, it->y);
                            out << line(GCodeLine::Rapid, "G0 X" + fc(ct[0].x() - ox) + " Y" + fc(ct[0].y() - oy), "", blkId);
                            out << line(GCodeLine::Rapid, "G0 Z" + fc(z + 1.0), "", blkId);
                            out << line(GCodeLine::Linear, "G1 Z" + fc(z) + " " + FpFin, "", blkId);
                            for (int q = 1; q <= ct.size(); ++q) {
                                const QPointF &pq = ct[q % ct.size()];
                                out << line(GCodeLine::Linear, "G1 X" + fc(pq.x() - ox) + " Y" + fc(pq.y() - oy)
                                                               + (q == 1 ? " " + FFin : QString()), "", blkId);
                            }
                            out << line(GCodeLine::Rapid, "G0 Z" + fc(z + 1.0), "", blkId);
                        }
                        anneau = InflatePaths(anneau, -pas, JoinType::Round, EndType::Polygon, 2.0, 3);
                    }
                    out << line(GCodeLine::Rapid, "G0 Z" + fc(zSecu), "", blkId);
                }
            }

            // ── Balayage parallèle : allers-retours qui suivent la surface ──
            //  Lignes dans le sens de la plus grande longueur, écartées de
            //  « écart max ». Z = hauteur de pointe T (drop-cutter) lue tous
            //  les 0,1 mm (bilinéaire), simplifiée à 0,005 mm.
            if (strat != 0) {
                PathsD lim;
                for (const Contour2D &c : limFin) { PathD pp; for (const QPointF &q : c) pp.push_back(PointD(q.x(), q.y())); lim.push_back(pp); }
                auto dansZone = [&](double x, double y) {
                    if (lim.empty()) return true;
                    for (const PathD &pp : lim) if (PointInPolygon(PointD(x, y), pp) != PointInPolygonResult::IsOutside) return true;
                    return false;
                };
                const bool selonY = (doc.bbMax[1] - doc.bbMin[1]) >= (doc.bbMax[0] - doc.bbMin[0]);
                const double pasB = ecartMax;
                const double u0 = selonY ? doc.bbMin[0] - r : doc.bbMin[1] - r, u1 = selonY ? doc.bbMax[0] + r : doc.bbMax[1] + r;
                const double w0 = selonY ? doc.bbMin[1] - r : doc.bbMin[0] - r, w1 = selonY ? doc.bbMax[1] + r : doc.bbMax[0] + r;
                const double pech = 0.1;
                const double zMin = pl.zFin;
                out << comment(QString("FINITION BALAYAGE  %1 lignes selon %2, ecart %3 mm")
                               .arg(int((u1 - u0) / pasB) + 1).arg(selonY ? "Y" : "X").arg(fc(pasB)), blkId);
                // simplification (u, z) : Douglas-Peucker
                std::function<void(const QVector<QPointF>&, int, int, QVector<bool>&)> dp =
                    [&](const QVector<QPointF> &p, int i0, int i1, QVector<bool> &garde) {
                        if (i1 <= i0 + 1) return;
                        const QPointF a = p[i0], b = p[i1];
                        double dMax = -1; int iM = -1;
                        for (int i = i0 + 1; i < i1; ++i) {
                            const double t = (p[i].x() - a.x()) / qMax(1e-9, b.x() - a.x());
                            const double d = std::fabs(p[i].y() - (a.y() + t * (b.y() - a.y())));
                            if (d > dMax) { dMax = d; iM = i; }
                        }
                        if (dMax > 0.0005) { garde[iM] = true; dp(p, i0, iM, garde); dp(p, iM, i1, garde); }
                    };
                // Z au 1/10 de micron : arrondir au micron fait des « marches » de 1 µm
                // qui dessinent des courbes de niveau sur les dessus presque plats.
                auto fz = [](double v) {
                    QString t = QString::number(v, 'f', 4);
                    while (t.endsWith('0')) t.chop(1);
                    if (t.endsWith('.')) t += '0';
                    return t;
                };
                // Passes en profondeur (option) : le même balayage refait de haut en
                // bas, sans jamais descendre sous le Z de la couche (à plat là où la
                // surface est plus bas), seulement là où il reste de la matière
                // au-dessus de la surface ; la dernière passe suit la surface.
                QVector<double> niveaux;
                if (m_d["profFin"].toBool(false)) {
                    const double pp = qMax(0.1, m_d["passeProfFin"].toDouble(1.0));
                    float tMin = 1e30f;
                    for (float tv : T) if (tv > VIDE + 1) tMin = qMin(tMin, tv);
                    tMin = qMax(tMin, float(zMin));
                    for (double L = pl.brutMax[2] - pp; L > tMin + 0.05; L -= pp) niveaux << L;
                }
                niveaux << std::numeric_limits<double>::quiet_NaN();   // passe finale sur la surface
                for (int iv = 0; iv < niveaux.size(); ++iv) {
                const double Lniv = niveaux[iv];
                const bool finale = std::isnan(Lniv);
                const double Lprec = iv == 0 ? pl.brutMax[2] + 1.0 : niveaux[iv - 1];
                if (niveaux.size() > 1)
                    out << comment(finale ? QString("--- balayage : passe finale sur la surface ---")
                                          : QString("--- balayage en profondeur : couche Z%1 ---").arg(fc(Lniv - oz)), blkId);
                int ligne = 0;
                bool sensPlus = true;
                for (double uu = u0; uu <= u1 + 1e-9; uu += pasB, ++ligne) {
                    Progression::phase = 3; Progression::nbEtapes = int((u1 - u0) / pasB) + 1; Progression::etape = ligne + 1;
                    if (Progression::annuler) break;
                    // morceaux continus de la ligne (surface présente et dans la zone)
                    QVector<QVector<QPointF>> morceaux;   // (w, z)
                    QVector<QPointF> cur;
                    const int nPts = int((w1 - w0) / pech) + 1;
                    for (int i = 0; i <= nPts; ++i) {
                        const double w = w0 + i * pech;
                        const double x = selonY ? uu : w, y = selonY ? w : uu;
                        const double t = Tval(x, y);
                        if (t <= VIDE + 1 || !dansZone(x, y) || !dansMasque(zoneB, x, y) || !contactSurFaces(x, y)
                            || (!finale && t >= Lprec - 1e-4)) { if (cur.size() >= 2) morceaux << cur; cur.clear(); continue; }
                        cur << QPointF(w, finale ? qMax(t, zMin) : qMax(qMax(t, Lniv), zMin));
                    }
                    if (cur.size() >= 2) morceaux << cur;
                    // Sortie / entrée en PROLONGEANT la surface : là où la ligne est
                    // coupée par la zone (pas au bord de la pièce), l'outil continue
                    // tout droit sur la tangente de la surface, un rayon d'outil plus
                    // loin, puis remonte. Il ne suit jamais la pièce : si elle passe
                    // au-dessus de la tangente, le prolongement s'arrête avant.
                    {
                        const double Lx = qMin(r, 4.0);
                        const int nx2 = qMax(1, int(Lx / pech));
                        auto prolonger = [&](QVector<QPointF> &m, bool debut) {
                            if (m.size() < 3) return;
                            const int n = int(m.size());
                            const QPointF a = debut ? m[0] : m[n - 1];
                            const int k = qMin(n - 1, 5);
                            const QPointF b = debut ? m[k] : m[n - 1 - k];
                            const double dwv = a.x() - b.x();
                            if (std::fabs(dwv) < 1e-9) return;
                            const double pente = (a.y() - b.y()) / dwv;          // dz par mm, vers l'extérieur
                            const double sgn = dwv > 0 ? 1.0 : -1.0;
                            // coupé par la zone ? (la pièce continue juste après)
                            {
                                const double w = a.x() + sgn * pech;
                                const double x = selonY ? uu : w, y = selonY ? w : uu;
                                if (Tval(x, y) <= VIDE + 1) return;
                            }
                            QVector<QPointF> ext;
                            for (int i = 1; i <= nx2; ++i) {
                                const double w = a.x() + sgn * i * pech;
                                if (w < w0 || w > w1) break;
                                const double x = selonY ? uu : w, y = selonY ? w : uu;
                                const double z = a.y() + pente * i * pech;
                                const double t = Tval(x, y);
                                // tangente pure : si la pièce remonte au-dessus, on s'arrête
                                // AVANT de la toucher (on ne la suit pas)
                                if (t > VIDE + 1 && t > z - 0.002) break;
                                if (z < zMin) break;
                                ext << QPointF(w, z);
                            }
                            if (debut) { std::reverse(ext.begin(), ext.end()); m = ext + m; }
                            else m += ext;
                        };
                        for (auto &m : morceaux) { prolonger(m, true); prolonger(m, false); }
                    }
                    if (!sensPlus) {                                  // aller-retour
                        std::reverse(morceaux.begin(), morceaux.end());
                        for (auto &m : morceaux) std::reverse(m.begin(), m.end());
                    }
                    sensPlus = !sensPlus;
                    for (QVector<QPointF> m : morceaux) {
                        // simplification : w doit croître pour dp -> on travaille dans le sens croissant
                        const bool inv = m.first().x() > m.last().x();
                        if (inv) std::reverse(m.begin(), m.end());
                        QVector<bool> garde(m.size(), false); garde.first() = garde.last() = true;
                        dp(m, 0, m.size() - 1, garde);
                        QVector<QPointF> s;
                        for (int i = 0; i < m.size(); ++i) if (garde[i]) s << m[i];
                        if (inv) std::reverse(s.begin(), s.end());
                        auto XY = [&](double w) { return selonY ? QPointF(uu, w) : QPointF(w, uu); };
                        const QPointF d0 = XY(s.first().x());
                        double zHaut = s.first().y();
                        out << line(GCodeLine::Rapid, "G0 X" + fc(d0.x() - ox) + " Y" + fc(d0.y() - oy), "", blkId);
                        out << line(GCodeLine::Rapid, "G0 Z" + fc(qMin(zSecu, zHaut - oz + 1.0)), "", blkId);
                        out << line(GCodeLine::Linear, "G1 Z" + fz(s.first().y() - oz) + " " + FpFin, "", blkId);
                        for (int i = 1; i < s.size(); ++i) {
                            const QPointF q = XY(s[i].x());
                            out << line(GCodeLine::Linear, "G1 X" + fc(q.x() - ox) + " Y" + fc(q.y() - oy) + " Z" + fz(s[i].y() - oz)
                                                           + (i == 1 ? " " + FFin : QString()), "", blkId);
                        }
                        out << line(GCodeLine::Rapid, "G0 Z" + fc(zSecu), "", blkId);
                    }
                }
                }   // niveaux
            }
        }
    }
    out << emitRetrait(m_sautBride, blkId);
    return out;
}
