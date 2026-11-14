#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  pocherectgenerator.cpp
//
//  Cycle POCHE RECTANGULAIRE. Code repris a l'identique de
//  l'ancien GCodeEngine::pocheRect / generatePoche, adapte a
//  l'architecture Strategy (heritage de BlockGenerator).
//
//  Aucune logique d'usinage n'a ete modifiee : seules les
//  signatures et l'acces aux donnees (m_d) ont change.
// ═══════════════════════════════════════════════════════════════
#include "pocherectgenerator.h"
#include "../tooldatabase.h"
#include "../clipper2utils.h"
#include "../pointfile.h"

#include <QtMath>
#include <QDebug>

// ─────────────────────────────────────────────────────────────
//  generate — point d'entree : enrobe l'usinage de la poche
//  (commentaire de bloc + appel outil + corps + retrait).
//  Equivalent de l'ancien GCodeEngine::generatePoche pour le
//  cas rectangulaire.
// ─────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────
//  plongee — fonction libre (helper local du cycle poche).
//  Descente depuis zApp vers zNext.
//  mode 0 = helice, 1 = rampe, 2 = directe.
//  Reprise a l'identique de l'ancien gcodeengine.cpp.
// ─────────────────────────────────────────────────────────────
//  plongee — descente depuis zApp vers zNext
//  mode 0 = hélice, 1 = rampe, 2 = directe
// ─────────────────────────────────────────────────────────────
static QVector<GCodeLine> plongee(double cx, double cy,
                                  double zApp, double zNext,
                                  double rHelico, double tanAngle,
                                  int mode, const QString &fDesc,
                                  int blkId)
{
    QVector<GCodeLine> out;
    auto ln = [&](GCodeLine::Type t, const QString &code, const QString &cmt=""){
        GCodeLine l; l.type=t; l.code=code; l.comment=cmt; l.blockId=blkId; return l;
    };
    if (mode == 2 || rHelico < 0.01) {
        // Directe
        out << ln(GCodeLine::Linear,
                  QString("G1 Z%1 %2").arg(QString::number(zNext,'f',3), fDesc),
                  "Plongee directe");
    } else if (mode == 0) {
        // Hélice : XY d'abord (au saut de bride), puis descente Z, puis arcs
        double xPl = cx + rHelico;
        double dzTour = 2.0 * M_PI * rHelico * tanAngle;
        if (dzTour < 1e-6) dzTour = qAbs(zApp - zNext);
        out << ln(GCodeLine::Rapid,
                  QString("G0 X%1 Y%2").arg(QString::number(xPl,'f',3),
                                            QString::number(cy,'f',3)),
                  "Pos. helice XY");
        // Approche rapide jusqu'à 0.5 mm au-dessus du départ d'hélice
        // (commence plus bas, pas de descente lente inutile en l'air)
        double zStart = zApp;
        out << ln(GCodeLine::Rapid,
                  QString("G0 Z%1").arg(QString::number(zStart,'f',3)),
                  "Approche Z helice");
        double zH = zStart; int safe = 0;
        while (zH > zNext + 1e-6 && safe++ < 500) {
            double zE = qMax(zH - dzTour, zNext);
            out << ln(GCodeLine::Arc,
                      QString("G3 X%1 Y%2 Z%3 I%4 J0.0 %5")
                          .arg(QString::number(xPl,'f',3), QString::number(cy,'f',3),
                               QString::number(zE,'f',3), QString::number(-rHelico,'f',3), fDesc),
                      "Helice");
            zH = zE;
        }
        out << ln(GCodeLine::Linear,
                  QString("G1 X%1 Y%2").arg(QString::number(cx,'f',3),
                                            QString::number(cy,'f',3)),
                  "Retour centre");
    } else {
        // ── Mode 1 : Rampe pendulaire ────────────────────────────────
        // L'outil descend en faisant des allers-retours (pendule) sur
        // un diamètre de 2*rHelico autour du centre, avec l'angle de
        // rampe du tableau outil.
        // Pas vertical par aller = rHelico * tan(angle) * 2
        // (facteur 2 car un aller couvre 2*rHelico horizontalement)
        const double dz_aller = 2.0 * rHelico * tanAngle;
        const double xL = cx - rHelico;   // point gauche
        const double xR = cx + rHelico;   // point droit

        // XY d'abord (au saut de bride), puis descente Z, puis rampe
        out << ln(GCodeLine::Rapid,
                  QString("G0 X%1 Y%2").arg(QString::number(xR,'f',3),
                                            QString::number(cy,'f',3)),
                  "Pos. rampe XY");
        out << ln(GCodeLine::Rapid,
                  QString("G0 Z%1").arg(QString::number(zApp,'f',3)),
                  "Approche Z rampe");

        double zH = zApp;
        bool goLeft = true;   // sens du prochain aller
        int safe = 0;
        while (zH > zNext + 1e-6 && safe++ < 1000) {
            double zE = qMax(zH - dz_aller, zNext);
            double xTarget = goLeft ? xL : xR;
            out << ln(GCodeLine::Linear,
                      QString("G1 X%1 Y%2 Z%3 %4")
                          .arg(QString::number(xTarget,'f',3),
                               QString::number(cy,'f',3),
                               QString::number(zE,'f',3), fDesc),
                      goLeft ? "Rampe aller" : "Rampe retour");
            zH = zE;
            goLeft = !goLeft;
        }
        // Retour centre à profondeur atteinte
        out << ln(GCodeLine::Linear,
                  QString("G1 X%1 Y%2 %3").arg(QString::number(cx,'f',3),
                                               QString::number(cy,'f',3), fDesc),
                  "Retour centre");
    }
    return out;
}

