#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  occoffset.cpp — implémentation offset 2D avec arcs (OpenCASCADE)
// ═══════════════════════════════════════════════════════════════

#include "occoffset.h"

#ifdef WITH_OCC
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <gp_Pnt.hxx>
#  include <gp_Circ.hxx>
#  include <gp_Ax2.hxx>
#  include <gp_Dir.hxx>
#  include <gp_Lin.hxx>
#  include <GC_MakeArcOfCircle.hxx>
#  include <GC_MakeSegment.hxx>
#  include <Geom_TrimmedCurve.hxx>
#  include <BRepBuilderAPI_MakeEdge.hxx>
#  include <BRepBuilderAPI_MakeWire.hxx>
#  include <BRepOffsetAPI_MakeOffset.hxx>
#  include <TopoDS.hxx>
#  include <TopoDS_Wire.hxx>
#  include <TopoDS_Edge.hxx>
#  include <TopExp_Explorer.hxx>
#  include <BRepAdaptor_Curve.hxx>
#  include <GeomAbs_CurveType.hxx>
#  include <Geom_Circle.hxx>
#  include <cmath>
#endif

#include <QtGlobal>

bool OccOffset::available()
{
#ifdef WITH_OCC
    return true;
#else
    return false;
#endif
}

#ifdef WITH_OCC
// ── Helper : convertit un wire OCC offsetté en PathElems ─────────
//  Parcourt chaque edge : ligne → PathElem::Line, cercle → PathElem::Arc
static PathElems wireToElems(const TopoDS_Wire &wire)
{
    PathElems elems;
    for (TopExp_Explorer ex(wire, TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Edge edge = TopoDS::Edge(ex.Current());
        BRepAdaptor_Curve curve(edge);

        double t0 = curve.FirstParameter();
        double t1 = curve.LastParameter();
        gp_Pnt p0 = curve.Value(t0);
        gp_Pnt p1 = curve.Value(t1);

        PathElem el;
        el.start = QPointF(p0.X(), p0.Y());
        el.end   = QPointF(p1.X(), p1.Y());

        if (curve.GetType() == GeomAbs_Circle) {
            gp_Circ circ = curve.Circle();
            gp_Pnt c = circ.Location();
            el.kind   = PathElem::Arc;
            el.center = QPointF(c.X(), c.Y());
            el.radius = circ.Radius();
            // Sens : l'axe Z du cercle indique l'orientation.
            // Si Z+ → CCW (G3), Z- → CW (G2). Combiné au sens de
            // parcours de l'edge (paramètre croissant).
            gp_Dir axisZ = circ.Axis().Direction();
            bool axisUp = (axisZ.Z() >= 0);
            // edge orientation
            bool fwd = (edge.Orientation() == TopAbs_FORWARD);
            // CCW si (axe up XOR edge reversed) ... on déduit empiriquement
            bool ccw = (axisUp == fwd);
            el.cw = !ccw;
        } else {
            el.kind = PathElem::Line;
        }
        elems << el;
    }
    return elems;
}
#endif

// ─────────────────────────────────────────────────────────────
//  offsetCircle — test simple : un cercle offsetté
// ─────────────────────────────────────────────────────────────
PathElems OccOffset::offsetCircle(double cx, double cy, double rayon,
                                  double delta)
{
    PathElems result;
#ifdef WITH_OCC
    if (rayon + delta <= 1e-3) return result;  // disparaît

    try {
        // Construire le cercle dans le plan XY (Z=0)
        gp_Ax2 ax(gp_Pnt(cx, cy, 0), gp_Dir(0, 0, 1));
        gp_Circ circ(ax, rayon);
        TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(circ);
        TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge);

        // Offset 2D (delta négatif = vers l'intérieur)
        BRepOffsetAPI_MakeOffset offsetter;
        offsetter.AddWire(wire);
        offsetter.Perform(delta);
        if (!offsetter.IsDone()) return result;

        TopoDS_Shape sh = offsetter.Shape();
        if (sh.IsNull()) return result;

        // Le résultat est un wire (ou compound de wires)
        for (TopExp_Explorer ex(sh, TopAbs_WIRE); ex.More(); ex.Next()) {
            TopoDS_Wire w = TopoDS::Wire(ex.Current());
            result = wireToElems(w);
            break;  // on prend le premier wire
        }
    } catch (...) {
        result.clear();
    }
#else
    Q_UNUSED(cx) Q_UNUSED(cy) Q_UNUSED(rayon) Q_UNUSED(delta)
#endif
    return result;
}

