#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  percagegenerator.cpp
//
//  Cycle PERCAGE. Code repris a l'identique de l'ancien
//  GCodeEngine::percageForet / percageHelico / generatePercage,
//  adapte a l'architecture Strategy (heritage de BlockGenerator).
// ═══════════════════════════════════════════════════════════════
#include "percagegenerator.h"
#include "../tooldatabase.h"
#include "../pointfile.h"

#include <QtMath>
#include <QDebug>

// ─────────────────────────────────────────────────────────────
//  computePositionsPercage — positions de repetition des trous.
//  Reprend la logique de l'ancien GCodeEngine::computePositions.
// ─────────────────────────────────────────────────────────────
QVector<QPointF> PercageGenerator::computePositionsPercage() const
{
    const PercageData &d = m_d;
    QVector<QPointF> pts;

    switch (d.positionMode) {

    case 0: { // ── Point unique / Fichier .pnt ──────────────
        if (!d.fichierPoints.isEmpty()) {
            PointFile pf;
            if (pf.load(d.fichierPoints)) {
                pf.generate();
                for (const PointEntry &pe : pf.points)
                    if (pe.actif) pts << QPointF(pe.x, pe.y);
            }
        }
        if (pts.isEmpty()) pts << QPointF(0, 0);
        break;
    }

    case 1: { // ── Ligne ──────────────────────────────────────
        if (d.ligneNbPts < 2) { pts << QPointF(d.ligneP1X, d.ligneP1Y); break; }
        double dx = (d.ligneP2X - d.ligneP1X) / (d.ligneNbPts - 1);
        double dy = (d.ligneP2Y - d.ligneP1Y) / (d.ligneNbPts - 1);
        for (int i = 0; i < d.ligneNbPts; ++i)
            pts << QPointF(d.ligneP1X + i * dx, d.ligneP1Y + i * dy);
        break;
    }

    case 2: { // ── Grille ─────────────────────────────────────
        for (int iy = 0; iy < d.grilleNbY; ++iy)
            for (int ix = 0; ix < d.grilleNbX; ++ix)
                pts << QPointF(d.grilleOX + ix * d.grillePasX,
                               d.grilleOY + iy * d.grillePasY);
        break;
    }

    case 3: { // ── Cercle ─────────────────────────────────────
        if (d.cercleNbPts < 1) break;
        // Garde-fou : un cercle de rayon nul placerait tous les trous
        // au même point (perçages superposés). Dans ce cas on ne génère
        // qu'un seul trou, au centre.
        if (d.cercleRayon <= 1e-6) {
            pts << QPointF(d.cercleCX, d.cercleCY);
            break;
        }
        // Convention du perçage : l'UI ne fournit qu'un angle de départ
        // et un incrément angulaire. Côté PercageData, cet incrément est
        // transporté dans le champ cercleAngFin (l'éditeur y recopie la
        // valeur du champ "incrément"). On l'utilise donc comme pas
        // angulaire entre deux trous — même résultat que le cycle poche.
        double angDep = qDegreesToRadians(d.cercleAngDep);
        double step   = qDegreesToRadians(d.cercleAngFin);
        for (int i = 0; i < d.cercleNbPts; ++i) {
            double a = angDep + i * step;
            pts << QPointF(d.cercleCX + d.cercleRayon * qCos(a),
                           d.cercleCY + d.cercleRayon * qSin(a));
        }
        break;
    }

    default:
        pts << QPointF(0, 0);
        break;
    }

    return pts;
}

