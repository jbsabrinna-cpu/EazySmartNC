#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  fraisagetrougenerator.h
//
//  Générateur de G-code pour le cycle FRAISAGE DE TROU
//  (alésage hélicoïdal).
//
//  DIFFÉRENCES vs les autres cycles :
//   - Utilise G41/G40 (compensation contrôleur) et NON le G40
//     inliné des poches. Raison : un trou est un cercle régulier
//     sans coin concave, donc le lookahead Centroid (10 événements)
//     ne pose aucun problème. G41 permet d'ajuster le Ø sur machine
//     sans regénérer le programme.
//   - Trajectoire programmée au Ø nominal. C'est le contrôleur
//     qui décale du rayon outil via G41.
//   - L'outil n'intervient dans le calcul que pour :
//       (a) vérifier que la fraise est assez grosse
//       (b) vérifier que l'angle de rampe est acceptable
//       (c) calculer les avances (Fz → mm/min)
//
//  SÉQUENCE G-CODE (par position) :
//   1. G0 Z saut de bride (1re position) OU Z liaison (positions suivantes)
//   2. G0/G1 XY centre (point de passage cx∓rOutil)
//   3. G0 Z approche
//   4. G41 D<T>  (segment d'établissement = arc d'amorce suivant)
//   5. Arc d'amorce demi-cercle vers le profil (G3 si avalant)
//   6. Hélice descendante (G3 si avalant), pas hélicoïdal/tour
//   7. Tour complet au fond (finition)
//   8. Arc de retour vers le centre + G40
//  Puis, une fois toutes les positions traitées : retrait final
//  (G0 Z saut de bride, cf. emitRetrait dans generate()).
//
//  Z PAR POSITION (validation 07/2026) : chaque position (tableau,
//  fichier .pnt, cercle, grille) peut définir sa propre surface Z —
//  utile pour des trous à des hauteurs différentes dans un même
//  bloc (pièce étagée). Calculé par computePositionsZ().
//
//  RETRAIT ENTRE POSITIONS (validation 07/2026) : entre deux trous
//  d'un même bloc, retrait à Z liaison (rapide) au lieu du saut de
//  bride complet, avec positionnement XY en G0 ou G1 F selon le
//  choix "Positionnement" du bloc. Le tout premier trou du bloc
//  garde le comportement d'origine (retrait complet au saut de
//  bride) — comportement inchangé pour un bloc à un seul trou.
//
//  UTILISATION :
//     FraisageTrouGenerator gen(data);
//     QVector<GCodeLine> lines = gen.generate(blkId);
// ═══════════════════════════════════════════════════════════════

#include "blockgenerator.h"
#include "../uniteditor.h"   // FraisageTrouData
#include <QPointF>
#include <QVector>

class FraisageTrouGenerator : public BlockGenerator
{
public:
    explicit FraisageTrouGenerator(const FraisageTrouData &data)
        : m_d(data) {}

    QVector<GCodeLine> generate(int blkId) override;

    // ── Position 3D (X,Y,Z) d'un trou à usiner ──────────────────
    struct PositionPt { double x = 0.0, y = 0.0, z = 0.0; };

    // Statique/publique : réutilisable telle quelle par l'UI (aperçu
    // graphique du modèle de points) sans dupliquer la logique — garantit
    // que l'aperçu correspond TOUJOURS exactement au G-code généré.
    // Version complète (X,Y,Z) — utilisée par le générateur.
    static QVector<PositionPt> computePositionsZ(const FraisageTrouData &d);

    // Version 2D (X,Y seulement) — utilisée par l'aperçu graphique
    // 2D (PointsPreviewWidget), qui n'a pas besoin du Z. Signature
    // inchangée pour ne pas casser les appels existants côté UI.
    static QVector<QPointF> computePositions(const FraisageTrouData &d);

private:
    QVector<GCodeLine> fraisageTrouBody(int blkId,
                                        double cx, double cy, double zSurfPt,
                                        bool isFirstPoint,
                                        double S, int nbDents,
                                        double rOutil) const;

    const FraisageTrouData m_d;
};
