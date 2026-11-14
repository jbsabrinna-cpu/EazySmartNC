#pragma execution_character_set("utf-8")
#include "tooldatabase.h"
#include "centroidbridgewrapper.h"

#ifdef WITH_CENTROID_API
#include "CentroidWrapper.h"
#endif

#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QDebug>
#include <algorithm>
#include <cmath>

// ═══════════════════════════════════════════════════════════════
//  ToolData — JSON + helpers
// ═══════════════════════════════════════════════════════════════
QJsonObject ToolData::toJson() const
{
    QJsonObject o;
    o["T"]           = T;
    o["description"] = description;
    o["toolType"]    = (int)toolType;
    o["Routil"]      = Routil;
    o["Long"]        = Long;
    o["Lu"]          = Lu;
    o["Lcoupe"]      = Lcoupe;
    o["LsousPince"]  = LsousPince;
    o["Routil2"]     = Routil2;
    o["REpaulement"] = REpaulement;
    o["rayonQueue"]  = rayonQueue;
    o["rayonGorge"]  = rayonGorge;
    o["AnglePointe"] = AnglePointe;
    o["rayonPointe"] = rayonPointe;
    o["nbDents"]     = nbDents;
    o["angleRampe"]  = angleRampe;
    o["pitch"]       = pitch;
    o["DL"]          = DL;
    o["DR"]          = DR;
    // Offsets Centroid
    o["HNumber"]     = HNumber;
    o["DNumber"]     = DNumber;
    o["spindleSpeed"]= spindleSpeed;
    o["spindleDir"]  = spindleDir;
    o["coolant"]     = coolant;
    o["bin"]         = bin;
    return o;
}

ToolData ToolData::fromJson(const QJsonObject &o)
{
    ToolData d;
    d.T           = o["T"].toInt();
    d.description = o["description"].toString();
    d.toolType    = (ToolType)o["toolType"].toInt(1); // défaut Fraise4T
    d.Routil      = o["Routil"].toDouble();
    d.Long        = o["Long"].toDouble();
    // Garde-fou anti-corruption : un rayon/longueur hors plage physique
    // (mémoire Centroid non initialisée → valeurs en 1e+26) est ramené à 0.
    if (!std::isfinite(d.Routil) || d.Routil < 0 || d.Routil > 500.0) d.Routil = 0.0;
    if (!std::isfinite(d.Long)   || d.Long   < 0 || d.Long   > 2000.0) d.Long  = 0.0;
    d.Lu          = o["Lu"].toDouble();
    d.Lcoupe      = o["Lcoupe"].toDouble();
    d.LsousPince  = o["LsousPince"].toDouble();
    d.Routil2     = o["Routil2"].toDouble();
    d.REpaulement = o["REpaulement"].toDouble();
    d.rayonQueue  = o["rayonQueue"].toDouble();
    d.rayonGorge  = o["rayonGorge"].toDouble();
    d.AnglePointe = o["AnglePointe"].toDouble();
    d.rayonPointe = o["rayonPointe"].toDouble();
    d.nbDents     = o["nbDents"].toInt(0);
    d.angleRampe  = o["angleRampe"].toDouble(15.0);
    d.pitch       = o["pitch"].toDouble();
    d.DL          = o["DL"].toDouble();
    d.DR          = o["DR"].toDouble();
    d.HNumber     = o["HNumber"].toInt();
    d.DNumber     = o["DNumber"].toInt();
    d.spindleSpeed= o["spindleSpeed"].toInt();
    d.spindleDir  = o["spindleDir"].toInt();
    d.coolant     = o["coolant"].toInt();
    d.bin         = o["bin"].toInt();
    // Compat ancienne version (Vc/Fz ignorés)
    return d;
}

QString ToolData::toolTypeName(ToolType t)
{
    switch (t) {
    case Fraise2T:  return "Fraise 2T";
    case Fraise4T:  return "Fraise 4T";
    case Foret:     return "Foret";
    case Torique:   return "Torique";
    case Boule:     return "Boule";
    case Conique:   return "Conique";
    case Taraud:    return "Taraud";
    case Alesoir:   return "Alésoir";
    default:        return "Autre";
    }
}

