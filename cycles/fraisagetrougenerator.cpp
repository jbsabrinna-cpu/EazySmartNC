#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  fraisagetrougenerator.cpp
//
//  Cycle FRAISAGE DE TROU (alésage hélicoïdal G41).
//  Calqué sur la macro fraisage_trou_g41.nc validée en simulation
//  et sur machine réelle (formule d'arc d'amorce/retour cx∓rOutil).
// ═══════════════════════════════════════════════════════════════
#include "fraisagetrougenerator.h"
#include "../tooldatabase.h"
#include "../pointfile.h"

#include <QtMath>
#include <QDebug>

// ─────────────────────────────────────────────────────────────
//  generate — enrobe : commentaire + appel outil + corps + retrait
// ─────────────────────────────────────────────────────────────
QVector<GCodeLine> FraisageTrouGenerator::generate(int blkId)
{
    const FraisageTrouData &d = m_d;
    QVector<GCodeLine> out;

    out << comment(QString("BLOC %1 - FRAISAGE DE TROU").arg(blkId), blkId);

    // ── Récupérer les données outil depuis la base ────────────
    double rOutil = 0.0;
    double S      = d.spindleVal;
    int    nbDents = 1;

    {
        QString tRef = d.toolRef.trimmed();
        if (tRef.startsWith('T', Qt::CaseInsensitive)) tRef = tRef.mid(1);
        bool ok = false;
        int ti = tRef.toInt(&ok);
        if (ok && ti > 0) {
            const ToolData *td = ToolDatabase::instance().toolByT(ti);
            if (td) {
                nbDents = qMax(td->nbDents, 1);
                if (td->Routil > 0) {
                    rOutil = td->Routil;
                    if (d.spindleByVc && d.spindleVal > 0)
                        S = (d.spindleVal * 1000.0) / (M_PI * 2.0 * rOutil);
                }
            }
        }
    }
    S = qMax(S, 0.0);

    // ── Contrôle (a) : fraise assez grosse ───────────────────
    //   Ø_outil ≥ (Ø_fini - Ø_avant_trou) / 2
    const double dOutil = rOutil * 2.0;
    const double minDiamOutil = (d.diametreFini - d.diametreAvant) / 2.0;
    if (rOutil > 0.1 && dOutil < minDiamOutil - 1e-3) {
        out << comment(QString("ALERTE : fraise trop petite (Ø%1 < %2 requis) — matière non usinée au centre")
                           .arg(fc(dOutil), fc(minDiamOutil)), blkId);
        // On continue quand même (l'opérateur est averti dans le NC)
    }

    // ── Contrôle (b) : angle de rampe ────────────────────────
    //   atan(pas / (2·π·rayon_hélice)) ≤ angleRampeMaxi
    const double rayonProg    = d.diametreFini / 2.0;
    const double rayonHelice  = rayonProg - rOutil;   // rayon réel de la trajectoire
    if (rayonHelice > 0.1 && d.pasHelicoidal > 0) {
        const double angleRampeReel = qRadiansToDegrees(
            qAtan(d.pasHelicoidal / (2.0 * M_PI * rayonHelice)));
        if (angleRampeReel > d.angleRampeMaxi + 1e-3) {
            out << comment(QString("ALERTE : angle de rampe %1° dépasse le maxi %2° — augmenter le pas")
                               .arg(fc(angleRampeReel), fc(d.angleRampeMaxi)), blkId);
        }
    }

    // ── Bloc appel outil ─────────────────────────────────────
    out << emitToolBlock(d.toolRef, d.toolByName,
                         d.spindleVal, d.spindleByVc,
                         0.0,
                         d.brocheCW,
                         d.fonctionM1, d.fonctionM2,
                         blkId);

    // ── Positions à usiner : Point / Ligne / Grille / Cercle / Tableau ─
    //  Version complète X,Y,Z — chaque position peut avoir sa propre
    //  surface Z (pièce étagée, ou fichier .pnt avec Z par point).
    const QVector<PositionPt> positions = computePositionsZ(d);

    // ── Contrôle (c) : Z liaison / saut de bride vs motif à surfaces
    //  mélangées (ex : trous à Z0 et Z-20 dans le même bloc) ────────
    //  Z liaison et saut de bride sont des plans ABSOLUS, communs à
    //  TOUT le bloc — donc réglés par rapport à la surface la PLUS
    //  HAUTE du motif, sinon on talonne dans la matière en traversant
    //  vers une position plus haute. Le contrôle ne porte que sur
    //  Z liaison (utilisé entre positions) quand il y a plus d'une
    //  position — inutile sur un bloc à un seul trou.
    if (positions.size() > 1) {
        double zMaxApproche = positions.first().z + d.approche;
        for (const PositionPt &p : positions)
            zMaxApproche = qMax(zMaxApproche, p.z + d.approche);

        if (d.zLiaison < zMaxApproche - 1e-3) {
            out << comment(QString("ALERTE : Z liaison (%1) trop bas pour ce motif — "
                                    "la surface la plus haute nécessite Z liaison ≥ %2 "
                                    "(sinon collision en traversant entre positions)")
                               .arg(fc(d.zLiaison), fc(zMaxApproche)), blkId);
        }
        if (d.sautBride < zMaxApproche - 1e-3) {
            out << comment(QString("ALERTE : saut de bride (%1) trop bas pour ce motif — "
                                    "remonter au-dessus de Z%2")
                               .arg(fc(d.sautBride), fc(zMaxApproche)), blkId);
        }
    }

    // ── Corps du cycle — répété pour chaque position ──────────
    //  Seule la 1re position fait un retrait complet au saut de
    //  bride (sécurité : on ne connaît pas la position précédente
    //  de l'outil). Les positions suivantes utilisent Z liaison —
    //  identique au comportement d'origine si une seule position.
    for (int i = 0; i < positions.size(); ++i) {
        const PositionPt &pos = positions[i];
        out << fraisageTrouBody(blkId, pos.x, pos.y, pos.z, (i == 0),
                                S, nbDents, rOutil);
    }

    // ── Retrait final ─────────────────────────────────────────
    out << emitRetrait(d.sautBride, blkId);

    return out;
}

