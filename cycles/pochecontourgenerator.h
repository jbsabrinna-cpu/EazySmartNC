#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  pochecontourgenerator.h
//
//  Cycle POCHE DE CONTOUR (facon TNC cycles SL) :
//   - contour exterieur ferme (.cnt) + ilots (.cnt fermes) qui restent
//   - ebauche par passes concentriques, de l'interieur vers les parois,
//     zone par zone (un ilot peut couper la poche en plusieurs zones)
//   - plongee directe (point choisi ou calcule), helicoidale ou en rampe
//   - finition fond + finition parois (contour + ilots)
//   - sens avalant / opposition respecte
//
//  Toute la compensation est calculee ici (G40), comme les autres cycles.
// ═══════════════════════════════════════════════════════════════
#include "blockgenerator.h"

class PocheContourGenerator : public BlockGenerator
{
public:
    explicit PocheContourGenerator(const ContourData &data) : m_d(data) {}
    QVector<GCodeLine> generate(int blkId) override;
private:
    const ContourData m_d;
};
