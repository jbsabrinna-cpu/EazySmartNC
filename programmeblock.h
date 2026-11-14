#pragma once
#include <QString>
#include <QMap>
#include <QVariant>

// ============================================================
//  EazyNC - Programme Block (Conversational Unit)
//  Représente une opération CNC dans le programme conversationnel
// ============================================================

enum class BlockType {
    // En-tête
    BlkForm,        // BLK FORM - Définition brut
    // Positionnement
    Rapid,          // G00 - Déplacement rapide
    Linear,         // G01 - Interpolation linéaire
    // Cycles fixes perçage
    Drilling,       // G81 - Perçage simple
    PeckDrilling,   // G83 - Perçage avec débourrage
    Tapping,        // G84 - Taraudage
    BoringCycle,    // G85 - Alésage
    Pointing,       // G82 - Pointage / Centrage
    // Fraisage poches
    RectPocket,     // Poche rectangulaire
    CircPocket,     // Poche circulaire
    FraisageTrou,   // Fraisage de trou (alésage hélicoïdal G41)
    // Fraisage contours
    Contour,        // Contour / Profil
    FaceMilling,    // Surfaçage
    Groove,         // Rainure rectangulaire
    CircGroove,     // Rainure circulaire
    // Tenons
    RectTenon,      // Tenon rectangulaire
    CircTenon,      // Tenon circulaire
    // Filetage fraisé
    ThreadMill,     // Filetage avec fraise
    DrillThread,    // Filetage avec foret
    // Gravure
    Engraving,      // Gravure texte
    // Transformations
    Rotation,       // Rotation origine
    Scale,          // Facteur échelle
    Mirror,         // Miroir
    Tilt,           // Inclinaison plan de travail
    // Utilitaires
    ToolChange,     // M6  - Changement d'outil
    SubProgram,     // M98 - Appel sous-programme
    InsertGCode,    // Insert G-code brut
    End             // M30 - Fin de programme
};

struct BlockTypeInfo {
    QString name;
    QString icon;       // chemin ressource Qt :/cycles/xxx.png
    QString category;   // catégorie pour le panneau de sélection
    QString description;
};

