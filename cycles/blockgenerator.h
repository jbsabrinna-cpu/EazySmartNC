#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  blockgenerator.h
//
//  Classe mere abstraite pour les generateurs de G-code par cycle.
//
//  PRINCIPE (Strategy Pattern) :
//   - Chaque cycle d'usinage (poche rect, poche circ, contournage,
//     percage...) a SON propre generateur, dans son propre fichier.
//   - Tout le code COMMUN a tous les cycles (formatage des nombres,
//     fabrication de lignes G-code, appel outil, retrait...) vit
//     ICI, dans la classe mere. Les generateurs en heritent.
//   - GCodeEngine devient un simple orchestrateur : il instancie
//     le bon generateur selon le type de bloc et appelle generate().
//
//  UTILISATION :
//     ContourGenerator gen(contourData);
//     QVector<GCodeLine> lines = gen.generate(blkId);
//
//  Chaque sous-classe recoit SA struct de donnees via son
//  constructeur (PocheData, ContourData, PercageData...).
// ═══════════════════════════════════════════════════════════════

#include <QVector>
#include <QString>
#include <QPointF>
#include "../gcodeengine.h"   // GCodeLine, structs de donnees
#include "../occoffset.h"     // PathElems, PathElem (offset OCC avec arcs)

class BlockGenerator
{
public:
    virtual ~BlockGenerator() = default;

    // ── Methode virtuelle pure : a implementer par chaque cycle ──
    //  Genere les lignes de G-code pour ce bloc.
    //  blkId = identifiant du bloc (pour les commentaires/tracabilite).
    virtual QVector<GCodeLine> generate(int blkId) = 0;

protected:
    // ─────────────────────────────────────────────────────────────
    //  HELPERS COMMUNS — disponibles pour TOUS les generateurs.
    //  Implementes une seule fois dans blockgenerator.cpp.
    // ─────────────────────────────────────────────────────────────

    // ── Formatage des nombres ───────────────────────────────────
    //  f()  : formate un reel avec 'dec' decimales (ex 12.340)
    //  fc() : formate une coordonnee (decimales adaptees, sans
    //         zeros inutiles)
    static QString f(double v, int dec = 3);
    static QString fc(double v);

    // ── Fabrication de lignes G-code ────────────────────────────
    static GCodeLine line(GCodeLine::Type t,
                          const QString &code,
                          const QString &comment = QString(),
                          int blkId = 0);
    static GCodeLine blank();
    static GCodeLine comment(const QString &txt, int blkId = 0);

    // ── Conversion d'avance ─────────────────────────────────────
    //  Convertit une valeur d'avance en F mm/min selon le mode
    //  choisi (mm/min, mm/tr, mm/dent...).
    //   val     : valeur saisie par l'operateur
    //   mode    : 0=mm/min, 1=mm/tr, 2=mm/dent (selon ta convention)
    //   S       : vitesse de broche (tr/min)
    //   nbDents : nombre de dents de l'outil
    static QString avanceStr(double val, int mode,
                             double S, int nbDents);

    // ── Bloc d'appel outil ──────────────────────────────────────
    //  Genere : appel T, M6, compensation longueur G43, mise en
    //  rotation broche, temporisation, fonctions M optionnelles.
    //   toolRef    : reference outil (numero "T1" ou nom)
    //   byName     : true si toolRef est un nom, false si numero
    //   spindleVal : vitesse broche (ou Vc si byVc)
    //   byVc       : true si spindleVal est une vitesse de coupe
    //   diametre   : diametre outil (reserve, non utilise ici car
    //                le rayon vient de ToolDatabase)
    //   brocheCW   : true = M3 (horaire), false = M4
    //   fM1, fM2   : fonctions M optionnelles (arrosage...)
    //   blkId      : identifiant du bloc
    QVector<GCodeLine> emitToolBlock(const QString &toolRef,
                                     bool   byName,
                                     double spindleVal,
                                     bool   byVc,
                                     double diametre,
                                     bool   brocheCW,
                                     const QString &fM1,
                                     const QString &fM2,
                                     int blkId) const;

    // ── Retrait final ───────────────────────────────────────────
    //  Genere le retrait securitaire de fin de cycle :
    //  G0 Z(sautBride), arret broche M5, arrosage off M9.
    QVector<GCodeLine> emitRetrait(double sautBride, int blkId) const;

    // ── Conversion polyligne → G1/G2/G3 ─────────────────────────
    //  Parcourt un contour Clipper2 (déjà calculé/compensé) et émet
    //  des G1 pour les segments droits, des G2/G3 pour les arcs.
    //  Détecte les arcs : 3+ points consécutifs équidistants d'un
    //  centre commun (coins arrondis, cercles). Tolérance ~0.02mm.
    //  Les positions ne sont PAS recalculées : on utilise les points
    //  de Clipper tels quels, on regroupe juste ceux qui forment un arc.
    //   pts       : contour fermé (le 1er point n'est PAS répété en fin)
    //   cwSense   : true=G2 (avalant), false=G3 (opposition)
    //   premiere  : commentaire/avance sur la 1ère ligne (ex "F954 ...")
    //   blkId     : identifiant bloc
    QVector<GCodeLine> emitContourArcs(const QVector<QPointF> &pts,
                                       bool cwSense,
                                       const QString &premiereAvance,
                                       int blkId) const;

    // ── Traduction PathElems (OCC) → G-code G1/G2/G3 ────────────
    //  Prend un contour issu de OccOffset (segments + arcs typés)
    //  et émet directement G1 pour les lignes, G2/G3 pour les arcs.
    //  premiereAvance : avance ajoutée sur la 1ère ligne (ex "F954 ...")
    //  rotPivotX/Y, rotRad : rotation optionnelle appliquée aux points
    //                        (0 = pas de rotation)
    QVector<GCodeLine> emitPathElems(const PathElems &elems,
                                     const QString &premiereAvance,
                                     double rotPivotX, double rotPivotY,
                                     double rotRad,
                                     int blkId) const;
};
