#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  pochecontourgenerator.cpp — cycle POCHE DE CONTOUR (liste poches/ilots)
//
//  Principe (facon TNC cycles SL) :
//   region = contour exterieur (CCW) + ilots (CW)
//   niveau k = offset(region, -(rEff + surepLat + k*ae))
//   Chaque niveau = une ou plusieurs ZONES (1 boucle ext + ses trous).
//   Une zone du niveau k+1 est toujours dans UNE zone du niveau k
//   -> arbre. On usine en post-ordre : du coeur vers les parois.
//   Une feuille = matiere pleine -> plongee (directe/helice/rampe).
//   Liaisons entre boucles : directe si le segment reste dans la
//   zone videe, sinon remontee au Z securite.
// ═══════════════════════════════════════════════════════════════
#include "pochecontourgenerator.h"
#include "../contourelement.h"
#include "../tooldatabase.h"
#include "../clipper2utils.h"
#include "../clipper2/clipper.h"
#include <QtMath>
#include <QFileInfo>
#include <algorithm>
#include <functional>

namespace {

// Contour .cnt -> polyligne fermee (sans point final repete)
Path2D loadClosedPath(const QString &file, QString &err)
{
    ContourFile cf;
    if (!cf.load(file)) { err = "lecture impossible : " + QFileInfo(file).fileName(); return {}; }
    Path2D path;
    for (const auto &s : cf.toSegments(64)) {
        if (s.pts.isEmpty()) continue;
        const int start = path.isEmpty() ? 0 : 1;
        for (int k = start; k < s.pts.size(); ++k) path << s.pts[k];
    }
    Path2D c;
    for (const QPointF &p : path)
        if (c.isEmpty() || std::hypot(p.x()-c.last().x(), p.y()-c.last().y()) > 1e-3) c << p;
    if (c.size() < 3) { err = "contour vide : " + QFileInfo(file).fileName(); return {}; }
    if (std::hypot(c.last().x()-c.first().x(), c.last().y()-c.first().y()) > 0.01) {
        err = "contour non ferme : " + QFileInfo(file).fileName(); return {};
    }
    c.removeLast();
    return c;
}

bool pointInPoly(const Path2D &p, const QPointF &q)
{
    bool in = false;
    for (int i = 0, j = p.size()-1; i < p.size(); j = i++) {
        const QPointF &a = p[j], &b = p[i];
        if (((a.y() > q.y()) != (b.y() > q.y())) &&
            (q.x() < (b.x()-a.x())*(q.y()-a.y())/(b.y()-a.y()) + a.x())) in = !in;
    }
    return in;
}
// Dans la zone = dans un nombre impair de boucles (exterieur + trous)
bool pointInArea(const Paths2D &area, const QPointF &q)
{ int n = 0; for (const Path2D &p : area) if (pointInPoly(p, q)) ++n; return n % 2 == 1; }

double distToLoop(const Path2D &p, const QPointF &q)
{
    double best = 1e30;
    for (int i = 0; i < p.size(); ++i) {
        const QPointF &a = p[i], &b = p[(i+1) % p.size()];
        const double dx = b.x()-a.x(), dy = b.y()-a.y(), L2 = dx*dx+dy*dy;
        double t = L2 > 1e-12 ? ((q.x()-a.x())*dx + (q.y()-a.y())*dy)/L2 : 0.0;
        t = qBound(0.0, t, 1.0);
        best = qMin(best, std::hypot(q.x()-(a.x()+t*dx), q.y()-(a.y()+t*dy)));
    }
    return best;
}
double distToLoops(const Paths2D &ps, const QPointF &q)
{ double b = 1e30; for (const Path2D &p : ps) b = qMin(b, distToLoop(p, q)); return b; }

// Point de la zone le plus loin des bords (grille + affinage)
QPointF deepestPoint(const Paths2D &area, double *room = nullptr)
{
    double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
    for (const Path2D &p : area) for (const QPointF &q : p) {
        x0 = qMin(x0, q.x()); x1 = qMax(x1, q.x());
        y0 = qMin(y0, q.y()); y1 = qMax(y1, q.y());
    }
    QPointF best = area.first().first(); double bd = -1;
    auto probe = [&](double cx, double cy, double w, double h, int N) {
        for (int i = 0; i <= N; ++i) for (int j = 0; j <= N; ++j) {
            const QPointF q(cx - w/2 + w*i/N, cy - h/2 + h*j/N);
            if (!pointInArea(area, q)) continue;
            const double d = distToLoops(area, q);
            if (d > bd) { bd = d; best = q; }
        }
    };
    probe((x0+x1)/2, (y0+y1)/2, x1-x0, y1-y0, 30);
    double w = (x1-x0)/15, h = (y1-y0)/15;
    for (int it = 0; it < 4 && bd >= 0; ++it) { probe(best.x(), best.y(), w, h, 10); w /= 5; h /= 5; }
    if (bd < 0) {   // zone tres fine : on prend un sommet
        best = area.first().first(); bd = 0;
    }
    if (room) *room = bd;
    return best;
}

bool segCross(const QPointF &a, const QPointF &b, const QPointF &c, const QPointF &d)
{
    auto cr = [](const QPointF &o, const QPointF &p, const QPointF &q){
        return (p.x()-o.x())*(q.y()-o.y()) - (p.y()-o.y())*(q.x()-o.x()); };
    const double d1 = cr(c,d,a), d2 = cr(c,d,b), d3 = cr(a,b,c), d4 = cr(a,b,d);
    return ((d1 > 1e-9 && d2 < -1e-9) || (d1 < -1e-9 && d2 > 1e-9)) &&
           ((d3 > 1e-9 && d4 < -1e-9) || (d3 < -1e-9 && d4 > 1e-9));
}
// Le segment [a,b] coupe-t-il une des boucles ?
bool segHitsLoops(const QPointF &a, const QPointF &b, const Paths2D &loops)
{
    for (const Path2D &p : loops)
        for (int i = 0; i < p.size(); ++i)
            if (segCross(a, b, p[i], p[(i+1) % p.size()])) return true;
    return false;
}

struct Area {
    Paths2D loops;          // [0] = exterieur CCW, puis trous CW
    QVector<int> children;
    int level = 0;
    int root  = -1;         // zone de niveau 0 a laquelle elle appartient
};

// Regroupe les chemins d'un offset en zones (exterieur + ses trous)
QVector<Paths2D> groupAreas(const Paths2D &paths)
{
    QVector<Paths2D> areas;
    QVector<double>  aArea;
    Paths2D holes;
    for (const Path2D &p : paths) {
        if (p.size() < 3) continue;
        const double s = Clipper2Utils::signedArea(p);
        if (s > 1e-6) { areas << Paths2D{p}; aArea << s; }
        else if (s < -1e-6) holes << p;
    }
    for (const Path2D &h : holes) {
        int bi = -1; double ba = 1e30;
        for (int i = 0; i < areas.size(); ++i)
            if (pointInPoly(areas[i].first(), h.first()) && aArea[i] < ba) { ba = aArea[i]; bi = i; }
        if (bi >= 0) areas[bi] << h;
    }
    return areas;
}


// ── Booleens Clipper2 en double ─────────────────────────────
Clipper2Lib::PathsD toD(const Paths2D &ps)
{
    Clipper2Lib::PathsD r;
    for (const Path2D &p : ps) {
        Clipper2Lib::PathD q;
        for (const QPointF &pt : p) q.push_back(Clipper2Lib::PointD(pt.x(), pt.y()));
        r.push_back(q);
    }
    return r;
}
Paths2D fromD(const Clipper2Lib::PathsD &ps)
{
    Paths2D r;
    for (const auto &p : ps) {
        Path2D q;
        for (const auto &pt : p) q << QPointF(pt.x, pt.y);
        if (q.size() >= 3) r << q;
    }
    return r;
}
// Remet l'orientation d'apres l'imbrication : exterieur CCW, trou CW
// (Clipper2 rend les boucles dans le sens inverse de notre convention)
Paths2D normalize(const Paths2D &ps)
{
    Paths2D r;
    for (int i = 0; i < ps.size(); ++i) {
        int depth = 0;
        for (int j = 0; j < ps.size(); ++j)
            if (j != i && pointInPoly(ps[j], ps[i].first())) ++depth;
        r << ((depth % 2 == 0) ? Clipper2Utils::ensureCCW(ps[i]) : Clipper2Utils::ensureCW(ps[i]));
    }
    return r;
}

Paths2D unite(const Paths2D &a, const Paths2D &b)
{
    if (a.isEmpty()) return normalize(fromD(Clipper2Lib::Union(toD(b), Clipper2Lib::FillRule::NonZero, 4)));
    return normalize(fromD(Clipper2Lib::Union(toD(a), toD(b), Clipper2Lib::FillRule::NonZero, 4)));
}
Paths2D subtract(const Paths2D &a, const Paths2D &b)
{
    if (a.isEmpty()) return {};
    return normalize(fromD(Clipper2Lib::Difference(toD(a), toD(b), Clipper2Lib::FillRule::NonZero, 4)));
}

// Un contour de la liste, charge
struct Entry {
    Path2D pts;          // CCW
    bool   ilot = false;
    double zTop = 0;     // poche : 0 (surface)   ilot : Z depart
    double zBot = 0;     // poche : -prof          ilot : Z depart - prof
    QString name;
};

// Zone a vider a la cote z (z negatif, centre de tranche) : calques dans l'ordre
Paths2D regionAt(const QVector<Entry> &E, double z)
{
    Paths2D r;
    for (const Entry &e : E) {
        if (!e.ilot) { if (z > e.zBot && z < e.zTop) r = unite(r, Paths2D{e.pts}); }
        else         { if (z < e.zTop) r = subtract(r, Paths2D{e.pts}); }
    }
    return r;
}

// Arbre des zones (passes concentriques) pour une region donnee
struct Tree {
    QVector<Area> areas;
    QVector<int>  roots, order;
};
Tree buildTree(const Paths2D &region, double d0, double ae, double rOutil)
{
    Tree T;
    QVector<int> prev;
    double off = d0, lastOff = d0; bool extraDone = false;
    for (int k = 0; k < 3000; ) {
        const QVector<Paths2D> lvl = groupAreas(Clipper2Utils::offset(region, -off, 0));
        if (lvl.isEmpty()) {
            if (k > 0 && !extraDone && ae > 0.95 * rOutil) { extraDone = true; off = lastOff + 0.9 * rOutil; continue; }
            break;
        }
        QVector<int> cur;
        for (const Paths2D &a : lvl) {
            Area A; A.loops = a; A.level = k;
            int parent = -1;
            for (int pi : prev) if (pointInArea(T.areas[pi].loops, a.first().first())) { parent = pi; break; }
            if (parent < 0 && !prev.isEmpty()) {
                double bd = 1e30;
                for (int pi : prev) { const double dd = distToLoops(T.areas[pi].loops, a.first().first());
                                      if (dd < bd) { bd = dd; parent = pi; } }
            }
            T.areas << A;
            const int id = T.areas.size() - 1;
            T.areas[id].root = (parent >= 0) ? T.areas[parent].root : id;
            if (parent >= 0) T.areas[parent].children << id;
            cur << id;
        }
        prev = cur; lastOff = off; off += ae; ++k;
    }
    for (int i = 0; i < T.areas.size(); ++i) if (T.areas[i].level == 0) T.roots << i;
    std::function<void(int)> post = [&](int i){ for (int c : T.areas[i].children) post(c); T.order << i; };
    for (int r : T.roots) post(r);
    return T;
}

} // namespace