inline BlockTypeInfo blockTypeInfo(BlockType t) {
    switch(t) {
    // ── En-tête ─────────────────────────────────────────────────────────────
    case BlockType::BlkForm:
        return {"En-tête programme",   ":/cycles/blk_form.png",        "Programme",        "Définition du brut (BLK FORM)"};
    // ── Positionnement ───────────────────────────────────────────────────────
    case BlockType::Rapid:
        return {"Déplacement rapide",  ":/cycles/rapid.png",           "Positionnement",   "Déplacement G00 sans usinage"};
    case BlockType::Linear:
        return {"Interpolation lin.",  ":/cycles/linear.png",          "Positionnement",   "Interpolation linéaire G01"};
    // ── Perçage ──────────────────────────────────────────────────────────────
    case BlockType::Drilling:
        return {"Perçage",             ":/cycles/drilling.png",        "Perçage",          "Cycle de perçage G81"};
    case BlockType::PeckDrilling:
        return {"Perçage débourrant",  ":/cycles/peck_drilling.png",   "Perçage",          "Perçage avec débourrage G83"};
    case BlockType::Pointing:
        return {"Pointage",            ":/cycles/pointage.png",        "Perçage",          "Pointage / centrage G82"};
    case BlockType::Tapping:
        return {"Taraudage",           ":/cycles/tapping.png",         "Perçage",          "Taraudage G84"};
    case BlockType::BoringCycle:
        return {"Alésage",             ":/cycles/boring.png",          "Perçage",          "Alésage G85"};
    case BlockType::DrillThread:
        return {"Filetage foret",      ":/cycles/filetage_foret.png",  "Perçage",          "Filetage avec foret-taraud"};
    // ── Poches ───────────────────────────────────────────────────────────────
    case BlockType::RectPocket:
        return {"Poche rectangulaire", ":/cycles/rect_pocket.png",     "Fraisage",         "Poche rectangulaire avec Clipper2"};
    case BlockType::CircPocket:
        return {"Poche circulaire",    ":/cycles/circ_pocket.png",     "Fraisage",         "Poche circulaire avec Clipper2"};
    case BlockType::FraisageTrou:
        return {"Fraisage de trou",    ":/cycles/fraisage_trou.png",   "Fraisage",         "Alésage hélicoïdal avec compensation G41"};
    case BlockType::Groove:
        return {"Rainure",             ":/cycles/groove.png",          "Fraisage",         "Rainure rectangulaire"};
    case BlockType::CircGroove:
        return {"Rainure circulaire",  ":/cycles/circ_groove.png",     "Fraisage",         "Rainure circulaire"};
    // ── Contours ─────────────────────────────────────────────────────────────
    case BlockType::Contour:
        return {"Contour",             ":/cycles/contour.png",         "Fraisage",         "Fraisage de contour"};
    case BlockType::FaceMilling:
        return {"Surfaçage",           ":/cycles/face_milling.png",    "Fraisage",         "Surfaçage de face"};
    // ── Tenons ───────────────────────────────────────────────────────────────
    case BlockType::RectTenon:
        return {"Tenon rectangulaire", ":/cycles/rect_tenon.png",      "Fraisage",         "Tenon rectangulaire"};
    case BlockType::CircTenon:
        return {"Tenon circulaire",    ":/cycles/circ_tenon.png",      "Fraisage",         "Tenon circulaire"};
    // ── Filetage ─────────────────────────────────────────────────────────────
    case BlockType::ThreadMill:
        return {"Filetage fraise",     ":/cycles/filetage_fraise.png", "Filetage",         "Filetage avec fraise à fileter"};
    // ── Gravure ──────────────────────────────────────────────────────────────
    case BlockType::Engraving:
        return {"Gravure texte",       ":/cycles/engraving.png",       "Divers",           "Gravure de texte"};
    // ── Transformations ──────────────────────────────────────────────────────
    case BlockType::Rotation:
        return {"Rotation origine",    ":/cycles/rotation.png",        "Transformations",  "Rotation du système de coordonnées"};
    case BlockType::Scale:
        return {"Facteur échelle",     ":/cycles/echelle.png",         "Transformations",  "Mise à l'échelle"};
    case BlockType::Mirror:
        return {"Miroir",              ":/cycles/miroir.png",          "Transformations",  "Symétrie / Miroir"};
    case BlockType::Tilt:
        return {"Inclinaison plan",    ":/cycles/inclinaison.png",     "Transformations",  "Inclinaison du plan de travail"};
    // ── Utilitaires ──────────────────────────────────────────────────────────
    case BlockType::ToolChange:
        return {"Changement outil",    ":/cycles/tool_change.png",     "Utilitaires",      "Appel outil M6"};
    case BlockType::SubProgram:
        return {"Sous-programme",      ":/cycles/sub_program.png",     "Utilitaires",      "Appel sous-programme M98"};
    case BlockType::InsertGCode:
        return {"Insérer G-code",      ":/cycles/insert_gcode.png",    "Utilitaires",      "Insérer une ligne G-code directe"};
    case BlockType::End:
        return {"Fin de programme",    ":/cycles/end.png",             "Utilitaires",      "Fin programme M30"};
    }
    return {"?", ":/cycles/blk_form.png", "", ""};
}

// Retourne tous les BlockTypes disponibles pour le panneau de sélection
inline QList<BlockType> allBlockTypes() {
    return {
        BlockType::BlkForm,
        BlockType::Rapid,      BlockType::Linear,
        BlockType::Drilling,   BlockType::PeckDrilling, BlockType::Pointing,
        BlockType::Tapping,    BlockType::BoringCycle,  BlockType::DrillThread,
        BlockType::RectPocket, BlockType::CircPocket, BlockType::FraisageTrou,
        BlockType::Groove,     BlockType::CircGroove,
        BlockType::Contour,    BlockType::FaceMilling,
        BlockType::RectTenon,  BlockType::CircTenon,
        BlockType::ThreadMill,
        BlockType::Engraving,
        BlockType::Rotation,   BlockType::Scale, BlockType::Mirror, BlockType::Tilt,
        BlockType::ToolChange, BlockType::SubProgram,
        BlockType::InsertGCode,BlockType::End
    };
}
