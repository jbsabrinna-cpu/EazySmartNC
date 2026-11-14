#pragma execution_character_set("utf-8")
#include "gcodeengine.h"
#include "pointfile.h"
#include "contourelement.h"
#include "tooldatabase.h"

#include <QDebug>
#include <QPointF>
#include <QtMath>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include "clipper2utils.h"

// ═══════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════
QString GCodeEngine::f(double v, int dec)
{
    return QString::number(v, 'f', dec);
}

QString GCodeEngine::fc(double v)
{
    QString s = QString::number(v, 'f', 3);
    if (s.contains('.')) {
        while (s.endsWith('0')) s.chop(1);
        if (s.endsWith('.')) s += '0';   // "10." → "10.0" plus lisible
    }
    return s;
}

GCodeLine GCodeEngine::line(GCodeLine::Type t, const QString &code,
                            const QString &cmt, int blkId)
{
    GCodeLine l;
    l.type    = t;
    l.code    = code;
    l.comment = cmt;
    l.blockId = blkId;
    return l;
}

GCodeLine GCodeEngine::blank()
{
    return line(GCodeLine::Blank, "");
}

GCodeLine GCodeEngine::comment(const QString &txt, int blkId)
{
    return line(GCodeLine::Comment, "( " + txt + " )", txt, blkId);
}

// avanceStr — convertit val en F mm/min
// mode 0 = mm/dent → F = val × nbDents × S  (défaut UI)
// mode 1 = mm/tr   → F = val × S
// mode 2 = mm/dent → F = val × nbDents × S  (legacy)
// mode 3 = mm/min  (direct)
QString GCodeEngine::avanceStr(double val, int mode, double S, int nbDents)
{
    if (val <= 0.0) return "F0";
    double Fmmmin = val;
    QString cmt;
    switch (mode) {
    case 0:  // mm/dent (UI index 0)
    case 2:  // mm/dent (legacy)
        if (S > 0) {
            int Z = (nbDents > 0) ? nbDents : 1;
            Fmmmin = val * Z * S;
            cmt = "  ( " + fc(val) + " mm/dt x" + QString::number(Z) + "Z )";
        }
        break;
    case 1:  // mm/tr → F = val × S
        if (S > 0) {
            Fmmmin = val * S;
            cmt = "  ( " + fc(val) + " mm/tr )";
        }
        break;
    default: break;  // mode 3 : val déjà en mm/min
    }
    return "F" + fc(qMax(Fmmmin, 1.0)) + cmt;
}

// avanceStr(val, mode) → utiliser avanceStr(val, mode, 0.0, 0) directement

// ─────────────────────────────────────────────────────────────
//  Extraire le numéro d'outil depuis toolRef
//  "T01" → "1"    "T5" → "5"    "1" → "1"    "Fraise12" → "Fraise12"
// ─────────────────────────────────────────────────────────────
static QString toolNumber(const QString &toolRef)
{
    QString s = toolRef.trimmed();
    // Si commence par T suivi de chiffres → extraire les chiffres
    if (s.startsWith('T', Qt::CaseInsensitive)) {
        QString num = s.mid(1);
        bool ok = false;
        num.toInt(&ok);
        if (ok) return num;
    }
    return s;   // retourner tel quel (nom ou numéro brut)
}

