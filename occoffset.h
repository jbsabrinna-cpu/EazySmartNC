#pragma once
#pragma execution_character_set("utf-8")

// ═══════════════════════════════════════════════════════════════
//  occoffset — Offset 2D de contour avec PRÉSERVATION DES ARCS
//
//  Contrairement à Clipper2 (qui discrétise tout en polylignes),
//  ce module utilise OpenCASCADE BRepOffsetAPI_MakeOffset pour
//  offsetter un contour en gardant les arcs de cercle natifs.
//
//  Résultat : une liste d'éléments typés (segment droit OU arc),
//  que le générateur traduit directement en G1 / G2 / G3.
//
//  Encapsulé sous WITH_OCC : si OCC absent, les fonctions
//  retournent une liste vide → le caller retombe sur Clipper2.
// ═══════════════════════════════════════════════════════════════

#include <QVector>
#include <QPointF>

// ─────────────────────────────────────────────────────────────
//  PathElem — un élément de contour : segment droit ou arc
// ─────────────────────────────────────────────────────────────
struct PathElem {
    enum Kind { Line, Arc };
    Kind    kind = Line;
    QPointF start;        // point de départ
    QPointF end;          // point d'arrivée
    // Pour les arcs uniquement :
    QPointF center;       // centre de l'arc
    bool    cw = true;    // sens horaire (true=G2) / anti-horaire (false=G3)
    double  radius = 0.0; // rayon de l'arc
};

using PathElems = QVector<PathElem>;     // un contour = suite d'éléments
using PathElemsList = QVector<PathElems>; // plusieurs passes

// ─────────────────────────────────────────────────────────────
//  OccOffset — fonctions statiques d'offset 2D
// ─────────────────────────────────────────────────────────────
class OccOffset
{
public:
    // Indique si OCC est compilé (true = offset disponible)
    static bool available();

    // ── Offset d'un CERCLE ──────────────────────────────────────
    //  Génère les passes concentriques d'une poche circulaire.
    //  cx,cy   : centre du cercle (paroi finale)
    //  rParoi  : rayon de la paroi finale (= diamètre/2)
    //  rOutil  : rayon outil
    //  dr      : correcteur DR
    //  pas     : pas radial entre passes (mm)
    //  Retourne : liste de passes, du centre vers la paroi.
    //             Chaque passe = liste d'éléments (ici : 1 arc complet).
    static PathElemsList circlePasses(double cx, double cy,
                                      double rParoi,
                                      double rOutil, double dr,
                                      double pas);

    // ── Offset d'un RECTANGLE À COINS ARRONDIS ──────────────────
    //  Génère les passes concentriques d'une poche rectangulaire.
    //  cx,cy    : centre du rectangle
    //  longueur : dimension X (paroi finale)
    //  largeur  : dimension Y (paroi finale)
    //  rc       : rayon des coins (0 = coins vifs)
    //  rOutil   : rayon outil
    //  dr       : correcteur DR
    //  pas      : pas radial entre passes (mm)
    //  Retourne : liste de passes, du centre vers la paroi. Chaque
    //             passe = 4 segments droits + 4 arcs (ou 4 lignes si rc=0).
    //  NOTE : rotation NON appliquée ici — le caller la fait après.
    static PathElemsList rectPasses(double cx, double cy,
                                    double longueur, double largeur,
                                    double rc,
                                    double rOutil, double dr,
                                    double pas);

    // ── Test simple : offset d'un cercle unique ─────────────────
    //  Retourne le contour offsetté à 'delta' (négatif = intérieur).
    //  Pour valider la chaîne OCC.
    static PathElems offsetCircle(double cx, double cy, double rayon,
                                  double delta);
};