// ─────────────────────────────────────────────────────────────
//  circlePasses — passes concentriques d'une poche circulaire
// ─────────────────────────────────────────────────────────────
PathElemsList OccOffset::circlePasses(double cx, double cy,
                                      double rParoi,
                                      double rOutil, double dr,
                                      double pas)
{
    PathElemsList passes;
#ifdef WITH_OCC
    const double rEff = rOutil + dr;
    if (rEff < 0.01 || rParoi <= rEff) return passes;

    // Rayon de la 1ère passe (paroi - rEff) jusqu'au centre.
    // On collecte du centre vers l'extérieur.
    QVector<double> rayons;
    double r = rParoi - rEff;
    while (r > 0.5) {        // tant qu'on a un cercle visible
        rayons.prepend(r);   // centre d'abord
        r -= pas;
    }
    if (rayons.isEmpty()) return passes;

    // Chaque passe = un cercle complet (1 seul élément Arc 360°).
    for (double rr : rayons) {
        PathElems elems;
        PathElem el;
        el.kind   = PathElem::Arc;
        el.center = QPointF(cx, cy);
        el.radius = rr;
        el.start  = QPointF(cx + rr, cy);   // point à 0°
        el.end    = QPointF(cx + rr, cy);   // cercle complet → même point
        el.cw     = true;                    // sens ajusté par le caller
        elems << el;
        passes << elems;
    }
#else
    Q_UNUSED(cx) Q_UNUSED(cy) Q_UNUSED(rParoi)
    Q_UNUSED(rOutil) Q_UNUSED(dr) Q_UNUSED(pas)
#endif
    return passes;
}