// ─────────────────────────────────────────────────────────────
//  toolTypeIcon — chemin de l'icône PNG associée au type d'outil.
//  Les fichiers PNG sont à placer sous le préfixe :/tools/ du .qrc.
//  La liste des noms attendus est dans icons/tool_icons_list.txt.
// ─────────────────────────────────────────────────────────────
QString ToolData::toolTypeIcon(ToolType t)
{
    switch (t) {
    case Fraise2T:  return ":/tools/fraise_2t.png";
    case Fraise4T:  return ":/tools/fraise_4t.png";
    case Foret:     return ":/tools/foret.png";
    case Torique:   return ":/tools/torique.png";
    case Boule:     return ":/tools/boule.png";
    case Conique:   return ":/tools/conique.png";
    case Taraud:    return ":/tools/taraud.png";
    case Alesoir:   return ":/tools/alesoir.png";
    default:        return ":/tools/autre.png";
    }
}

QStringList ToolData::toolTypeNames()
{
    return {"Fraise 2T","Fraise 4T","Foret","Torique",
            "Boule","Conique","Taraud","Alésoir","Autre"};
}

ToolData::ToolType ToolData::toolTypeFromIndex(int i)
{
    static const ToolType map[] = {
        Fraise2T, Fraise4T, Foret, Torique,
        Boule, Conique, Taraud, Alesoir, Autre
    };
    if (i >= 0 && i < 9) return map[i];
    return Autre;
}

int ToolData::toolTypeToIndex(ToolType t)
{
    static const ToolType map[] = {
        Fraise2T, Fraise4T, Foret, Torique,
        Boule, Conique, Taraud, Alesoir, Autre
    };
    for (int i = 0; i < 9; ++i)
        if (map[i] == t) return i;
    return 8; // Autre
}

// ═══════════════════════════════════════════════════════════════
//  ToolDatabase
// ═══════════════════════════════════════════════════════════════
ToolDatabase& ToolDatabase::instance()
{
    static ToolDatabase db;
    return db;
}

void ToolDatabase::init(CentroidBridgeWrapper *bridge, const QString &jsonPath)
{
    m_bridge   = bridge;
    m_jsonPath = jsonPath;
}

void ToolDatabase::load()
{
    m_tools.clear();
    loadFromCentroid();
    loadFromJson();
    sortByT();
    if (onChanged) onChanged();
}

void ToolDatabase::sortByT()
{
    std::sort(m_tools.begin(), m_tools.end(),
              [](const ToolData &a, const ToolData &b){ return a.T < b.T; });
}

void ToolDatabase::loadFromCentroid()
{
#ifdef WITH_CENTROID_API
    for (int T = 1; T <= MAX_TOOLS; ++T) {
        ToolData d;
        d.T = T;

        char buf[256] = {};
        if (CNC_Tool_GetToolDescriptionByIndex(T - 1, buf, sizeof(buf)) == 0)
            d.description = QString::fromLocal8Bit(buf).trimmed();

        double heightOff = 0, diaOff = 0;
        CNC_Tool_GetHeightOffsetAmountByH(T, &heightOff);
        CNC_Tool_GetDiameterOffsetAmountByD(T, &diaOff);

        // Garde-fou : certains index outils renvoient des offsets aberrants
        // (mémoire non initialisée côté API). On rejette toute valeur hors
        // d'une plage physique plausible — sinon Ø/Long délirants (1e8 mm).
        auto plausible = [](double v, double maxAbs) {
            return std::isfinite(v) && std::fabs(v) <= maxAbs;
        };
        if (!plausible(heightOff, 2000.0)) heightOff = 0.0; // longueur max ~2 m
        if (!plausible(diaOff,    500.0))  diaOff    = 0.0; // Ø max 500 mm

        // Centroid stocke le DIAMÈTRE → on convertit en rayon
        d.Long   = heightOff;
        d.Routil = diaOff / 2.0;

        if (d.description.isEmpty() && d.Routil <= 0) continue;

        int hNum = T, dNum = T;
        CNC_Tool_GetToolHNumberByT(T, &hNum);
        CNC_Tool_GetToolDNumberByT(T, &dNum);
        d.HNumber = hNum; d.DNumber = dNum;

        int speed = 0; CNC_Tool_GetToolSpindleSpeedByT(T, &speed); d.spindleSpeed = speed;
        int dir   = 0; CNC_Tool_GetToolSpindleDirectionByT(T, &dir); d.spindleDir = dir;
        int cool  = 0; CNC_Tool_GetCoolantByT(T, &cool); d.coolant = cool;
        int bin   = 0; CNC_Tool_GetToolBinByT(T, &bin); d.bin = bin;

        m_tools << d;
    }
#endif
}

