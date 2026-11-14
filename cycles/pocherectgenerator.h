#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  pocherectgenerator.h
//
//  Generateur de G-code pour le cycle POCHE RECTANGULAIRE.
//  Herite de BlockGenerator (helpers communs : appel outil,
//  formatage, retrait...).
//
//  Convention de sens : pour une poche (usinage interieur a
//  passes concentriques), avalant => CW. C'est gere en interne
//  via d.modeFraisage, repris a l'identique de l'ancien moteur.
//
//  UTILISATION :
//     PocheRectGenerator gen(pocheData);
//     QVector<GCodeLine> lines = gen.generate(blkId);
// ═══════════════════════════════════════════════════════════════

#include "blockgenerator.h"

class PocheRectGenerator : public BlockGenerator
{
public:
    // Recoit la struct de donnees de la poche via le constructeur.
    explicit PocheRectGenerator(const PocheData &data)
        : m_d(data) {}

    // Genere le bloc complet : commentaire + appel outil +
    // usinage de la poche + retrait.
    QVector<GCodeLine> generate(int blkId) override;

private:
    // Corps de l'usinage poche rectangulaire (ancien pocheRect).
    QVector<GCodeLine> pocheRectBody(int blkId) const;

    // Calcul des positions de repetition (point/ligne/grille/
    // cercle/tableau) — copie depuis l'ancien moteur.
    QVector<QPointF> computePositionsPoche() const;

    const PocheData m_d;   // donnees de la poche (copie locale)
};