// ─────────────────────────────────────────────────────────────
//  BLOC OUTIL — T M6 G43 + S + M3/M4
//  ORDRE CENTROID CNC12 :
//    1. T M6          (appel outil)
//    2. G43 H         (correction longueur)
//    3. S             (vitesse broche — AVANT M3 obligatoire)
//    4. M3/M4         (mise en rotation)
//    5. M7/M8         (arrosage si coolant)
//    6. Fonctions M optionnelles
// ─────────────────────────────────────────────────────────────
QVector<GCodeLine> GCodeEngine::toolBlock(const QString &toolRef, bool /*byName*/,
                                          double spindleVal, bool byVc,
                                          bool cw,
                                          const QString &fm1, const QString &fm2,
                                          double DL, int blkId) const
{
    QVector<GCodeLine> out;
    const QString tNum = toolNumber(toolRef);

    out << blank();
    out << comment("Appel outil T" + tNum, blkId);

    // 1 — Appel outil
    out << line(GCodeLine::ToolCall,
                QString("T%1 M6").arg(tNum),
                "Changement outil T" + tNum, blkId);

    // 2 — G43 : compensation longueur (Centroid CNC12 utilise H, pas D)
    // DL = correcteur delta longueur → appliqué via G43.1 ou commentaire opérateur
    QString g43cmt = "Compensation longueur D" + tNum;
    if (DL != 0.0) g43cmt += QString(" | DL=%1mm → ajuster Z de %2mm").arg(fc(DL), fc(DL));
    out << line(GCodeLine::ToolCall,
                QString("G43 H%1").arg(tNum),
                g43cmt, blkId);
    // Si DL non nul → décalage Z explicite pour Centroid qui ne gère pas DL natif
    if (qAbs(DL) > 1e-6)
        out << line(GCodeLine::Other,
                    QString("( DL=%1mm : decaler tous les Z de %2mm dans ce bloc )").arg(fc(DL), fc(DL)),
                    "Correcteur longueur DL", blkId);

    // 3 — Vitesse broche S (AVANT M3 sur Centroid CNC12)
    // Si byVc=true : spindleVal est une Vc (m/min) → calculer S depuis Routil
    double S_reel = spindleVal;
    double Routil_reel = 0.0;
    {
        // Résoudre numéro T depuis toolRef ("T3" → 3, "3" → 3)
        QString tRef = toolRef.trimmed();
        if (tRef.startsWith('T', Qt::CaseInsensitive)) tRef = tRef.mid(1);
        bool ok = false;
        int tInt = tRef.toInt(&ok);
        if (ok && tInt > 0) {
            const ToolData *td = ToolDatabase::instance().toolByT(tInt);
            if (td) {
                Routil_reel = (td->Routil > 0) ? td->Routil : 0.0;
                if (byVc && spindleVal > 0 && Routil_reel > 0)
                    S_reel = (spindleVal * 1000.0) / (M_PI * 2.0 * Routil_reel); // Routil
                else if (!byVc)
                    S_reel = spindleVal;
            }
        }
    }
    S_reel = qMax(S_reel, 0.0);

    // 3+4 — S et M3/M4 sur la MÊME ligne (conforme PP Fusion Centroid)
    //   "S3000 M3" ou "S3000  ( Vc=200 m/min ) M3"
    QString sLabel;
    if (byVc && Routil_reel > 0)
        sLabel = QString("S%1 %2  ( Vc=%3 m/min, D=%4 mm )")                     .arg(qRound(S_reel)).arg(cw ? "M3" : "M4").arg(fc(spindleVal), fc(Routil_reel * 2.0));
    else
        sLabel = QString("S%1 %2").arg(qRound(S_reel)).arg(cw ? "M3" : "M4");
    out << line(GCodeLine::Spindle, sLabel,
                cw ? "Broche horaire (M3)" : "Broche anti-horaire (M4)", blkId);

    // 4b — Tempo montée en vitesse broche (Centroid standard)
    out << line(GCodeLine::Other, "G4 P3.0", "Attente montee broche (3s)", blkId);

    // 5 — Fonctions M optionnelles (arrosage, etc.)
    if (!fm1.trimmed().isEmpty())
        out << line(GCodeLine::MFunction, fm1.trimmed(), "Fonction M1", blkId);
    if (!fm2.trimmed().isEmpty())
        out << line(GCodeLine::MFunction, fm2.trimmed(), "Fonction M2", blkId);

    return out;
}

// ─────────────────────────────────────────────────────────────
//  RETRAIT ENTRE BLOCS
//  FIX : M9 ajouté (arrosage coupé après chaque bloc)
// ─────────────────────────────────────────────────────────────
QVector<GCodeLine> GCodeEngine::retrait(double sautBride, int blkId) const
{
    QVector<GCodeLine> out;
    out << line(GCodeLine::Rapid,
                "G0 Z" + fc(sautBride),
                "Retrait saut de bride", blkId);
    out << line(GCodeLine::Spindle,  "M5", "Arrêt broche",   blkId);
    out << line(GCodeLine::MFunction,"M9", "Arrêt arrosage", blkId);
    return out;
}