void ToolDatabase::loadFromJson()
{
    QFile f(m_jsonPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return;
    mergeJson(doc.object());
}

void ToolDatabase::mergeJson(const QJsonObject &root)
{
    QJsonArray arr = root["tools"].toArray();
    for (const QJsonValue &v : arr) {
        QJsonObject o = v.toObject();
        int T = o["T"].toInt();
        if (T < 1) continue;

        ToolData *td = toolByT(T);
        if (!td) {
            ToolData d = ToolData::fromJson(o);
            // Migration ancien format : DL/DR contenaient longueur/rayon Centroid
            // Si Long=0 et DL ressemble à une longueur outil (>5mm) → migrer vers Long
            if (d.Long <= 0 && d.DL > 5.0) {
                d.Long = d.DL;
                d.DL   = 0.0;
            }
            // Si Routil=0 et DR ressemble à un rayon outil (>0.5mm) → migrer vers Routil
            if (d.Routil <= 0 && d.DR > 0.5) {
                d.Routil = d.DR;
                d.DR     = 0.0;
            }
            m_tools << d;
            continue;
        }
        // Fusionner champs EazyNC (ne pas écraser offsets Centroid)
        td->toolType    = (ToolData::ToolType)o["toolType"].toInt((int)td->toolType);
        // Garde-fou : un Routil/Long lu depuis le JSON peut être corrompu
        // (anciennes sessions, mémoire Centroid non initialisée → 1e+26).
        // On ne ré-applique que des valeurs physiquement plausibles.
        auto plausible = [](double v, double maxAbs) {
            return std::isfinite(v) && v > 0 && v <= maxAbs;
        };
        if (plausible(o["Routil"].toDouble(), 500.0))
            td->Routil = o["Routil"].toDouble();
        // Long : prendre la valeur Centroid (chargée via loadFromCentroid) en priorité
        // Ne pas écraser Long depuis JSON si Centroid l'a déjà renseigné
        if (td->Long <= 0 && plausible(o["Long"].toDouble(), 2000.0))
            td->Long = o["Long"].toDouble();
        // Champs EazyNC : c'est l'utilisateur qui les saisit dans le
        // formulaire, donc le JSON fait FOI et écrase toujours (même 0).
        // Ne pas conditionner à "> 0" : sinon, effacer un champ (le mettre
        // à 0) ne serait jamais persisté et l'ancienne valeur reviendrait.
        td->Lu          = o["Lu"].toDouble(td->Lu);
        td->Lcoupe      = o["Lcoupe"].toDouble(td->Lcoupe);
        td->LsousPince  = o["LsousPince"].toDouble(td->LsousPince);
        td->Routil2     = o["Routil2"].toDouble(td->Routil2);
        td->REpaulement = o["REpaulement"].toDouble(td->REpaulement);
        td->rayonQueue  = o["rayonQueue"].toDouble(td->rayonQueue);
        td->rayonGorge  = o["rayonGorge"].toDouble(td->rayonGorge);
        td->AnglePointe = o["AnglePointe"].toDouble(td->AnglePointe);
        td->rayonPointe = o["rayonPointe"].toDouble(td->rayonPointe);
        td->nbDents     = o["nbDents"].toInt(td->nbDents);
        td->angleRampe  = o["angleRampe"].toDouble(td->angleRampe);
        td->pitch       = o["pitch"].toDouble(td->pitch);
        // DL/DR : correcteurs delta — seulement si petites valeurs (< 5mm)
        // Les grandes valeurs dans l'ancien JSON = longueur/rayon outil mal placés → ignorer
        double jsonDL = o["DL"].toDouble(0.0);
        double jsonDR = o["DR"].toDouble(0.0);
        if (qAbs(jsonDL) < 5.0) td->DL = jsonDL;  // correcteur delta longueur
        if (qAbs(jsonDR) < 5.0) td->DR = jsonDR;  // correcteur delta rayon
        // Nom : le JSON (saisi par l'utilisateur) fait foi s'il est présent.
        if (o.contains("description")) {
            QString jsonNom = o["description"].toString();
            if (!jsonNom.isEmpty() || td->description.isEmpty())
                td->description = jsonNom;
        }
    }
}

void ToolDatabase::save()
{
    sortByT();
    QJsonArray arr;
    for (const ToolData &d : m_tools)
        if (!d.isEmpty()) arr << d.toJson();
    QJsonObject root;
    root["tools"] = arr;

    // Diagnostic : si le chemin est vide, on le signale clairement
    // (sinon l'écriture échoue silencieusement et les modifs sont perdues).
    if (m_jsonPath.isEmpty()) {
        qWarning() << "[ToolDatabase::save] ERREUR : m_jsonPath VIDE — "
                      "rien n'est sauvegardé ! init() n'a pas été appelé ?";
        return;
    }
    QFile f(m_jsonPath);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "[ToolDatabase::save] ERREUR : impossible d'écrire"
                   << m_jsonPath << ":" << f.errorString();
        return;
    }
    qint64 n = f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    qInfo() << "[ToolDatabase::save] OK :" << arr.size() << "outils écrits dans"
            << m_jsonPath << "(" << n << "octets )";
}

