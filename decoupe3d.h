#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  Decoupe3D — découpage d'un modèle 3D (maillage) en couches Z
//
//  Pour chaque niveau Z, calcule l'OMBRE de la pièce au-dessus de
//  ce Z : la projection en XY de toute la matière située à Z ou
//  plus haut. C'est la zone que l'outil (3 axes, venant du dessus)
//  ne doit pas toucher à ce niveau. Zone à vider = brut − ombre.
//
//  Niveaux : du haut vers le bas au pas de la passe d'ébauche,
//  + le Z de chaque face plane horizontale (fonds et marches).
// ═══════════════════════════════════════════════════════════════
#include <QVector>
#include <QPointF>

using Contour2D  = QVector<QPointF>;
using Contours2D = QVector<Contour2D>;

struct Couche3D
{
    double     z = 0.0;
    bool       facePlane = false;   // niveau venant d'une face plane
    Contours2D ombre;               // contours fermés (extérieurs CCW, trous CW)
    Contours2D aVider;              // zone à vider à ce niveau (limite − ombre agrandie)
};

class Decoupe3D
{
public:
    // mesh : 6 floats par sommet (x y z nx ny nz), 3 sommets par triangle
    // zHaut / zBas : limites du découpage ; passe : profondeur de passe (> 0)
    static QVector<Couche3D> couches(const QVector<float> &mesh,
                                     double zHaut, double zBas, double passe);

    // Z des faces planes horizontales trouvées dans le maillage (décroissant)
    static QVector<double> zFacesPlanes(const QVector<float> &mesh);

    // Ombre cumulée à des niveaux donnés (z décroissant, bool = face plane)
    static QVector<Couche3D> couchesAuxNiveaux(const QVector<float> &mesh,
                                               const QVector<QPair<double, bool>> &niveaux);

    // Zone à vider = limite − (ombre agrandie de surep)
    static Contours2D zoneAVider(const Contours2D &limite, const Contours2D &ombre, double surep);
    // Agrandir (delta > 0) ou rétrécir des contours, coins arrondis
    static Contours2D decaler(const Contours2D &c, double delta);
    static Contours2D intersection(const Contours2D &a, const Contours2D &b);
};
