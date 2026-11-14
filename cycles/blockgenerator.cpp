#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  blockgenerator.cpp
//
//  Implementation des helpers communs a tous les generateurs
//  de cycle. Le code est repris a l'identique de l'ancien
//  GCodeEngine pour garantir un comportement strictement
//  equivalent apres le decoupage.
// ═══════════════════════════════════════════════════════════════
#include "blockgenerator.h"
#include "../tooldatabase.h"

#include <QtMath>

// ─────────────────────────────────────────────────────────────
//  Formatage des nombres
// ─────────────────────────────────────────────────────────────
QString BlockGenerator::f(double v, int dec)
{
    return QString::number(v, 'f', dec);
}

QString BlockGenerator::fc(double v)
{
    QString s = QString::number(v, 'f', 3);
    if (s.contains('.')) {
        while (s.endsWith('0')) s.chop(1);
        if (s.endsWith('.')) s += '0';   // "10." -> "10.0" plus lisible
    }
    return s;
}

// ─────────────────────────────────────────────────────────────
//  Fabrication de lignes G-code
// ─────────────────────────────────────────────────────────────
GCodeLine BlockGenerator::line(GCodeLine::Type t, const QString &code,
                               const QString &cmt, int blkId)
{
    GCodeLine l;
    l.type    = t;
    l.code    = code;
    l.comment = cmt;
    l.blockId = blkId;
    return l;
}

GCodeLine BlockGenerator::blank()
{
    return line(GCodeLine::Blank, "");
}

GCodeLine BlockGenerator::comment(const QString &txt, int blkId)
{
    return line(GCodeLine::Comment, "( " + txt + " )", txt, blkId);
}

// ─────────────────────────────────────────────────────────────
//  avanceStr — convertit val en F mm/min
//  Même codage que l'interface (softkeys F / Fu / Fz) :
//   mode 0 = F  mm/min   (direct)          <- défaut
//   mode 1 = Fu mm/tr    -> F = val x S
//   mode 2 = Fz mm/dent  -> F = val x nbDents x S
//   mode 3 = mm/min      (ancien codage, direct)
//  Avant : le mode 0 était lu en mm/dent alors que l'écran affiche
//  mm/min -> 1500 « mm/min » devenait 1500 x 3 x 8000 -> F20000.
// ─────────────────────────────────────────────────────────────
QString BlockGenerator::avanceStr(double val, int mode,
                                  double S, int nbDents)
{
    if (val <= 0.0) return "F0";
    double Fmmmin = val;
    QString cmt;
    const int Z = (nbDents > 0) ? nbDents : 1;
    // Garde-fou anciens fichiers : une valeur < 2 en « mm/min » est en
    // réalité une avance par dent (ex 0.05) -> on la convertit.
    if ((mode == 0 || mode == 3) && val < 2.0 && S > 0)
        mode = 2;
    switch (mode) {
    case 2:  // Fz mm/dent
        if (S > 0) {
            Fmmmin = val * Z * S;
            cmt = "  ( Fz " + fc(val) + " x" + QString::number(Z) + "Z )";
        }
        break;
    case 1:  // Fu mm/tr
        if (S > 0) {
            Fmmmin = val * S;
            cmt = "  ( Fu " + fc(val) + " mm/tr )";
        }
        break;
    default: break;  // 0 / 3 : déjà en mm/min
    }
    // Garde-fou : une avance > 20000 mm/min est physiquement irréaliste.
    if (Fmmmin > 20000.0) {
        Fmmmin = 20000.0;
        cmt += "  ( ATTENTION avance limitee a 20000 - verifier F )";
    }
    return "F" + fc(qMax(Fmmmin, 1.0)) + cmt;
}

// ─────────────────────────────────────────────────────────────
//  Extraire le numero d'outil depuis toolRef
//   "T01" -> "1"   "T5" -> "5"   "1" -> "1"   "Fraise12" -> "Fraise12"
// ─────────────────────────────────────────────────────────────
static QString blockgen_toolNumber(const QString &toolRef)
{
    QString s = toolRef.trimmed();
    if (s.startsWith('T', Qt::CaseInsensitive)) {
        QString num = s.mid(1);
        bool ok = false;
        num.toInt(&ok);
        if (ok) return num;
    }
    return s;
}