// ─────────────────────────────────────────────────────────────
//  rectPasses — passes concentriques d'une poche rectangulaire
//  (coins arrondis préservés en arcs)
// ─────────────────────────────────────────────────────────────
PathElemsList OccOffset::rectPasses(double cx, double cy,
                                    double longueur, double largeur,
                                    double rc,
                                    double rOutil, double dr,
                                    double pas)
{
    PathElemsList passes;
#ifdef WITH_OCC
    const double rEff = rOutil + dr;
    if (rEff < 0.01) return passes;
    if (longueur <= 2*rEff || largeur <= 2*rEff) return passes;

    // Clamp rayon de coin
    double rcc = qBound(0.0, rc, qMin(longueur, largeur)/2.0);

    try {
        // ── Construire le wire du rectangle arrondi (paroi finale) ──
        // Demi-dimensions
        const double hx = longueur/2.0;
        const double hy = largeur/2.0;

        // Construction du wire : 4 segments + 4 arcs de coin.
        // Points de tangence (sens trigo, départ milieu côté droit) :
        //   côté droit  X=+hx, Y de -(hy-rcc) à +(hy-rcc)
        //   coin HD, côté haut, coin HG, côté gauche, coin BG,
        //   côté bas, coin BD, fermeture.
        auto P = [&](double x, double y){ return gp_Pnt(cx+x, cy+y, 0); };

        BRepBuilderAPI_MakeWire mkWire;

        auto addLine = [&](const gp_Pnt &a, const gp_Pnt &b){
            if (a.Distance(b) < 1e-7) return;
            Handle(Geom_TrimmedCurve) seg = GC_MakeSegment(a, b).Value();
            mkWire.Add(BRepBuilderAPI_MakeEdge(seg).Edge());
        };
        auto addArc = [&](const gp_Pnt &a, const gp_Pnt &mid, const gp_Pnt &b){
            Handle(Geom_TrimmedCurve) arc = GC_MakeArcOfCircle(a, mid, b).Value();
            mkWire.Add(BRepBuilderAPI_MakeEdge(arc).Edge());
        };

        if (rcc < 1e-6) {
            // Coins vifs : 4 segments
            gp_Pnt p1=P(hx,hy), p2=P(-hx,hy), p3=P(-hx,-hy), p4=P(hx,-hy);
            addLine(p4,p1); addLine(p1,p2); addLine(p2,p3); addLine(p3,p4);
        } else {
            // Coins arrondis. Points de tangence :
            // Côté droit : de (hx,-(hy-rcc)) à (hx,hy-rcc)
            gp_Pnt rB = P(hx, -(hy-rcc));   // bas du côté droit
            gp_Pnt rT = P(hx,  (hy-rcc));   // haut du côté droit
            gp_Pnt tR = P(hx-rcc,  hy);     // droite du côté haut
            gp_Pnt tL = P(-(hx-rcc), hy);   // gauche du côté haut
            gp_Pnt lT = P(-hx,  (hy-rcc));  // haut du côté gauche
            gp_Pnt lB = P(-hx, -(hy-rcc));  // bas du côté gauche
            gp_Pnt bL = P(-(hx-rcc), -hy);  // gauche du côté bas
            gp_Pnt bR = P(hx-rcc, -hy);     // droite du côté bas

            // Milieux d'arcs (à 45°) pour GC_MakeArcOfCircle
            const double k = rcc*(1.0 - 0.70710678); // décalage pour le point à 45°
            // Coin HD centre (hx-rcc, hy-rcc)
            gp_Pnt mHD = P(hx-k, hy-k);
            gp_Pnt mHG = P(-(hx-k), hy-k);
            gp_Pnt mBG = P(-(hx-k), -(hy-k));
            gp_Pnt mBD = P(hx-k, -(hy-k));

            // Parcours trigo : côté droit ↑, coin HD, côté haut ←,
            // coin HG, côté gauche ↓, coin BG, côté bas →, coin BD.
            addLine(rB, rT);
            addArc (rT, mHD, tR);
            addLine(tR, tL);
            addArc (tL, mHG, lT);
            addLine(lT, lB);
            addArc (lB, mBG, bL);
            addLine(bL, bR);
            addArc (bR, mBD, rB);
        }

        if (!mkWire.IsDone()) return passes;
        TopoDS_Wire wire = mkWire.Wire();

        // ── Offsets concentriques ───────────────────────────────
        // delta négatif = vers l'intérieur. 1ère passe à -rEff,
        // puis -rEff-pas, etc. On collecte du centre vers l'extérieur.
        QVector<double> deltas;
        double delta = -rEff;
        // limite : tant que l'offset reste valide (rectangle pas annulé)
        double maxOff = qMin(hx, hy) - 0.3;
        while (-delta <= maxOff) {
            deltas.prepend(delta);   // centre d'abord
            delta -= pas;
        }
        if (deltas.isEmpty()) return passes;

        for (double dlt : deltas) {
            BRepOffsetAPI_MakeOffset offsetter;
            offsetter.AddWire(wire);
            offsetter.Perform(dlt);
            if (!offsetter.IsDone()) continue;
            TopoDS_Shape sh = offsetter.Shape();
            if (sh.IsNull()) continue;
            for (TopExp_Explorer ex(sh, TopAbs_WIRE); ex.More(); ex.Next()) {
                TopoDS_Wire w = TopoDS::Wire(ex.Current());
                PathElems elems = wireToElems(w);
                if (!elems.isEmpty()) passes << elems;
                break;
            }
        }
    } catch (...) {
        passes.clear();
    }
#else
    Q_UNUSED(cx) Q_UNUSED(cy) Q_UNUSED(longueur) Q_UNUSED(largeur)
    Q_UNUSED(rc) Q_UNUSED(rOutil) Q_UNUSED(dr) Q_UNUSED(pas)
#endif
    return passes;
}

