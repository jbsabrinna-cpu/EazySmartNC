#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  pochecircgenerator.h
//
//  Generateur de G-code pour le cycle POCHE CIRCULAIRE.
//  Herite de BlockGenerator (helpers communs : appel outil,
//  formatage, retrait...).
//
//  Convention de sens : pour une poche (usinage interieur a
//  passes concentriques), avalant => CW. Gere en interne via
//  d.modeFraisage, repris a l'identique de l'ancien moteur.
//
//  UTILISATION :
//     PocheCircGenerator gen(pocheData);
//     QVector<GCodeLine> lines = gen.generate(blkId);
// ═══════════════════════════════════════════════════════════════

#include "blockgenerator.h"

class PocheCircGenerator : public BlockGenerator
{
public:
    explicit PocheCircGenerator(const PocheData &data)
        : m_d(data) {}

    QVector<GCodeLine> generate(int blkId) override;

private:
    // Corps de l'usinage poche circulaire (ancien pocheCirc).
    QVector<GCodeLine> pocheCircBody(int blkId) const;

    // Calcul des positions de repetition (point/ligne/grille/
    // cercle/tableau).
    QVector<QPointF> computePositionsPoche() const;

    const PocheData m_d;
};