QVector<GCodeLine> PocheContourGenerator::generate(int blkId)
{
    const ContourData &d = m_d;
    QVector<GCodeLine> out;
    auto skip = [&](const QString &why){ out << comment("POCHE CONTOUR SKIP : " + why, blkId); return out; };
    if (d.profPasse <= 0) return skip("prise de passe invalide");

    // ── Outil ─────────────────────────────────────────────────
    double rOutil = 0; int tNum = 0, nbDents = 1; double angleRampe = 5.0;
    {
        QString t = d.toolRef.trimmed();
        if (t.startsWith('T', Qt::CaseInsensitive)) t = t.mid(1);
        bool ok = false; const int ti = t.toInt(&ok);
        if (ok && ti > 0) {
            tNum = ti;
            if (const ToolData *td = ToolDatabase::instance().toolByT(ti)) {
                rOutil = td->Routil;
                if (td->nbDents > 0) nbDents = td->nbDents;
                if (td->angleRampe > 0.1) angleRampe = qMin(td->angleRampe, 30.0);
            }
        }
    }
    if (rOutil < 0.1 || tNum <= 0) return skip(QString("outil %1 absent de la table").arg(d.toolRef));
    const double rEff = rOutil + d.DR;

    // ── Liste des contours ────────────────────────────────────
    QVector<Entry> E;
    QStringList warn;
    double zFloorMin = 0;
    for (const PocEntry &pe : d.pocList) {
        if (pe.file.trimmed().isEmpty()) continue;
        QString err;
        Path2D p = loadClosedPath(pe.file, err);
        if (p.isEmpty()) { warn << "CONTOUR IGNORE : " + err; continue; }
        Entry e; e.pts = Clipper2Utils::ensureCCW(p); e.ilot = pe.ilot;
        e.name = QFileInfo(pe.file).baseName();
        if (!pe.ilot) {
            if (pe.profondeur <= 0) { warn << "POCHE IGNOREE (profondeur nulle) : " + e.name; continue; }
            // Z depart 0 -> profondeur totale ; Z depart < 0 -> profondeur depuis ce Z
            e.zTop = qMin(0.0, pe.zDepart); e.zBot = e.zTop - pe.profondeur;
            zFloorMin = qMin(zFloorMin, e.zBot);
        } else {
            e.zTop = qMin(0.0, pe.zDepart);
            e.zBot = e.zTop - qMax(0.0, pe.profondeur);
        }
        E << e;
    }
    int nPoches = 0, nIlots = 0;
    for (const Entry &e : E) (e.ilot ? nIlots : nPoches)++;
    if (nPoches == 0) return skip("aucune poche dans la liste");

    // Ilot qui ne descend pas jusqu'au fond de la poche qui l'entoure :
    // on ne peut pas usiner dessous -> il est prolonge jusqu'au fond (on previent)
    for (const Entry &e : E) {
        if (!e.ilot) continue;
        double floorUnder = 0;
        for (const Entry &p : E)
            if (!p.ilot && pointInPoly(p.pts, e.pts.first())) floorUnder = qMin(floorUnder, p.zBot);
        if (floorUnder < 0 && e.zBot > floorUnder + 0.01 && e.zTop > floorUnder + 0.01)
            warn << QString("ATTENTION : ilot %1 s'arrete a Z%2, fond a Z%3 -> ilot prolonge jusqu'au fond")
                        .arg(e.name, fc(e.zBot), fc(floorUnder));
    }

    // ── Tranches en Z : coupure a chaque cote de la liste ─────
    QVector<double> cuts{0.0};
    for (const Entry &e : E) {
        if (!e.ilot) { cuts << e.zBot; if (e.zTop < -1e-6) cuts << e.zTop; }
        else if (e.zTop < -1e-6 && e.zTop > zFloorMin) cuts << e.zTop;
    }
    std::sort(cuts.begin(), cuts.end(), std::greater<double>());
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [](double a, double b){ return std::abs(a-b) < 1e-4; }), cuts.end());

    const int    stepPct = qBound(10, d.stepOverPct, 95);
    const double ae      = 2.0 * rOutil * stepPct / 100.0;
    const double d0      = rEff + qMax(0.0, d.surepLat);
    const double sF      = qMax(0.0, d.surepProf);

    struct Band { double top, bot; Paths2D region; Tree tree; Paths2D level0; bool presume = false; };
    QVector<Band> bands;
    for (int i = 0; i + 1 < cuts.size(); ++i) {
        Band b; b.top = cuts[i]; b.bot = cuts[i+1];
        b.region = regionAt(E, 0.5*(b.top + b.bot));
        if (b.region.isEmpty()) continue;
        b.tree = buildTree(b.region, d0, ae, rOutil);
        if (b.tree.roots.isEmpty()) {
            warn << QString("Tranche Z%1 a Z%2 trop etroite pour l'outil D%3").arg(fc(b.top), fc(b.bot), fc(2*rOutil));
            continue;
        }
        for (int r : b.tree.roots) b.level0 << b.tree.areas[r].loops;
        // Zone qui n'est pas videe par une tranche au-dessus : on PRESUME
        // qu'elle est deja vide (poche avec Z depart < 0) -> descente en avance + alerte
        {
            const bool contigu = !bands.isEmpty() && std::abs(bands.last().bot - b.top) < 1e-4;
            Paths2D neuf = (b.top > -1e-6) ? Paths2D{} : (contigu ? subtract(b.region, bands.last().region) : b.region);
            double aire = 0; for (const Path2D &p : neuf) aire += Clipper2Utils::signedArea(p);
            if (std::abs(aire) > 1.0) {
                b.presume = true;
                warn << QString("ATTENTION : au-dessus de Z%1 la zone est supposee DEJA VIDE (poche a Z depart). "
                                "Descente en avance jusqu'a Z%1.").arg(fc(b.top));
            }
        }
        bands << b;
    }
    if (bands.isEmpty()) return skip(QString("poche trop etroite pour l'outil D%1").arg(fc(2*rOutil)));

    // ── En-tete + outil ──────────────────────────────────────
    out << blank();
    out << comment(QString("POCHE CONTOUR : %1 poche(s), %2 ilot(s), %3 tranche(s), passe=%4")
                       .arg(nPoches).arg(nIlots).arg(bands.size()).arg(fc(d.profPasse)), blkId);
    for (const Entry &e : E)
        out << comment(e.ilot ? QString("  ilot  %1 : Z%2 a Z%3").arg(e.name, fc(e.zTop), fc(e.zBot))
                              : QString("  poche %1 : prof %2").arg(e.name, fc(-e.zBot)), blkId);
    for (const QString &w : warn) out << comment(w, blkId);
    out << emitToolBlock(d.toolRef, d.toolByName, d.spindleVal, d.spindleByVc, 0.0,
                         d.brocheCW, d.fonctionM1, d.fonctionM2, blkId);
    // Vitesse broche en tr/min pour convertir les avances (Fz, Fu) : en mode Vc,
    // d.spindleVal est la vitesse de coupe (m/min), pas des tr/min.
    const double sTr = (d.spindleByVc && d.spindleVal > 0 && rOutil > 0)
                           ? d.spindleVal * 1000.0 / (M_PI * 2.0 * rOutil) : d.spindleVal;
    const QString fTrav = avanceStr(d.avanceVal, d.avanceMode, sTr, nbDents);
    const QString fDesc = avanceStr(d.descenteVal > 0 ? d.descenteVal : d.avanceVal,
                                    d.descenteMode, sTr, nbDents);
    const bool    pendulDef = (d.pendulVal > 0);
    const QString fRampe = pendulDef ? avanceStr(d.pendulVal, d.pendulMode, sTr, nbDents) : fDesc;
    const QString fFin  = avanceStr(d.avanceFinition > 0 ? d.avanceFinition : d.avanceVal,
                                    d.avanceFinMode, sTr, nbDents);
    const double zSafe = qMax(qMax(d.zLiaison, d.approche), 1.0);
    const bool climb = (d.modeFraisage == 0);

    auto G0xy = [&](const QPointF &p){ out << line(GCodeLine::Rapid, QString("G0 X%1 Y%2").arg(fc(p.x()), fc(p.y())), "", blkId); };
    auto G0z  = [&](double z){ out << line(GCodeLine::Rapid, "G0 Z" + fc(z), "", blkId); };
    QString lastF;
    auto G1   = [&](const QPointF &p, const QString &f){
        const bool nf = (f != lastF); lastF = f;
        out << line(GCodeLine::Linear, QString("G1 X%1 Y%2%3").arg(fc(p.x()), fc(p.y()), nf ? " " + f : QString()), "", blkId); };
    auto G1z  = [&](double z, const QString &f){
        lastF = f; out << line(GCodeLine::Linear, "G1 Z" + fc(z) + " " + f, "", blkId); };
    auto rotated = [&](Path2D p, const QPointF &from){
        if (!climb) std::reverse(p.begin(), p.end());
        int bi = 0; double bd = 1e30;
        for (int i = 0; i < p.size(); ++i) {
            const double dd = std::hypot(p[i].x()-from.x(), p[i].y()-from.y());
            if (dd < bd) { bd = dd; bi = i; }
        }
        std::rotate(p.begin(), p.begin()+bi, p.end());
        p << p.first();
        return p;
    };

    const QPointF manualPt(d.entreeX, d.entreeY);
    const bool    manualOn = (d.pocPlongee == 0 && d.entreeDefinie);
    bool manualWarned = false;
    if (!pendulDef && d.pocPlongee != 0)
        out << comment("ATTENTION : avance pendulaire non definie -> avance de descente utilisee", blkId);

    // Descente vers un Z dans la poche : rapide si tout est vide au-dessus,
    // sinon (zone presumee deja vide) rapide jusqu'a l'approche puis en avance.
    bool curPresume = false;
    auto descendTo = [&](double zLevel) {
        if (curPresume && zLevel < d.approche - 1e-6) { G0z(d.approche); G1z(zLevel, fDesc); }
        else G0z(zLevel);
    };

    const double tanA = std::tan(qDegreesToRadians(angleRampe));
    const QString gHel = climb ? "G3" : "G2";

    // Plongee dans une feuille de l'arbre de la tranche
    auto plunge = [&](const Tree &T, int leaf, double zFrom, double zCut, bool &manualUsed) -> QPointF {
        const Paths2D &A   = T.areas[leaf].loops;
        const Paths2D &env = T.areas[T.areas[leaf].root].loops;
        QPointF c = deepestPoint(A);
        const double room = distToLoops(env, c);
        int mode = d.pocPlongee;
        G0z(zSafe);
        if (manualOn && !manualUsed) {
            if (pointInArea(env, manualPt) && !segHitsLoops(manualPt, c, env)) {
                manualUsed = true;
                G0xy(manualPt); descendTo(zFrom + d.approche); G1z(zCut, fDesc);
                if (std::hypot(c.x()-manualPt.x(), c.y()-manualPt.y()) > 0.01) G1(c, fTrav);
                return c;
            }
        }
        double rh = qMin(0.9 * rOutil, room);
        if (mode == 1 && rh < 0.25 * rOutil) { mode = 2; out << comment("Helice impossible (zone trop etroite) -> rampe", blkId); }
        if (mode == 1) {
            const QPointF s(c.x() + rh, c.y());
            G0xy(s); descendTo(zFrom + d.approche); G1z(zFrom, fDesc);
            const double half = qMax(0.05, M_PI * rh * tanA);
            double z = zFrom; bool first = true;
            while (z > zCut + 1e-6) {
                const double z1 = qMax(zCut, z - half);
                out << line(GCodeLine::Arc, QString("%1 X%2 Y%3 Z%4 I%5 J0%6").arg(gHel, fc(c.x()-rh), fc(c.y()), fc(z1), fc(-rh),
                            first ? " " + fRampe : QString()), first ? "Plongee helice" : "", blkId);
                first = false;
                const double z2 = qMax(zCut, z1 - half);
                out << line(GCodeLine::Arc, QString("%1 X%2 Y%3 Z%4 I%5 J0").arg(gHel, fc(s.x()), fc(s.y()), fc(z2), fc(rh)), "", blkId);
                z = z2;
            }
            out << line(GCodeLine::Arc, QString("%1 X%2 Y%3 I%4 J0 %5").arg(gHel, fc(c.x()-rh), fc(c.y()), fc(-rh), fTrav), "", blkId);
            out << line(GCodeLine::Arc, QString("%1 X%2 Y%3 I%4 J0").arg(gHel, fc(s.x()), fc(s.y()), fc(rh)), "", blkId);
            lastF = fTrav;
            return s;
        }
        if (mode == 2) {
            double bestL = 0; QPointF dir(1, 0);
            for (int a = 0; a < 12; ++a) {
                const double th = M_PI * a / 12;
                const QPointF u(std::cos(th), std::sin(th));
                double L = 0;
                while (L < 8*rOutil) {
                    const QPointF q1 = c + u*(L+0.2), q2 = c - u*(L+0.2);
                    if (!pointInArea(env, q1) || !pointInArea(env, q2)
                        || distToLoops(env, q1) < 0.05 || distToLoops(env, q2) < 0.05) break;
                    L += 0.2;
                }
                if (L > bestL) { bestL = L; dir = u; }
            }
            if (bestL < 0.3 * rOutil) {
                out << comment("Rampe impossible (zone trop etroite) -> plongee directe", blkId);
                G0xy(c); descendTo(zFrom + d.approche); G1z(zCut, fDesc);
                return c;
            }
            const QPointF pA = c - dir*bestL, pB = c + dir*bestL;
            G0xy(pA); descendTo(zFrom + d.approche); G1z(zFrom, fDesc);
            const double L = 2*bestL;
            double z = zFrom; bool toB = true, first = true;
            while (z > zCut + 1e-6) {
                z = qMax(zCut, z - L*tanA);
                const QPointF t = toB ? pB : pA;
                out << line(GCodeLine::Linear, QString("G1 X%1 Y%2 Z%3%4").arg(fc(t.x()), fc(t.y()), fc(z),
                            first ? " " + fRampe : QString()), first ? "Plongee rampe" : "", blkId);
                first = false; toB = !toB;
            }
            const QPointF t = toB ? pB : pA;
            out << line(GCodeLine::Linear, QString("G1 X%1 Y%2 %3").arg(fc(t.x()), fc(t.y()), fTrav), "", blkId);
            lastF = fTrav;
            return t;
        }
        G0xy(c); descendTo(zFrom + d.approche); G1z(zCut, fDesc);
        return c;
    };

    auto linkTo = [&](const QPointF &from, const QPointF &to, const Paths2D &cleared,
                      double zFrom, double zCut, const QString &f) {
        const double L = std::hypot(to.x()-from.x(), to.y()-from.y());
        if (L < 1e-3) return;
        if (L <= 2.0*ae + 0.01 && !segHitsLoops(from, to, cleared)) { G1(to, f); return; }
        G0z(zSafe); G0xy(to); descendTo(zFrom + d.approche); G1z(zCut, fDesc);
    };

    auto passZ = [&](const Tree &T, double zFrom, double zCut, const QString &f) {
        QPointF cur; bool manualUsed = false;
        for (int idx : T.order) {
            const Area &A = T.areas[idx];
            if (A.children.isEmpty()) cur = plunge(T, idx, zFrom, zCut, manualUsed);
            QVector<bool> done(A.loops.size(), false);
            for (int n = 0; n < A.loops.size(); ++n) {
                int bi = -1; double bd = 1e30;
                for (int i = 0; i < A.loops.size(); ++i) {
                    if (done[i]) continue;
                    const double dd = distToLoop(A.loops[i], cur);
                    if (dd < bd) { bd = dd; bi = i; }
                }
                done[bi] = true;
                const Path2D p = rotated(A.loops[bi], cur);
                linkTo(cur, p.first(), A.loops, zFrom, zCut, f);
                for (int i = 1; i < p.size(); ++i) G1(p[i], f);
                cur = p.last();
            }
        }
        if (manualOn && !manualUsed && !manualWarned) {
            manualWarned = true;
            out << comment(QString("ATTENTION : point d'entree X%1 Y%2 hors zone ou trop pres d'une paroi -> point calcule")
                               .arg(fc(manualPt.x()), fc(manualPt.y())), blkId);
        }
        G0z(zSafe);
    };

    const bool doEb  = (d.modeUsin == 0 || d.modeUsin == 1);
    const bool doFin = (d.modeUsin == 0 || d.modeUsin == 2);

    // ── EBAUCHE : tranche par tranche, surcote fond au bas de chaque tranche
    //    (la surcote est reprise par la tranche suivante la ou elle continue)
    if (doEb) {
        const QString plTxt = d.pocPlongee == 0 ? "directe" : d.pocPlongee == 2 ? "rampe" : "helice";
        out << blank();
        out << comment(QString("-- EBAUCHE : ae=%1, plongee %2 --").arg(fc(ae), plTxt), blkId);
        double zPrev = 0.0; int n = 0;
        for (int bi = 0; bi < bands.size(); ++bi) {
            const Band &b = bands[bi];
            const bool contigu = bi > 0 && std::abs(bands[bi-1].bot - b.top) < 1e-4;
            const double zStart = (bi == 0 || !contigu) ? b.top : zPrev;
            const double zEnd   = b.bot + (sF < (b.top - b.bot) ? sF : 0.0);
            const double h = zStart - zEnd;
            if (h <= 1e-6) { zPrev = zEnd; continue; }
            const int np = qMax(1, int(std::ceil(h / d.profPasse - 1e-6)));
            out << comment(QString("Tranche Z%1 a Z%2 : %3 passe(s)").arg(fc(b.top), fc(b.bot)).arg(np), blkId);
            curPresume = b.presume;
            double z = zStart;
            for (int k = 1; k <= np; ++k) {
                const double zn = (k == np) ? zEnd : zStart - h * k / np;
                out << comment(QString("Passe %1  Z=%2").arg(++n).arg(fc(zn)), blkId);
                passZ(b.tree, z, zn, fTrav);
                z = zn;
            }
            zPrev = zEnd;
        }
    }

    if (doFin) {
        // Finition des fonds : chaque bas de tranche (fonds de poche + dessus d'ilots)
        if (doEb && sF > 1e-6) {
            out << blank();
            out << comment("-- FINITION FONDS --", blkId);
            for (const Band &b : bands) {
                if (sF >= (b.top - b.bot)) continue;
                out << comment(QString("Fond Z%1").arg(fc(b.bot)), blkId);
                curPresume = b.presume;
                passZ(b.tree, b.bot + sF, b.bot, fFin);
            }
        }
        // Finition des parois : par tranche, sur leur hauteur reelle
        out << blank();
        out << comment("-- FINITION PAROIS --", blkId);
        for (const Band &b : bands) {
            const Paths2D walls = Clipper2Utils::offset(b.region, -rEff, 0, 0.005);
            for (const Path2D &w : walls) {
                if (w.size() < 3) continue;
                const Path2D p = rotated(w, w.first());
                const QPointF a = p[0], c1 = p[1];
                QPointF t(c1.x()-a.x(), c1.y()-a.y());
                const double tl = std::hypot(t.x(), t.y());
                if (tl > 1e-9) t /= tl;
                QPointF nIn(-t.y(), t.x());
                if (!climb) nIn = -nIn;
                double e = qMax(1.0, d.surepLat + 0.5);
                QPointF ap = a + nIn*e;
                for (int k = 0; k < 10 && (!pointInArea(walls, ap) || distToLoops(walls, ap) < 0.5*e); ++k) { e *= 0.6; ap = a + nIn*e; }
                curPresume = b.presume;
                G0z(zSafe); G0xy(ap); descendTo(b.top + d.approche);
                const double h = b.top - b.bot;
                const int np = qMax(1, int(std::ceil(h / d.profPasse - 1e-6)));
                for (int k = 1; k <= np; ++k) {
                    G1z(b.top - h * k / np, fDesc);
                    G1(a, fFin);
                    for (int i = 1; i < p.size(); ++i) G1(p[i], fFin);
                    G1(ap, fFin);
                }
                G0z(zSafe);
            }
        }
    }
    out << emitRetrait(d.sautBride, blkId);
    return out;
}