// ─────────────────────────────────────────────────────────────
//  emitToolBlock — bloc d'appel outil
//  ORDRE CENTROID CNC12 :
//    1. T M6      (appel outil)
//    2. G43 H     (correction longueur)
//    3. S         (vitesse broche — AVANT M3 obligatoire)
//    4. M3/M4     (mise en rotation)
//    5. M7/M8     (arrosage si coolant)
//    6. Fonctions M optionnelles
// ─────────────────────────────────────────────────────────────
QVector<GCodeLine> BlockGenerator::emitToolBlock(const QString &toolRef,
                                                 bool   /*byName*/,
                                                 double spindleVal,
                                                 bool   byVc,
                                                 double /*diametre*/,
                                                 bool   cw,
                                                 const QString &fm1,
                                                 const QString &fm2,
                                                 int blkId) const
{
    QVector<GCodeLine> out;
    const QString tNum = blockgen_toolNumber(toolRef);

    out << blank();
    out << comment("Appel outil T" + tNum, blkId);

    // 1 — Appel outil
    out << line(GCodeLine::ToolCall,
                QString("T%1 M6").arg(tNum),
                "Changement outil T" + tNum, blkId);

    // 2 — G43 : compensation longueur (Centroid CNC12 utilise H)
    out << line(GCodeLine::ToolCall,
                QString("G43 H%1").arg(tNum),
                "Compensation longueur D" + tNum, blkId);

    // 3 — Vitesse broche S (AVANT M3 sur Centroid CNC12)
    //  Si byVc=true : spindleVal est une Vc (m/min) -> calculer S
    //  depuis le rayon outil de la base d'outils.
    double S_reel = spindleVal;
    double Routil_reel = 0.0;
    {
        QString tRef = toolRef.trimmed();
        if (tRef.startsWith('T', Qt::CaseInsensitive)) tRef = tRef.mid(1);
        bool ok = false;
        int tInt = tRef.toInt(&ok);
        if (ok && tInt > 0) {
            const ToolData *td = ToolDatabase::instance().toolByT(tInt);
            if (td) {
                Routil_reel = (td->Routil > 0) ? td->Routil : 0.0;
                if (byVc && spindleVal > 0 && Routil_reel > 0)
                    S_reel = (spindleVal * 1000.0)
                             / (M_PI * 2.0 * Routil_reel);
                else if (!byVc)
                    S_reel = spindleVal;
            }
        }
    }
    S_reel = qMax(S_reel, 0.0);

    // 3+4 — S et M3/M4 sur la MEME ligne (conforme PP Fusion Centroid)
    QString sLabel;
    if (byVc && Routil_reel > 0)
        sLabel = QString("S%1 %2  ( Vc=%3 m/min, D=%4 mm )")
                     .arg(qRound(S_reel))
                     .arg(cw ? "M3" : "M4")
                     .arg(fc(spindleVal), fc(Routil_reel * 2.0));
    else
        sLabel = QString("S%1 %2").arg(qRound(S_reel))
                                  .arg(cw ? "M3" : "M4");
    out << line(GCodeLine::Spindle, sLabel,
                cw ? "Broche horaire (M3)" : "Broche anti-horaire (M4)",
                blkId);

    // 4b — Tempo montee en vitesse broche (Centroid standard)
    out << line(GCodeLine::Other, "G4 P3.0",
                "Attente montee broche (3s)", blkId);

    // 5 — Fonctions M optionnelles (arrosage, etc.)
    if (!fm1.trimmed().isEmpty())
        out << line(GCodeLine::MFunction, fm1.trimmed(),
                    "Fonction M1", blkId);
    if (!fm2.trimmed().isEmpty())
        out << line(GCodeLine::MFunction, fm2.trimmed(),
                    "Fonction M2", blkId);

    return out;
}

