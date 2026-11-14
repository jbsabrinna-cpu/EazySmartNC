#pragma once
#pragma execution_character_set("utf-8")

// ═══════════════════════════════════════════════════════════════
//  Clipper2Utils — interface EazyNC ↔ Clipper2
//  Calcul d'offsets 2D pour trajectoires centre outil
//
//  Principe :
//    PathOffset(profil, -rayon_effectif) = trajectoire centre outil
//    rayon_effectif = Routil + DR
//    DR < 0 → enlève plus   DR > 0 → laisse matière
//
//  Toutes les coordonnées EazyNC sont en mm (double)
//  Clipper2 travaille en entiers → échelle × 1000 (microns)
// ═══════════════════════════════════════════════════════════════

#include <QVector>
#include <QPointF>
#include <QVector3D>

// ─────────────────────────────────────────────────────────────
//  Types de base
// ─────────────────────────────────────────────────────────────
using Path2D  = QVector<QPointF>;   // un contour
using Paths2D = QVector<Path2D>;    // plusieurs contours

// ─────────────────────────────────────────────────────────────
//  Clipper2Utils — fonctions statiques
// ─────────────────────────────────────────────────────────────
class Clipper2Utils
{
public:
    // ── Offset d'un profil ──────────────────────────────────────
    // delta < 0 → réduction (fraise à l'intérieur d'une poche)
    // delta > 0 → expansion (fraise à l'extérieur d'un contour)
    // joinType : 0=Round, 1=Miter, 2=Square
    static Paths2D offset(const Path2D &profil, double delta, int joinType = 0);
    // arcTol : precision des arrondis (mm). 0.1 par defaut ; finition : 0.005
    static Paths2D offset(const Paths2D &profils, double delta, int joinType = 0, double arcTol = 0.1);

    // ── Génération de passes concentriques (poche) ────────────
    // Retourne les passes du centre vers l'extérieur
    // profil = parois de la poche
    // routil = rayon fraise (géométrie)
    // dr     = correcteur delta (DR du bloc)
    // recouvrement = % de chevauchement (0-100)
    static Paths2D concentricPasses(const Path2D &profil,
                                    double routil,
                                    double dr,
                                    double recouvrement);

    // ── Difference booleenne A \ B ────────────────────────────
    // Retourne les zones qui sont dans A mais pas dans B.
    // Utile pour calculer "brut moins contour" en ebauche adaptative.
    static Paths2D difference(const Paths2D &subjects, const Paths2D &clips);
    static Paths2D difference(const Path2D  &subject,  const Path2D  &clip);

    // ── Intersection booleenne A ∩ B ─────────────────────────
    // Retourne les zones qui sont dans A ET dans B.
    // Utile pour limiter un offset au perimetre du brut.
    static Paths2D intersect(const Paths2D &subjects, const Paths2D &clips);
    static Paths2D intersect(const Path2D  &subject,  const Path2D  &clip);

    // ── Constructeurs de profils simples ─────────────────────
    static Path2D makeRect(double cx, double cy,
                           double longueur, double largeur,
                           double rayonCoin = 0.0);
    static Path2D makeCircle(double cx, double cy, double rayon,
                             int segments = 64);

    // ── Vérification ─────────────────────────────────────────
    // Retourne true si le profil est valide (non vide, non dégénéré)
    static bool isValid(const Path2D &p);

    // ── Aire signée — positif = CCW, négatif = CW ────────────
    static double signedArea(const Path2D &p);
    static bool   isCCW(const Path2D &p);

    // ── Forcer orientation ────────────────────────────────────
    static Path2D  ensureCCW(const Path2D &p);
    static Path2D  ensureCW (const Path2D &p);
};