// ─────────────────────────────────────────────────────────────
//  computePositionsZ — calcule la liste des centres de trous à
//  usiner (X,Y,Z) selon le mode de position sélectionné.
//
//  Z PAR POSITION : d.zSurface est une base commune (0 par défaut,
//  pas de champ UI dédié pour l'instant). Les champs Z déjà
//  existants dans l'UI (tableauPts[].z, cercleZ, grilleOZ, ou le Z
//  d'un fichier .pnt) s'ajoutent à cette base — donc si tu ne
//  remplis aucun Z, tout reste à 0 comme avant (rétro-compatible).
// ─────────────────────────────────────────────────────────────
QVector<FraisageTrouGenerator::PositionPt>
FraisageTrouGenerator::computePositionsZ(const FraisageTrouData &d)
{
    QVector<PositionPt> pts;

    // ── Priorité au fichier de points .pnt (réutilisable, comme .cnt) ──
    //  Si un fichier est associé, ses points (déjà combinés depuis
    //  tous ses motifs empilés) remplacent ENTIÈREMENT les anciens
    //  champs positionMode/ligne/grille/cercle/tableau du bloc.
    if (!d.fichierPoints.isEmpty()) {
        PointFile pf;
        if (pf.load(d.fichierPoints)) {
            for (const auto &p : pf.points)
                if (p.actif) pts << PositionPt{p.x, p.y, d.zSurface + p.z};
            if (!pts.isEmpty()) return pts;
        }
        // Si le fichier est introuvable/vide, on retombe sur les
        // anciens champs ci-dessous (sécurité, ne bloque pas la génération).
    }

    switch (d.positionMode) {

    case 0: { // ── Point unique (ou Point1/Point2 rapides du Sommaire) ──
        for (const auto &tp : d.tableauPts)
            if (qAbs(tp.x) > 1e-9 || qAbs(tp.y) > 1e-9)
                pts << PositionPt{tp.x, tp.y, d.zSurface + tp.z};
        if (pts.isEmpty()) pts << PositionPt{d.posX, d.posY, d.zSurface + d.posZ};
        break;
    }

    case 1: { // ── Ligne ─────────────────────────────────────
        double pas = d.ligneP2Y;
        for (int i = 0; i < qMax(d.ligneNbPts, 1); ++i)
            pts << PositionPt{d.ligneP1X + i * pas, d.ligneP1Y, d.zSurface};
        break;
    }

    case 2: { // ── Grille ────────────────────────────────────
        for (int iy = 0; iy < qMax(d.grilleNbY, 1); ++iy)
            for (int ix = 0; ix < qMax(d.grilleNbX, 1); ++ix)
                pts << PositionPt{d.grilleOX + ix * d.grillePasX,
                                  d.grilleOY + iy * d.grillePasY,
                                  d.zSurface + d.grilleOZ};
        break;
    }

    case 3: { // ── Cercle ────────────────────────────────────
        if (d.cercleNbPts < 1) break;
        double angDep = qDegreesToRadians(d.cercleAngDep);
        double step   = qDegreesToRadians(d.cercleAngInc);
        for (int i = 0; i < d.cercleNbPts; ++i) {
            double a = angDep + i * step;
            pts << PositionPt{d.cercleCX + d.cercleRayon * qCos(a),
                              d.cercleCY + d.cercleRayon * qSin(a),
                              d.zSurface + d.cercleZ};
        }
        break;
    }

    case 4: { // ── Tableau manuel X/Y/Z ──────────────────────
        for (const auto &tp : d.tableauPts) {
            if (qAbs(tp.x) > 1e-9 || qAbs(tp.y) > 1e-9)
                pts << PositionPt{tp.x, tp.y, d.zSurface + tp.z};
        }
        break;
    }

    default: break;
    }

    if (pts.isEmpty()) {
        // Fallback : tableauPts en priorité, sinon posX/posY
        for (const auto &tp : d.tableauPts)
            if (qAbs(tp.x) > 1e-9 || qAbs(tp.y) > 1e-9)
                pts << PositionPt{tp.x, tp.y, d.zSurface + tp.z};
        if (pts.isEmpty())
            pts << PositionPt{d.posX, d.posY, d.zSurface + d.posZ};
    }

    return pts;
}