// ═══════════════════════════════════════════════════════════════
//  PERCAGE AU FORET — cycles canned Centroid CNC12.
//   d.cycle == 0 -> G81 (percage simple)
//   d.cycle != 0 -> G83 (debourrage, increment Q)
// ═══════════════════════════════════════════════════════════════
QVector<GCodeLine> PercageGenerator::percageForet(int blkId) const
{
    const PercageData &d = m_d;
    QVector<GCodeLine> out;

    // Guard : parametre obligatoire
    if (d.profondeur <= 0) {
        qDebug() << "[PercageGenerator::percageForet] GUARD prof=" << d.profondeur;
        return out;
    }

    QVector<QPointF> pts = computePositionsPercage();

    // ── Vitesse broche réelle S (tr/min) ────────────────────────────
    //  Si la broche est programmée en Vc (m/min), il faut convertir en
    //  tr/min : S = Vc·1000 / (π·Ø_outil). Ce S sert ensuite à convertir
    //  l'avance mm/tr en mm/min (la CNC12 n'a pas de mode mm/tr).
    //  Même logique que emitToolBlock — sinon avanceStr ne convertit pas.
    double diamOutil = d.diametreOutil;
    double S_reel    = d.spindleVal;
    int    nbDents   = 1;
    {
        QString tRef = d.toolRef.trimmed();
        if (tRef.startsWith('T', Qt::CaseInsensitive)) tRef = tRef.mid(1);
        bool ok = false; int tInt = tRef.toInt(&ok);
        if (ok && tInt > 0) {
            const ToolData *td = ToolDatabase::instance().toolByT(tInt);
            if (td) {
                if (td->Routil > 0)  diamOutil = td->Routil * 2.0;
                if (td->nbDents > 0) nbDents   = td->nbDents;
                if (d.spindleByVc && d.spindleVal > 0 && td->Routil > 0)
                    S_reel = (d.spindleVal * 1000.0)
                             / (M_PI * 2.0 * td->Routil);
            }
        }
    }
    S_reel = qMax(S_reel, 0.0);

    // ── Conversion du mode d'avance ─────────────────────────────────
    //  L'UI code le mode ainsi  : 0=F mm/min, 1=Fu mm/tr, 2=Fz mm/dent.
    //  avanceStr() utilise maintenant le même codage.
    int avMode = d.avanceMode;   // même codage que avanceStr (0 F, 1 Fu, 2 Fz)

    // Plan R = approche au-dessus de la surface (Z=0)
    double zR   = d.approche;                                       // ex +2.0 mm
    double zFin = d.traversant ? -(d.profondeur + 2.0) : -d.profondeur;

    out << blank();

    if (pts.isEmpty()) return out;

    // ── Choix du cycle : G81 simple ou G83 débourrage ───────────────
    //  Le débourrage s'active dès qu'une profondeur de passe (profPasse)
    //  est renseignée : l'outil plonge par incréments de profPasse en se
    //  dégageant entre chaque. Q = increment de plongée du G83.
    //  (d.cycle == 2 force aussi le G83 — bloc dédié débourrage.)
    const bool debourrage = (d.profPasse > 0.0) || (d.cycle != 0);
    const double qPasse   = (d.profPasse > 0.0) ? d.profPasse : d.degagement;

    out << comment(QString("═══ PERÇAGE %1  Ø%2  prof.%3  — %4 trou(s) ═══")
                       .arg(debourrage ? "G83" : "G81")
                       .arg(fc(diamOutil), fc(d.profondeur))
                       .arg(pts.size()), blkId);

    // Sécurité : on monte au plan de saut de bride AVANT tout déplacement
    // XY, pour ne jamais balayer la pièce avec l'outil en position basse.
    out << line(GCodeLine::Rapid, "G0 Z" + fc(d.sautBride),
                "Plan saut de bride", blkId);
    out << line(GCodeLine::Rapid,
                QString("G0 X%1 Y%2").arg(fc(pts[0].x()), fc(pts[0].y())),
                "Premier trou", blkId);

    if (!debourrage) {
        // ── G81 : percage simple ───────────────────────────────
        //  FORMAT CENTROID : G81 X Y Z[fond] R[plan_R] F[avance]
        out << line(GCodeLine::Cycle,
                    QString("G98 G90 G81 X%1 Y%2 Z%3 R%4 %5")
                        .arg(fc(pts[0].x()), fc(pts[0].y()),
                             fc(zFin), fc(zR),
                             avanceStr(d.avancePerc, avMode, S_reel, nbDents)),
                    "Cycle G81 percage simple", blkId);
    } else {
        // ── G83 : debourrage ───────────────────────────────────
        //  FORMAT CENTROID : G83 X Y Z[fond] R[plan_R] Q[increment] F[avance]
        out << line(GCodeLine::Cycle,
                    QString("G98 G90 G83 X%1 Y%2 Z%3 R%4 Q%5 %6")
                        .arg(fc(pts[0].x()), fc(pts[0].y()),
                             fc(zFin), fc(zR), fc(qPasse),
                             avanceStr(d.avancePerc, avMode, S_reel, nbDents)),
                    "Cycle G83 debourrage Q=" + fc(qPasse), blkId);
    }

    // Trous suivants : X Y seulement
    for (int i = 1; i < pts.size(); ++i) {
        out << line(GCodeLine::Cycle,
                    QString("X%1 Y%2").arg(fc(pts[i].x()), fc(pts[i].y())),
                    QString("Trou %1/%2").arg(i + 1).arg(pts.size()), blkId);
    }

    // Annulation cycle
    out << line(GCodeLine::Cycle, "G80", "Annulation cycle perçage", blkId);

    return out;
}

