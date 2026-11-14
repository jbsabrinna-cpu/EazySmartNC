#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  pochecircgenerator.cpp
//
//  Cycle POCHE CIRCULAIRE. Code repris a l'identique de l'ancien
//  GCodeEngine::pocheCirc / generatePoche, adapte a l'architecture
//  Strategy (heritage de BlockGenerator).
// ═══════════════════════════════════════════════════════════════
#include "pochecircgenerator.h"
#include "../tooldatabase.h"
#include "../clipper2utils.h"
#include "../pointfile.h"

#include <QtMath>
#include <QDebug>

// ─────────────────────────────────────────────────────────────
//  plongee — fonction libre (helper local du cycle poche).
//  Descente depuis zApp vers zNext. mode 0=helice 1=rampe 2=directe.
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
        out << ln(GCodeLine::Rapid,
                  QString("G0 Z%1").arg(QString::number(zApp,'f',3)),
                  "Approche Z helice");
        double zH = zApp; int safe = 0;
        while (zH > zNext + 1e-6 && safe++ < 500) {
            double zE = qMax(zH - dzTour, zNext);
            out << ln(GCodeLine::Arc,
                      QString("G2 X%1 Y%2 Z%3 I%4 J0.0 %5")
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

QVector<QPointF> PocheCircGenerator::computePositionsPoche() const
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

// ─────────────────────────────────────────────────────────────
//  generate — enrobe l'usinage : commentaire de bloc + appel
//  outil + corps + retrait.
// ─────────────────────────────────────────────────────────────
QVector<GCodeLine> PocheCircGenerator::generate(int blkId)
{
    const PocheData &d = m_d;
    QVector<GCodeLine> out;

    out << comment(QString("BLOC %1 - POCHE CIRCULAIRE").arg(blkId),
                   blkId);

    out << emitToolBlock(d.toolRef, d.toolByName,
                         d.spindleVal, d.spindleByVc,
                         0.0,
                         d.brocheCW,
                         d.fonctionM1, d.fonctionM2,
                         blkId);

    out << pocheCircBody(blkId);

    out << emitRetrait(d.sautBride, blkId);

    return out;
}

QVector<GCodeLine> PocheCircGenerator::pocheCircBody(int blkId) const
{
    const PocheData &d = m_d;
    QVector<GCodeLine> out;

    if (d.diametre<=0 || d.profondeur<=0 || d.profPasse<=0) return out;

    // ── Outil ────────────────────────────────────────────────
    double rOutil=0.0, S=d.spindleVal; int nbDents=1; double angleRampe=15.0;
    {
        QString t=d.toolRef.trimmed();
        if(t.startsWith('T',Qt::CaseInsensitive)) t=t.mid(1);
        bool ok=false; int ti=t.toInt(&ok);
        if(ok&&ti>0){
            const ToolData *td=ToolDatabase::instance().toolByT(ti);
            if(td){ nbDents=qMax(td->nbDents,1);
                if(td->angleRampe>0.01) angleRampe=td->angleRampe;
                if(td->Routil>0){ rOutil=td->Routil;
                    if(d.spindleByVc&&d.spindleVal>0)
                        S=(d.spindleVal*1000.0)/(M_PI*2.0*rOutil); } }
        }
    }
    if(rOutil<0.1){
        out<<comment(QString("ERREUR : outil T%1 absent de la table").arg(d.toolRef),blkId);
        return out;
    }
    const double rEff = qMax(rOutil+d.DR, 0.01);
    const double rTrou = d.diametre/2.0;
    if(rEff>=rTrou){ out<<comment("ERREUR : fraise trop grande",blkId); return out; }

    S=qMax(S,0.0);
    const QString fTrav=avanceStr(d.avanceVal,     d.avanceMode,    S,nbDents);
    const QString fDesc=avanceStr(d.descenteVal,   d.descenteMode,  S,nbDents);
    // Fallback : si pendulVal=0 (champ non rempli), utiliser fDesc
    const QString fPend=(d.pendulVal>1e-6)
                              ?avanceStr(d.pendulVal,d.pendulMode,S,nbDents)
                              :fDesc; // hélice + rampe
    const QString fFin =(d.avanceFinition>0)
                             ?avanceStr(d.avanceFinition,d.avanceFinMode, S,nbDents):fTrav;

    const double tanA=qTan(qDegreesToRadians(angleRampe));
    double rHel=qBound(rEff*0.1, qMax(rTrou*0.15,0.5), rEff*0.45);

    // Avalant=CW(G2), opposition=CCW(G3)  — poche intérieure
    const bool cw=(d.modeFraisage==0);

    out<<blank();
    out<<comment(QString("=== POCHE CIRC. %1  prof.%2 ===")
                       .arg(fc(d.diametre),fc(d.profondeur)),blkId);
    // Note visible dans le fichier NC — NE PAS SUPPRIMER
    out<<comment("  Logique modeUsin: 0=Ebauche+Finition  1=Ebauche seule  2=Finition seule",blkId);

    const QVector<QPointF> positions=computePositionsPoche();
    if(positions.isEmpty()) return out;

    for(int posIdx=0;posIdx<positions.size();++posIdx){
        const QPointF &ref=positions[posIdx];
        double zDep=0.0;
        if(d.positionMode==3) zDep=d.cercleZ;
        else if(d.positionMode==4&&posIdx<d.tableauPts.size()) zDep=d.tableauPts[posIdx].z;

        const double cx=ref.x(), cy=ref.y();
        out<<line(GCodeLine::Rapid,"G0 Z"+fc(d.sautBride),"Plan saut bride",blkId);
        out<<line(GCodeLine::Rapid,"G0 X"+fc(cx)+" Y"+fc(cy),"Centre poche circ.",blkId);

        const double zSurf  = zDep;
        const double zFond  = zDep - d.profondeur;
        const double zEbFond= zFond + d.surepProf;
        // Surep. finition : + laisse de la matiere, - enleve plus (correction)
        // Le contour fini est agrandi (s<0) ou reduit (s>0) ; le DR outil reste inchange
        const double rTrouFin = rTrou - d.surepFinLat;
        const double zFondFin = zFond + d.surepFinProf;
        if(qAbs(d.surepFinLat)>1e-9 || qAbs(d.surepFinProf)>1e-9)
            out<<comment(QString("Surep. finition : lat %1  fond %2").arg(fc(d.surepFinLat),fc(d.surepFinProf)),blkId);
        const double zApp   = zSurf + d.approche;

        // Profils Clipper2
        Path2D profEb  = Clipper2Utils::makeCircle(cx,cy, rTrou-d.surepLat, 64);
        Path2D profFin = Clipper2Utils::makeCircle(cx,cy, rTrouFin, 64);
        Paths2D passesEb  = Clipper2Utils::concentricPasses(profEb,  rOutil,d.DR,d.recouvrement);
        Paths2D passesFin = Clipper2Utils::concentricPasses(profFin, rOutil,d.DR,d.recouvrement);

        // Helper soft exit : décalage d'un rayon vers le centre, puis saut de bride
        auto softExitC=[&](QPointF pt, const QString &fAv){
            double edx=cx-pt.x(), edy=cy-pt.y(), ed=qSqrt(edx*edx+edy*edy);
            if(ed>1e-6)
                out<<line(GCodeLine::Linear,
                            QString("G1 X%1 Y%2 %3")
                                .arg(fc(pt.x()+edx/ed*rOutil),fc(pt.y()+edy/ed*rOutil),fAv),
                            "Degagement paroi",blkId);
            out<<line(GCodeLine::Rapid,"G0 Z"+fc(d.sautBride),"Saut de bride",blkId);
        };

        // ── Phase 1 : Ébauche ─────────────────────────────────
        if(d.modeUsin!=2){
            out<<blank(); out<<comment("-- Ebauche circ. --",blkId);
            out<<line(GCodeLine::Rapid,"G0 Z"+fc(zApp),"Approche Z",blkId);
            double zCur=zSurf; int pn=0;
            while(zCur>zEbFond+1e-6){
                double zNext=qMax(zCur-d.profPasse,zEbFond);
                if(++pn>10000) break;
                out<<comment(QString("Passe %1  Z=%2").arg(pn).arg(fc(zNext)),blkId);
                out<<plongee(cx,cy,zApp,zNext,rHel,tanA,d.modePlongee,(d.modePlongee==2?fDesc:fPend),blkId);
                QPointF lastPt(cx,cy);
                for(const Path2D &pass:passesEb){
                    if(pass.size()<2) continue;
                    Path2D p=cw?Clipper2Utils::ensureCW(pass):Clipper2Utils::ensureCCW(pass);
                    out<<line(GCodeLine::Linear,
                                QString("G1 X%1 Y%2 %3").arg(fc(p[0].x()),fc(p[0].y()),fTrav),"",blkId);
                    for(int i=1;i<p.size();i++)
                        out<<line(GCodeLine::Linear,
                                    QString("G1 X%1 Y%2").arg(fc(p[i].x()),fc(p[i].y())),"",blkId);
                    out<<line(GCodeLine::Linear,
                                QString("G1 X%1 Y%2").arg(fc(p[0].x()),fc(p[0].y())),"Fermeture",blkId);
                    lastPt=QPointF(p[0].x(),p[0].y());
                }
                softExitC(lastPt,fTrav);
                out<<line(GCodeLine::Rapid,"G0 X"+fc(cx)+" Y"+fc(cy),"Retour centre",blkId);
                zCur=zNext;
            }
        }

        // ── Phase 2 : Finition fond ───────────────────────────
        if(d.modeUsin!=1 && d.surepProf>1e-6){
            out<<blank(); out<<comment("-- Finition fond circ. --",blkId);
            // Rapide jusqu'au fond ebauche + approche (matiere connue via surepProf)
            const double zRapFond = zEbFond + d.approche;
            out<<line(GCodeLine::Rapid,"G0 Z"+fc(zRapFond),"Rapide fond ebauche + approche",blkId);
            out<<plongee(cx,cy,zRapFond,zFondFin,rHel,tanA,d.modePlongee,(d.modePlongee==2?fDesc:fPend),blkId);
            QPointF lastPtF(cx,cy);
            for(const Path2D &pass:passesEb){
                if(pass.size()<2) continue;
                Path2D p=cw?Clipper2Utils::ensureCW(pass):Clipper2Utils::ensureCCW(pass);
                out<<line(GCodeLine::Linear,
                            QString("G1 X%1 Y%2 %3").arg(fc(p[0].x()),fc(p[0].y()),fFin),"",blkId);
                for(int i=1;i<p.size();i++)
                    out<<line(GCodeLine::Linear,
                                QString("G1 X%1 Y%2").arg(fc(p[i].x()),fc(p[i].y())),"",blkId);
                out<<line(GCodeLine::Linear,
                            QString("G1 X%1 Y%2").arg(fc(p[0].x()),fc(p[0].y())),"Fermeture",blkId);
                lastPtF=QPointF(p[0].x(),p[0].y());
            }
            softExitC(lastPtF,fFin);
            // Saut de bride déjà fait par softExit
        }

        // ── Phase 3 : Finition paroi ──────────────────────────
        if(d.modeUsin!=1 && d.surepLat>1e-6 && !passesFin.isEmpty()){
            out<<blank(); out<<comment("-- Finition paroi circ. --",blkId);
            const Path2D &dern=passesFin.last();
            if(dern.size()>=2){
                auto traceFin=[&](double zNF){
                    out<<line(GCodeLine::Rapid,"G0 X"+fc(cx)+" Y"+fc(cy),"Centre",blkId);
                    // Poche deja videe (ebauche) : rapide jusqu'a zNF + approche
                    const double zRapF = qMin(zApp, zNF + d.approche);
                    out<<line(GCodeLine::Rapid,"G0 Z"+fc(zRapF),"Rapide zone ebauchee",blkId);
                    out<<plongee(cx,cy,zRapF,zNF,rHel,tanA,d.modePlongee,(d.modePlongee==2?fDesc:fPend),blkId);
                    // ── Finition paroi = cercle parfait → un seul arc G2/G3 ──
                    // Le rayon est celui du contour compensé par Clipper2
                    // (dern = dernière passe de finition, déjà offsettée du
                    //  rayon outil). On mesure le rayon réel depuis le 1er point.
                    const double rFin = qSqrt((dern[0].x()-cx)*(dern[0].x()-cx)
                                            + (dern[0].y()-cy)*(dern[0].y()-cy));
                    const double startX = cx + rFin;    // point à droite (0°)
                    const double startY = cy;
                    const double I = -rFin;             // vers le centre
                    const double J = 0.0;
                    out<<line(GCodeLine::Linear,
                                QString("G1 X%1 Y%2 %3").arg(fc(startX),fc(startY),fFin),"Entree cercle",blkId);
                    QString gArc = cw ? "G2" : "G3";
                    out<<line(GCodeLine::Arc,
                                QString("%1 X%2 Y%3 I%4 J%5")
                                  .arg(gArc, fc(startX), fc(startY), fc(I), fc(J)),
                                "Cercle finition",blkId);
                    softExitC(QPointF(startX,startY),fFin);
                    out<<line(GCodeLine::Rapid,"G0 Z"+fc(d.sautBride),"Degagement",blkId);
                };
                out<<line(GCodeLine::Rapid,"G0 Z"+fc(d.sautBride),"Plan saut bride",blkId);
                if(d.passeFinition>1e-6){
                    double zCurF=zSurf; int pn=0;
                    while(zCurF>zFondFin+1e-6){ double zNF=qMax(zCurF-d.passeFinition,zFondFin);
                        ++pn; out<<comment(QString("Fin paroi passe %1 Z=%2").arg(pn).arg(fc(zNF)),blkId);
                        traceFin(zNF); zCurF=zNF; }
                } else {
                    traceFin(zFondFin);
                }
            }
        }

        // ── Cas spécial : surepLat=0 + finition seule ─────────
        // (paroi toujours finie en mode Finition seule, meme si surepProf>0)
        if(d.modeUsin==2 && d.surepLat<1e-6 && !passesFin.isEmpty()){
            out<<blank(); out<<comment("-- Finition paroi directe (surepLat=0) --",blkId);
            out<<line(GCodeLine::Rapid,"G0 Z"+fc(d.sautBride),"Plan saut bride",blkId);
            out<<line(GCodeLine::Rapid,"G0 X"+fc(cx)+" Y"+fc(cy),"Centre",blkId);
            const double zRapD = qMin(zApp, zFondFin + d.approche);
            out<<line(GCodeLine::Rapid,"G0 Z"+fc(zRapD),"Rapide zone ebauchee",blkId);
            out<<plongee(cx,cy,zRapD,zFondFin,rHel,tanA,d.modePlongee,(d.modePlongee==2?fDesc:fPend),blkId);
            // Cercle complet en un seul arc — rayon du contour compensé Clipper2
            const Path2D &dern2=passesFin.last();
            const double rFin = qSqrt((dern2[0].x()-cx)*(dern2[0].x()-cx)
                                    + (dern2[0].y()-cy)*(dern2[0].y()-cy));
            const double startX = cx + rFin, startY = cy;
            const double I = -rFin, J = 0.0;
            out<<line(GCodeLine::Linear,
                        QString("G1 X%1 Y%2 %3").arg(fc(startX),fc(startY),fFin),"Entree cercle",blkId);
            QString gArc = cw ? "G2" : "G3";
            out<<line(GCodeLine::Arc,
                        QString("%1 X%2 Y%3 I%4 J%5")
                          .arg(gArc, fc(startX), fc(startY), fc(I), fc(J)),
                        "Cercle finition",blkId);
            softExitC(QPointF(startX,startY),fFin);
            out<<line(GCodeLine::Rapid,"G0 Z"+fc(d.sautBride),"Degagement",blkId);
        }

        // NOTE : le retrait final (G0 Z sautBride + M5/M9) est gere par emitRetrait() dans generate()
    }

    return out;
}