QVector<GCodeLine> PocheRectGenerator::generate(int blkId)
{
    const PocheData &d = m_d;
    QVector<GCodeLine> out;

    out << comment(QString("BLOC %1 - POCHE RECTANGULAIRE").arg(blkId),
                   blkId);

    // Appel outil (helper commun de BlockGenerator)
    out << emitToolBlock(d.toolRef, d.toolByName,
                         d.spindleVal, d.spindleByVc,
                         0.0,                 // diametre : non utilise
                         d.brocheCW,
                         d.fonctionM1, d.fonctionM2,
                         blkId);

    // Usinage de la poche
    out << pocheRectBody(blkId);

    // Retrait final (helper commun)
    out << emitRetrait(d.sautBride, blkId);

    return out;
}


QVector<QPointF> PocheRectGenerator::computePositionsPoche() const
{
    const PocheData &d = m_d;
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
        if (pts.isEmpty()) pts << QPointF(d.posX, d.posY);
        break;
    }

    case 1: { // ── Ligne ─────────────────────────────────────
        // XY départ + pas (ligneP2Y) répété ligneNbPts fois
        // Axe de répétition : X si pasX != 0, sinon Y
        // Note: ligneP2X = Z départ (pas utilisé pour le positionnement XY)
        double pas = d.ligneP2Y;  // Pas entre usinages
        // Direction de la ligne : axe X par défaut
        // Pour une ligne en Y : mettre pasX=0 et utiliser l'angle
        for (int i = 0; i < qMax(d.ligneNbPts, 1); ++i)
            pts << QPointF(d.ligneP1X + i * pas, d.ligneP1Y);
        break;
    }

    case 2: { // ── Grille ────────────────────────────────────
        // Départ (grilleOX, grilleOY) + nb colonnes/lignes + pas X/Y
        for (int iy = 0; iy < qMax(d.grilleNbY, 1); ++iy)
            for (int ix = 0; ix < qMax(d.grilleNbX, 1); ++ix)
                pts << QPointF(d.grilleOX + ix * d.grillePasX,
                               d.grilleOY + iy * d.grillePasY);
        break;
    }

    case 3: { // ── Cercle ────────────────────────────────────
        // Centre (cercleCX, cercleCY) + rayon + Z départ + nb pts + incrément
        if (d.cercleNbPts < 1) break;
        double angDep = qDegreesToRadians(d.cercleAngDep);
        double step   = qDegreesToRadians(d.cercleAngInc);
        for (int i = 0; i < d.cercleNbPts; ++i) {
            double a = angDep + i * step;
            pts << QPointF(d.cercleCX + d.cercleRayon * qCos(a),
                           d.cercleCY + d.cercleRayon * qSin(a));
        }
        break;
    }

    case 4: { // ── Tableau manuel X/Y/Z ──────────────────────
        // Essayer d'abord un fichier .pnt
        if (!d.fichierPoints.isEmpty()) {
            PointFile pf;
            if (pf.load(d.fichierPoints)) {
                pf.generate();
                for (const PointEntry &pe : pf.points)
                    if (pe.actif) pts << QPointF(pe.x, pe.y);
            }
        }
        // Sinon tableau interne
        if (pts.isEmpty()) {
            for (const PocheData::TableauPoint &tp : d.tableauPts) {
                if (qAbs(tp.x) > 1e-9 || qAbs(tp.y) > 1e-9)
                    pts << QPointF(tp.x, tp.y);
            }
        }
        if (pts.isEmpty()) pts << QPointF(0, 0);
        break;
    }
    default:
        pts << QPointF(d.posX, d.posY);
        break;
    }
    return pts;
}