// ─────────────────────────────────────────────────────────────
//  emitRetrait — retrait securitaire de fin de cycle
//   G0 Z(sautBride) + M5 (arret broche) + M9 (arret arrosage)
// ─────────────────────────────────────────────────────────────
QVector<GCodeLine> BlockGenerator::emitRetrait(double sautBride,
                                               int blkId) const
{
    QVector<GCodeLine> out;
    out << line(GCodeLine::Rapid,
                "G0 Z" + fc(sautBride),
                "Retrait saut de bride", blkId);
    out << line(GCodeLine::Spindle,   "M5", "Arret broche",   blkId);
    out << line(GCodeLine::MFunction, "M9", "Arret arrosage", blkId);
    return out;
}

// ─────────────────────────────────────────────────────────────
//  emitContourArcs — polyligne Clipper2 → G1 (droites) + G2/G3 (arcs)
//
//  Principe : on parcourt le contour. Pour chaque point, on teste si
//  une suite de points consécutifs se trouve sur un même cercle
//  (centre + rayon communs, à la tolérance près). Si oui, on émet
//  un seul arc G2/G3 du début à la fin de la suite. Sinon, G1.
//
//  Les positions ne sont JAMAIS recalculées : on utilise les points
//  exacts donnés par Clipper2. On regroupe seulement.
// ─────────────────────────────────────────────────────────────
QVector<GCodeLine> BlockGenerator::emitContourArcs(const QVector<QPointF> &ptsIn,
                                                   bool cwSense,
                                                   const QString &premiereAvance,
                                                   int blkId) const
{
    QVector<GCodeLine> out;
    if (ptsIn.size() < 2) return out;

    // On utilise les points tels quels, SANS auto-fermeture :
    // le caller fournit déjà le contour complet (fermeture incluse si
    // nécessaire). Ajouter un point dupliquerait un segment parasite.
    const QVector<QPointF> &p = ptsIn;

    const int N = p.size();
    const double TOL = 0.02;   // mm — tolérance pour considérer un arc

    // Calcule le centre du cercle passant par 3 points (retourne false si alignés)
    auto circleFrom3 = [](const QPointF &a, const QPointF &b, const QPointF &c,
                          double &ox, double &oy, double &r) -> bool {
        double ax=a.x(), ay=a.y(), bx=b.x(), by=b.y(), cx=c.x(), cy=c.y();
        double d = 2.0*(ax*(by-cy) + bx*(cy-ay) + cx*(ay-by));
        if (qAbs(d) < 1e-9) return false;   // alignés
        double a2=ax*ax+ay*ay, b2=bx*bx+by*by, c2=cx*cx+cy*cy;
        ox = (a2*(by-cy) + b2*(cy-ay) + c2*(ay-by)) / d;
        oy = (a2*(cx-bx) + b2*(ax-cx) + c2*(bx-ax)) / d;
        r  = std::sqrt((ax-ox)*(ax-ox) + (ay-oy)*(ay-oy));
        return true;
    };

    bool first = true;
    int i = 0;
    while (i < N-1) {
        // Tenter de détecter un arc démarrant à i
        int arcEnd = i;   // index du dernier point de l'arc
        double ox=0, oy=0, r=0;
        // Rayon max accepté pour un arc : au-delà, c'est une droite.
        // Un coin arrondi de poche a un rayon modeste (qq mm à qq cm).
        // Un côté quasi-droit donne un "cercle" de rayon >100mm → rejeté.
        const double R_MAX = 100.0;   // mm
        if (i+2 < N && circleFrom3(p[i], p[i+1], p[i+2], ox, oy, r)
            && r < R_MAX && r > 1e-3) {
            // Vérifier que les 3 points couvrent un vrai angle (pas quasi-droit)
            // Sagitta : écart entre le milieu de la corde et l'arc.
            // Si trop petit par rapport à la corde → c'est une droite.
            double chord = std::sqrt((p[i+2].x()-p[i].x())*(p[i+2].x()-p[i].x())
                                   + (p[i+2].y()-p[i].y())*(p[i+2].y()-p[i].y()));
            double sagitta = r - std::sqrt(qMax(0.0, r*r - chord*chord/4.0));
            if (sagitta < 0.03) {
                // Quasi-droit → traiter comme segment, pas arc
            } else {
                // Étendre l'arc tant que les points suivants restent sur le cercle
                int k = i+2;
                while (k+1 < N) {
                    double dist = std::sqrt((p[k+1].x()-ox)*(p[k+1].x()-ox)
                                          + (p[k+1].y()-oy)*(p[k+1].y()-oy));
                    if (qAbs(dist - r) > TOL) break;
                    k++;
                }
                arcEnd = k;
            }
        }

        if (arcEnd >= i+2) {
            // ── Arc de p[i] à p[arcEnd], centre (ox,oy) ──
            // Déterminer le sens réel de l'arc (CW ou CCW) via produit vectoriel
            double v1x=p[i].x()-ox,   v1y=p[i].y()-oy;
            double v2x=p[i+1].x()-ox, v2y=p[i+1].y()-oy;
            double cross = v1x*v2y - v1y*v2x;
            bool arcCW = (cross < 0);   // sens horaire si produit vectoriel négatif
            QString g = arcCW ? "G2" : "G3";
            double I = ox - p[i].x();
            double J = oy - p[i].y();
            QString code = QString("%1 X%2 Y%3 I%4 J%5")
                             .arg(g, fc(p[arcEnd].x()), fc(p[arcEnd].y()), fc(I), fc(J));
            if (first) { code += " " + premiereAvance; first = false; }
            out << line(GCodeLine::Arc, code, "Arc", blkId);
            i = arcEnd;
        } else {
            // ── Segment droit G1 de p[i] à p[i+1] ──
            QString code = QString("G1 X%1 Y%2").arg(fc(p[i+1].x()), fc(p[i+1].y()));
            if (first) { code += " " + premiereAvance; first = false; }
            out << line(GCodeLine::Linear, code, "", blkId);
            i++;
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────
//  emitPathElems — PathElems OCC → G1 (lignes) + G2/G3 (arcs)
//
//  Traduit un contour issu de OccOffset directement en G-code.
//  Les arcs sont préservés (pas de discrétisation).
//  Rotation optionnelle appliquée à chaque point (pivot + angle).
// ─────────────────────────────────────────────────────────────
QVector<GCodeLine> BlockGenerator::emitPathElems(const PathElems &elems,
                                                 const QString &premiereAvance,
                                                 double rotPivotX, double rotPivotY,
                                                 double rotRad,
                                                 int blkId) const
{
    QVector<GCodeLine> out;
    if (elems.isEmpty()) return out;

    const double cosA = std::cos(rotRad), sinA = std::sin(rotRad);
    const bool rot = (qAbs(rotRad) > 1e-9);
    auto R = [&](QPointF p) -> QPointF {
        if (!rot) return p;
        double dx = p.x()-rotPivotX, dy = p.y()-rotPivotY;
        return QPointF(rotPivotX + dx*cosA - dy*sinA,
                       rotPivotY + dx*sinA + dy*cosA);
    };

    // Aller au point de départ du 1er élément
    QPointF startPt = R(elems.first().start);
    out << line(GCodeLine::Linear,
                QString("G1 X%1 Y%2 %3").arg(fc(startPt.x()), fc(startPt.y()), premiereAvance),
                "Debut contour", blkId);

    for (const PathElem &el : elems) {
        QPointF e = R(el.end);
        if (el.kind == PathElem::Arc) {
            // I/J = vecteur du point de départ vers le centre (relatif)
            QPointF s = R(el.start);
            QPointF c = R(el.center);
            double I = c.x() - s.x();
            double J = c.y() - s.y();
            // Le sens cw est conservé ; la rotation ne l'inverse pas
            // (rotation rigide directe). Si rotation miroir → à gérer.
            QString g = el.cw ? "G2" : "G3";
            out << line(GCodeLine::Arc,
                        QString("%1 X%2 Y%3 I%4 J%5")
                          .arg(g, fc(e.x()), fc(e.y()), fc(I), fc(J)),
                        "Arc", blkId);
        } else {
            out << line(GCodeLine::Linear,
                        QString("G1 X%1 Y%2").arg(fc(e.x()), fc(e.y())),
                        "", blkId);
        }
    }
    return out;
}
