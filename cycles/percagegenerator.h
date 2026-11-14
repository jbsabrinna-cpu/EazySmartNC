#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  percagegenerator.h
//
//  Generateur de G-code pour le cycle PERCAGE.
//  Herite de BlockGenerator (helpers communs : appel outil,
//  formatage, retrait...).
//
//  Couvre les deux modes de PercageData :
//    - Foret               -> cycles canned G81 / G83 (Centroid)
//    - FraisageHelicoidale  -> fraisage de trou en helice
//
//  Le champ d.cycle porte le type de cycle foret :
//    0 = simple (G81)   2 = debourrage (G83)
//
//  UTILISATION :
//     PercageGenerator gen(percageData);
//     QVector<GCodeLine> lines = gen.generate(blkId);
// ═══════════════════════════════════════════════════════════════

#include "blockgenerator.h"

class PercageGenerator : public BlockGenerator
{
public:
    explicit PercageGenerator(const PercageData &data)
        : m_d(data) {}

    QVector<GCodeLine> generate(int blkId) override;

private:
    // Corps d'usinage : percage au foret (cycles canned G81/G83).
    QVector<GCodeLine> percageForet(int blkId) const;

    // Corps d'usinage : fraisage de trou helicoidal.
    QVector<GCodeLine> percageHelico(int blkId) const;

    // Calcul des positions de repetition (point/fichier/ligne/
    // grille/cercle).
    QVector<QPointF> computePositionsPercage() const;

    const PercageData m_d;
};