// ═══════════════════════════════════════════════════════════════
//  FRAISAGE DE TROU HELICOIDAL
// ═══════════════════════════════════════════════════════════════
QVector<GCodeLine> PercageGenerator::percageHelico(int blkId) const
{
    const PercageData &d = m_d;
    QVector<GCodeLine> out;

    QVector<QPointF> pts = computePositionsPercage();

    double rTrou   = d.diametreTrou / 2.0;
    double rFraise = 0.0;
    double S_reel  = d.spindleVal;
    int    nbDents = 1;
    {
        QString tRefH = d.toolRef.trimmed();
        if (tRefH.startsWith('T', Qt::CaseInsensitive)) tRefH = tRefH.mid(1);
        bool okH = false; int tIntH = tRefH.toInt(&okH);
        if (okH && tIntH > 0) {
            const ToolData *tdH = ToolDatabase::instance().toolByT(tIntH);
            if (tdH && tdH->Routil > 0) {
                rFraise = tdH->Routil;
                if (tdH->nbDents > 0) nbDents = tdH->nbDents;
                if (d.spindleByVc && d.spindleVal > 0)
                    S_reel = (d.spindleVal * 1000.0)
                             / (M_PI * 2.0 * tdH->Routil);
            }
        }
    }
    S_reel = qMax(S_reel, 0.0);

    // Conversion du mode d'avance UI -> avanceStr (voir percageForet).
    int avMode = d.avanceMode;   // même codage que avanceStr (0 F, 1 Fu, 2 Fz)

    if (rFraise < 0.1) {
        out << comment(QString("ERREUR HELICO : outil T%1 absent de la table")
                           .arg(d.toolRef), blkId);
        return out;
    }
    double rHelico = rTrou - rFraise;
    double zTotal  = -d.profondeur;
    double zApp    = d.approche;

    out << blank();
    out << comment(QString("═══ FRAISAGE HÉLICOÏDAL Ø%1  prof.%2  — %3 trou(s) ═══")
                       .arg(fc(d.diametreTrou), fc(d.profondeur))
                       .arg(pts.size()), blkId);

    for (int ti = 0; ti < pts.size(); ++ti) {
        double cx = pts[ti].x();
        double cy = pts[ti].y();

        if (ti > 0) out << blank();
        out << comment(QString("Trou %1/%2").arg(ti + 1).arg(pts.size()), blkId);

        out << line(GCodeLine::Rapid,
                    QString("G0 X%1 Y%2").arg(fc(cx), fc(cy)),
                    "Centre trou", blkId);
        out << line(GCodeLine::Rapid, "G0 Z" + fc(zApp), "Approche Z", blkId);

        double xStart = cx + rHelico;
        out << line(GCodeLine::Linear,
                    QString("G1 X%1 Y%2 %3")
                        .arg(fc(xStart), fc(cy),
                             avanceStr(d.avanceTrav, d.avanceTravMode, S_reel, nbDents)),
                    "Déplacement rayon hélice", blkId);

        double pasH    = (d.pasHelicoidale > 0) ? d.pasHelicoidale : 1.5; // guard /0
        int    nbTours = qMax(1, (int)qCeil(d.profondeur / pasH));
        double zPas    = -d.profondeur / nbTours;

        out << comment(QString("%1 tours, pas=%2 mm/tour")
                           .arg(nbTours).arg(fc(pasH)), blkId);

        double zCur = 0.0;
        for (int t = 0; t < nbTours; ++t) {
            double zNext = zCur + zPas;
            out << line(GCodeLine::Arc,
                        QString("G2 X%1 Y%2 Z%3 I%4 J0.0 %5")
                            .arg(fc(xStart), fc(cy), fc(zNext), fc(-rHelico),
                                 avanceStr(d.avancePerc, avMode, S_reel, nbDents)),
                        QString("Tour hélicoïdal %1 → Z=%2").arg(t + 1).arg(fc(zNext)),
                        blkId);
            zCur = zNext;
        }

        if (qAbs(zCur - zTotal) > 1e-4) {
            out << line(GCodeLine::Arc,
                        QString("G2 X%1 Y%2 Z%3 I%4 J0.0 %5")
                            .arg(fc(xStart), fc(cy), fc(zTotal), fc(-rHelico),
                                 avanceStr(d.avancePerc, avMode, S_reel, nbDents)),
                        "Rattrapage profondeur finale", blkId);
        }

        // Tour de finition fond
        out << comment("Tour de finition fond", blkId);
        out << line(GCodeLine::Arc,
                    QString("G2 X%1 Y%2 I%3 J0.0 %4")
                        .arg(fc(xStart), fc(cy), fc(-rHelico),
                             avanceStr(d.avanceTrav, d.avanceTravMode, S_reel, nbDents)),
                    "Finition fond", blkId);

        // Finition paroi
        if (d.passeFinition > 0) {
            double rFin = rHelico + d.passeFinition;
            out << line(GCodeLine::Linear,
                        QString("G1 X%1 Y%2 %3")
                            .arg(fc(cx + rFin), fc(cy),
                                 avanceStr(d.avanceTrav, d.avanceTravMode, S_reel, nbDents)),
                        "Rayon finition paroi", blkId);
            out << line(GCodeLine::Arc,
                        QString("G2 X%1 Y%2 I%3 J0.0")
                            .arg(fc(cx + rFin), fc(cy), fc(-rFin)),
                        "Finition paroi", blkId);
        }

        // Retour centre + degagement
        out << line(GCodeLine::Linear,
                    "G1 X" + fc(cx) + " Y" + fc(cy), "Retour centre", blkId);
        out << line(GCodeLine::Rapid, "G0 Z" + fc(zApp), "Dégagement Z", blkId);
    }

    return out;
}

// ─────────────────────────────────────────────────────────────
//  generate — enrobe l'usinage : commentaire de bloc + appel
//  outil + corps + retrait.
// ─────────────────────────────────────────────────────────────
QVector<GCodeLine> PercageGenerator::generate(int blkId)
{
    const PercageData &d = m_d;
    QVector<GCodeLine> out;

    out << comment(QString("BLOC %1 - PERCAGE %2")
                       .arg(blkId)
                       .arg(d.mode == PercageData::Foret
                                ? "AU FORET" : "FRAISAGE HELICOIDALE"),
                   blkId);

    out << emitToolBlock(d.toolRef, d.toolByName,
                         d.spindleVal, d.spindleByVc,
                         0.0,
                         d.brocheCW,
                         d.fonctionM1, d.fonctionM2,
                         blkId);

    out << (d.mode == PercageData::Foret
                ? percageForet(blkId)
                : percageHelico(blkId));

    out << emitRetrait(d.sautBride, blkId);

    return out;
}