ToolData* ToolDatabase::toolByT(int T)
{
    for (ToolData &d : m_tools) if (d.T == T) return &d;
    return nullptr;
}
ToolData* ToolDatabase::ensureTool(int T)
{
    if (ToolData *d = toolByT(T)) return d;
    ToolData n; n.T = T;
    m_tools.append(n);
    sortByT();
    return toolByT(T);
}
const ToolData* ToolDatabase::toolByT(int T) const
{
    for (const ToolData &d : m_tools) if (d.T == T) return &d;
    return nullptr;
}

bool ToolDatabase::writeToolToCentroid(const ToolData &tool)
{
#ifdef WITH_CENTROID_API
    int T = tool.T;
    bool ok = true;
    QByteArray desc = tool.description.toLocal8Bit();
    ok &= (CNC_Tool_SetToolDescription(T, desc.constData()) == 0);
    ok &= (CNC_Tool_SetToolHeightOffsetAmount(T, tool.Long) == 0);
    // EazyNC rayon → Centroid diamètre
    ok &= (CNC_Tool_SetToolDiameterOffsetAmount(T, tool.Routil * 2.0) == 0);
    ok &= (CNC_Tool_SetToolHNumber(T, tool.HNumber) == 0);
    ok &= (CNC_Tool_SetToolDNumber(T, tool.DNumber) == 0);
    ok &= (CNC_Tool_SetSpindleSpeed(T, (long long)tool.spindleSpeed) == 0);
    ok &= (CNC_Tool_SetSpindleDirection(T, tool.spindleDir) == 0);
    ok &= (CNC_Tool_SetCoolant(T, tool.coolant) == 0);
    ok &= (CNC_Tool_SetBinNumber(T, tool.bin) == 0);
    return ok;
#else
    Q_UNUSED(tool); return false;
#endif
}