QVector<GCodeLine> PocheRectGenerator::pocheRectBody(int blkId) const
{
    const PocheData &d = m_d;
    QVector<GCodeLine> out;

    if (d.profondeur <= 0 || d.profPasse <= 0) return out;
    if (d.longueur <= 0 || d.largeur <= 0) return out;

    // ── Outil ────────────────────────────────────────────────
    double rOutil = 0.0, S = d.spindleVal; int nbDents = 1; double angleRampe = 15.0;
    {
        QString t = d.toolRef.trimmed();
        if (t.startsWith('T', Qt::CaseInsensitive)) t = t.mid(1);
        bool ok=false; int ti=t.toInt(&ok);
        if (ok && ti > 0) {
            const ToolData *td = ToolDatabase::instance().toolByT(ti);
            if (td) {
                nbDents = qMax(td->nbDents,1);
                if (td->angleRampe > 0.01) angleRampe = td->angleRampe;
                if (td->Routil > 0) { rOutil = td->Routil;
                    if (d.spindleByVc && d.spindleVal > 0)
                        S = (d.spindleVal*1000.0)/(M_PI*2.0*rOutil); }
            }
        }
    }
    if (rOutil < 0.1) {
        out << comment(QString("ERREUR : outil T%1 absent de la table").arg(d.toolRef),blkId);
        return out;
    }
    const double rEff = qMax(rOutil + d.DR, 0.01);  // rayon effectif avec correcteur DR
    S = qMax(S, 0.0);
    const QString fTrav = avanceStr(d.avanceVal,      d.avanceMode,    S, nbDents);
    const QString fDesc = avanceStr(d.descenteVal,    d.descenteMode,  S, nbDents);
    // Fallback : si pendulVal=0 (champ non rempli), utiliser fDesc
    const QString fPend = (d.pendulVal > 1e-6)
                              ? avanceStr(d.pendulVal, d.pendulMode, S, nbDents)
                              : fDesc; // hélice + rampe
    const QString fFin  = (d.avanceFinition > 0)
                             ? avanceStr(d.avanceFinition, d.avanceFinMode, S, nbDents)
                             : fTrav;

    // Hélice
    const double tanA  = qTan(qDegreesToRadians(angleRampe));
    // Avance plongée : pendulaire pour hélice/rampe, descente pour direct
    // (fPend calculé plus haut)
    double rHel = (tanA > 1e-6) ? d.profPasse/(2.0*M_PI*tanA) : rEff*0.4;
    rHel = qBound(rEff*0.1, rHel, rEff*0.45);

    // Coins
    const double rc = (d.rayonAngle > 0)
                          ? qMin(d.rayonAngle, qMin(d.longueur,d.largeur)/2.0) : 0.0;

    // Sens : avalant=CW(G2), opposition=CCW(G3)
    const bool cw = (d.modeFraisage == 0);

    out << blank();
    out << comment(QString("=== POCHE RECT. %1 x %2  prof.%3 ===")
                       .arg(fc(d.longueur),fc(d.largeur),fc(d.profondeur)), blkId);
    // Note visible dans le fichier NC — NE PAS SUPPRIMER
    out << comment("  Logique modeUsin: 0=Ebauche+Finition  1=Ebauche seule  2=Finition seule", blkId);
    out << comment("  Phase3 finition paroi: approche arc tangentiel — NE PAS MODIFIER", blkId);

    const QVector<QPointF> positions = computePositionsPoche();
    if (positions.isEmpty()) return out;

    // ╔═══════════════════════════════════════════════════════════════╗
    // ║  LOGIQUE POCHE RECT — NE PAS MODIFIER                        ║
    // ║  Cette logique a été validée et stabilisée. Toute             ║
    // ║  modification DOIT être discutée avant implémentation.        ║
    // ╠═══════════════════════════════════════════════════════════════╣
    // ║  modeUsin=0  Ébauche + Finition                              ║
    // ║    Phase 1 : passes concentriques jusqu'à                    ║
    // ║              (profondeur-surepProf, paroi-surepLat)           ║
    // ║    Phase 2 : finition fond — une passe à Z final              ║
    // ║    Phase 3 : finition paroi — passes avec passeFinition,      ║
    // ║              approche tangentielle par arc, sans retour centre ║
    // ║                                                               ║
    // ║  modeUsin=1  Ébauche seule                                   ║
    // ║    Phase 1 uniquement.                                        ║
    // ║    Les paramètres de finition RESTENT REMPLIS dans le bloc    ║
    // ║    pour permettre une copie en mode Finition seule            ║
    // ║    (autre fraise si besoin).                                  ║
    // ║                                                               ║
    // ║  modeUsin=2  Finition seule                                  ║
    // ║    Phase 2 + Phase 3 uniquement.                              ║
    // ║    Suppose que l'ébauche a déjà été faite.                   ║
    // ╚═══════════════════════════════════════════════════════════════╝

    for (int posIdx = 0; posIdx < positions.size(); ++posIdx) {
        const QPointF &ref = positions[posIdx];
        double zDep = 0.0;
        if (d.positionMode==3) zDep=d.cercleZ;
        else if (d.positionMode==4 && posIdx<d.tableauPts.size()) zDep=d.tableauPts[posIdx].z;

        double cx=ref.x(), cy=ref.y();
        switch(d.positionPoche){
        case 1: cx+=d.longueur/2; cy+=d.largeur/2; break;
        case 2:                   cy+=d.largeur/2; break;
        case 3: cx-=d.longueur/2; cy+=d.largeur/2; break;
        case 4: cx+=d.longueur/2;                  break;
        case 5:                                    break;
        case 6: cx-=d.longueur/2;                  break;
        case 7: cx+=d.longueur/2; cy-=d.largeur/2; break;
        case 8:                   cy-=d.largeur/2; break;
        case 9: cx-=d.longueur/2; cy-=d.largeur/2; break;
        }

        // Positionnement initial — montée Z garantie avant déplacement XY
        out << line(GCodeLine::Rapid,"G0 Z"+fc(d.sautBride),"Plan saut bride",blkId);
        out << line(GCodeLine::Rapid,"G0 X"+fc(cx)+" Y"+fc(cy),"Centre poche",blkId);
        const bool hasAngle = qAbs(d.posAngulaire) > 0.001;

        const double zSurf = zDep;                          // Z surface pièce
        const double zFond = zDep - d.profondeur;           // Z fond final
        const double zEbFond = zFond + d.surepProf;         // Z fond ébauche
        // Surep. finition : + laisse de la matiere, - enleve plus (correction)
        // Le contour fini est agrandi (s<0) ou reduit (s>0) ; le DR outil reste inchange
        const double sF      = d.surepFinLat;
        const double longFin = d.longueur - 2.0*sF;
        const double largFin = d.largeur  - 2.0*sF;
        const double rcFin   = qMax(0.0, rc - sF);
        const double zFondFin = zFond + d.surepFinProf;     // Z fond apres finition
        if (qAbs(d.surepFinLat) > 1e-9 || qAbs(d.surepFinProf) > 1e-9)
            out << comment(QString("Surep. finition : lat %1  fond %2").arg(fc(d.surepFinLat), fc(d.surepFinProf)), blkId);
        const double zApp  = zSurf + d.approche;            // Z approche

        // ── Profils Clipper2 ──────────────────────────────────
        // Profil ébauche : paroi - surepLat
        Path2D profEb = Clipper2Utils::makeRect(cx,cy,
                                                d.longueur - d.surepLat*2,
                                                d.largeur  - d.surepLat*2, rc);
        // Profil finition : paroi exacte
        Path2D profFin = Clipper2Utils::makeRect(cx,cy, longFin, largFin, rcFin);

        // Passes ébauche
        Paths2D passesEb = Clipper2Utils::concentricPasses(
            profEb, rOutil, d.DR, d.recouvrement);
        // Passes finition paroi (profil exact)
        Paths2D passesFin = Clipper2Utils::concentricPasses(
            profFin, rOutil, d.DR, d.recouvrement);

        // ── Pré-rotation des paths si angle défini ────────────
        // Pivot = point de sélection (ref), pas le centre géométrique.
        // Ex: poche posée par son coin → tourne autour de ce coin.
        if (hasAngle) {
            const double rpx  = ref.x(), rpy = ref.y();   // pivot = point sélectionné
            const double rad  = d.posAngulaire * M_PI / 180.0;
            const double cosA = std::cos(rad), sinA = std::sin(rad);
            auto rotatePaths = [&](Paths2D &paths) {
                for (auto &path : paths)
                    for (auto &pt : path) {
                        double dx = pt.x()-rpx, dy = pt.y()-rpy;
                        pt = QPointF(rpx + dx*cosA - dy*sinA,
                                     rpy + dx*sinA + dy*cosA);
                    }
            };
            rotatePaths(passesEb);
            rotatePaths(passesFin);
        }

        // ─────────────────────────────────────────────────────
        //  PHASE 1 — ÉBAUCHE
        //  modeUsin 0 ou 1
        // ─────────────────────────────────────────────────────
        if (d.modeUsin != 2) {
            out << blank();
            out << comment("-- Ebauche --",blkId);

            double zCur = zSurf; int passNum=0;
            while (zCur > zEbFond + 1e-6) {
                double zNext = qMax(zCur - d.profPasse, zEbFond);
                if (++passNum > 10000) break;
                out << comment(QString("Passe %1  Z=%2").arg(passNum).arg(fc(zNext)),blkId);

                // Plongée depuis zApp
                out << plongee(cx,cy, zApp, zNext, rHel, tanA, d.modePlongee, (d.modePlongee==2?fDesc:fPend), blkId);

                // Passes concentriques
                QPointF lastPt(cx,cy);
                if (!passesEb.isEmpty()) {
                    for (const Path2D &pass : passesEb) {
                        if (pass.size()<2) continue;
                        Path2D p = cw ? Clipper2Utils::ensureCW(pass)
                                      : Clipper2Utils::ensureCCW(pass);
                        out << line(GCodeLine::Linear,
                                    QString("G1 X%1 Y%2 %3").arg(fc(p[0].x()),fc(p[0].y()),fTrav),
                                    "",blkId);
                        for (int i=1;i<p.size();i++)
                            out << line(GCodeLine::Linear,
                                        QString("G1 X%1 Y%2").arg(fc(p[i].x()),fc(p[i].y())),
                                        "",blkId);
                        out << line(GCodeLine::Linear,
                                    QString("G1 X%1 Y%2").arg(fc(p[0].x()),fc(p[0].y())),
                                    "Fermeture",blkId);
                        lastPt = QPointF(p[0].x(), p[0].y());
                    }
                }
                // Dégagement doux : décalage d'un rayon vers le centre, puis saut de bride
                { double edx=cx-lastPt.x(), edy=cy-lastPt.y();
                    double ed=qSqrt(edx*edx+edy*edy);
                    if(ed>1e-6)
                        out<<line(GCodeLine::Linear,
                                    QString("G1 X%1 Y%2 %3")
                                        .arg(fc(lastPt.x()+edx/ed*rOutil),fc(lastPt.y()+edy/ed*rOutil),fTrav),
                                    "Degagement paroi",blkId);
                }
                out << line(GCodeLine::Rapid,"G0 Z"+fc(d.sautBride),"Saut de bride",blkId);
                out << line(GCodeLine::Rapid,"G0 X"+fc(cx)+" Y"+fc(cy),"Retour centre",blkId);
                zCur = zNext;
            }
        }

        // ─────────────────────────────────────────────────────
        //  PHASE 2 — FINITION FOND
        //  seulement si surepProf > 0 ET modeUsin != 1
        // ─────────────────────────────────────────────────────
        if (d.modeUsin != 1 && d.surepProf > 1e-6) {
            out << blank();
            out << comment("-- Finition fond --",blkId);

            // Plongée finition fond : démarre à la distance d'approche
            // AU-DESSUS du niveau déjà ébauché (zFond + surepProf), pas
            // depuis le haut. Évite une descente lente inutile en l'air.
            const double zEbauche = zFond + d.surepProf;        // niveau laissé par l'ébauche
            const double zAppFond = zEbauche + d.approche;      // départ plongée
            out << line(GCodeLine::Rapid, "G0 Z"+fc(zAppFond), "Rapide fond ebauche + approche", blkId);
            out << plongee(cx,cy, zAppFond, zFondFin, rHel, tanA, d.modePlongee, (d.modePlongee==2?fDesc:fPend), blkId);

            // Escargot sur le fond — passes concentriques.
            // Avec OCC : chaque passe a ses côtés droits (G1) et coins
            // (G2/G3) propres. Sinon : fallback polyligne Clipper.
            QPointF lastPtFond(cx,cy);
            double fondTanX = 0.0, fondTanY = 1.0;   // tangente paroi au pt final
            double rotF = hasAngle ? (d.posAngulaire * M_PI/180.0) : 0.0;
            PathElemsList occFond;
            if (OccOffset::available()) {
                // Dimensions du fond = poche - 2*surepLat (reste sur parois)
                double longF = d.longueur - 2.0*d.surepLat;
                double largF = d.largeur  - 2.0*d.surepLat;
                double rcF   = qMax(0.0, rc - d.surepLat);
                // Pas réel = 2*rOutil*(recouvrement/100), comme Clipper
                double pasMM = 2.0*rOutil*(d.recouvrement/100.0);
                if (pasMM < rOutil*0.1) pasMM = rOutil*0.5;
                occFond = OccOffset::rectPasses(cx, cy, longF, largF, rcF,
                                                rOutil, d.DR, pasMM);
            }
            if (!occFond.isEmpty()) {
                bool firstP = true;
                for (const PathElems &passe : occFond) {
                    if (passe.isEmpty()) continue;
                    out << emitPathElems(passe, firstP ? fFin : QString(),
                                         ref.x(), ref.y(), rotF, blkId);
                    firstP = false;
                    QPointF s = passe.first().start;
                    lastPtFond = s;
                    // Tangente de la paroi au point final (1er segment)
                    const PathElem &e0 = passe.first();
                    QPointF tEnd = (e0.kind==PathElem::Line) ? e0.end : e0.start;
                    fondTanX = tEnd.x()-s.x();
                    fondTanY = tEnd.y()-s.y();
                }
            } else if (!passesEb.isEmpty()) {
                for (const Path2D &pass : passesEb) {
                    if (pass.size()<2) continue;
                    Path2D p = cw ? Clipper2Utils::ensureCW(pass)
                                  : Clipper2Utils::ensureCCW(pass);
                    out << line(GCodeLine::Linear,
                                QString("G1 X%1 Y%2 %3").arg(fc(p[0].x()),fc(p[0].y()),fFin),"",blkId);
                    for (int i=1;i<p.size();i++)
                        out << line(GCodeLine::Linear,
                                    QString("G1 X%1 Y%2").arg(fc(p[i].x()),fc(p[i].y())),"",blkId);
                    out << line(GCodeLine::Linear,
                                QString("G1 X%1 Y%2").arg(fc(p[0].x()),fc(p[0].y())),"Fermeture",blkId);
                    lastPtFond = QPointF(p[0].x(),p[0].y());
                }
            }
            // Dégagement en biais : l'outil s'éloigne du mur en diagonale
            // (un peu le long de la paroi + un peu vers l'intérieur), ce
            // qui évite une marque au point de sortie. Mouvement linéaire.
            {   double tl = qSqrt(fondTanX*fondTanX + fondTanY*fondTanY);
                if (tl > 1e-6) {
                    double tx = fondTanX/tl, ty = fondTanY/tl;  // le long du mur
                    // Normale vers l'intérieur de la poche
                    double nx = -ty, ny = tx;
                    double vcx = cx-lastPtFond.x(), vcy = cy-lastPtFond.y();
                    if (nx*vcx + ny*vcy < 0) { nx=-nx; ny=-ny; }
                    double dDeg = qMax(1.0, rOutil);   // longueur du biais
                    // Biais = moitié le long du mur, moitié vers l'intérieur
                    double ex = lastPtFond.x() + (tx*0.7 + nx*0.7)*dDeg;
                    double ey = lastPtFond.y() + (ty*0.7 + ny*0.7)*dDeg;
                    out<<line(GCodeLine::Linear,
                        QString("G1 X%1 Y%2 %3").arg(fc(ex),fc(ey),fFin),
                        "Degagement biais",blkId);
                }
            }
            out << line(GCodeLine::Rapid,"G0 Z"+fc(d.sautBride),"Saut de bride",blkId);
        }

        // ═══════════════════════════════════════════════════════════
        //  PHASE 3 — FINITION PAROI
        //  ⚠ NE PAS MODIFIER — approche tangentielle par arc,
        //  sans retour au centre entre les passes.
        //  La logique modeUsin 0/1/2 est documentée en tête de fonction.
        // ═══════════════════════════════════════════════════════════
        auto genFinitionParoi = [&](const Path2D &derniere,
                                    double zDepart, double zCible,
                                    double passeF)
        {
            if (derniere.size() < 2) return;

            // ── Contrôle rayon outil vs rayon de coin ──────────────
            // Si l'outil est plus gros que le rayon de coin demandé,
            // le rayon final NE PEUT PAS être respecté (l'outil ne
            // rentre pas dans le coin). On prévient l'utilisateur.
            if (rc > 0.001 && rOutil > rc + 1e-3) {
                out << comment("ATTENTION rayon outil trop grand - rayons de coin non respectes", blkId);
                qWarning() << "[PocheRect] Rayon outil" << rOutil
                           << "> rayon coin" << rc
                           << ": rayons non respectes dans la poche";
            }

            // ── Profil orienté sens fraisage ──────────────────
            Path2D p = cw ? Clipper2Utils::ensureCW(derniere)
                          : Clipper2Utils::ensureCCW(derniere);

            // ── Contour OCC (arcs préservés) pour la finition ──────
            // Si OCC dispo : on génère le contour de paroi exact avec
            // de vrais arcs de coin. Sinon : fallback polyligne Clipper.
            PathElems occContour;
            if (OccOffset::available()) {
                // Dernière passe = paroi exacte (offset = rOutil+DR depuis paroi)
                PathElemsList occPasses = OccOffset::rectPasses(
                    cx, cy, longFin, largFin, rcFin, rOutil, d.DR, 999.0);
                // 999 = pas énorme → une seule passe (la paroi)
                if (!occPasses.isEmpty()) occContour = occPasses.last();
            }

            // ── Entrée/sortie tangentielles au point de départ ────
            // Le contour OCC démarre au début d'un côté droit (p0).
            // L'entrée arrive en p0 par un arc tangent de rayon rCoin,
            // dont le centre est décalé perpendiculairement vers
            // l'intérieur de la poche. La sortie repart symétriquement.
            // Reproduit le trajet validé manuellement sur .nc.
            double rotRad = hasAngle ? (d.posAngulaire * M_PI/180.0) : 0.0;
            const double cR = std::cos(rotRad), sR = std::sin(rotRad);
            auto rot = [&](QPointF q)->QPointF{
                if (!hasAngle) return q;
                double dx=q.x()-ref.x(), dy=q.y()-ref.y();
                return QPointF(ref.x()+dx*cR-dy*sR, ref.y()+dx*sR+dy*cR);
            };

            // p0 = début du contour ; t = tangente (direction du 1er segment)
            QPointF p0, p1;
            p0 = rot(occContour.isEmpty() ? p[0] : occContour.first().start);
            if (!occContour.isEmpty()) {
                const PathElem &e0 = occContour.first();
                p1 = rot(e0.kind == PathElem::Line ? e0.end : e0.start);
            } else {
                p1 = (p.size()>1) ? p[1] : p[0];
            }
            double tx = p1.x()-p0.x(), ty = p1.y()-p0.y();
            double tl = std::sqrt(tx*tx+ty*ty);
            if (tl > 1e-6) { tx/=tl; ty/=tl; } else { tx=0; ty=1; }

            // Rayon de l'arc d'entrée/sortie = rayon de coin trajectoire
            const double rcTraj = (rc > rOutil) ? (rc - rOutil) : 0.0;
            double R = (rcTraj > 0.5) ? rcTraj : qMax(0.1, rOutil);

            // Normale vers l'intérieur de la poche (perpendiculaire à t).
            // Le centre de la poche est en (cx,cy) : on oriente la normale
            // vers lui pour être sûr de pointer vers l'intérieur (vide).
            double nx = -ty, ny = tx;   // perpendiculaire à t
            // Vérifier qu'elle pointe vers le centre de la poche
            double versCentreX = cx - p0.x(), versCentreY = cy - p0.y();
            if (nx*versCentreX + ny*versCentreY < 0) { nx = -nx; ny = -ny; }

            // Centre de l'arc d'entrée/sortie = p0 + R*normale_intérieur
            const double arc_cx = p0.x() + R*nx;
            const double arc_cy = p0.y() + R*ny;

            // ── ENTRÉE : demi-cercle, arrive en p0 ────────────────
            // app = point diamétralement opposé à p0 / centre
            const double app_x = 2.0*arc_cx - p0.x();
            const double app_y = 2.0*arc_cy - p0.y();
            const double ij_ei = arc_cx - app_x;
            const double ij_ej = arc_cy - app_y;

            // ── SORTIE : quart de cercle, dégage dans le vide ─────
            // exit = un quart de tour après p0 sur le cercle
            const double exit_x = arc_cx + tx*R;
            const double exit_y = arc_cy + ty*R;
            const double ij_si = arc_cx - p0.x();
            const double ij_sj = arc_cy - p0.y();

            // Sens des arcs : entrée et sortie dans le sens d'usinage
            const QString gArcIn  = cw ? "G3" : "G2";
            const QString gArcOut = cw ? "G3" : "G2";

            out << blank();
            out << comment("-- Finition paroi --", blkId);
            out << line(GCodeLine::Rapid,
                        "G0 Z"+fc(d.sautBride), "Plan saut bride", blkId);

            double zCurF = zDepart;
            int passNum  = 0;
            while (zCurF > zCible + 1e-6) {
                const double zNF = qMax(zCurF - passeF, zCible);
                ++passNum;
                out << blank();
                out << comment(
                    QString("Finition paroi passe %1  Z=%2").arg(passNum).arg(fc(zNF)),
                    blkId);

                // Positionnement hors poche — pas de matière ici
                // XY d'abord (on est au saut de bride), Z ensuite
                out << line(GCodeLine::Rapid,
                            QString("G0 X%1 Y%2").arg(fc(app_x), fc(app_y)),
                            "Approche XY", blkId);
                // Point d'approche a 2R de la trajectoire finie : il est
                // dans la zone ebauchee si 2R >= surepLat (+0.2 marge).
                // Alors la CN connait le vide -> rapide jusqu'a zNF+approche.
                {
                    const bool appDansVide = (2.0*R >= d.surepLat + 0.2);
                    const double zRapP = appDansVide
                        ? qMin(zDepart + d.approche, zNF + d.approche)
                        : (zDepart + d.approche);
                    out << line(GCodeLine::Rapid,
                                QString("G0 Z%1").arg(fc(zRapP)),
                                appDansVide ? "Rapide zone ebauchee" : "Approche Z", blkId);
                }
                // Plongée directe (hors matière)
                out << line(GCodeLine::Linear,
                            QString("G1 Z%1 %2").arg(fc(zNF), fDesc),
                            "Plongee finition paroi", blkId);
                // Arc tangentiel d'entrée
                out << line(GCodeLine::Arc,
                            QString("%1 X%2 Y%3 I%4 J%5 %6")
                                .arg(gArcIn,
                                     fc(p0.x()), fc(p0.y()),
                                     fc(ij_ei),  fc(ij_ej), fFin),
                            "Arc entree tangentiel", blkId);
                // ── Parcours du profil ──────────────────────────
                if (!occContour.isEmpty()) {
                    // Contour OCC : segments droits G1 + arcs de coin G2/G3.
                    // Le contour est déjà fermé (dernier arc revient au
                    // point de départ). L'arc d'entrée nous amène pile sur
                    // occContour.first().start → pas de segment de liaison.
                    out << emitPathElems(occContour, "",
                                         ref.x(), ref.y(), rotRad, blkId);
                } else {
                    // Fallback Clipper : polyligne
                    for (int i = 1; i < p.size(); i++)
                        out << line(GCodeLine::Linear,
                                    QString("G1 X%1 Y%2").arg(fc(p[i].x()), fc(p[i].y())),
                                    "", blkId);
                    out << line(GCodeLine::Linear,
                                QString("G1 X%1 Y%2").arg(fc(p0.x()), fc(p0.y())),
                                "Fermeture", blkId);
                }
                // Arc tangentiel de sortie (point de sortie distinct)
                out << line(GCodeLine::Arc,
                            QString("%1 X%2 Y%3 I%4 J%5 %6")
                                .arg(gArcOut,
                                     fc(exit_x), fc(exit_y),
                                     fc(ij_si),  fc(ij_sj), fFin),
                            "Arc sortie tangentiel", blkId);
                // Retrait
                out << line(GCodeLine::Rapid,
                            "G0 Z"+fc(d.sautBride), "Saut de bride", blkId);

                zCurF = zNF;
            }
        }; // genFinitionParoi

        // Phase 3 standard : surepLat > 0, modeUsin != Ebauche seule
        if (d.modeUsin != 1 && d.surepLat > 1e-6 && !passesFin.isEmpty()) {
            const double passeF = (d.passeFinition > 1e-6)
            ? d.passeFinition : d.profPasse;
            genFinitionParoi(passesFin.last(), zSurf, zFondFin, passeF);
        }

        // ═══════════════════════════════════════════════════════════
        //  CAS SPÉCIAL — surepLat=0 (surepProf quelconque)
        //  modeUsin=2 : passes paroi de z0 à zFond, approche tangentielle
        //  (avant : exigeait aussi surepProf=0 -> paroi oubliee si surepProf>0)
        //  ⚠ NE PAS MODIFIER — voir commentaire logique en tête
        // ═══════════════════════════════════════════════════════════
        if (d.modeUsin == 2 && d.surepLat < 1e-6
            && !passesFin.isEmpty())
        {
            const double passeF = (d.passeFinition > 1e-6)
            ? d.passeFinition : d.profPasse;
            genFinitionParoi(passesFin.last(), zSurf, zFondFin, passeF);
        }

        // NOTE : le retrait final (G0 Z sautBride + M5/M9) est gere par emitRetrait() dans generate()
        // hasAngle géré par pré-rotation — pas de G69 nécessaire
    }

    return out;
}
