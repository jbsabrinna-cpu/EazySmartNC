#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  contourgenerator.h
//
//  Generateur de G-code pour le cycle CONTOURNAGE (usinage
//  exterieur). Herite de BlockGenerator.
//
//  C'est ce generateur qui recevra plus tard l'import DXF :
//  isole des autres cycles, on peut y travailler la geometrie
//  du contour sans risquer de casser les poches.
//
//  NOTE SENS AVALANT/OPPOSITION :
//  La logique de sens (modeFraisage) est pour l'instant reprise
//  STRICTEMENT a l'identique de l'ancien gcodeengine.cpp, y
//  compris les portions marquees "TEST". Le but de cette etape
//  est uniquement le decoupage : le comportement doit rester
//  identique. La stabilisation du sens se fera ensuite, dans
//  ce fichier isole.
//
//  UTILISATION :
//     ContourGenerator gen(contourData);
//     QVector<GCodeLine> lines = gen.generate(blkId);
// ═══════════════════════════════════════════════════════════════

#include "blockgenerator.h"

class ContourGenerator : public BlockGenerator
{
public:
    explicit ContourGenerator(const ContourData &data)
        : m_d(data) {}

    QVector<GCodeLine> generate(int blkId) override;

private:
    // Corps du contournage (ancien GCodeEngine::generateContour).
    QVector<GCodeLine> contourBody(int blkId) const;

    const ContourData m_d;
};
