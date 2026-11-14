#pragma once
#pragma execution_character_set("utf-8")

#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <functional>
#include <QtMath>

// ═══════════════════════════════════════════════════════════════
//  ToolData — données complètes d'un outil EazyNC
//
//  Convention : tout en RAYON dans EazyNC
//  Centroid stocke le diamètre → conversion à l'entrée/sortie
//
//  Colonnes tableau :
//    T | Nom | Type | Ø (mm) | R (mm) | Long. | Lu |
//    R bec | R épaulement | Angle pointe | Nb dents | DL | DR
//
//  DL / DR = correcteurs delta permanents de l'outil
//    → peuvent être overridés dans chaque bloc programme
//    → DL = correcteur longueur, DR = correcteur rayon
//  Pas de Vc/Fz — données de coupe = savoir-faire opérateur
// ═══════════════════════════════════════════════════════════════
struct ToolData
{
    // ── Identification ────────────────────────────────────────
    int     T           = 0;
    QString description;

    // ── Type outil ────────────────────────────────────────────
    enum ToolType {
        Fraise2T    = 0,
        Fraise4T    = 1,
        Foret       = 2,
        Torique     = 3,
        Boule       = 4,
        Conique     = 5,
        Taraud      = 6,
        Alesoir     = 7,
        Autre       = 99
    };
    ToolType toolType = Fraise4T;

    // ── Géométrie outil (RAYON partout) ──────────────────────
    double  Routil      = 0.0;   // rayon réel (mm) — Ø = Routil×2
    double  Long        = 0.0;   // longueur totale outil (mm)
    double  Lu          = 0.0;   // longueur utile / sortie max (mm)
    double  Lcoupe      = 0.0;   // longueur de coupe LCUTS (mm)
                                 //   = longueur effective des arêtes de coupe
    double  LsousPince  = 0.0;   // longueur sortie sous pince/mandrin (mm)
                                 //   = jusqu'où l'outil dépasse de la pince
    double  Routil2     = 0.0;   // rayon de bec / coin (mm) — torique, bull-nose
    double  REpaulement = 0.0;   // rayon d'épaulement (mm)
    double  rayonQueue  = 0.0;   // rayon de la queue cylindrique (mm)
                                 //   = rayon de l'attache porte-outil
    double  rayonGorge  = 0.0;   // rayon de gorge / dégagement (mm)
                                 //   = rayon réduit entre coupe et corps
    double  AnglePointe = 0.0;   // angle de pointe (°) — foret, conique
    double  rayonPointe = 0.0;   // foret : rayon de la pointe tronquée (mm)
                                 //   0   = pointe parfaite (cône plein)
                                 //   >0  = tronc de cône (foret réel)
                                 // L_pointe = (Routil - rayonPointe)/tan(AnglePointe/2)
    int     nbDents     = 0;     // nombre de dents / arêtes
    double  angleRampe  = 15.0;  // angle descente hélicoïdale/rampe (°)
    double  pitch       = 0.0;   // pas de filetage (mm) — taraud, fraise à fileter
                                 //   ex. M8x1.25 -> pitch = 1.25

    // ── Correcteurs delta permanents ──────────────────────────
    double  DL = 0.0;   // correcteur longueur delta (mm)
    double  DR = 0.0;   // correcteur rayon delta (mm)

    // ── Offsets Centroid (arrière-plan) ───────────────────────
    int     HNumber     = 0;
    int     DNumber     = 0;
    int     spindleSpeed  = 0;
    int     spindleDir    = 0;
    int     coolant       = 0;
    int     bin           = 0;

    // ── Validité ─────────────────────────────────────────────
    bool isEmpty() const { return T == 0 && description.isEmpty() && Routil <= 0; }

    // ── Calculs ───────────────────────────────────────────────
    double diametre()    const { return Routil * 2.0; }
    double rEffectif()   const { return Routil + DR; }

    // ── Sérialisation JSON ────────────────────────────────────
    QJsonObject toJson() const;
    static ToolData  fromJson(const QJsonObject &obj);
    static QString   toolTypeName(ToolType t);
    static QString   toolTypeIcon(ToolType t);   // chemin PNG icône type (:/tools/...)
    static QStringList toolTypeNames();
    static ToolType  toolTypeFromIndex(int i);
    static int       toolTypeToIndex(ToolType t);
};

// ═══════════════════════════════════════════════════════════════
//  ToolDatabase
// ═══════════════════════════════════════════════════════════════
class CentroidBridgeWrapper;

class ToolDatabase
{
public:
    static ToolDatabase& instance();

    void init(CentroidBridgeWrapper *bridge, const QString &jsonPath);
    void load();
    void save();

    const QVector<ToolData>& tools() const { return m_tools; }
    ToolData*       toolByT(int T);
    const ToolData* toolByT(int T) const;
    // Renvoie l'outil T ; le crée (vide) s'il n'existe pas encore.
    ToolData*       ensureTool(int T);

    static constexpr int MAX_TOOLS = 255;
    bool writeToolToCentroid(const ToolData &tool);

    std::function<void()> onChanged;

private:
    ToolDatabase() = default;
    ToolDatabase(const ToolDatabase&) = delete;

    void loadFromCentroid();
    void loadFromJson();
    void mergeJson(const QJsonObject &root);
    void sortByT();

    CentroidBridgeWrapper *m_bridge   = nullptr;
    QString                m_jsonPath;
    QVector<ToolData>      m_tools;
};