// ─────────────────────────────────────────────────────────────
//  computePositions — version 2D (X,Y) pour l'aperçu graphique.
//  Dérivée de computePositionsZ() pour garantir que l'aperçu
//  correspond TOUJOURS exactement aux positions du G-code généré.
// ─────────────────────────────────────────────────────────────
QVector<QPointF> FraisageTrouGenerator::computePositions(const FraisageTrouData &d)
{
    QVector<QPointF> pts;
    for (const PositionPt &p : computePositionsZ(d))
        pts << QPointF(p.x, p.y);
    return pts;
}

// ─────────────────────────────────────────────────────────────
//  fraisageTrouBody — séquence hélicoïdale avec G41/G40
//
//  Géométrie (depuis la macro validée) :
//   - rayonProg = Ø_fini / 2     → trajectoire programmée (G41 compense)
//   - xDep = cx + rayonProg      → point de départ XY de l'hélice
//   - arc amorce  : I = +rayonProg/2, J=0 (demi-cercle depuis centre)
//   - arc hélice  : I = -rayonProg,   J=0 (tour complet depuis xDep)
//   - arc retour  : I = -rayonProg/2, J=0 (demi-cercle retour centre)
//   - Avalant = G3 (sens anti-horaire vu du dessus pour intérieur)
//
//  isFirstPoint : true → retrait/positionnement initial au saut de
//  bride (comportement d'origine, inchangé). false → retrait à Z
//  liaison + positionnement XY en G0 ou G1 F selon le choix
//  "Positionnement" du bloc (avanceLiaison > 0 → F, sinon G0).
// ─────────────────────────────────────────────────────────────
QVector<GCodeLine> FraisageTrouGenerator::fraisageTrouBody(
    int blkId, double cx, double cy, double zSurfPt, bool isFirstPoint,
    double S, int nbDents, double rOutil) const
{
    const FraisageTrouData &d = m_d;
    QVector<GCodeLine> out;

    // ── Vérifications de base ─────────────────────────────────
    if (d.diametreFini <= 0 || d.profondeur <= 0 || d.pasHelicoidal <= 0) {
        out << comment("ERREUR : données fraisage trou invalides (Ø, prof ou pas = 0)", blkId);
        return out;
    }

    // ── Avances ──────────────────────────────────────────────
    //  fContour  : tour de finition/ébauche (avanceVal/avanceMode)
    //  fDescente : plongée hélicoïdale (descenteVal/descenteMode) —
    //  désormais réellement indépendante de fContour (Fix validation
    //  07/2026 : avant, descenteVal était forcé = avanceVal côté UI).
    const QString fContour  = avanceStr(d.avanceVal,   d.avanceMode,   S, nbDents);
    const QString fDescente = avanceStr(d.descenteVal, d.descenteMode, S, nbDents);

    // ── Géométrie commune ──────────────────────────────────────
    const double zSurf     = zSurfPt;   // surface Z propre à CETTE position
    const double zFond     = zSurf - d.profondeur;
    const double zApproche = zSurf + d.approche;

    // Avalant=G3 (intérieur, CNC12 convention), opposition=G2
    const bool avalant = (d.modeFraisage == 0);
    const QString gArc = avalant ? "G3" : "G2";
    // Compensation : G41 pour avalant (déjà validé sur machine). En
    // opposition, il faut OBLIGATOIREMENT basculer sur G42 — garder G41
    // avec un sens de rotation inversé décale l'outil du mauvais côté
    // du profil programmé (vers l'extérieur du trou au lieu de l'intérieur),
    // ce qui usine un trou surdimensionné d'environ 2× le rayon outil.
    const QString gComp = avalant ? "G41" : "G42";

    // ── Rayons selon le mode ────────────────────────────────────
    //  Fin (0)    : une seule hélice au rayon nominal
    //  Eb (1)     : hélice au rayon réduit (surépaisseur laissée)
    //  Eb+Fin (2) : hélice réduite, puis tour(s) au rayon nominal
    const double rayonNominal = d.diametreFini / 2.0;
    const double rayonEbauche = (d.modeFin != 0)
        ? qMax(rayonNominal - d.surepLat, 0.1)
        : rayonNominal;

    QString tRef = d.toolRef.trimmed();
    if (tRef.startsWith('T', Qt::CaseInsensitive)) tRef = tRef.mid(1);

    out << blank();
    out << comment(QString("=== FRAISAGE TROU Ø%1  prof.%2  pas %3mm/tr  mode=%4 ===")
                       .arg(fc(d.diametreFini), fc(d.profondeur), fc(d.pasHelicoidal))
                       .arg(d.modeFin==0?"Fin":d.modeFin==1?"Eb":"Eb+Fin"), blkId);

    // ── 1. Retrait avant déplacement ────────────────────────
    //  1re position du bloc : retrait complet au saut de bride
    //  (sécurité, comportement d'origine inchangé).
    //  Positions suivantes : Z liaison seulement (plus rapide),
    //  suffisant puisqu'on vient déjà d'une position sûre du
    //  même bloc.
    if (isFirstPoint) {
        out << line(GCodeLine::Rapid, "G0 Z" + fc(d.sautBride), "Plan saut de bride", blkId);
    } else {
        out << line(GCodeLine::Rapid, "G0 Z" + fc(d.zLiaison), "Plan liaison (entre positions)", blkId);
    }

    // Point de passage entrée/sortie — même principe validé CNC12 :
    // décalé du rayon outil par rapport au centre, PAS le centre exact.
    // Entrée ET sortie utilisent ce même point (symétrie parfaite).
    // Point de passage entrée/sortie — TOUJOURS du côté opposé à xDep
    // (donc près du centre), quel que soit le sens de fraisage. Même
    // règle qu'en avalant (seule direction validée sur machine à
    // l'origine) : un grand arc balayant depuis près du centre jusqu'à
    // la paroi, jamais un engagement brutal juste contre la paroi.
    // Mathématiquement valide dans les deux sens : la tangente de l'arc
    // d'amorce en xDep dépend uniquement du fait que xDep soit le point
    // "loin" du diamètre de l'arc d'amorce (donc du côté opposé à
    // ptPassage) et du sens G2/G3 — pas du côté choisi pour ptPassage.
    const double ptPassageX = cx - rOutil;

    // ── 2. Positionnement XY (point de passage, pas le centre exact) ──
    //  G0 par défaut, ou G1 F(avanceLiaison) si "Positionnement F"
    //  choisi dans le bloc (avanceLiaison > 0).
    if (d.avanceLiaison > 1e-6) {
        out << line(GCodeLine::Linear,
                    QString("G1 X%1 Y%2 F%3").arg(fc(ptPassageX), fc(cy), fc(d.avanceLiaison)),
                    "Point de passage (centre ∓ rOutil)", blkId);
    } else {
        out << line(GCodeLine::Rapid,
                    QString("G0 X%1 Y%2").arg(fc(ptPassageX), fc(cy)),
                    "Point de passage (centre ∓ rOutil)", blkId);
    }

    // ── 3. Approche Z ────────────────────────────────────────
    out << line(GCodeLine::Rapid, "G0 Z" + fc(zApproche), "Z approche", blkId);

    // ═══════════════════════════════════════════════════════════
    //  PASSE 1 : hélice (rayon ébauche ou nominal selon mode)
    // ═══════════════════════════════════════════════════════════
    {
        const double rayonProg = rayonEbauche;
        const double xDep = cx + rayonProg;

        // G41/G42 + arc d'amorce — formule symétrique à la sortie validée
        // CNC12 : demi-cercle simple entre le point de passage et xDep.
        {
            const double ijX = (xDep - ptPassageX) / 2.0;
            out << line(GCodeLine::Arc,
                        QString("%1 D%2 %3 X%4 Y%5 I%6 J0.0 %7")
                            .arg(gComp, tRef, gArc, fc(xDep), fc(cy), fc(ijX), fContour),
                        d.modeFin==1 ? QString(gComp+" + arc amorce (ébauche)") : QString(gComp+" + arc amorce vers profil"),
                        blkId);
        }

        // Hélice descendante
        out << blank();
        out << comment(d.modeFin==1 ? "Hélice descendante (ébauche)" : "Hélice descendante", blkId);
        double zCur = zApproche;
        int safe = 0;
        while (zCur > zFond + 1e-6 && safe++ < 2000) {
            double zNext = qMax(zCur - d.pasHelicoidal, zFond);
            out << line(GCodeLine::Arc,
                        QString("%1 X%2 Y%3 I%4 J0.0 Z%5 %6")
                            .arg(gArc, fc(xDep), fc(cy), fc(-rayonProg), fc(zNext), fDescente),
                        QString("Hélice Z→%1").arg(fc(zNext)), blkId);
            zCur = zNext;
        }

        // Tour de finition/fermeture complet au fond — TOUJOURS exécuté,
        // quel que soit le mode (Fin/Eb/Eb+Fin), pour fermer proprement
        // le profil sur 360° avant le dégagement.
        out << blank();
        out << comment("Tour complet au fond", blkId);
        out << line(GCodeLine::Arc,
                    QString("%1 X%2 Y%3 I%4 J0.0 %5")
                        .arg(gArc, fc(xDep), fc(cy), fc(-rayonProg), fContour),
                    "Tour fond complet", blkId);

        // Arc retour — même point de passage que l'amorce (symétrie).
        auto emitRetourArc = [&](const QString &labelG40) {
            const double ijX = (ptPassageX - xDep) / 2.0;
            out << line(GCodeLine::Arc,
                        QString("%1 X%2 Y%3 I%4 J0.0 %5")
                            .arg(gArc, fc(ptPassageX), fc(cy), fc(ijX), fContour),
                        "Arc retour", blkId);
            out << line(GCodeLine::Other, "G40", labelG40, blkId);
        };
        if (d.modeFin == 2) {
            emitRetourArc("Annulation compensation (ébauche)");
        } else {
            emitRetourArc("Annulation compensation");
        }
    }

    // ═══════════════════════════════════════════════════════════
    //  PASSE 2 (Eb+Fin uniquement) : finition au rayon nominal
    // ═══════════════════════════════════════════════════════════
    if (d.modeFin == 2) {
        const double rayonProg = rayonNominal;
        const double xDep = cx + rayonProg;

        out << blank();
        out << comment("=== PASSE FINITION (Ø nominal) ===", blkId);

        // Repositionnement au point de passage + redescente à l'approche
        out << line(GCodeLine::Rapid,
                    QString("G0 X%1 Y%2").arg(fc(ptPassageX), fc(cy)),
                    "Point de passage (finition)", blkId);
        out << line(GCodeLine::Rapid, "G0 Z" + fc(zApproche), "Z approche finition", blkId);

        // Descente directe au centre (zone déjà dégagée par l'ébauche)
        // jusqu'à Z fond + approche — gain de temps vs hélice complète
        const double zPreFond = zFond + d.approche;
        if (d.descenteFinDirecte) {
            out << blank();
            out << comment("Descente directe au centre (finition)", blkId);
            out << line(GCodeLine::Rapid, "G0 Z" + fc(zPreFond),
                        "Descente axiale jusqu'à approche fond", blkId);
        }

        // G41/G42 + arc d'amorce (finition), formule symétrique validée
        {
            const double ijX = (xDep - ptPassageX) / 2.0;
            out << line(GCodeLine::Arc,
                        QString("%1 D%2 %3 X%4 Y%5 I%6 J0.0 %7")
                            .arg(gComp, tRef, gArc, fc(xDep), fc(cy), fc(ijX), fContour),
                        gComp + " + arc amorce (finition)", blkId);
        }

        if (d.descenteFinDirecte) {
            // Hélice uniquement sur la zone d'approche restante
            // (zPreFond → zFond) — pas besoin de refaire toute la
            // profondeur, la descente axiale a déjà fait le gros.
            out << blank();
            out << comment("Hélice sur approche restante (finition)", blkId);
            double zCur = zPreFond;
            int safe = 0;
            while (zCur > zFond + 1e-6 && safe++ < 200) {
                double zNext = qMax(zCur - d.pasHelicoidal, zFond);
                out << line(GCodeLine::Arc,
                            QString("%1 X%2 Y%3 I%4 J0.0 Z%5 %6")
                                .arg(gArc, fc(xDep), fc(cy), fc(-rayonProg), fc(zNext), fDescente),
                            QString("Hélice finition Z→%1").arg(fc(zNext)), blkId);
                zCur = zNext;
            }
        } else {
            // Hélice descendante de finition — pas dédié "Passe finition"
            // (souvent DIFFÉRENT du pas hélicoïdal d'ébauche, indépendant
            // — typiquement plus grand car moins de matière à enlever en
            // finition). Si non renseignée, retombe sur le pas d'ébauche.
            out << blank();
            out << comment("Hélice descendante (finition)", blkId);
            const double pasFinition = (d.passeFinition > 1e-6) ? d.passeFinition : d.pasHelicoidal;
            double zCur = zApproche;
            int safe = 0;
            while (zCur > zFond + 1e-6 && safe++ < 2000) {
                double zNext = qMax(zCur - pasFinition, zFond);
                out << line(GCodeLine::Arc,
                            QString("%1 X%2 Y%3 I%4 J0.0 Z%5 %6")
                                .arg(gArc, fc(xDep), fc(cy), fc(-rayonProg), fc(zNext), fDescente),
                            QString("Hélice finition Z→%1").arg(fc(zNext)), blkId);
                zCur = zNext;
            }
        }

        // Tour de finition complet au fond
        out << line(GCodeLine::Arc,
                    QString("%1 X%2 Y%3 I%4 J0.0 %5")
                        .arg(gArc, fc(xDep), fc(cy), fc(-rayonProg), fContour),
                    "Tour fond finition", blkId);

        // Arc retour (finition) — même point de passage
        {
            const double ijX = (ptPassageX - xDep) / 2.0;
            out << line(GCodeLine::Arc,
                        QString("%1 X%2 Y%3 I%4 J0.0 %5")
                            .arg(gArc, fc(ptPassageX), fc(cy), fc(ijX), fContour),
                        "Arc retour (finition)", blkId);
            out << line(GCodeLine::Other, "G40", "Annulation compensation (finition)", blkId);
        }
    }

    // NOTE : le retrait G0 Z sautBride + M5 + M9 est géré par
    // emitRetrait() dans generate(). Pas de G0 Z ici.

    return out;
}