// ═══════════════════════════════════════════════════════════════
//  POSITIONS DE TROUS
// ═══════════════════════════════════════════════════════════════
QVector<QPointF> GCodeEngine::computePositions(const PercageData &d) const
{
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

    case 1: { // Ligne
        if (d.ligneNbPts < 2) { pts << QPointF(d.ligneP1X, d.ligneP1Y); break; }
        double dx = (d.ligneP2X - d.ligneP1X) / (d.ligneNbPts - 1);
        double dy = (d.ligneP2Y - d.ligneP1Y) / (d.ligneNbPts - 1);
        for (int i = 0; i < d.ligneNbPts; ++i)
            pts << QPointF(d.ligneP1X + i * dx, d.ligneP1Y + i * dy);
        break;
    }

    case 2: { // Grille
        for (int iy = 0; iy < d.grilleNbY; ++iy)
            for (int ix = 0; ix < d.grilleNbX; ++ix)
                pts << QPointF(d.grilleOX + ix * d.grillePasX,
                               d.grilleOY + iy * d.grillePasY);
        break;
    }

    case 3: { // Cercle
        if (d.cercleNbPts < 1) break;
        double angDep = qDegreesToRadians(d.cercleAngDep);
        double angFin = qDegreesToRadians(d.cercleAngFin);
        double step   = (d.cercleNbPts > 1)
                          ? (angFin - angDep) / (d.cercleNbPts - 1)
                          : 0;
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
//  PERÇAGE AU FORET
//  FIX : G99 retiré (comportement par défaut sur Centroid CNC12)
//  FIX : R doit être valeur absolue positive (plan de référence)
// ═══════════════════════════════════════════════════════════════
QVector<GCodeLine> GCodeEngine::percageForet(const PercageData &d, int blkId) const
{
    QVector<GCodeLine> out;
    // Guard : paramètres obligatoires
    if (d.profondeur <= 0) {
        qDebug() << "[percageForet] GUARD prof=" << d.profondeur;
        return out;
    }
    QVector<QPointF> pts = computePositions(d);

    // Plan R = approche au-dessus de la surface (Z=0)
    double zR   =  d.approche;          // ex: +2.0 mm au-dessus de Z0
    double zFin = d.traversant ? -(d.profondeur + 2.0) : -d.profondeur;

    out << blank();
    out << comment(QString("═══ PERÇAGE %1  Ø%2  prof.%3  — %4 trou(s) ═══")                       .arg(d.cycle == 0 ? "G81" : "G83").arg(fc(d.diametreOutil), fc(d.profondeur)).arg(pts.size()), blkId);

    if (pts.isEmpty()) return out;

    // Positionnement rapide au premier trou
    out << line(GCodeLine::Rapid,
                QString("G0 X%1 Y%2").arg(fc(pts[0].x()), fc(pts[0].y())),
                "Premier trou", blkId);
    out << line(GCodeLine::Rapid, "G0 Z" + fc(d.sautBride), "Plan saut de bride", blkId);

    if (d.cycle == 0) {
        // ── G81 : perçage simple ─────────────────────────────
        // FORMAT CENTROID : G81 Z[fond] R[plan_R] F[avance] X[x1] Y[y1]
        out << line(GCodeLine::Cycle,
                    QString("G98 G90 G81 Z%1 R%2 %3 X%4 Y%5")                        .arg(fc(zFin), fc(zR), avanceStr(d.avancePerc, d.avanceMode, 0.0, 0), fc(pts[0].x()), fc(pts[0].y())),
                    "Cycle G81 percage simple", blkId);
    } else {
        // ── G83 : débourrage ─────────────────────────────────
        // FORMAT CENTROID : G83 Z[fond] R[plan_R] Q[degagement] F[avance] X Y
        out << line(GCodeLine::Cycle,
                    QString("G98 G90 G83 Z%1 R%2 Q%3 %4 X%5 Y%6")                        .arg(fc(zFin), fc(zR), fc(d.degagement), avanceStr(d.avancePerc, d.avanceMode, 0.0, 0), fc(pts[0].x()), fc(pts[0].y())),
                    "Cycle G83 debourrage Q=" + fc(d.degagement), blkId);
    }

    // Trous suivants : X Y seulement
    for (int i = 1; i < pts.size(); ++i) {
        out << line(GCodeLine::Cycle,
                    QString("X%1 Y%2").arg(fc(pts[i].x()), fc(pts[i].y())),
                    QString("Trou %1/%2").arg(i+1, pts.size()), blkId);
    }

    // Annulation cycle
    out << line(GCodeLine::Cycle, "G80", "Annulation cycle perçage", blkId);

    return out;
}

// ═══════════════════════════════════════════════════════════════
//  FRAISAGE DE TROU HÉLICOÏDAL
// ═══════════════════════════════════════════════════════════════
QVector<GCodeLine> GCodeEngine::percageHelico(const PercageData &d, int blkId) const
{
    QVector<GCodeLine> out;
    QVector<QPointF> pts = computePositions(d);

    double rTrou   = d.diametreTrou / 2.0;
    // DR = rayon direct (ou Routil + DR si outil en table — à améliorer)
    double rFraise = 0.0;
    {
        QString tRefH = d.toolRef.trimmed();
        if (tRefH.startsWith('T', Qt::CaseInsensitive)) tRefH = tRefH.mid(1);
        bool okH = false; int tIntH = tRefH.toInt(&okH);
        if (okH && tIntH > 0) {
            const ToolData *tdH = ToolDatabase::instance().toolByT(tIntH);
            if (tdH && tdH->Routil > 0) rFraise = tdH->Routil;
        }
    }
    if (rFraise < 0.1) {
        out << comment(QString("ERREUR HELICO : outil T%1 absent de la table").arg(d.toolRef), blkId);
        return out;
    }
    double rHelico = rTrou - rFraise;
    double zTotal  = -d.profondeur;
    double zApp    = d.approche;

    out << blank();
    out << comment(QString("═══ FRAISAGE HÉLICOÏDAL Ø%1  prof.%2  — %3 trou(s) ═══")                       .arg(fc(d.diametreTrou), fc(d.profondeur)).arg(pts.size()), blkId);

    for (int ti = 0; ti < pts.size(); ++ti) {
        double cx = pts[ti].x();
        double cy = pts[ti].y();

        if (ti > 0) out << blank();
        out << comment(QString("Trou %1/%2").arg(ti+1, pts.size()), blkId);

        out << line(GCodeLine::Rapid,
                    QString("G0 X%1 Y%2").arg(fc(cx), fc(cy)),
                    "Centre trou", blkId);
        out << line(GCodeLine::Rapid, "G0 Z" + fc(zApp), "Approche Z", blkId);

        double xStart = cx + rHelico;
        out << line(GCodeLine::Linear,
                    QString("G1 X%1 Y%2 %3")                        .arg(fc(xStart), fc(cy), avanceStr(d.avanceTrav,      d.avanceTravMode, 0.0, 0)),
                    "Déplacement rayon hélice", blkId);

        double pasH = (d.pasHelicoidale > 0) ? d.pasHelicoidale : 1.5;  // guard /0
        int    nbTours = qMax(1, (int)qCeil(d.profondeur / pasH));
        double zPas    = -d.profondeur / nbTours;

        out << comment(QString("%1 tours, pas=%2 mm/tour")                           .arg(nbTours).arg(fc(pasH)), blkId);

        double zCur = 0.0;
        for (int t = 0; t < nbTours; ++t) {
            double zNext = zCur + zPas;
            out << line(GCodeLine::Arc,
                        QString("G2 X%1 Y%2 Z%3 I%4 J0.0 %5")                            .arg(fc(xStart), fc(cy), fc(zNext), fc(-rHelico), avanceStr(d.avancePerc,      d.avanceMode, 0.0, 0)),
                        QString("Tour hélicoïdal %1 → Z=%2").arg(t+1).arg(fc(zNext)), blkId);
            zCur = zNext;
        }

        if (qAbs(zCur - zTotal) > 1e-4) {
            out << line(GCodeLine::Arc,
                        QString("G2 X%1 Y%2 Z%3 I%4 J0.0 %5")                            .arg(fc(xStart), fc(cy), fc(zTotal), fc(-rHelico), avanceStr(d.avancePerc,      d.avanceMode, 0.0, 0)),
                        "Rattrapage profondeur finale", blkId);
        }

        // Tour de finition fond
        out << comment("Tour de finition fond", blkId);
        out << line(GCodeLine::Arc,
                    QString("G2 X%1 Y%2 I%3 J0.0 %4")                        .arg(fc(xStart), fc(cy), fc(-rHelico), avanceStr(d.avanceTrav,      d.avanceTravMode, 0.0, 0)),
                    "Finition fond", blkId);

        // Finition paroi
        if (d.passeFinition > 0) {
            double rFin = rHelico + d.passeFinition;
            out << line(GCodeLine::Linear,
                        QString("G1 X%1 Y%2 %3")                            .arg(fc(cx + rFin), fc(cy), avanceStr(d.avanceTrav,      d.avanceTravMode, 0.0, 0)),
                        "Rayon finition paroi", blkId);
            out << line(GCodeLine::Arc,
                        QString("G2 X%1 Y%2 I%3 J0.0")                            .arg(fc(cx + rFin), fc(cy), fc(-rFin)),
                        "Finition paroi", blkId);
        }

        // Retour centre + dégagement
        out << line(GCodeLine::Linear,
                    "G1 X" + fc(cx) + " Y" + fc(cy), "Retour centre", blkId);
        out << line(GCodeLine::Rapid, "G0 Z" + fc(zApp), "Dégagement Z", blkId);
    }

    return out;
}


QVector<GCodeLine> GCodeEngine::generatePercage(const PercageData &d, int blockId) const
{
    QVector<GCodeLine> out;
    out << comment(QString("BLOC %1 - PERCAGE %2")
                       .arg(blockId)
                       .arg(d.mode == PercageData::Foret ? "AU FORET" : "FRAISAGE HELICOIDALE"),
                   blockId);
    out << toolBlock(d.toolRef, d.toolByName,
                     d.spindleVal, d.spindleByVc,
                     d.brocheCW, d.fonctionM1, d.fonctionM2, d.DL, blockId);
    out << (d.mode == PercageData::Foret
                ? percageForet(d, blockId)
                : percageHelico(d, blockId));
    out << retrait(d.sautBride, blockId);
    return out;
}

// ═══════════════════════════════════════════════════════════════
//  EN-TÊTE PROGRAMME
//  FIX : types GCodeLine corrects pour G21/G90/G17/G54
// ═══════════════════════════════════════════════════════════════
QVector<GCodeLine> GCodeEngine::generateProgram(const QString &programName, bool m25RetourOrigine) const
{
    QVector<GCodeLine> out;
    out << comment("═══════════════════════════════════════");
    out << comment("  EazyNC — " + programName);
    out << comment("  Genere par EazyNC");
    out << comment("═══════════════════════════════════════");
    out << blank();
    // Sequence init Centroid CNC12 (conforme Intercon)
    // M25 = M-fonction spéciale forçant un retour Z home AVANT tout —
    // perturbe l'affichage du viewer CNC12 (chemin outil mal placé
    // tant que le 1er G0 en coordonnées pièce n'est pas traité).
    // G28 = même mouvement (retour au 1er point de référence) mais en
    // G-code standard, correctement interprété par le viewer.
    // Choix laissé à l'opérateur via BLK FORM > Paramètres.
    if (m25RetourOrigine) {
        out << line(GCodeLine::Other, "M25 G49", "Retour Z home, effacer compensation longueur");
    } else {
        out << line(GCodeLine::Other, "G28 G91 Z0", "Retour point de reference (Z)");
        out << line(GCodeLine::Other, "G90 G49",    "Mode absolu, effacer compensation longueur");
    }
    out << line(GCodeLine::Other,    "G17 G40", "Plan XY, comp outil inactive");
    out << line(GCodeLine::Other,    "G21",     "Unites : millimetres");
    out << line(GCodeLine::Other,    "G80",     "Annulation cycles");
    out << line(GCodeLine::Other,    "G90",     "Mode absolu");
    out << line(GCodeLine::Other,    "G98",     "Retour plan initial cycles");
    out << blank();
    return out;
}

// ═══════════════════════════════════════════════════════════════
//  BLOC FORME — dimensions matiere + origine piece
//  Genere les lignes :::: X- = ... X+ = ... etc. (format Intercon)
// ═══════════════════════════════════════════════════════════════
QVector<GCodeLine> GCodeEngine::generateBlkForm(const QJsonObject &data, int blkId) const
{
    QVector<GCodeLine> out;

    double xm = data["xMinus"].toDouble(-5);
    double xp = data["xPlus" ].toDouble(100);
    double ym = data["yMinus"].toDouble(-5);
    double yp = data["yPlus" ].toDouble(100);
    double zm = data["zMinus"].toDouble(-50);
    double zp = data["zPlus" ].toDouble(0);

    out << blank();
    // Label conforme Intercon — CNC12 identifie ce bloc par ce texte exact
    out << comment("--- Material Size ---", blkId);
    // N0002 : numéro de bloc Intercon (requis par CNC12)
    out << line(GCodeLine::Other, "N0002", "", blkId);
    // FIX: GCodeLine::Other → toText() émet l.code brut sans parenthèses
    //      GCodeLine::Comment enveloppait en "( :::: ... )" et CNC12 ne reconnaissait pas le ::::
    out << line(GCodeLine::Other,
                QString("  :::: X- = %1, X+ = %2").arg(fc(xm), fc(xp)),
                QString("X- = %1, X+ = %2").arg(fc(xm), fc(xp)), blkId);
    out << line(GCodeLine::Other,
                QString("  :::: Y- = %1, Y+ = %2").arg(fc(ym), fc(yp)),
                QString("Y- = %1, Y+ = %2").arg(fc(ym), fc(yp)), blkId);
    out << line(GCodeLine::Other,
                QString("  :::: Z- = %1, Z+ = %2").arg(fc(zm), fc(zp)),
                QString("Z- = %1, Z+ = %2").arg(fc(zm), fc(zp)), blkId);
    out << blank();
    return out;
}

// ═══════════════════════════════════════════════════════════════
//  FIN DE PROGRAMME — séquence correcte Centroid CNC12
//  Appeler depuis ProgrammePage::emitGCodeReady() à la fin
// ═══════════════════════════════════════════════════════════════
// ─────────────────────────────────────────────────────────────────────
//  offsetPolyline — décalage parallèle d'une polyligne via Clipper2
//
//  Utilise Clipper2Utils::offset (joinType=Round) :
//   - Round = trajectoire physique de la fraise qui tourne autour des coins
//   - Pour les rayons concaves impossibles (R < delta), Clipper supprime
//     proprement la zone et laisse un sommet pointu ou une zone non usinée
//   - Conforme à ce que ferait Heidenhain TNC en interne
// ─────────────────────────────────────────────────────────────────────
static QVector<QPointF> offsetPolyline(const QVector<QPointF> &pts,
                                       double delta, bool closed)
{
    if (pts.size() < 3 || qAbs(delta) < 1e-6) return pts;

    // Préparer le path Clipper : enlever doublon final si fermé
    Path2D profil;
    profil.reserve(pts.size());
    for (const QPointF &p : pts) profil.append(p);
    if (closed && profil.size() >= 2 &&
        std::hypot(profil.last().x()-profil.first().x(),
                   profil.last().y()-profil.first().y()) < 1e-3) {
        profil.removeLast();
    }
    if (profil.size() < 3) return pts;

    auto signedAreaMath = [](const Path2D &p) -> double {
        double a = 0.0;
        int n = p.size();
        for (int i = 0; i < n; ++i) {
            int j = (i + 1) % n;
            a += p[i].x() * p[j].y() - p[j].x() * p[i].y();
        }
        return a * 0.5;
    };

    auto extractBest = [&](const Paths2D &res) -> Path2D {
        if (res.isEmpty()) return Path2D();
        Path2D best = res.first();
        double bestArea = qAbs(signedAreaMath(best));
        for (int i = 1; i < res.size(); ++i) {
            double a = qAbs(signedAreaMath(res[i]));
            if (a > bestArea) { best = res[i]; bestArea = a; }
        }
        return best;
    };

    // Stratégie robuste : on essaie les deux orientations (CCW et CW)
    // et on garde celle qui produit un offset VERS L'EXTERIEUR (= aire plus grande
    // que le contour original) pour delta > 0. Independant de la convention interne
    // de Clipper2 (qui peut varier selon version Y up/down).
    const double aireOriginale = qAbs(signedAreaMath(profil));

    Path2D ccw = profil;
    if (signedAreaMath(ccw) < 0) std::reverse(ccw.begin(), ccw.end());
    Path2D cw = ccw;
    std::reverse(cw.begin(), cw.end());

    Paths2D res1 = Clipper2Utils::offset(ccw, delta, 0);
    Paths2D res2 = Clipper2Utils::offset(cw,  delta, 0);

    Path2D best1 = extractBest(res1);
    Path2D best2 = extractBest(res2);
    double a1 = best1.isEmpty() ? -1.0 : qAbs(signedAreaMath(best1));
    double a2 = best2.isEmpty() ? -1.0 : qAbs(signedAreaMath(best2));

    // Pour delta > 0 (expansion) : on veut l'aire plus grande que l'original
    // Pour delta < 0 (rétrécissement) : on veut l'aire plus petite
    Path2D best;
    if (delta > 0) {
        bool ok1 = a1 > aireOriginale * 1.001;
        bool ok2 = a2 > aireOriginale * 1.001;
        if (ok1 && ok2)      best = (a1 > a2) ? best1 : best2;
        else if (ok1)        best = best1;
        else if (ok2)        best = best2;
        else                 best = (a1 > a2) ? best1 : best2; // fallback
    } else {
        bool ok1 = (a1 >= 0) && (a1 < aireOriginale * 0.999);
        bool ok2 = (a2 >= 0) && (a2 < aireOriginale * 0.999);
        if (ok1 && ok2)      best = (a1 < a2) ? best1 : best2;
        else if (ok1)        best = best1;
        else if (ok2)        best = best2;
        else                 best = (a1 < a2) ? best1 : best2;
    }

    if (best.size() < 3) return QVector<QPointF>();

    // Forcer le sens de sortie a etre coherent avec le profil d'entree
    bool origCCW = signedAreaMath(profil) > 0;
    bool bestCCW = signedAreaMath(best) > 0;
    if (origCCW != bestCCW) std::reverse(best.begin(), best.end());

    // Refermer (Clipper2 ne duplique pas le 1er point en fin)
    if (closed && !best.isEmpty()) best.append(best.first());

    return best;
}

// ─────────────────────────────────────────────────────────────────────
//  alignClosedPathStart — réordonne une polyligne fermée pour qu'elle
//  commence au point le plus proche de "anchor".
// ─────────────────────────────────────────────────────────────────────
static QVector<QPointF> alignClosedPathStart(const QVector<QPointF> &pts,
                                             QPointF anchor)
{
    if (pts.size() < 3) return pts;
    QVector<QPointF> work = pts;
    bool wasClosed = false;
    if (work.size() >= 2 &&
        std::hypot(work.last().x()-work.first().x(),
                   work.last().y()-work.first().y()) < 1e-3) {
        work.removeLast();
        wasClosed = true;
    }
    int    bestIdx  = 0;
    double bestDist = std::numeric_limits<double>::max();
    for (int i = 0; i < work.size(); ++i) {
        double d = std::hypot(work[i].x()-anchor.x(), work[i].y()-anchor.y());
        if (d < bestDist) { bestDist = d; bestIdx = i; }
    }
    if (bestIdx > 0) std::rotate(work.begin(), work.begin()+bestIdx, work.end());
    if (wasClosed) work.append(work.first());
    return work;
}

// ─────────────────────────────────────────────────────────────────────
//  computeContourPath — discrétise + nettoie le contour brut depuis .cnt
//  Retourne (path, closed)
// ─────────────────────────────────────────────────────────────────────
static QVector<QPointF> computeContourPath(const ContourFile &cf, bool &closed)
{
    QVector<QPointF> path;
    auto segs = cf.toSegments(64);
    for (const auto &s : segs) {
        if (s.pts.isEmpty()) continue;
        int start = path.isEmpty() ? 0 : 1;
        for (int k = start; k < s.pts.size(); ++k) path << s.pts[k];
    }
    QVector<QPointF> cleaned;
    for (const auto &p : path) {
        if (cleaned.isEmpty() ||
            std::hypot(p.x()-cleaned.last().x(), p.y()-cleaned.last().y()) > 1e-3)
            cleaned << p;
    }
    path = cleaned;
    closed = (path.size() >= 2 &&
              std::hypot(path.last().x()-path.first().x(),
                         path.last().y()-path.first().y()) < 1e-3);
    return path;
}

QVector<GCodeLine> GCodeEngine::generateEndProgram(int /*unused*/, bool m25RetourOrigine) const
{
    QVector<GCodeLine> out;
    out << blank();
    out << comment("--- End of Program ---");
    if (m25RetourOrigine) {
        out << line(GCodeLine::Other,     "G49 H0 M25", "Retour Z home, effacer comp longueur");
    } else {
        out << line(GCodeLine::Other,     "G49 H0",     "Effacer comp longueur");
        out << line(GCodeLine::Other,     "G28 G91 Z0", "Retour point de reference (Z)");
        out << line(GCodeLine::Other,     "G90",        "Mode absolu");
    }
    out << line(GCodeLine::Other,     "G40",        "Comp outil inactif");
    out << line(GCodeLine::Spindle,   "M5",         "Arret broche");
    out << line(GCodeLine::MFunction, "M9",         "Arrosage Off");
    out << line(GCodeLine::Other,     "G80",        "Cycles annules");
    out << line(GCodeLine::MFunction, "M30",        "Fin de programme");
    return out;
}

// ═══════════════════════════════════════════════════════════════
//  VALIDATION
// ═══════════════════════════════════════════════════════════════
QStringList GCodeEngine::validatePercage(const PercageData &d) const
{
    QStringList w;
    if (d.mode == PercageData::FraisageHelicoidale) {
        if (d.DR <= 0)
            w << "ERREUR : diamètre fraise (DR) non défini.";
        else if (d.DR >= d.diametreTrou)
            w << QString("ERREUR : fraise Ø%1 mm trop grande pour trou Ø%2 mm.")                     .arg(fc(d.DR), fc(d.diametreTrou));
        else if (d.DR < d.diametreTrou / 3.0)
            w << QString("AVERT. : risque d'îlot central — Ø fraise %1 < Ø trou / 3 (%2).")                     .arg(fc(d.DR), fc(d.diametreTrou / 3.0));
        if (d.pasHelicoidale <= 0)
            w << "ERREUR : pas hélicoïdal doit être > 0.";
        if (d.pasHelicoidale > d.profondeur)
            w << "AVERT. : pas hélicoïdal supérieur à la profondeur totale.";
    }
    if (d.profondeur  <= 0) w << "ERREUR : profondeur doit être > 0.";
    if (d.avancePerc  <= 0) w << "ERREUR : avance de perçage non définie.";
    if (d.spindleVal  <= 0) w << "ERREUR : vitesse broche non définie.";
    return w;
}


// ═══════════════════════════════════════════════════════════════
//  CONVERSION EN TEXTE BRUT
// ═══════════════════════════════════════════════════════════════
// ─────────────────────────────────────────────────────────────
//  asciiComment — supprime les caractères non-ASCII (Latin1 safe)
// ─────────────────────────────────────────────────────────────
static QString asciiComment(const QString &s)
{
    static const QMap<QChar,QString> rep = {
                                             {u'═', "="}, {u'─', "-"}, {u'║', "|"},
                                             {u'é', "e"}, {u'è', "e"}, {u'ê', "e"}, {u'ë', "e"},
                                             {u'à', "a"}, {u'â', "a"}, {u'ä', "a"},
                                             {u'î', "i"}, {u'ï', "i"},
                                             {u'ô', "o"}, {u'ö', "o"},
                                             {u'ù', "u"}, {u'û', "u"}, {u'ü', "u"},
                                             {u'ç', "c"}, {u'ñ', "n"},
                                             {u'É', "E"}, {u'È', "E"}, {u'Ê', "E"},
                                             {u'À', "A"}, {u'Â', "A"},
                                             {u'Î', "I"}, {u'Ô', "O"}, {u'Ù', "U"},
                                             {u'Ç', "C"},
                                             {u'—', "-"}, {u'–', "-"}, {u'…', "..."},
                                             {u'’', "'"}, {u'“', """}, {u'”', """},
                                             };
    QString out;
    out.reserve(s.size());
    for (const QChar &c : s) {
        if (rep.contains(c))
            out += rep[c];
        else if (c.unicode() < 128)
            out += c;
        // sinon : ignorer le caractère non-ASCII
    }
    return out;
}

QString GCodeEngine::toText(const QVector<GCodeLine> &lines)
{
    QStringList out;
    for (const GCodeLine &l : lines) {
        if (l.type == GCodeLine::Blank) {
            out << "";
        } else if (l.type == GCodeLine::Comment) {
            // Commenter en ASCII pur pour CNC12
            out << "( " + asciiComment(l.comment) + " )";
        } else {
            out << l.code;
        }
    }
    return out.join('\n');
}