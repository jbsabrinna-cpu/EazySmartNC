#include <algorithm>
#pragma execution_character_set("utf-8")
#include "programmepage.h"
#include <QRadioButton>
#include <QButtonGroup>
#include "pointfile.h"
#include "pointfileeditordialog.h"
#include "pntfilebrowserdialog.h"
#include "cadviewwidget.h"
#include "cadimporter.h"
#include "contoureditor.h"
#include "pocherectgenerator.h"   // architecture Strategy : cycle poche rect
#include "pochecircgenerator.h"   // architecture Strategy : cycle poche circ
#include "contourgenerator.h"
#include "pochecontourgenerator.h"     // architecture Strategy : cycle contournage
#include "percagegenerator.h"     // architecture Strategy : cycle percage
#include "helpanimwidget.h"         // dessins d'aide animés
#include "fraisagetrougenerator.h" // architecture Strategy : cycle fraisage de trou

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QTabWidget>
#include <QComboBox>
#include <QLabel>
#include <QFrame>
#include <QScrollArea>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QFileDialog>
#include <QDir>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QJsonArray>
#include <QSignalBlocker>
#include <QDebug>
#include <QThreadPool>
#include <QPainter>
#include <QApplication>
#include <QDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPixmap>
#include <QSvgRenderer>
#include <QMutex>
#include <QAtomicInt>
#include "crashhandler.h"

// ═══════════════════════════════════════════════════════════════
//  Sérialisation JSON PocheData / TrouData
// ═══════════════════════════════════════════════════════════════
static QJsonObject pocheToJson(const PocheData &d)
{
    QJsonObject o;
    o["toolRef"]        = d.toolRef;
    o["toolByName"]     = d.toolByName;
    o["spindleVal"]     = d.spindleVal;
    o["spindleByVc"]    = d.spindleByVc;
    o["descenteVal"]    = d.descenteVal;
    o["descenteMode"]   = d.descenteMode;
    o["pendulVal"]      = d.pendulVal;
    o["pendulMode"]     = d.pendulMode;
    o["avanceVal"]      = d.avanceVal;
    o["avanceMode"]     = d.avanceMode;
    o["fonctionM1"]     = d.fonctionM1;
    o["fonctionM2"]     = d.fonctionM2;
    o["DL"]             = d.DL;
    o["DR"]             = d.DR;
    o["Routil2"]            = d.Routil2;
    o["brocheCW"]       = d.brocheCW;
    o["isCirculaire"]   = d.isCirculaire;
    o["longueur"]       = d.longueur;
    o["largeur"]        = d.largeur;
    o["rayonAngle"]     = d.rayonAngle;
    o["diametre"]       = d.diametre;
    o["profondeur"]     = d.profondeur;
    o["profPasse"]      = d.profPasse;
    o["surepLat"]       = d.surepLat;
    o["surepProf"]      = d.surepProf;
    o["passeFinition"]  = d.passeFinition;
    o["surepFinLat"]    = d.surepFinLat;
    o["surepFinProf"]   = d.surepFinProf;
    o["avanceFin"]      = d.avanceFinition;
    o["rayonEntreeSortie"] = d.rayonEntreeSortie;
    o["avanceFinMode"]  = d.avanceFinMode;
    o["posAngulaire"]   = d.posAngulaire;
    o["positionPoche"]  = d.positionPoche;
    o["fichierPoints"]  = d.fichierPoints;
    o["approche"]       = d.approche;
    o["sautBride"]      = d.sautBride;
    o["zLiaison"]       = d.zLiaison;
    o["avanceLiaison"]  = d.avanceLiaison;
    o["origine"]        = d.origine;
    o["origineDefinie"] = d.origineDefinie;
    o["approcheLocal"]      = d.approcheLocal;
    o["sautBrideLocal"]     = d.sautBrideLocal;
    o["zLiaisonLocal"]      = d.zLiaisonLocal;
    o["avanceLiaisonLocal"] = d.avanceLiaisonLocal;
    o["origineLocal"]       = d.origineLocal;
    o["recouvrement"]   = d.recouvrement;
    o["posG0"]          = d.positionnementG0;
    o["positionnementF"]= d.positionnementF;
    o["modeFraisage"]   = d.modeFraisage;
    o["modePlongee"]    = d.modePlongee;
    o["modeUsin"]       = d.modeUsin;
    // ── Position ─────────────────────────────────────────
    o["positionMode"]   = d.positionMode;
    o["posX"]           = d.posX;
    o["posY"]           = d.posY;
    o["ligneP1X"]       = d.ligneP1X;
    o["ligneP1Y"]       = d.ligneP1Y;
    o["ligneP2X"]       = d.ligneP2X;   // Z départ
    o["ligneP2Y"]       = d.ligneP2Y;   // Pas
    o["ligneNbPts"]     = d.ligneNbPts;
    o["grilleOX"]       = d.grilleOX;
    o["grilleOY"]       = d.grilleOY;
    o["grilleOZ"]       = d.grilleOZ;
    o["grillePasX"]     = d.grillePasX;
    o["grillePasY"]     = d.grillePasY;
    o["grilleNbX"]      = d.grilleNbX;
    o["grilleNbY"]      = d.grilleNbY;
    o["cercleCX"]       = d.cercleCX;
    o["cercleCY"]       = d.cercleCY;
    o["cercleRayon"]    = d.cercleRayon;
    o["cercleZ"]        = d.cercleZ;
    o["cercleNbPts"]    = d.cercleNbPts;
    o["cercleAngDep"]   = d.cercleAngDep;
    o["cercleAngInc"]   = d.cercleAngInc;
    // Tableau de points X/Y/Z/Angle
    QJsonArray ptArr;
    for (const PocheData::TableauPoint &p : d.tableauPts)
        ptArr.append(QJsonObject{{"x",p.x},{"y",p.y},{"z",p.z}});
    o["tableauPts"]     = ptArr;
    return o;
}

static PocheData pocheFromJson(const QJsonObject &o)
{
    PocheData d;
    d.toolRef          = o["toolRef"].toString();
    d.toolByName       = o["toolByName"].toBool();
    d.spindleVal       = o["spindleVal"].toDouble();
    d.spindleByVc      = o["spindleByVc"].toBool();
    d.descenteVal      = o["descenteVal"].toDouble();
    d.descenteMode     = o["descenteMode"].toInt();
    d.pendulVal        = o["pendulVal"].toDouble();
    d.pendulMode       = o["pendulMode"].toInt();
    d.avanceVal        = o["avanceVal"].toDouble();
    d.avanceMode       = o["avanceMode"].toInt();
    d.fonctionM1       = o["fonctionM1"].toString();
    d.fonctionM2       = o["fonctionM2"].toString();
    d.DL               = o["DL"].toDouble();
    d.DR               = o["DR"].toDouble();
    d.Routil2              = o.contains("Routil2") ? o["Routil2"].toDouble() : o["DR2"].toDouble();  // rétro-compat
    d.brocheCW         = o["brocheCW"].toBool(true);
    d.isCirculaire     = o["isCirculaire"].toBool();
    d.longueur         = o["longueur"].toDouble();
    d.largeur          = o["largeur"].toDouble();
    d.rayonAngle       = o["rayonAngle"].toDouble();
    d.diametre         = o["diametre"].toDouble();
    d.profondeur       = o["profondeur"].toDouble();
    d.profPasse        = o["profPasse"].toDouble(2.0);
    d.surepLat         = o["surepLat"].toDouble();
    d.surepProf        = o["surepProf"].toDouble();
    d.passeFinition    = o["passeFinition"].toDouble();
    d.surepFinLat      = o["surepFinLat"].toDouble(0.0);
    d.surepFinProf     = o["surepFinProf"].toDouble(0.0);
    d.avanceFinition   = o["avanceFin"].toDouble();
    d.rayonEntreeSortie = o["rayonEntreeSortie"].toDouble();
    d.avanceFinMode    = o["avanceFinMode"].toInt();
    d.posAngulaire     = o["posAngulaire"].toDouble();
    d.positionPoche    = o["positionPoche"].toInt(5);
    d.fichierPoints    = o["fichierPoints"].toString();
    d.approche         = o["approche"].toDouble(2.0);
    d.sautBride        = o["sautBride"].toDouble(50.0);
    d.zLiaison         = o["zLiaison"].toDouble(5.0);
    d.avanceLiaison    = o["avanceLiaison"].toDouble(0.0);
    d.origine          = o["origine"].toInt(0);
    d.origineDefinie   = o["origineDefinie"].toBool(false);
    // Drapeaux "valeur locale" (true = modifié, ignore BLK FORM)
    d.approcheLocal      = o["approcheLocal"].toBool(false);
    d.sautBrideLocal     = o["sautBrideLocal"].toBool(false);
    d.zLiaisonLocal      = o["zLiaisonLocal"].toBool(false);
    d.avanceLiaisonLocal = o["avanceLiaisonLocal"].toBool(false);
    d.origineLocal       = o["origineLocal"].toBool(false);
    d.recouvrement     = o["recouvrement"].toInt(75);
    d.positionnementG0 = o["posG0"].toBool(true);
    d.positionnementF  = o["positionnementF"].toDouble();
    d.modeFraisage     = o["modeFraisage"].toInt();
    d.modePlongee      = o["modePlongee"].toInt();
    d.modeUsin         = o["modeUsin"].toInt(0);   // 0=ébauche+fin, 1=ébauche seule, 2=fin seule
    // ── Position ─────────────────────────────────────────
    // défaut 0 (point unique) pour compatibilité vieux .enc sans cette clé
    d.positionMode     = o.contains("positionMode") ? o["positionMode"].toInt() : 0;
    d.posX             = o["posX"].toDouble();
    d.posY             = o["posY"].toDouble();
    d.ligneP1X         = o["ligneP1X"].toDouble();
    d.ligneP1Y         = o["ligneP1Y"].toDouble();
    d.ligneP2X         = o["ligneP2X"].toDouble();
    d.ligneP2Y         = o["ligneP2Y"].toDouble();
    d.ligneNbPts       = o["ligneNbPts"].toInt(1);
    d.grilleOX         = o["grilleOX"].toDouble();
    d.grilleOY         = o["grilleOY"].toDouble();
    d.grilleOZ         = o["grilleOZ"].toDouble();
    d.grillePasX       = o["grillePasX"].toDouble();
    d.grillePasY       = o["grillePasY"].toDouble();
    d.grilleNbX        = o["grilleNbX"].toInt(1);
    d.grilleNbY        = o["grilleNbY"].toInt(1);
    d.cercleCX         = o["cercleCX"].toDouble();
    d.cercleCY         = o["cercleCY"].toDouble();
    d.cercleRayon      = o["cercleRayon"].toDouble();
    d.cercleZ          = o["cercleZ"].toDouble();
    d.cercleNbPts      = o["cercleNbPts"].toInt(4);
    d.cercleAngDep     = o["cercleAngDep"].toDouble();
    d.cercleAngInc     = o["cercleAngInc"].toDouble(90.0);
    // Tableau de points X/Y/Z/Angle
    d.tableauPts.clear();
    for (const QJsonValue &v : o["tableauPts"].toArray()) {
        QJsonObject p = v.toObject();
        PocheData::TableauPoint tp;
        tp.x = p["x"].toDouble(); tp.y = p["y"].toDouble(); tp.z = p["z"].toDouble();
        d.tableauPts << tp;
    }
    if (d.tableauPts.isEmpty())
        d.tableauPts = QVector<PocheData::TableauPoint>(10);
    return d;
}

static QJsonObject trouToJson(const TrouData &d)
{
    QJsonObject o;
    o["toolRef"]        = d.toolRef;
    o["toolByName"]     = d.toolByName;
    o["spindleVal"]     = d.spindleVal;
    o["spindleByVc"]    = d.spindleByVc;
    o["descenteVal"]    = d.descenteVal;
    o["descenteMode"]   = d.descenteMode;
    o["fonctionM1"]     = d.fonctionM1;
    o["fonctionM2"]     = d.fonctionM2;
    o["DL"]             = d.DL;
    o["DR"]             = d.DR;
    o["brocheCW"]       = d.brocheCW;
    o["diametre"]       = d.diametre;
    o["profondeur"]     = d.profondeur;
    o["traversant"]     = d.traversant;
    o["cycle"]          = d.cycle;
    o["degagement"]     = d.degagement;
    o["profPasse"]           = d.profPasse;
    o["reductionPasse"]      = d.reductionPasse;
    o["passeMin"]            = d.passeMin;
    o["profBriseCopeaux"]    = d.profBriseCopeaux;
    o["retraitBriseCopeaux"] = d.retraitBriseCopeaux;
    o["avancePlongee"]       = d.avancePlongee;
    o["avancePrepos"]        = d.avancePrepos;
    o["avanceRetrait"]       = d.avanceRetrait;
    o["tempoFond"]           = d.tempoFond;
    o["approche"]       = d.approche;
    o["sautBride"]      = d.sautBride;
    o["zLiaison"]       = d.zLiaison;
    o["avanceLiaison"]  = d.avanceLiaison;
    o["origine"]        = d.origine;
    o["origineDefinie"] = d.origineDefinie;
    o["approcheLocal"]      = d.approcheLocal;
    o["sautBrideLocal"]     = d.sautBrideLocal;
    o["zLiaisonLocal"]      = d.zLiaisonLocal;
    o["avanceLiaisonLocal"] = d.avanceLiaisonLocal;
    o["origineLocal"]       = d.origineLocal;
    o["posG0"]          = d.positionnementG0;
    o["positionnementF"]= d.positionnementF;
    o["fichierPoints"]  = d.fichierPoints;
    // ── Positions ──────────────────────────────────────────
    o["positionMode"]   = d.positionMode;
    o["ligneP1X"]       = d.ligneP1X;   o["ligneP1Y"] = d.ligneP1Y;
    o["ligneP2X"]       = d.ligneP2X;   o["ligneP2Y"] = d.ligneP2Y;
    o["ligneNbPts"]     = d.ligneNbPts;
    o["grilleOX"]       = d.grilleOX;   o["grilleOY"]  = d.grilleOY;
    o["grillePasX"]     = d.grillePasX; o["grillePasY"] = d.grillePasY;
    o["grilleNbX"]      = d.grilleNbX;  o["grilleNbY"]  = d.grilleNbY;
    o["cercleCX"]       = d.cercleCX;   o["cercleCY"]   = d.cercleCY;
    o["cercleRayon"]    = d.cercleRayon;
    o["cercleNbPts"]    = d.cercleNbPts;
    o["cercleAngDep"]   = d.cercleAngDep;
    o["cercleAngFin"]   = d.cercleAngFin;
    return o;
}

static TrouData trouFromJson(const QJsonObject &o)
{
    TrouData d;
    d.toolRef          = o["toolRef"].toString();
    d.toolByName       = o["toolByName"].toBool();
    d.spindleVal       = o["spindleVal"].toDouble();
    d.spindleByVc      = o["spindleByVc"].toBool();
    d.descenteVal      = o["descenteVal"].toDouble();
    d.descenteMode     = o["descenteMode"].toInt();
    d.fonctionM1       = o["fonctionM1"].toString();
    d.fonctionM2       = o["fonctionM2"].toString();
    d.DL               = o["DL"].toDouble();
    d.DR               = o["DR"].toDouble();
    d.brocheCW         = o["brocheCW"].toBool(true);
    d.diametre         = o["diametre"].toDouble(10.0);
    d.profondeur       = o["profondeur"].toDouble(20.0);
    d.traversant       = o["traversant"].toBool();
    d.cycle            = o["cycle"].toInt();
    d.degagement       = o["degagement"].toDouble(3.0);
    d.profPasse           = o["profPasse"].toDouble(0.0);
    d.reductionPasse      = o["reductionPasse"].toDouble(0.0);
    d.passeMin            = o["passeMin"].toDouble(0.0);
    d.profBriseCopeaux    = o["profBriseCopeaux"].toDouble(0.0);
    d.retraitBriseCopeaux = o["retraitBriseCopeaux"].toDouble(0.2);
    d.avancePlongee       = o["avancePlongee"].toDouble(0.0);
    d.avancePrepos        = o["avancePrepos"].toDouble(0.0);
    d.avanceRetrait       = o["avanceRetrait"].toDouble(0.0);
    d.tempoFond           = o["tempoFond"].toDouble(0.0);
    d.approche         = o["approche"].toDouble(2.0);
    d.sautBride        = o["sautBride"].toDouble(50.0);
    d.zLiaison         = o["zLiaison"].toDouble(5.0);
    d.avanceLiaison    = o["avanceLiaison"].toDouble(0.0);
    d.origine          = o["origine"].toInt(0);
    d.origineDefinie   = o["origineDefinie"].toBool(false);
    d.approcheLocal      = o["approcheLocal"].toBool(false);
    d.sautBrideLocal     = o["sautBrideLocal"].toBool(false);
    d.zLiaisonLocal      = o["zLiaisonLocal"].toBool(false);
    d.avanceLiaisonLocal = o["avanceLiaisonLocal"].toBool(false);
    d.origineLocal       = o["origineLocal"].toBool(false);
    d.positionnementG0 = o["posG0"].toBool(true);
    d.positionnementF  = o["positionnementF"].toDouble();
    d.fichierPoints    = o["fichierPoints"].toString();
    // ── Positions ──────────────────────────────────────────
    d.positionMode  = o["positionMode"].toInt(0);
    d.ligneP1X      = o["ligneP1X"].toDouble();   d.ligneP1Y  = o["ligneP1Y"].toDouble();
    d.ligneP2X      = o["ligneP2X"].toDouble();   d.ligneP2Y  = o["ligneP2Y"].toDouble();
    d.ligneNbPts    = o["ligneNbPts"].toInt(2);
    d.grilleOX      = o["grilleOX"].toDouble();   d.grilleOY  = o["grilleOY"].toDouble();
    d.grillePasX    = o["grillePasX"].toDouble();  d.grillePasY= o["grillePasY"].toDouble();
    d.grilleNbX     = o["grilleNbX"].toInt(1);    d.grilleNbY = o["grilleNbY"].toInt(1);
    d.cercleCX      = o["cercleCX"].toDouble();   d.cercleCY  = o["cercleCY"].toDouble();
    d.cercleRayon   = o["cercleRayon"].toDouble();
    d.cercleNbPts   = o["cercleNbPts"].toInt(4);
    d.cercleAngDep  = o["cercleAngDep"].toDouble();
    d.cercleAngFin  = o["cercleAngFin"].toDouble(360.0);
    return d;
}

// ═══════════════════════════════════════════════════════════════
//  Sérialisation JSON ContourData
// ═══════════════════════════════════════════════════════════════
static QJsonObject contourToJson(const ContourData &d)
{
    QJsonObject o;
    o["toolRef"]        = d.toolRef;
    o["toolByName"]     = d.toolByName;
    o["spindleVal"]     = d.spindleVal;
    o["spindleByVc"]    = d.spindleByVc;
    o["descenteVal"]    = d.descenteVal;
    o["descenteMode"]   = d.descenteMode;
    o["pendulVal"]      = d.pendulVal;
    o["pendulMode"]     = d.pendulMode;
    o["avanceVal"]      = d.avanceVal;
    o["avanceMode"]     = d.avanceMode;
    o["avanceFinition"] = d.avanceFinition;
    o["avanceFinMode"]  = d.avanceFinMode;
    o["fonctionM1"]     = d.fonctionM1;
    o["fonctionM2"]     = d.fonctionM2;
    o["DL"]             = d.DL;
    o["DR"]             = d.DR;
    o["Routil2"]        = d.Routil2;
    o["brocheCW"]       = d.brocheCW;
    o["modeUsin"]       = d.modeUsin;
    o["modeContour"]    = d.modeContour;
    o["profondeur"]     = d.profondeur;
    o["profPasse"]      = d.profPasse;
    o["surepLat"]       = d.surepLat;
    o["surepProf"]      = d.surepProf;
    o["passeFinition"]  = d.passeFinition;
    o["modeFraisage"]   = d.modeFraisage;
    o["modePlongee"]    = d.modePlongee;
    o["approche"]       = d.approche;
    o["sautBride"]      = d.sautBride;
    o["zLiaison"]       = d.zLiaison;
    o["avanceLiaison"]  = d.avanceLiaison;
    o["origine"]        = d.origine;
    o["origineDefinie"] = d.origineDefinie;
    o["approcheLocal"]      = d.approcheLocal;
    o["sautBrideLocal"]     = d.sautBrideLocal;
    o["zLiaisonLocal"]      = d.zLiaisonLocal;
    o["avanceLiaisonLocal"] = d.avanceLiaisonLocal;
    o["origineLocal"]       = d.origineLocal;
    o["correctionFraise"] = d.correctionFraise;
    o["positionOutil"]    = d.positionOutil;
    // ── Nouveaux champs : plongée + entrée/sortie matière ────
    o["plongeeRapide"]  = d.plongeeRapide;
    o["typeEntree"]     = d.typeEntree;
    o["paramEntree"]    = d.paramEntree;
    o["typeSortie"]     = d.typeSortie;
    o["paramSortie"]    = d.paramSortie;
    o["contourName"]    = d.contourName;
    o["contourFile"]    = d.contourFile;
    o["origineX"]       = d.origineX;
    o["origineY"]       = d.origineY;
    o["origineZ"]       = d.origineZ;
    // ── Mode usinage + stratégies ébauche/finition ───────────
    o["stepOverPct"]         = d.stepOverPct;
    o["strategieFinition"]   = d.strategieFinition;
    o["nbPassesFinition"]    = d.nbPassesFinition;
    o["toolRefEbauche"]      = d.toolRefEbauche;
    o["toolRefEbaucheByName"]= d.toolRefEbaucheByName;
    // ── Poche de contour ─────────────────────────────────────
    o["typeCycle"]     = d.typeCycle;
    {
        QJsonArray arr;
        for (const PocEntry &e : d.pocList) {
            QJsonObject j;
            j["file"] = e.file; j["ilot"] = e.ilot;
            j["profondeur"] = e.profondeur; j["zDepart"] = e.zDepart;
            arr.append(j);
        }
        o["pocList"] = arr;
    }
    o["pocPlongee"]    = d.pocPlongee;
    o["entreeDefinie"] = d.entreeDefinie;
    o["entreeX"]       = d.entreeX;
    o["entreeY"]       = d.entreeY;
    return o;
}

static ContourData contourFromJson(const QJsonObject &o)
{
    ContourData d;
    d.toolRef       = o["toolRef"].toString();
    d.toolByName    = o["toolByName"].toBool();
    d.spindleVal    = o["spindleVal"].toDouble();
    d.spindleByVc   = o["spindleByVc"].toBool();
    d.descenteVal   = o["descenteVal"].toDouble();
    d.descenteMode  = o["descenteMode"].toInt();
    d.pendulVal     = o["pendulVal"].toDouble();
    d.pendulMode    = o["pendulMode"].toInt();
    d.avanceVal     = o["avanceVal"].toDouble();
    d.avanceMode    = o["avanceMode"].toInt();
    d.avanceFinition= o["avanceFinition"].toDouble();
    d.avanceFinMode = o["avanceFinMode"].toInt();
    d.fonctionM1    = o["fonctionM1"].toString();
    d.fonctionM2    = o["fonctionM2"].toString();
    d.DL            = o["DL"].toDouble();
    d.DR            = o["DR"].toDouble();
    d.Routil2       = o["Routil2"].toDouble();
    d.brocheCW      = o["brocheCW"].toBool(true);
    d.modeUsin      = o["modeUsin"].toInt(0);
    d.modeContour   = o["modeContour"].toInt(0);
    d.profondeur    = o["profondeur"].toDouble(10.0);
    d.profPasse     = o["profPasse"].toDouble(3.0);
    d.surepLat      = o["surepLat"].toDouble(0.2);
    d.surepProf     = o["surepProf"].toDouble(0.1);
    d.passeFinition = o["passeFinition"].toDouble();
    d.modeFraisage  = o["modeFraisage"].toInt(0);
    d.modePlongee   = o["modePlongee"].toInt(0);
    d.approche      = o["approche"].toDouble(2.0);
    d.sautBride     = o["sautBride"].toDouble(50.0);
    d.zLiaison      = o["zLiaison"].toDouble(5.0);
    d.avanceLiaison = o["avanceLiaison"].toDouble(0.0);
    d.origine       = o["origine"].toInt(0);
    d.origineDefinie = o["origineDefinie"].toBool(false);
    d.approcheLocal      = o["approcheLocal"].toBool(false);
    d.sautBrideLocal     = o["sautBrideLocal"].toBool(false);
    d.zLiaisonLocal      = o["zLiaisonLocal"].toBool(false);
    d.avanceLiaisonLocal = o["avanceLiaisonLocal"].toBool(false);
    d.origineLocal       = o["origineLocal"].toBool(false);
    d.correctionFraise = o["correctionFraise"].toInt(40);
    d.positionOutil    = o["positionOutil"].toInt(0);
    // ── Nouveaux champs : plongée + entrée/sortie matière ────
    d.plongeeRapide = o["plongeeRapide"].toBool(true);
    d.typeEntree    = o["typeEntree"].toInt(0);
    d.paramEntree   = o["paramEntree"].toDouble(5.0);
    d.typeSortie    = o["typeSortie"].toInt(0);
    d.paramSortie   = o["paramSortie"].toDouble(5.0);
    d.contourName   = o["contourName"].toString();
    d.contourFile   = o["contourFile"].toString();
    d.origineX      = o["origineX"].toDouble();
    d.origineY      = o["origineY"].toDouble();
    d.origineZ      = o["origineZ"].toDouble();
    // ── Mode usinage + stratégies ébauche/finition ───────────
    d.stepOverPct         = o["stepOverPct"].toInt(60);
    d.strategieFinition   = o["strategieFinition"].toInt(0);
    d.nbPassesFinition    = o["nbPassesFinition"].toInt(1);
    d.toolRefEbauche      = o["toolRefEbauche"].toString();
    d.toolRefEbaucheByName= o["toolRefEbaucheByName"].toBool(false);
    // ── Poche de contour ─────────────────────────────────────
    d.typeCycle     = o["typeCycle"].toInt(0);
    for (const auto &v : o["ilotFiles"].toArray()) d.ilotFiles << v.toString();
    for (const auto &v : o["pocList"].toArray()) {
        const QJsonObject j = v.toObject();
        PocEntry e;
        e.file = j["file"].toString(); e.ilot = j["ilot"].toBool();
        e.profondeur = j["profondeur"].toDouble(10.0); e.zDepart = j["zDepart"].toDouble(0.0);
        d.pocList << e;
    }
    // Ancien format (contour + liste d'ilots) -> liste
    if (d.pocList.isEmpty() && d.typeCycle == 1 && !d.contourFile.isEmpty()) {
        PocEntry p; p.file = d.contourFile; p.profondeur = d.profondeur; d.pocList << p;
        for (const QString &f : d.ilotFiles) { PocEntry e; e.file = f; e.ilot = true; e.profondeur = d.profondeur; d.pocList << e; }
    }
    d.pocPlongee    = o["pocPlongee"].toInt(1);
    d.entreeDefinie = o["entreeDefinie"].toBool(false);
    d.entreeX       = o["entreeX"].toDouble();
    d.entreeY       = o["entreeY"].toDouble();
    return d;
}


// ═══════════════════════════════════════════════════════════════
//  Sérialisation JSON FraisageTrouData
// ═══════════════════════════════════════════════════════════════
static QJsonObject fraisageTrouToJson(const FraisageTrouData &d)
{
    QJsonObject o;
    o["toolRef"]=d.toolRef; o["toolByName"]=d.toolByName;
    o["spindleVal"]=d.spindleVal; o["spindleByVc"]=d.spindleByVc;
    o["brocheCW"]=d.brocheCW;
    o["descenteVal"]=d.descenteVal; o["descenteMode"]=d.descenteMode;
    o["avanceVal"]=d.avanceVal; o["avanceMode"]=d.avanceMode;
    o["fonctionM1"]=d.fonctionM1; o["fonctionM2"]=d.fonctionM2;
    o["diametreFini"]=d.diametreFini; o["diametreAvant"]=d.diametreAvant;
    o["profondeur"]=d.profondeur; o["zSurface"]=d.zSurface;
    o["pasHelicoidal"]=d.pasHelicoidal; o["angleRampeMaxi"]=d.angleRampeMaxi;
    o["surepLat"]=d.surepLat; o["passeFinition"]=d.passeFinition;
    o["posX"]=d.posX; o["posY"]=d.posY; o["posZ"]=d.posZ;
    o["positionMode"]=d.positionMode;
    o["fichierPoints"]=d.fichierPoints;
    o["ligneP1X"]=d.ligneP1X; o["ligneP1Y"]=d.ligneP1Y;
    o["ligneP2X"]=d.ligneP2X; o["ligneP2Y"]=d.ligneP2Y;
    o["ligneNbPts"]=d.ligneNbPts;
    o["grilleOX"]=d.grilleOX; o["grilleOY"]=d.grilleOY; o["grilleOZ"]=d.grilleOZ;
    o["grillePasX"]=d.grillePasX; o["grillePasY"]=d.grillePasY;
    o["grilleNbX"]=d.grilleNbX; o["grilleNbY"]=d.grilleNbY;
    o["cercleCX"]=d.cercleCX; o["cercleCY"]=d.cercleCY; o["cercleZ"]=d.cercleZ;
    o["cercleRayon"]=d.cercleRayon; o["cercleNbPts"]=d.cercleNbPts;
    o["cercleAngDep"]=d.cercleAngDep; o["cercleAngInc"]=d.cercleAngInc;
    {
        QJsonArray arr;
        for (const auto &tp : d.tableauPts) {
            QJsonObject po; po["x"]=tp.x; po["y"]=tp.y; po["z"]=tp.z;
            arr.append(po);
        }
        o["tableauPts"] = arr;
    }
    o["approche"]=d.approche; o["sautBride"]=d.sautBride;
    o["zLiaison"]=d.zLiaison; o["avanceLiaison"]=d.avanceLiaison;
    o["approcheLocal"]=d.approcheLocal; o["sautBrideLocal"]=d.sautBrideLocal;
    o["zLiaisonLocal"]=d.zLiaisonLocal; o["avanceLiaisonLocal"]=d.avanceLiaisonLocal;
    o["origine"]=d.origine; o["origineDefinie"]=d.origineDefinie;
    o["origineLocal"]=d.origineLocal;
    o["modeFraisage"]=d.modeFraisage; o["modeFin"]=d.modeFin;
    o["descenteFinDirecte"]=d.descenteFinDirecte;
    return o;
}
static FraisageTrouData fraisageTrouFromJson(const QJsonObject &o)
{
    FraisageTrouData d;
    d.toolRef=o["toolRef"].toString(); d.toolByName=o["toolByName"].toBool();
    d.spindleVal=o["spindleVal"].toDouble(); d.spindleByVc=o["spindleByVc"].toBool();
    d.brocheCW=o["brocheCW"].toBool(true);
    d.descenteVal=o["descenteVal"].toDouble(0.05); d.descenteMode=o["descenteMode"].toInt(2);
    d.avanceVal=o["avanceVal"].toDouble(0.08); d.avanceMode=o["avanceMode"].toInt(2);
    d.fonctionM1=o["fonctionM1"].toString(); d.fonctionM2=o["fonctionM2"].toString();
    d.diametreFini=o["diametreFini"].toDouble(45.0); d.diametreAvant=o["diametreAvant"].toDouble(10.0);
    d.profondeur=o["profondeur"].toDouble(15.0); d.zSurface=o["zSurface"].toDouble();
    d.pasHelicoidal=o["pasHelicoidal"].toDouble(1.5); d.angleRampeMaxi=o["angleRampeMaxi"].toDouble(30.0);
    d.surepLat=o["surepLat"].toDouble(); d.passeFinition=o["passeFinition"].toDouble();
    d.posX=o["posX"].toDouble(); d.posY=o["posY"].toDouble(); d.posZ=o["posZ"].toDouble();
    d.positionMode=o["positionMode"].toInt(0);
    d.fichierPoints=o["fichierPoints"].toString();
    d.ligneP1X=o["ligneP1X"].toDouble(); d.ligneP1Y=o["ligneP1Y"].toDouble();
    d.ligneP2X=o["ligneP2X"].toDouble(); d.ligneP2Y=o["ligneP2Y"].toDouble();
    d.ligneNbPts=o["ligneNbPts"].toInt(1);
    d.grilleOX=o["grilleOX"].toDouble(); d.grilleOY=o["grilleOY"].toDouble(); d.grilleOZ=o["grilleOZ"].toDouble();
    d.grillePasX=o["grillePasX"].toDouble(); d.grillePasY=o["grillePasY"].toDouble();
    d.grilleNbX=o["grilleNbX"].toInt(1); d.grilleNbY=o["grilleNbY"].toInt(1);
    d.cercleCX=o["cercleCX"].toDouble(); d.cercleCY=o["cercleCY"].toDouble(); d.cercleZ=o["cercleZ"].toDouble();
    d.cercleRayon=o["cercleRayon"].toDouble(); d.cercleNbPts=o["cercleNbPts"].toInt(4);
    d.cercleAngDep=o["cercleAngDep"].toDouble(); d.cercleAngInc=o["cercleAngInc"].toDouble(90.0);
    {
        d.tableauPts.clear();
        QJsonArray arr = o["tableauPts"].toArray();
        for (const auto &v : arr) {
            QJsonObject po = v.toObject();
            FraisageTrouData::TableauPoint tp;
            tp.x = po["x"].toDouble(); tp.y = po["y"].toDouble(); tp.z = po["z"].toDouble();
            d.tableauPts << tp;
        }
        if (d.tableauPts.size() < 10) d.tableauPts.resize(10);
    }
    d.approche=o["approche"].toDouble(2.0); d.sautBride=o["sautBride"].toDouble(50.0);
    d.zLiaison=o["zLiaison"].toDouble(5.0); d.avanceLiaison=o["avanceLiaison"].toDouble();
    d.approcheLocal=o["approcheLocal"].toBool(); d.sautBrideLocal=o["sautBrideLocal"].toBool();
    d.zLiaisonLocal=o["zLiaisonLocal"].toBool(); d.avanceLiaisonLocal=o["avanceLiaisonLocal"].toBool();
    d.origine=o["origine"].toInt(); d.origineDefinie=o["origineDefinie"].toBool();
    d.origineLocal=o["origineLocal"].toBool();
    d.modeFraisage=o["modeFraisage"].toInt(); d.modeFin=o["modeFin"].toInt();
    d.descenteFinDirecte=o["descenteFinDirecte"].toBool(false);
    return d;
}

// ═══════════════════════════════════════════════════════════════
//  Helper interne : construire le G-code depuis m_blocks
// ═══════════════════════════════════════════════════════════════
// ─────────────────────────────────────────────────────────────────
//  clearDirectPositionFields — vide les anciens champs Point/Ligne/
//  Grille/Cercle/Tableau du bloc lui-même. Appelée dès qu'un fichier
//  .pnt est associé, pour éviter que les deux systèmes coexistent
//  (données fantômes visibles dans l'onglet Position alors que le
//  fichier est la seule source réellement utilisée par le générateur).
// ─────────────────────────────────────────────────────────────────
static void clearDirectPositionFields(FraisageTrouData &d)
{
    d.positionMode = 0;
    d.posX = d.posY = d.posZ = 0;
    d.tableauPts.fill(FraisageTrouData::TableauPoint{});
    d.ligneP1X = d.ligneP1Y = d.ligneP2X = d.ligneP2Y = 0;
    d.ligneNbPts = 1;
    d.grilleOX = d.grilleOY = d.grilleOZ = 0;
    d.grillePasX = d.grillePasY = 0;
    d.grilleNbX = d.grilleNbY = 1;
    d.cercleCX = d.cercleCY = d.cercleZ = 0;
    d.cercleRayon = 0;
    d.cercleAngDep = 0; d.cercleAngInc = 90;
    d.cercleNbPts = 4;
}

static QVector<GCodeLine> buildGCode(const QVector<BlockData> &blocks,
                                     const QString &baseName)
{
    GCodeEngine engine;

    // ══════════════════════════════════════════════════════════
    //  Lire les paramètres généraux du BLK FORM (hérités par les blocs)
    //  Le premier bloc BlkForm détermine les valeurs globales
    //  AVANT generateProgram() : m25RetourOrigine doit être connu
    //  pour écrire le bon en-tête (M25 ou G28).
    // ══════════════════════════════════════════════════════════
    bool    gOrigineDefinie = false;
    int     gOrigine        = 0;
    double  gZSautBride     = 50.0;
    double  gZLiaison       = 5.0;
    double  gZApproche      = 2.0;
    double  gAvanceLiaison  = 0.0;
    bool    gM25RetourOrigine = true;
    // ── Dimensions du brut BLK FORM (utilisees par l'ebauche contour) ──
    bool    gBlkValid       = false;
    double  gBlkXmin        = 0, gBlkXmax = 0;
    double  gBlkYmin        = 0, gBlkYmax = 0;
    for (const BlockData &b : std::as_const(blocks)) {
        if (b.type == BlockType::BlkForm && !b.data.isEmpty()) {
            gOrigineDefinie = b.data["origineDefinie"].toBool(false);
            gOrigine        = b.data["origine"].toInt(0);
            gZSautBride     = b.data["zSautBride"].toDouble(50.0);
            gZLiaison       = b.data["zLiaison"].toDouble(5.0);
            gZApproche      = b.data["zApproche"].toDouble(2.0);
            gAvanceLiaison  = b.data["avanceLiaison"].toDouble(0.0);
            gM25RetourOrigine = b.data["m25RetourOrigine"].toBool(true);
            gBlkXmin        = b.data["xMinus"].toDouble(0);
            gBlkXmax        = b.data["xPlus"].toDouble(0);
            gBlkYmin        = b.data["yMinus"].toDouble(0);
            gBlkYmax        = b.data["yPlus"].toDouble(0);
            gBlkValid       = (gBlkXmax > gBlkXmin) && (gBlkYmax > gBlkYmin);
            break;
        }
    }

    QVector<GCodeLine> lines = engine.generateProgram(baseName, gM25RetourOrigine);

    // Écrire la ligne G54-G59 si case cochée
    // (construction directe car line()/blank() sont privés dans GCodeEngine)
    if (gOrigineDefinie) {
        static const char* origines[] = {"G54","G55","G56","G57","G58","G59"};
        GCodeLine gl;
        gl.type    = GCodeLine::Other;
        gl.code    = origines[qBound(0, gOrigine, 5)];
        gl.comment = "Origine piece";
        lines << gl;
        GCodeLine blk;
        blk.type = GCodeLine::Blank;
        lines << blk;
    }

    // Bloc ignoré : on l'écrit dans le G-code au lieu de le sauter en
    // silence (sinon l'opération disparaît sans que personne le voie).
    auto skipNote = [&lines](const BlockData &bk, const QString &why) {
        GCodeLine c;
        c.type    = GCodeLine::Comment;
        c.comment = QString("!! BLOC %1 - %2 IGNORE : %3 !!").arg(bk.id).arg(bk.name.toUpper(), why);
        c.code    = "( " + c.comment + " )";
        lines << c;
    };
    for (const BlockData &b : std::as_const(blocks)) {
        qDebug() << "[buildGCode] bloc id=" << b.id << "name=" << b.name
                 << "type=" << (int)b.type << "data.isEmpty=" << b.data.isEmpty();
        if (b.type == BlockType::BlkForm) {
            if (!b.data.isEmpty()) {
                lines << engine.generateBlkForm(b.data, b.id);
            }
        } else if (b.type == BlockType::Poche
                   || b.type == BlockType::PocheRect
                   || b.type == BlockType::PocheCirc
                   || b.type == BlockType::Rainure
                   || b.type == BlockType::RainureCirc
                   || b.type == BlockType::TenonRect
                   || b.type == BlockType::TenonCirc
                   || b.type == BlockType::Surfacage) {
            if (b.data.isEmpty()) { skipNote(b, "aucune donnee"); continue; }
            PocheData d = pocheFromJson(b.data);
            // Forcer le type selon le bloc
            if (b.type == BlockType::PocheCirc
                || b.type == BlockType::RainureCirc
                || b.type == BlockType::TenonCirc)
                d.isCirculaire = true;
            else if (b.type == BlockType::PocheRect
                     || b.type == BlockType::Rainure
                     || b.type == BlockType::TenonRect
                     || b.type == BlockType::Surfacage)
                d.isCirculaire = false;
            if (d.profondeur <= 0 || d.profPasse <= 0) {
                skipNote(b, "profondeur ou passe nulle"); continue;
            }
            // Héritage BLK FORM : injecter les valeurs globales si non modifiées localement
            if (!d.approcheLocal)      d.approche      = gZApproche;
            if (!d.sautBrideLocal)     d.sautBride     = gZSautBride;
            if (!d.zLiaisonLocal)      d.zLiaison      = gZLiaison;
            if (!d.avanceLiaisonLocal) d.avanceLiaison = gAvanceLiaison;
            // ── Architecture Strategy : un generateur par cycle ──
            //  Poche rectangulaire -> PocheRectGenerator
            //  Poche circulaire    -> PocheCircGenerator
            if (!d.isCirculaire) {
                PocheRectGenerator gen(d);
                lines << gen.generate(b.id);
            } else {
                PocheCircGenerator gen(d);
                lines << gen.generate(b.id);
            }
        } else if (b.type == BlockType::Trou
                   || b.type == BlockType::Percage
                   || b.type == BlockType::PercageDebourr
                   || b.type == BlockType::Pointage
                   || b.type == BlockType::Taraudage
                   || b.type == BlockType::Alesage) {
            if (b.data.isEmpty()) { skipNote(b, "aucune donnee"); continue; }
            TrouData td = trouFromJson(b.data);
            // Héritage BLK FORM : injecter les valeurs globales si non modifiées localement
            if (!td.approcheLocal)      td.approche      = gZApproche;
            if (!td.sautBrideLocal)     td.sautBride     = gZSautBride;
            if (!td.zLiaisonLocal)      td.zLiaison      = gZLiaison;
            if (!td.avanceLiaisonLocal) td.avanceLiaison = gAvanceLiaison;
            qDebug() << "[buildGCode] Trou id=" << b.id
                     << "prof=" << td.profondeur << "diam=" << td.diametre;
            if (td.profondeur <= 0) {
                skipNote(b, "profondeur nulle"); continue;
            }
            PercageData d;
            // ── Outil ──────────────────────────────────────────────────
            d.toolRef          = td.toolRef;
            d.toolByName       = td.toolByName;
            d.spindleVal       = td.spindleVal;
            d.spindleByVc      = td.spindleByVc;
            d.fonctionM1       = td.fonctionM1;
            d.fonctionM2       = td.fonctionM2;
            d.DL               = td.DL;
            d.DR               = td.DR;
            d.brocheCW         = td.brocheCW;
            // ── Avance : descenteVal = avance de perçage ───────────────
            d.avancePerc       = td.descenteVal;
            d.avanceMode       = td.descenteMode;
            // ── Géométrie ──────────────────────────────────────────────
            d.diametreTrou     = td.diametre;    // diamètre du trou à percer
            d.diametreOutil    = td.DR * 2.0;    // diamètre outil depuis correcteur (rayon→diam)
            d.profondeur       = td.profondeur;
            d.traversant       = td.traversant;
            d.cycle            = td.cycle;
            d.degagement       = td.degagement;
            d.profPasse           = td.profPasse;
            d.reductionPasse      = td.reductionPasse;
            d.passeMin            = td.passeMin;
            d.profBriseCopeaux    = td.profBriseCopeaux;
            d.retraitBriseCopeaux = td.retraitBriseCopeaux;
            d.avancePlongee       = td.avancePlongee;
            d.avancePrepos        = td.avancePrepos;
            d.avanceRetrait       = td.avanceRetrait;
            d.tempoFond           = td.tempoFond;
            d.approche         = td.approche;
            d.sautBride        = td.sautBride;
            d.positionnementG0 = td.positionnementG0;
            d.positionnementF  = td.positionnementF;
            // ── Positions ──────────────────────────────────────────────
            d.positionMode     = td.positionMode;
            d.fichierPoints    = td.fichierPoints;
            d.ligneP1X         = td.ligneP1X;    d.ligneP1Y = td.ligneP1Y;
            d.ligneP2X         = td.ligneP2X;    d.ligneP2Y = td.ligneP2Y;
            d.ligneNbPts       = td.ligneNbPts;
            d.grilleOX         = td.grilleOX;    d.grilleOY  = td.grilleOY;
            d.grillePasX       = td.grillePasX;  d.grillePasY= td.grillePasY;
            d.grilleNbX        = td.grilleNbX;   d.grilleNbY = td.grilleNbY;
            d.cercleCX         = td.cercleCX;    d.cercleCY  = td.cercleCY;
            d.cercleRayon      = td.cercleRayon;
            d.cercleNbPts      = td.cercleNbPts;
            d.cercleAngDep     = td.cercleAngDep;
            d.cercleAngFin     = td.cercleAngFin;
            // ── Mode : tous ces blocs sont du perçage au foret ────────
            //  Le champ d.cycle porte le type de cycle :
            //   0 = simple (G81), 1 = brise-copeaux (G73), 2 = débourrage (G83)
            d.mode = PercageData::Foret;
            if (b.type == BlockType::PercageDebourr)
                d.cycle = 2;   // bloc dédié débourrage -> G83
            // ── Architecture Strategy : cycle perçage ──
            PercageGenerator gen(d);
            lines << gen.generate(b.id);
        } else if (b.type == BlockType::FraisageTrou) {
            if (b.data.isEmpty()) { skipNote(b, "aucune donnee"); continue; }
            FraisageTrouData ftd = fraisageTrouFromJson(b.data);
            if (!ftd.approcheLocal)      ftd.approche      = gZApproche;
            if (!ftd.sautBrideLocal)     ftd.sautBride     = gZSautBride;
            if (!ftd.zLiaisonLocal)      ftd.zLiaison      = gZLiaison;
            if (!ftd.avanceLiaisonLocal) ftd.avanceLiaison = gAvanceLiaison;
            if (ftd.profondeur <= 0) { skipNote(b, "profondeur nulle"); continue; }
            FraisageTrouGenerator gen(ftd);
            lines << gen.generate(b.id);
        } else if (b.type == BlockType::Contour || b.type == BlockType::PocheContour) {
            if (b.data.isEmpty()) { skipNote(b, "aucun contour defini"); continue; }
            ContourData cd = contourFromJson(b.data);
            qDebug() << "[buildGCode] Contour id=" << b.id
                     << "name=" << cd.contourName
                     << "file=" << cd.contourFile
                     << "prof=" << cd.profondeur;
            if (cd.contourFile.isEmpty()) {
                skipNote(b, "aucun contour dessine");
                continue;
            }
            // Héritage BLK FORM
            if (!cd.approcheLocal)      cd.approche      = gZApproche;
            if (!cd.sautBrideLocal)     cd.sautBride     = gZSautBride;
            if (!cd.zLiaisonLocal)      cd.zLiaison      = gZLiaison;
            if (!cd.avanceLiaisonLocal) cd.avanceLiaison = gAvanceLiaison;
            // ── Brut BLK FORM (necessaire pour l'ebauche adaptative) ──
            cd.blkValid = gBlkValid;
            cd.blkXmin  = gBlkXmin;
            cd.blkXmax  = gBlkXmax;
            cd.blkYmin  = gBlkYmin;
            cd.blkYmax  = gBlkYmax;
            // ── Architecture Strategy : cycle contournage ──
            if (b.type == BlockType::PocheContour) {
                cd.typeCycle = 1;
                PocheContourGenerator gen(cd);
                lines << gen.generate(b.id);
            } else {
                ContourGenerator gen(cd);
                lines << gen.generate(b.id);
            }
        }
    }
    lines << engine.generateEndProgram(0, gM25RetourOrigine);
    return lines;
}

// ─────────────────────────────────────────────────────────────
//  importCadFile — import DXF/STEP/IGES → ContourEditor
// ─────────────────────────────────────────────────────────────
void ProgrammePage::importCadFile(const QString &cadPath)
{
    QString name = QFileInfo(cadPath).baseName();
    QString ext  = QFileInfo(cadPath).suffix().toLower();

    // ── 1. Charger le fichier CAD ─────────────────────────────
    CadImporter importer;
    CadDocument doc = importer.load(cadPath);

    if (doc.contours.isEmpty()) {
        QMessageBox::warning(this, "Import CAD",
                             QString("Aucun contour trouv\u00e9 dans\n%1").arg(QFileInfo(cadPath).fileName()));
        return;
    }

    // ── 2. Afficher la CadViewWidget pour sélection visuelle ────
    // Fenêtre modale avec la visionneuse DXF
    QDialog dlg(this);
    dlg.setWindowTitle(QString("Sélectionner les contours — %1").arg(QFileInfo(cadPath).fileName()));
    dlg.resize(1000, 700);

    auto *dlgLay = new QVBoxLayout(&dlg);
    dlgLay->setContentsMargins(4,4,4,4);
    dlgLay->setSpacing(4);

    // Label d'instruction
    auto *lblInstr = new QLabel(
        "Cliquez sur les contours à usiner (orange = sélectionné)  —  "
        "Clic droit pour désélectionner");
    lblInstr->setStyleSheet("color:#cccccc;background:#1a1a2e;padding:4px;font-size:10pt;");
    dlgLay->addWidget(lblInstr);

    // Visionneuse DXF
    auto *cadView = new CadViewWidget(&dlg);
    cadView->loadDocument(doc);
    dlgLay->addWidget(cadView, 1);

    // Boutons OK / Annuler
    auto *btnRow = new QHBoxLayout;
    auto *btnOk  = new QPushButton("✓  Valider la sélection");
    auto *btnAll = new QPushButton("Tout sélectionner");
    auto *btnAnn = new QPushButton("Annuler");
    btnOk->setStyleSheet("QPushButton{background:#006600;color:white;font-weight:bold;"
                         "padding:6px 16px;border-radius:4px;}"
                         "QPushButton:hover{background:#008800;}");
    btnRow->addWidget(btnAll);
    btnRow->addStretch();
    btnRow->addWidget(btnAnn);
    btnRow->addWidget(btnOk);
    dlgLay->addLayout(btnRow);

    connect(btnAll, &QPushButton::clicked, cadView, &CadViewWidget::selectAll);
    connect(btnAnn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnOk,  &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted) return;

    QVector<int> selIds = cadView->selectedContourIds();
    if (selIds.isEmpty()) {
        // Si rien sélectionné → prendre tout
        cadView->selectAll();
        selIds = cadView->selectedContourIds();
    }

    // ── 3. Convertir les contours sélectionnés en ContourFile ─
    ContourFile cf;
    cf.name = name;

    // Fonction locale de conversion d'un CadContour
    auto convertContour = [&](const CadContour &cc) {
        for (const CadSegment &seg : std::as_const(cc.segments)) {
            ContourElement e;
            if (seg.type == CadSegment::Line) {
                e.type = ContourElement::L;
                e.x2   = seg.x2;
                e.y2   = seg.y2;
            } else if (seg.type == CadSegment::Arc) {
                e.type   = ContourElement::CR;
                e.x2     = seg.x2;
                e.y2     = seg.y2;
                e.r      = seg.radius;
                e.drPlus = seg.ccw;
            } else if (seg.type == CadSegment::Circle) {
                ContourElement ecc;
                ecc.type = ContourElement::CC;
                ecc.cx = seg.x1; ecc.cy = seg.y1;
                cf.elements << ecc;
                e.type   = ContourElement::CR;
                e.x2     = seg.x1 + seg.radius;
                e.y2     = seg.y1;
                e.r      = seg.radius;
                e.drPlus = true;
            }
            cf.elements << e;
        }
    };

    for (const CadContour &c : std::as_const(doc.contours)) {
        if (selIds.contains(c.id))
            convertContour(c);
    }

    if (cf.elements.isEmpty()) {
        QMessageBox::warning(this, "Import CAD", "Contour vide après conversion.");
        return;
    }

    // ── 4. Sauvegarder en .cnt ────────────────────────────────
    QString dir = m_currentPath.isEmpty()
                      ? QDir::homePath()
                      : QFileInfo(m_currentPath).absolutePath();
    QString cntPath = dir + "/" + name + ".cnt";
    cf.save(cntPath);

    // ── 5. Mémoriser dans le bloc courant (sans déclencher dataChanged) ──
    if (m_currentIndex >= 0 && m_currentIndex < m_blocks.size()
        && (m_blocks[m_currentIndex].type == BlockType::Contour
            || m_blocks[m_currentIndex].type == BlockType::PocheContour)) {
        m_editor->blockSignals(true);
        m_editor->setContourFile(cntPath, name);
        m_editor->blockSignals(false);
        saveCurrentBlock();
    }

    // ── 6. Ouvrir le ContourEditor avec le contour importé ────
    // Déclencher le signal openContourEditor via QMetaObject
    QMetaObject::invokeMethod(m_editor, "openContourEditor",
                              Qt::QueuedConnection,
                              Q_ARG(QString, cntPath), Q_ARG(QString, name));
}

// ─────────────────────────────────────────────────────────────
//  Helper interne : écrire le .nc dans C:/cncm/
// ─────────────────────────────────────────────────────────────
static void writeNcFile(const QVector<GCodeLine> &lines, const QString &encPath)
{
    if (encPath.isEmpty()) return;
    QString dir      = QFileInfo(encPath).absolutePath();
    QString baseName = QFileInfo(encPath).baseName();
    QString ncPath   = dir + "/" + baseName + ".nc";

    QFile nc(ncPath);
    if (nc.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&nc);
        out.setEncoding(QStringConverter::Latin1);
        out << GCodeEngine::toText(lines);
        nc.close();
        qDebug() << "[EazyNC] .nc écrit :" << ncPath;
    } else {
        qDebug() << "[EazyNC] ERREUR écriture .nc :" << nc.errorString();
    }
}

// ─────────────────────────────────────────────────────────────
//  writeContourSubFiles — DEPRECATED depuis refonte inline
//
//  Anciennement : écrivait un sous-fichier .cnc par contour avec
//  la géométrie pure. Maintenant tout est inline dans le NC
//  principal (calculé par Clipper côté EazyNC), donc plus de
//  sous-fichier .cnc à générer.
//
//  La fonction est conservée comme NO-OP pour ne pas casser
//  l'appelant. Elle peut être supprimée définitivement après
//  vérification que rien d'autre n'en dépend.
// ─────────────────────────────────────────────────────────────
static void writeContourSubFiles(const QVector<BlockData> &blocks,
                                  const QString &encPath)
{
    Q_UNUSED(blocks);
    Q_UNUSED(encPath);
    // Plus de sous-fichier .cnc — tout est inline dans le NC.
}

// Token global : chaque saveEncFile incrémente le token.
// La lambda de thread ne s'exécute que si le token n'a pas changé.
static QAtomicInt s_ncWriteToken{0};

// ═══════════════════════════════════════════════════════════════
//  BlkFormWidget
// ═══════════════════════════════════════════════════════════════
class DroSpinBox : public QDoubleSpinBox
{
public:
    explicit DroSpinBox(QWidget *p = nullptr) : QDoubleSpinBox(p)
    { setProperty("isDroSpinBox", true); }
    QLineEdit* le() { return lineEdit(); }
};

static DroSpinBox* makeDro(double val, double lo, double hi)
{
    auto *sb = new DroSpinBox;
    sb->setDecimals(3); sb->setRange(lo, hi); sb->setSingleStep(0.1);
    sb->setValue(val); sb->setButtonSymbols(QAbstractSpinBox::NoButtons);
    sb->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sb->setFixedHeight(64); sb->setMinimumWidth(200);
    sb->setStyleSheet(
        "QDoubleSpinBox {"
        "  background:#FFFFFF; color:#0000FF;"
        "  border:2px solid #A0A0A0; border-radius:6px;"
        "  font-family:\"IBM Plex Mono\",\"Consolas\",monospace;"
        "  font-size:21pt; font-weight:700; padding:8px 16px;"
        "}"
        "QDoubleSpinBox:focus { border:2px solid #00AA00; background:#F4FFF0; }");
    return sb;
}

static QLabel* makeAxisLabel(const QString &text)
{
    auto *lbl = new QLabel(text);
    lbl->setAlignment(Qt::AlignCenter);
    // Couleur par axe : rouge X, vert Y, bleu Z (convention machine-outil)
    QString col = "#404040";
    if      (text == "X") col = "#CC3333";
    else if (text == "Y") col = "#2E9A2E";
    else if (text == "Z") col = "#3366CC";
    lbl->setStyleSheet(QString(
                           "QLabel { color:%1; font-size:28pt; font-weight:900;"
                           " font-family:\"Arial Black\",\"Arial\",sans-serif;"
                           " letter-spacing:2px; }").arg(col));
    lbl->setFixedWidth(64);
    return lbl;
}

// ═══════════════════════════════════════════════════════════════
//  BrutPreviewWidget — schéma 3D de la pièce avec cotes et origine
//  Dessiné directement en QPainter (pas de SVG)
// ═══════════════════════════════════════════════════════════════
class BrutPreviewWidget : public QWidget
{
public:
    enum HighlightFace { NoHighlight, Xmin, Xmax, Ymin, Ymax, Zmin, Zmax };

    explicit BrutPreviewWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(320);
        setStyleSheet("background:transparent;");
    }
    void setDimensions(double xm, double xp, double ym, double yp, double zm, double zp) {
        m_xm = xm; m_xp = xp; m_ym = ym; m_yp = yp; m_zm = zm; m_zp = zp;
        update();
    }
    void setHighlight(HighlightFace f) {
        if (m_highlight != f) { m_highlight = f; update(); }
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const int W = width(), H = height();
        p.fillRect(rect(), QColor("#EBEBEB"));

        const double lx = m_xp - m_xm;
        const double ly = m_yp - m_ym;
        const double lz = m_zp - m_zm;
        if (lx < 0.01 || ly < 0.01 || lz < 0.01) {
            p.setPen(QColor("#808080"));
            p.drawText(rect(), Qt::AlignCenter, "Dimensions invalides");
            return;
        }

        // ── Projection isométrique ───────────────────────────
        const double ang = 30.0 * M_PI / 180.0;
        const double cx = std::cos(ang), sy = std::sin(ang);
        auto proj = [&](double x, double y, double z) -> QPointF {
            return QPointF( x * cx - y * cx,
                           -z      + (x * sy + y * sy));
        };

        // ══════════════════════════════════════════════════════
        //  DESSINER UN CUBE AVEC SPLIT À LA POSITION m_highlight
        //  On dessine 2 sous-cubes empilés :
        //   - Sous-cube INFÉRIEUR (partie non sélectionnée) : GRIS
        //   - Sous-cube SUPÉRIEUR (partie sélectionnée)     : BLEU
        //  Chaque sous-cube a ses 3 faces visibles peintes,
        //  dans l\'ordre du peintre (du plus loin au plus proche)
        // ══════════════════════════════════════════════════════

        // Déterminer le "split" selon le highlight
        // On sépare le cube en 2 volumes : A (non highlighted) + B (highlighted)
        struct Vol { double xm,xp,ym,yp,zm,zp; };
        Vol volA{m_xm,m_xp,m_ym,m_yp,m_zm,m_zp};
        Vol volB{0,0,0,0,0,0};
        bool hasB = false;

        if (m_highlight != NoHighlight) {
            volA = {m_xm,m_xp,m_ym,m_yp,m_zm,m_zp};
            volB = {m_xm,m_xp,m_ym,m_yp,m_zm,m_zp};
            switch (m_highlight) {
            case Xmin:
                if (m_xm < 0 && 0 < m_xp) { volA.xm=0; volB.xp=0; hasB=true; }
                break;
            case Xmax:
                if (m_xm < 0 && 0 < m_xp) { volA.xp=0; volB.xm=0; hasB=true; }
                break;
            case Ymin:
                if (m_ym < 0 && 0 < m_yp) { volA.ym=0; volB.yp=0; hasB=true; }
                break;
            case Ymax:
                if (m_ym < 0 && 0 < m_yp) { volA.yp=0; volB.ym=0; hasB=true; }
                break;
            case Zmin:
                if (m_zm < 0 && 0 < m_zp) { volA.zm=0; volB.zp=0; hasB=true; }
                break;
            case Zmax:
                if (m_zm < 0 && 0 < m_zp) { volA.zp=0; volB.zm=0; hasB=true; }
                break;
            default: break;
            }
        }

        // Calcul de la bounding box pour scale (utilise le cube entier)
        QPointF allCorners[8] = {
            proj(m_xm, m_ym, m_zm), proj(m_xp, m_ym, m_zm),
            proj(m_xp, m_yp, m_zm), proj(m_xm, m_yp, m_zm),
            proj(m_xm, m_ym, m_zp), proj(m_xp, m_ym, m_zp),
            proj(m_xp, m_yp, m_zp), proj(m_xm, m_yp, m_zp)
        };
        double minX = allCorners[0].x(), maxX = minX;
        double minY = allCorners[0].y(), maxY = minY;
        for (int i = 1; i < 8; i++) {
            minX = qMin(minX, allCorners[i].x()); maxX = qMax(maxX, allCorners[i].x());
            minY = qMin(minY, allCorners[i].y()); maxY = qMax(maxY, allCorners[i].y());
        }
        const int margin = 70;
        double sx_scale = (W - 2 * margin) / (maxX - minX);
        double sy_scale = (H - 2 * margin) / (maxY - minY);
        double s = qMin(sx_scale, sy_scale);
        double ox = (W - s * (minX + maxX)) / 2.0;
        double oy = (H - s * (minY + maxY)) / 2.0;
        auto toScreen = [&](QPointF pt){ return QPointF(ox + s*pt.x(), oy + s*pt.y()); };

        const QColor edge(40, 40, 40);

        // ── Lambda pour dessiner un volume avec sa couleur ──
        //  Dessine les 6 faces dans l\'ordre du peintre (arrière → avant)
        //  pour que les faces internes (visibles quand 2 volumes se touchent)
        //  soient correctement peintes.
        auto drawVolume = [&](const Vol &v, const QColor &faceColor){
            if (v.xp - v.xm < 1e-6 || v.yp - v.ym < 1e-6 || v.zp - v.zm < 1e-6)
                return;
            QPointF C[8];
            C[0] = toScreen(proj(v.xm, v.ym, v.zm));
            C[1] = toScreen(proj(v.xp, v.ym, v.zm));
            C[2] = toScreen(proj(v.xp, v.yp, v.zm));
            C[3] = toScreen(proj(v.xm, v.yp, v.zm));
            C[4] = toScreen(proj(v.xm, v.ym, v.zp));
            C[5] = toScreen(proj(v.xp, v.ym, v.zp));
            C[6] = toScreen(proj(v.xp, v.yp, v.zp));
            C[7] = toScreen(proj(v.xm, v.yp, v.zp));

            p.setPen(QPen(edge, 1.5));
            p.setBrush(faceColor);

            // Ordre du peintre : faces arrières d\'abord, faces avant ensuite
            // En iso 30° caméra avant-droite-haut :
            //   Arrière (Y+) : 3-2-6-7
            //   Gauche  (X-) : 0-3-7-4
            //   Dessous (Z-) : 0-1-2-3
            //   Avant   (Y-) : 0-1-5-4
            //   Droite  (X+) : 1-2-6-5
            //   Dessus  (Z+) : 4-5-6-7
            QPolygonF back;   back   << C[3] << C[2] << C[6] << C[7]; p.drawPolygon(back);
            QPolygonF left;   left   << C[0] << C[3] << C[7] << C[4]; p.drawPolygon(left);
            QPolygonF bottom; bottom << C[0] << C[1] << C[2] << C[3]; p.drawPolygon(bottom);
            QPolygonF front;  front  << C[0] << C[1] << C[5] << C[4]; p.drawPolygon(front);
            QPolygonF right;  right  << C[1] << C[2] << C[6] << C[5]; p.drawPolygon(right);
            QPolygonF top;    top    << C[4] << C[5] << C[6] << C[7]; p.drawPolygon(top);
        };

        // ══════════════════════════════════════════════════════
        //  ORDRE DE DESSIN selon le type de highlight
        //  Le volume situé "derrière" doit être dessiné en premier
        // ══════════════════════════════════════════════════════
        const QColor grayFill ( 155, 170, 190);  // gris moyen (plein)
        const QColor blueFill (  80, 160, 255);  // bleu (plein)

        if (!hasB) {
            // Pas de highlight ou origine hors plage : dessiner le cube entier en gris
            drawVolume(volA, grayFill);
        } else {
            // Déterminer lequel dessiner en premier (le plus loin = celui derrière)
            // En iso 30° caméra en avant-haut-droite :
            //   - Plus petit X, plus grand Y, plus petit Z = plus loin
            // Ici on compare les centres des 2 volumes
            auto center = [](const Vol &v){
                return QVector3D((v.xm+v.xp)/2, (v.ym+v.yp)/2, (v.zm+v.zp)/2);
            };
            QVector3D cA = center(volA), cB = center(volB);
            // Distance à la caméra (plus = plus loin)
            // Caméra regarde vers -X+Y-Z approximativement
            auto depth = [](const QVector3D &c){
                return -c.x() + c.y() - c.z();
            };
            bool aFirst = depth(cA) > depth(cB);

            if (aFirst) {
                drawVolume(volA, grayFill);
                drawVolume(volB, blueFill);
            } else {
                drawVolume(volB, blueFill);
                drawVolume(volA, grayFill);
            }
        }

        // ══════════════════════════════════════════════════════
        //  Passe finale : redessiner toutes les arêtes du grand cube
        //  + arête de séparation au plan de coupe (pour bien voir
        //  la ligne entre zone bleue et zone grise)
        // ══════════════════════════════════════════════════════
        {
            QPointF GC[8] = {
                toScreen(proj(m_xm, m_ym, m_zm)), toScreen(proj(m_xp, m_ym, m_zm)),
                toScreen(proj(m_xp, m_yp, m_zm)), toScreen(proj(m_xm, m_yp, m_zm)),
                toScreen(proj(m_xm, m_ym, m_zp)), toScreen(proj(m_xp, m_ym, m_zp)),
                toScreen(proj(m_xp, m_yp, m_zp)), toScreen(proj(m_xm, m_yp, m_zp))
            };
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(edge, 1.5));
            // 4 arêtes du dessous
            p.drawLine(GC[0], GC[1]); p.drawLine(GC[1], GC[2]);
            p.drawLine(GC[2], GC[3]); p.drawLine(GC[3], GC[0]);
            // 4 arêtes du dessus
            p.drawLine(GC[4], GC[5]); p.drawLine(GC[5], GC[6]);
            p.drawLine(GC[6], GC[7]); p.drawLine(GC[7], GC[4]);
            // 4 arêtes verticales
            p.drawLine(GC[0], GC[4]); p.drawLine(GC[1], GC[5]);
            p.drawLine(GC[2], GC[6]); p.drawLine(GC[3], GC[7]);

            // ── Arêtes du plan de coupe (entre bleu et gris) ──
            if (hasB) {
                auto drawSeg3D = [&](double x1,double y1,double z1,
                                     double x2,double y2,double z2){
                    p.drawLine(toScreen(proj(x1,y1,z1)),
                               toScreen(proj(x2,y2,z2)));
                };
                // Pour chaque type de highlight, on trace les 4 arêtes
                // du rectangle interne du plan de coupe
                switch (m_highlight) {
                case Xmin: case Xmax: {
                    // Plan X=0 dans le cube : rectangle (0, ym..yp, zm..zp)
                    drawSeg3D(0, m_ym, m_zm, 0, m_yp, m_zm);
                    drawSeg3D(0, m_yp, m_zm, 0, m_yp, m_zp);
                    drawSeg3D(0, m_yp, m_zp, 0, m_ym, m_zp);
                    drawSeg3D(0, m_ym, m_zp, 0, m_ym, m_zm);
                    break;
                }
                case Ymin: case Ymax: {
                    // Plan Y=0 : rectangle (xm..xp, 0, zm..zp)
                    drawSeg3D(m_xm, 0, m_zm, m_xp, 0, m_zm);
                    drawSeg3D(m_xp, 0, m_zm, m_xp, 0, m_zp);
                    drawSeg3D(m_xp, 0, m_zp, m_xm, 0, m_zp);
                    drawSeg3D(m_xm, 0, m_zp, m_xm, 0, m_zm);
                    break;
                }
                case Zmin: case Zmax: {
                    // Plan Z=0 : rectangle (xm..xp, ym..yp, 0)
                    drawSeg3D(m_xm, m_ym, 0, m_xp, m_ym, 0);
                    drawSeg3D(m_xp, m_ym, 0, m_xp, m_yp, 0);
                    drawSeg3D(m_xp, m_yp, 0, m_xm, m_yp, 0);
                    drawSeg3D(m_xm, m_yp, 0, m_xm, m_ym, 0);
                    break;
                }
                default: break;
                }
            }
        }

        // ══════════════════════════════════════════════════════
        //  Origine + triade
        // ══════════════════════════════════════════════════════
        bool originInside = (0 >= m_xm - 0.01 && 0 <= m_xp + 0.01 &&
                             0 >= m_ym - 0.01 && 0 <= m_yp + 0.01 &&
                             0 >= m_zm - 0.01 && 0 <= m_zp + 0.01);
        double oxOri = originInside ? qBound(m_xm, 0.0, m_xp) : (m_xm + m_xp)/2;
        double oyOri = originInside ? qBound(m_ym, 0.0, m_yp) : (m_ym + m_yp)/2;
        QPointF origin = toScreen(proj(oxOri, oyOri, m_zp));

        p.setBrush(QColor(255, 51, 51));
        p.setPen(QPen(QColor(255, 255, 255), 1.8));
        p.drawEllipse(origin, 7, 7);
        p.setPen(QColor(0, 0, 0));
        QFont f = p.font(); f.setBold(true); f.setPointSize(9); p.setFont(f);
        p.drawText(origin + QPointF(12, -8), "X0 Y0 Z0");

        double axisLen = qMax(lx, qMax(ly, lz)) * 0.18;
        auto drawAxis = [&](QPointF from, QPointF to, const QColor &col, const QString &label){
            p.setPen(QPen(col, 2.8));
            p.drawLine(from, to);
            QLineF l(from, to);
            double a = std::atan2(l.dy(), l.dx());
            QPointF tip = to;
            QPointF a1(tip.x() - 12*std::cos(a - 0.4),
                       tip.y() - 12*std::sin(a - 0.4));
            QPointF a2(tip.x() - 12*std::cos(a + 0.4),
                       tip.y() - 12*std::sin(a + 0.4));
            p.setBrush(col);
            QPolygonF arrow; arrow << tip << a1 << a2;
            p.drawPolygon(arrow);
            p.setPen(col);
            QFont af = p.font(); af.setBold(true); af.setPointSize(12); p.setFont(af);
            p.drawText(tip + QPointF(8, 4), label);
        };
        drawAxis(origin, toScreen(proj(oxOri + axisLen, oyOri, m_zp)),
                 QColor(204, 51, 51), "X+");
        drawAxis(origin, toScreen(proj(oxOri, oyOri - axisLen, m_zp)),
                 QColor(46, 154, 46), "Y+");
        drawAxis(origin, toScreen(proj(oxOri, oyOri, m_zp + axisLen)),
                 QColor(51, 102, 204), "Z+");

        // ══════════════════════════════════════════════════════
        //  Cotes numériques
        // ══════════════════════════════════════════════════════
        // Recalculer les 8 sommets du grand cube à l\'écran pour positionner les cotes
        QPointF S[8];
        for (int i = 0; i < 8; i++) S[i] = toScreen(allCorners[i]);

        QFont cf = p.font(); cf.setPointSize(10); cf.setBold(true); p.setFont(cf);
        p.setPen(QColor(48, 48, 48));
        QPointF pX = (S[0] + S[1]) / 2.0 + QPointF(-35, 30);
        p.drawText(pX, QString("X = %1 mm").arg(lx, 0, 'f', 1));
        QPointF pY = (S[1] + S[2]) / 2.0 + QPointF(16, 6);
        p.drawText(pY, QString("Y = %1 mm").arg(ly, 0, 'f', 1));
        QPointF pZ = (S[0] + S[4]) / 2.0 + QPointF(-82, 6);
        p.drawText(pZ, QString("Z = %1 mm").arg(lz, 0, 'f', 1));

        QFont vf = p.font(); vf.setPointSize(9); vf.setBold(true); p.setFont(vf);
        auto drawCote = [&](QPointF pt, const QString &lbl, double val, HighlightFace f) {
            bool hi = (m_highlight == f);
            p.setPen(hi ? QColor(26, 127, 238) : QColor(80, 80, 80));
            p.drawText(pt, QString("%1 = %2").arg(lbl).arg(val, 0, 'f', 1));
        };
        drawCote((S[0] + S[3]) / 2.0 + QPointF(-70, 2),  "X-", m_xm, Xmin);
        drawCote((S[1] + S[2]) / 2.0 + QPointF(14, 22),  "X+", m_xp, Xmax);
        drawCote((S[0] + S[1]) / 2.0 + QPointF(-35, 50), "Y-", m_ym, Ymin);
        drawCote((S[3] + S[2]) / 2.0 + QPointF(-35, -10),"Y+", m_yp, Ymax);
        drawCote(S[0] + QPointF(-32, 16),                "Z-", m_zm, Zmin);
        drawCote(S[4] + QPointF(-32, -6),                "Z+", m_zp, Zmax);
    }

private:
    double m_xm = -5,  m_xp = 200;
    double m_ym = -5,  m_yp = 150;
    double m_zm = -50, m_zp = 0;
    HighlightFace m_highlight = NoHighlight;
};

BlkFormWidget::BlkFormWidget(QWidget *parent) : QWidget(parent) { setupUi(); }

void BlkFormWidget::setupUi()
{
    setStyleSheet("QWidget {background:#D6D6D6;}");
    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0,0,0,0); mainLay->setSpacing(0);

    m_tabs = new QTabWidget;
    m_tabs->setStyleSheet(
        "QTabWidget::pane{border:none;background:#D6D6D6;}"
        "QTabBar::tab{background:#BEBEBE;color:#404040;padding:10px 22px;"
        "  font-size:10pt;font-weight:600;border:none;min-width:140px;}"
        "QTabBar::tab:selected{background:#D6D6D6;color:#0000FF;"
        "  border-bottom:2px solid #0000FF;}"
        "QTabBar::tab:hover:!selected{background:#CCCCCC;}"
        );
    m_tabs->addTab(buildDimensionsTab(), "📐  Dimensions");
    m_tabs->addTab(buildParamsTab(),     "⚙️  Paramètres");
    mainLay->addWidget(m_tabs);
}

QWidget* BlkFormWidget::buildDimensionsTab()
{
    auto *page = new QWidget;
    auto *mainLay = new QVBoxLayout(page);
    mainLay->setContentsMargins(40, 32, 40, 32); mainLay->setSpacing(24);

    // ── Titre ─────────────────────────────────────────────────
    auto *title = new QLabel("DÉFINITION DU BRUT");
    title->setStyleSheet(
        "color:#303030;font-size:12pt;font-weight:bold;letter-spacing:2px;");
    mainLay->addWidget(title);

    auto *sub = new QLabel("Dimensions du brut et origine pièce (X0 Y0 Z0).");
    sub->setStyleSheet("color:#606060;font-size:10pt;");
    mainLay->addWidget(sub);

    auto *sep = new QFrame; sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("QFrame{background:#A0A0A0;}"); sep->setFixedHeight(2);
    mainLay->addWidget(sep);

    // ── Entêtes colonnes ──────────────────────────────────────
    auto *grid = new QGridLayout; grid->setSpacing(18);
    grid->setColumnStretch(2, 1); grid->setColumnStretch(4, 1);

    auto makeColHdr = [](const QString &t){
        auto *l = new QLabel(t);
        l->setStyleSheet("color:#505050;font-size:10pt;"
                         "font-weight:bold;letter-spacing:1px;");
        l->setAlignment(Qt::AlignCenter); return l;
    };
    grid->addWidget(makeColHdr("AXE"),           0, 0);
    grid->addWidget(makeColHdr("−  valeur min"), 0, 1, 1, 2);
    grid->addWidget(makeColHdr("+  valeur max"), 0, 3, 1, 2);

    // ── Spinboxes ─────────────────────────────────────────────
    m_xMinus = makeDro( -5.0,-9999.999,9999.999); m_xPlus = makeDro(200.0,-9999.999,9999.999);
    m_yMinus = makeDro( -5.0,-9999.999,9999.999); m_yPlus = makeDro(150.0,-9999.999,9999.999);
    m_zMinus = makeDro(-50.0,-9999.999,9999.999); m_zPlus = makeDro(  0.0,-9999.999,9999.999);

    auto connectPrefix = [this](QDoubleSpinBox *sb){
        connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this,sb](double){ updatePlusPrefix(sb); emit dataChanged(); });
        updatePlusPrefix(sb);
    };
    connectPrefix(m_xPlus); connectPrefix(m_yPlus); connectPrefix(m_zPlus);
    for (QDoubleSpinBox *sb : {m_xMinus, m_yMinus, m_zMinus})
        connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double){ emit dataChanged(); });

    // ── Signes − et + (gros et colorés) ──────────────────────
    auto makeSign = [](const QString &t, bool isPlus){
        auto *l = new QLabel(t);
        QString col = isPlus ? "#2E9A2E" : "#CC3333";
        l->setStyleSheet(QString(
                             "color:%1; font-size:26pt; font-weight:900;").arg(col));
        l->setAlignment(Qt::AlignCenter); l->setFixedWidth(32);
        return l;
    };

    grid->addWidget(makeAxisLabel("X"), 1, 0);
    grid->addWidget(makeSign("−", false), 1, 1);
    grid->addWidget(m_xMinus, 1, 2);
    grid->addWidget(makeSign("+", true), 1, 3);
    grid->addWidget(m_xPlus, 1, 4);

    grid->addWidget(makeAxisLabel("Y"), 2, 0);
    grid->addWidget(makeSign("−", false), 2, 1);
    grid->addWidget(m_yMinus, 2, 2);
    grid->addWidget(makeSign("+", true), 2, 3);
    grid->addWidget(m_yPlus, 2, 4);

    grid->addWidget(makeAxisLabel("Z"), 3, 0);
    grid->addWidget(makeSign("−", false), 3, 1);
    grid->addWidget(m_zMinus, 3, 2);
    grid->addWidget(makeSign("+", true), 3, 3);
    grid->addWidget(m_zPlus, 3, 4);

    mainLay->addLayout(grid);

    // ── Note explicative ──────────────────────────────────────
    auto *noteBox = new QFrame;
    noteBox->setStyleSheet(
        "QFrame{background:rgba(86,212,78,0.12);"
        "border:1px solid rgba(86,212,78,0.4);border-radius:6px;}");
    auto *noteLay = new QHBoxLayout(noteBox); noteLay->setContentsMargins(18, 14, 18, 14);
    auto *noteText = new QLabel(
        "ℹ  L\'origine pièce X0 Y0 Z0 correspond à X−=0, Y−=0, Z+=0.\n"
        "    Valeurs négatives = dans la matière (Z−), positives = au-dessus (Z+).");
    noteText->setStyleSheet("color:#2A6B2A;font-size:10pt;border:none;background:transparent;");
    noteText->setWordWrap(true);
    noteLay->addWidget(noteText);
    mainLay->addWidget(noteBox);

    mainLay->addStretch();

    for (QDoubleSpinBox *sb : {m_xMinus,m_xPlus,m_yMinus,m_yPlus,m_zMinus,m_zPlus})
        sb->installEventFilter(this);

    return page;
}

QWidget* BlkFormWidget::buildParamsTab()
{
    auto *page = new QWidget;
    auto *mainLay = new QVBoxLayout(page);
    mainLay->setContentsMargins(40, 32, 40, 32); mainLay->setSpacing(20);

    auto *title = new QLabel("PARAMÈTRES GÉNÉRAUX");
    title->setStyleSheet(
        "color:#303030;font-size:12pt;font-weight:bold;letter-spacing:2px;");
    mainLay->addWidget(title);

    auto *sub = new QLabel("Ces valeurs sont héritées par défaut par tous les blocs qui suivent.\n"
                           "Un bloc peut redéfinir sa propre valeur localement (badge rouge).");
    sub->setStyleSheet("color:#606060;font-size:10pt;");
    mainLay->addWidget(sub);

    auto *sep = new QFrame; sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("QFrame{background:#A0A0A0;}"); sep->setFixedHeight(2);
    mainLay->addWidget(sep);

    // ── Style commun spinboxes ────────────────────────────────
    auto styleSpin = [](QDoubleSpinBox *sb){
        sb->setRange(-9999.999, 9999.999);
        sb->setDecimals(3);
        sb->setSingleStep(0.5);
        sb->setAlignment(Qt::AlignRight);
        sb->setMinimumWidth(160);
        sb->setFixedHeight(48);
        sb->setStyleSheet(
            "QDoubleSpinBox{background:#FFFFFF;color:#0000FF;"
            "  border:2px solid #A0A0A0;border-radius:6px;padding:6px 12px;"
            "  font-family:\"IBM Plex Mono\",\"Consolas\",monospace;"
            "  font-size:15pt;font-weight:700;}"
            "QDoubleSpinBox:focus{border:2px solid #00AA00;background:#F4FFF0;}");
    };

    auto makeLabel = [](const QString &t){
        auto *l = new QLabel(t);
        l->setStyleSheet("color:#303030;font-size:11pt;font-weight:600;");
        return l;
    };

    auto makeBadgeG = [](const QString &tip){
        auto *b = new QLabel("G");
        b->setToolTip(tip);
        b->setFixedSize(26, 26);
        b->setAlignment(Qt::AlignCenter);
        b->setStyleSheet(
            "QLabel{background:#00AA00;color:#FFFFFF;"
            "  font-weight:bold;font-size:12pt;"
            "  border-radius:13px;}");
        return b;
    };

    auto *grid = new QGridLayout; grid->setSpacing(18);
    grid->setColumnStretch(1,1);

    // ══════════════════════════════════════════════════════════
    //  Ligne 1 : Numéro d\'origine (avec case à cocher)
    //  Si décochée → pas de G54-G59 écrit dans le G-code
    // ══════════════════════════════════════════════════════════
    m_chkDefineOrigin = new QCheckBox("Définir");
    m_chkDefineOrigin->setChecked(false);
    m_chkDefineOrigin->setStyleSheet(
        "QCheckBox{color:#303030;font-size:11pt;font-weight:600;padding-right:8px;}"
        "QCheckBox::indicator{width:22px;height:22px;}"
        "QCheckBox::indicator:unchecked{background:#FFFFFF;"
        "  border:2px solid #808080;border-radius:4px;}"
        "QCheckBox::indicator:checked{background:#00AA00;"
        "  border:2px solid #00AA00;border-radius:4px;}");

    m_cbOrigine = new QComboBox;
    m_cbOrigine->addItems({"G54", "G55", "G56", "G57", "G58", "G59"});
    m_cbOrigine->setEnabled(false);  // désactivé tant que case non cochée
    m_cbOrigine->setFixedHeight(40);
    m_cbOrigine->setMinimumWidth(100);
    m_cbOrigine->setStyleSheet(
        "QComboBox{background:#FFFFFF;color:#0000FF;"
        "  border:2px solid #A0A0A0;border-radius:6px;padding:4px 12px;"
        "  font-family:\"IBM Plex Mono\",\"Consolas\",monospace;"
        "  font-size:13pt;font-weight:700;}"
        "QComboBox:disabled{background:#E8E8E8;color:#A0A0A0;border:2px solid #C0C0C0;}"
        "QComboBox:focus{border:2px solid #00AA00;}"
        "QComboBox::drop-down{width:24px;}");
    connect(m_cbOrigine, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ emit dataChanged(); });

    connect(m_chkDefineOrigin, &QCheckBox::toggled, this, [this](bool on){
        m_cbOrigine->setEnabled(on);
        emit dataChanged();
    });

    auto *hOrig = new QHBoxLayout;
    hOrig->setContentsMargins(0,0,0,0); hOrig->setSpacing(16);
    hOrig->addWidget(m_chkDefineOrigin);
    hOrig->addWidget(m_cbOrigine);
    hOrig->addStretch();

    grid->addWidget(makeLabel("Numéro d\'origine"), 0, 0);
    grid->addLayout(hOrig,                           0, 1);
    grid->addWidget(makeBadgeG("Hérité par tous les blocs\nDécocher = ne pas écrire G54-G59 dans le G-code"), 0, 2);

    // ══════════════════════════════════════════════════════════
    //  Ligne 2 : Z saut de bride
    // ══════════════════════════════════════════════════════════
    m_spZSautBride = new QDoubleSpinBox; styleSpin(m_spZSautBride);
    m_spZSautBride->setValue(50.0);
    m_spZSautBride->setSuffix(" mm");
    connect(m_spZSautBride, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double){ emit dataChanged(); });

    grid->addWidget(makeLabel("Z saut de bride"), 1, 0);
    grid->addWidget(m_spZSautBride,               1, 1);
    grid->addWidget(makeBadgeG("Z haut pour déplacements sécurisés au-dessus des brides"), 1, 2);

    // ══════════════════════════════════════════════════════════
    //  Ligne 3 : Z liaison (nouveau)
    // ══════════════════════════════════════════════════════════
    m_spZLiaison = new QDoubleSpinBox; styleSpin(m_spZLiaison);
    m_spZLiaison->setValue(5.0);
    m_spZLiaison->setSuffix(" mm");
    connect(m_spZLiaison, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double){ emit dataChanged(); });

    grid->addWidget(makeLabel("Z liaison"), 2, 0);
    grid->addWidget(m_spZLiaison,           2, 1);
    grid->addWidget(makeBadgeG("Z intermédiaire entre usinages sans obstacle\n(plus bas que saut de bride → plus rapide)"), 2, 2);

    // ══════════════════════════════════════════════════════════
    //  Ligne 4 : Z approche
    // ══════════════════════════════════════════════════════════
    m_spZApproche = new QDoubleSpinBox; styleSpin(m_spZApproche);
    m_spZApproche->setValue(2.0);
    m_spZApproche->setSuffix(" mm");
    connect(m_spZApproche, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double){ emit dataChanged(); });

    grid->addWidget(makeLabel("Z approche"), 3, 0);
    grid->addWidget(m_spZApproche,           3, 1);
    grid->addWidget(makeBadgeG("Z juste au-dessus de la matière avant plongée"), 3, 2);

    // ══════════════════════════════════════════════════════════
    //  Ligne 5 : Avance de liaison
    // ══════════════════════════════════════════════════════════
    m_spAvanceLiaison = new QDoubleSpinBox; styleSpin(m_spAvanceLiaison);
    m_spAvanceLiaison->setRange(0.0, 99999.0);
    m_spAvanceLiaison->setDecimals(0);
    m_spAvanceLiaison->setValue(0.0);
    m_spAvanceLiaison->setSuffix(" mm/min");
    m_spAvanceLiaison->setSpecialValueText("G0 (rapide)");
    connect(m_spAvanceLiaison, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double){ emit dataChanged(); });

    grid->addWidget(makeLabel("Avance de liaison"), 4, 0);
    grid->addWidget(m_spAvanceLiaison,              4, 1);
    grid->addWidget(makeBadgeG("Vitesse pour les déplacements entre usinages\n(0 = G0 rapide)"), 4, 2);

    // ══════════════════════════════════════════════════════════
    //  Ligne 6 : Retour Z début/fin de programme — M25 ou G28
    //  M25 = M-fonction Centroid, force un retour Z au point de
    //  référence machine AVANT tout — perturbe l'affichage du
    //  chemin outil dans le viewer CNC12 (le cône apparaît hors
    //  du bloc matière). G28 = même mouvement mais en G-code
    //  standard (comme le post-processeur Fusion 360/Centroid,
    //  option "Safe Retracts: G28") — correctement interprété
    //  par le viewer.
    // ══════════════════════════════════════════════════════════
    m_rbM25Origine = new QRadioButton("M25 (origine machine)");
    m_rbM25Parking = new QRadioButton("G28 (point de référence)");
    const QString rbStyle =
        "QRadioButton{color:#303030;font-size:10pt;font-weight:600;}"
        "QRadioButton::indicator{width:20px;height:20px;}"
        "QRadioButton::indicator:unchecked{background:#FFFFFF;"
        "  border:2px solid #808080;border-radius:10px;}"
        "QRadioButton::indicator:checked{background:#00AA00;"
        "  border:2px solid #00AA00;border-radius:10px;}";
    m_rbM25Origine->setStyleSheet(rbStyle);
    m_rbM25Parking->setStyleSheet(rbStyle);
    m_rbM25Origine->setChecked(true);
    auto *bgM25 = new QButtonGroup(this);
    bgM25->addButton(m_rbM25Origine, 0);
    bgM25->addButton(m_rbM25Parking, 1);
    connect(m_rbM25Origine, &QRadioButton::toggled, this, [this](bool){ emit dataChanged(); });
    connect(m_rbM25Parking, &QRadioButton::toggled, this, [this](bool){ emit dataChanged(); });

    auto *hM25 = new QHBoxLayout;
    hM25->setContentsMargins(0,0,0,0); hM25->setSpacing(16);
    hM25->addWidget(m_rbM25Origine);
    hM25->addWidget(m_rbM25Parking);
    hM25->addStretch();

    grid->addWidget(makeLabel("Retour Z prog."), 5, 0);
    grid->addLayout(hM25,                        5, 1);
    grid->addWidget(makeBadgeG("M25 : sécurité machine réelle, mais affichage\n"
                               "chemin outil décalé dans le viewer CNC12.\n"
                               "G28 : même mouvement, viewer correct\n"
                               "(comme Fusion 360 \"Safe Retracts: G28\")."), 5, 2);

    mainLay->addLayout(grid);
    mainLay->addStretch();

    return page;
}

bool BlkFormWidget::eventFilter(QObject *obj, QEvent *event)
{
    // ── FocusIn sur un des 6 spinboxes → signal pour highlight 3D ──
    if (event->type() == QEvent::FocusIn) {
        int face = 0;
        if      (obj == m_xMinus) face = 1;  // Xmin
        else if (obj == m_xPlus ) face = 2;  // Xmax
        else if (obj == m_yMinus) face = 3;  // Ymin
        else if (obj == m_yPlus ) face = 4;  // Ymax
        else if (obj == m_zMinus) face = 5;  // Zmin
        else if (obj == m_zPlus ) face = 6;  // Zmax
        if (face > 0) emit axisFieldFocused(face);
    }
    else if (event->type() == QEvent::FocusOut) {
        // Petit délai via signal 0 pour laisser un autre champ prendre le focus
        // avant de désactiver le highlight — sinon il clignote
        QTimer::singleShot(50, this, [this]{
            auto *w = QApplication::focusWidget();
            if (w != m_xMinus && w != m_xPlus &&
                w != m_yMinus && w != m_yPlus &&
                w != m_zMinus && w != m_zPlus)
                emit axisFieldFocused(0);
        });
    }
    if (event->type() == QEvent::KeyPress) {
        auto *widget = qobject_cast<QDoubleSpinBox*>(obj);
        if (widget && widget->property("isDroSpinBox").toBool()) {
            auto *sb = static_cast<DroSpinBox*>(widget);
            auto *ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Period || ke->key() == Qt::Key_Comma) {
                QString sel = sb->le()->selectedText(), txt = sb->le()->text();
                if (sel == txt || txt.isEmpty()) {
                    sb->le()->setText("0."); sb->le()->setCursorPosition(2); return true;
                }
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void BlkFormWidget::updatePlusPrefix(QDoubleSpinBox *sb)
{ QString p=(sb->value()>0.0)?"+":" "; if(sb->prefix()!=p) sb->setPrefix(p); }

// ── Accesseurs Dimensions ─────────────────────────────────────
double BlkFormWidget::xMinus() const { return m_xMinus->value(); }
double BlkFormWidget::xPlus()  const { return m_xPlus->value();  }
double BlkFormWidget::yMinus() const { return m_yMinus->value(); }
double BlkFormWidget::yPlus()  const { return m_yPlus->value();  }
double BlkFormWidget::zMinus() const { return m_zMinus->value(); }
double BlkFormWidget::zPlus()  const { return m_zPlus->value();  }

void BlkFormWidget::setValues(double xm,double xp,double ym,double yp,double zm,double zp)
{ m_xMinus->setValue(xm); m_xPlus->setValue(xp); m_yMinus->setValue(ym);
    m_yPlus->setValue(yp);  m_zMinus->setValue(zm); m_zPlus->setValue(zp); }

// ── Accesseurs Paramètres Généraux ────────────────────────────
bool   BlkFormWidget::origineDefinie() const { return m_chkDefineOrigin && m_chkDefineOrigin->isChecked(); }
int    BlkFormWidget::origine()       const { return m_cbOrigine       ? m_cbOrigine->currentIndex() : 0; }
double BlkFormWidget::zSautBride()    const { return m_spZSautBride    ? m_spZSautBride->value()     : 50.0; }
double BlkFormWidget::zLiaison()      const { return m_spZLiaison      ? m_spZLiaison->value()       : 5.0; }
double BlkFormWidget::zApproche()     const { return m_spZApproche     ? m_spZApproche->value()      : 2.0; }
double BlkFormWidget::avanceLiaison() const { return m_spAvanceLiaison ? m_spAvanceLiaison->value()  : 0.0; }
bool   BlkFormWidget::m25RetourOrigine() const { return m_rbM25Origine ? m_rbM25Origine->isChecked() : true; }

void BlkFormWidget::setParamsGlobaux(bool origineDefinie, int origine,
                                     double zSautBride, double zLiaison,
                                     double zApproche,  double avanceLiaison,
                                     bool   m25RetourOrigine)
{
    if (m_chkDefineOrigin) m_chkDefineOrigin->setChecked(origineDefinie);
    if (m_cbOrigine)       m_cbOrigine->setCurrentIndex(qBound(0, origine, 5));
    if (m_cbOrigine)       m_cbOrigine->setEnabled(origineDefinie);
    if (m_spZSautBride)    m_spZSautBride->setValue(zSautBride);
    if (m_spZLiaison)      m_spZLiaison->setValue(zLiaison);
    if (m_spZApproche)     m_spZApproche->setValue(zApproche);
    if (m_spAvanceLiaison) m_spAvanceLiaison->setValue(avanceLiaison);
    if (m_rbM25Origine)    m_rbM25Origine->setChecked(m25RetourOrigine);
    if (m_rbM25Parking)    m_rbM25Parking->setChecked(!m25RetourOrigine);
}

// ═══════════════════════════════════════════════════════════════
//  ProgrammePage
// ═══════════════════════════════════════════════════════════════
ProgrammePage::ProgrammePage(QWidget *parent) : QWidget(parent)
{ setupUi(); newProgram(); }

void ProgrammePage::setupUi()
{
    CrashHandler::log("ProgrammePage::setupUi debut...");
    setStyleSheet("QWidget {background:#D6D6D6;}");
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0,0,0,0); lay->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setStyleSheet("QSplitter::handle { background: rgba(0,0,0,0.14); }");

    // ── Gauche : liste blocs ─────────────────────────────────
    auto *leftPanel = new QWidget;
    leftPanel->setStyleSheet("background:#C8C8C8;border-right:1px solid #808080;");
    auto *leftLay = new QVBoxLayout(leftPanel);
    leftLay->setContentsMargins(0,0,0,0); leftLay->setSpacing(0);

    auto *hdr = new QLabel("  PROGRAMME");
    hdr->setFixedHeight(36);
    hdr->setStyleSheet("QLabel{background:#EAEAEA;color:#404040;font-weight:bold;"
                       "font-size:6pt;font-weight:700;letter-spacing:3px;}");
    leftLay->addWidget(hdr);

    m_list = new QListWidget;
    m_list->setStyleSheet(
        "QListWidget{background:#FFFFFF;border:none;outline:none;}"
        "QListWidget::item{"
        "  border-bottom:1px solid #D0D0D0;"
        "  border-left:3px solid transparent;"
        "  padding:10px 14px; color:#000000;"
        "  font-size:10pt;}"
        "QListWidget::item:hover{background:#EBEBEB;}"
        "QListWidget::item:selected{"
        "  background:#D6E8FF;"
        "  border-left:3px solid #0000FF; color:#0000FF;}");
    m_list->setFocusPolicy(Qt::StrongFocus);
    m_list->setIconSize(QSize(96, 59));
    leftLay->addWidget(m_list, 1);
    connect(m_list, &QListWidget::currentRowChanged, this, &ProgrammePage::onBlockSelected);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem*){
        if (m_selectionMode) QTimer::singleShot(0, this, [this]{ updateSelectionHighlight(); });
    });

    // (Le cadre d'aide m_helpFrame est maintenant dans la 3ème colonne)

    // ── Droite : BlkFormWidget ou UnitEditor ─────────────────
    m_rightStack = new QStackedWidget;
    m_blkForm = new BlkFormWidget(this);
    m_editor  = new UnitEditor(this);
    // Sauvegarde automatique du bloc BlkForm lors d\'un changement
    connect(m_blkForm, &BlkFormWidget::dataChanged,
            this, &ProgrammePage::saveCurrentBlock);
    m_rightStack->addWidget(m_blkForm);  // index 0
    // Éditeur dans une zone qui défile : avec les grands caractères,
    // les pages les plus longues (PM poche, PM fraisage trou) ne doivent
    // pas agrandir la fenêtre au-delà de l'écran (softkeys coupées).
    {
        auto *defil = new QScrollArea;
        defil->setWidget(m_editor);
        defil->setWidgetResizable(true);
        defil->setFrameShape(QFrame::NoFrame);
        defil->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_rightStack->addWidget(defil);  // index 1
    }
    m_rightStack->setCurrentIndex(0);
    // Signal UnitEditor → ouvrir l'éditeur de contour
    connect(m_editor, &UnitEditor::openContourEditor,
            this, [this](const QString &path, const QString &name){
                // Ouvrir ContourEditor dans un dialog
                auto *dlg = new QDialog(this);
                dlg->setWindowTitle(QString("Éditeur de contour — %1").arg(
                    name.isEmpty() ? "Nouveau" : name));
                dlg->resize(1200, 700);
                auto *lay = new QVBoxLayout(dlg);
                lay->setContentsMargins(0,0,0,0);
                auto *editor = new ContourEditor(dlg);
                lay->addWidget(editor);
                // Transmettre les limites BlkForm
                double xm=-100, xp=100, ym=-60, yp=60;
                for (const BlockData &bl : std::as_const(m_blocks)) {
                    if (bl.type == BlockType::BlkForm && !bl.data.isEmpty()) {
                        xm = bl.data["xMinus"].toDouble(-100);
                        xp = bl.data["xPlus"].toDouble(100);
                        ym = bl.data["yMinus"].toDouble(-60);
                        yp = bl.data["yPlus"].toDouble(60);
                        break;
                    }
                }
                editor->setWorkArea(xm, xp, ym, yp);
                if (!path.isEmpty()) {
                    editor->loadFile(path);
                }
                if (!name.isEmpty()) editor->setContourName(name);
                // Boutons OK / Annuler
                auto *btnRow = new QHBoxLayout;
                auto *btnOk  = new QPushButton("✓  Sauvegarder et fermer");
                auto *btnAnn = new QPushButton("Annuler");
                btnOk->setDefault(false);     // ne pas capturer Enter
                btnOk->setAutoDefault(false);
                btnAnn->setDefault(false);
                btnAnn->setAutoDefault(false);
                // Bloquer la touche Enter/Return au niveau du dialog
                // pour ne pas fermer le dialog depuis les champs du ContourEditor
                // Filtre clavier sur le dialog : bloquer Enter/Return
                // pour ne pas déclencher accept() depuis les champs de saisie du ContourEditor
                struct DlgFilter : public QObject {
                    bool eventFilter(QObject *o, QEvent *e) override {
                        if (e->type() == QEvent::KeyPress) {
                            auto *ke = static_cast<QKeyEvent*>(e);
                            if (ke->key()==Qt::Key_Return || ke->key()==Qt::Key_Enter)
                                return true; // bloquer
                        }
                        return QObject::eventFilter(o, e);
                    }
                };
                dlg->installEventFilter(new DlgFilter());
                btnRow->addStretch(); btnRow->addWidget(btnAnn); btnRow->addWidget(btnOk);
                lay->addLayout(btnRow);
                connect(btnAnn, &QPushButton::clicked, dlg, &QDialog::reject);
                connect(btnOk,  &QPushButton::clicked, this, [this, editor, dlg]{
                    // Demander un nom si pas encore défini
                    QString nm = editor->contourName();
                    if (nm.isEmpty()) {
                        bool ok;
                        nm = QInputDialog::getText(this, "Nom du contour",
                                                   "Nom du contour :", QLineEdit::Normal, "Contour_1", &ok);
                        if (!ok || nm.isEmpty()) return;
                        editor->setContourName(nm);
                    }
                    // Sauvegarder dans le dossier du programme
                    QString dir = m_currentPath.isEmpty()
                                      ? QDir::homePath()
                                      : QFileInfo(m_currentPath).absolutePath();
                    QString path = dir + "/" + nm + ".cnt";
                    if (editor->save(path)) {
                        m_editor->setContourFile(path, nm);
                        saveCurrentBlock();
                    }
                    dlg->accept();
                });
                dlg->exec();
                dlg->deleteLater();
            });

    // ── Signal "Fichier .cnt existant" depuis softkey Contour ──────
    connect(m_editor, &UnitEditor::openPointFile, this, [this]{
        if (m_currentIndex < 0 || m_currentIndex >= m_blocks.size()) return;
        BlockData &b = m_blocks[m_currentIndex];

        if (b.type == BlockType::FraisageTrou) {
            // Ouvrir un fichier .pnt existant (potentiellement plusieurs
            // motifs combinés) — réutilisable entre blocs/cycles, comme
            // les .cnt. Le générateur lit directement ce fichier au
            // moment de produire le G-code (FraisageTrouGenerator::
            // computePositions) — pas besoin de dupliquer les points ici.
            QString dir = m_currentPath.isEmpty() ? QDir::homePath() : QFileInfo(m_currentPath).absolutePath();
            PntFileBrowserDialog browserDlg(this);
            QString path = browserDlg.chooseExisting(dir);
            if (path.isEmpty()) return;
            PointFile pf;
            if (!pf.load(path)) {
                QMessageBox::warning(this, "Fichier de points", "Impossible de lire : " + path);
                return;
            }
            FraisageTrouData ftd = b.data.isEmpty() ? FraisageTrouData{} : fraisageTrouFromJson(b.data);
            clearDirectPositionFields(ftd);
            ftd.fichierPoints = path;
            b.data = fraisageTrouToJson(ftd);
            m_editor->loadFraisageTrou(ftd);
            saveCurrentBlock();
            return;
        }

        // Perçages et poches : même fichier .pnt réutilisable entre blocs.
        // Le chemin est rangé dans le bloc (champ « Modèle de points ») et
        // le générateur lit le fichier au moment de produire le G-code.
        if (b.type != BlockType::Contour && b.type != BlockType::PocheContour) {
            QString dir = m_currentPath.isEmpty() ? QDir::homePath() : QFileInfo(m_currentPath).absolutePath();
            PntFileBrowserDialog browserDlg(this);
            QString path = browserDlg.chooseExisting(dir);
            if (path.isEmpty()) return;
            PointFile pf;
            if (!pf.load(path)) {
                QMessageBox::warning(this, "Fichier de points", "Impossible de lire : " + path);
                return;
            }
            m_editor->setPointFile(path);
            saveCurrentBlock();
            return;
        }
        QString dir = m_currentPath.isEmpty() ? QDir::homePath() : QFileInfo(m_currentPath).absolutePath();
        QString path = QFileDialog::getOpenFileName(this,
                                                    "Ouvrir contour .cnt", dir,
                                                    "Contours EazyNC (*.cnt);;Tous (*)");
        if (path.isEmpty()) return;
        QString name = QFileInfo(path).baseName();
        m_editor->blockSignals(true);
        m_editor->setContourFile(path, name);
        m_editor->blockSignals(false);
        saveCurrentBlock();
        // Ouvrir le ContourEditor avec ce fichier
        QMetaObject::invokeMethod(m_editor, "openContourEditor",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, path), Q_ARG(QString, name));
    });

    // ── Signal "Nouveau .cnt" depuis softkey Contour ─────────────
    connect(m_editor, &UnitEditor::createNewPointFile, this, [this]{
        if (m_currentIndex < 0 || m_currentIndex >= m_blocks.size()) return;
        BlockData &b = m_blocks[m_currentIndex];

        if (b.type == BlockType::FraisageTrou) {
            // Ouvre l'éditeur de fichier de points (regroupe les 5
            // modèles, entrées combinables à volonté) — c'est LUI qui
            // demande le nom et enregistre le .pnt (bouton "Enregistrer").
            QString dir = m_currentPath.isEmpty()
                              ? QDir::homePath()
                              : QFileInfo(m_currentPath).absolutePath();
            PointFileEditorDialog dlg(this);
            dlg.setPointFile(PointFile{});
            if (!dlg.runAndSave(dir)) return;  // Annulé
            PointFile pf = dlg.pointFile();

            FraisageTrouData ftd = b.data.isEmpty() ? FraisageTrouData{} : fraisageTrouFromJson(b.data);
            ftd.fichierPoints = pf.filePath;
            clearDirectPositionFields(ftd);
            b.data = fraisageTrouToJson(ftd);
            m_editor->loadFraisageTrou(ftd);
            saveCurrentBlock();
            return;
        }

        if (b.type != BlockType::Contour && b.type != BlockType::PocheContour) {
            // Perçages et poches : nouveau .pnt via l'éditeur de points
            QString dir = m_currentPath.isEmpty() ? QDir::homePath()
                                                  : QFileInfo(m_currentPath).absolutePath();
            PointFileEditorDialog dlg(this);
            dlg.setPointFile(PointFile{});
            if (!dlg.runAndSave(dir)) return;
            m_editor->setPointFile(dlg.pointFile().filePath);
            saveCurrentBlock();
            return;
        }
        // Demander un nom
        bool ok;
        QString nm = QInputDialog::getText(this, "Nouveau contour",
                                           "Nom du contour :", QLineEdit::Normal, "Contour_1", &ok);
        if (!ok || nm.isEmpty()) return;
        QString dir = m_currentPath.isEmpty()
                          ? QDir::homePath()
                          : QFileInfo(m_currentPath).absolutePath();
        QString path = dir + "/" + nm + ".cnt";
        m_editor->blockSignals(true);
        m_editor->setContourFile(path, nm);
        m_editor->blockSignals(false);
        saveCurrentBlock();
        QMetaObject::invokeMethod(m_editor, "openContourEditor",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, ""), Q_ARG(QString, nm));
    });

    // ── Signal "Éditer" fichier de points (.pnt) — réouvre l'éditeur
    //  multi-modèles avec le contenu existant, puis réenregistre.
    connect(m_editor, &UnitEditor::editPointFile, this, [this](const QString &path){
        if (m_currentIndex < 0 || m_currentIndex >= m_blocks.size()) return;
        BlockData &b = m_blocks[m_currentIndex];
        if (b.type == BlockType::Contour || b.type == BlockType::PocheContour || b.type == BlockType::BlkForm) return;
        if (path.trimmed().isEmpty()) return;
        if (b.type != BlockType::FraisageTrou) {
            // Perçages et poches : modifier le .pnt (partagé avec les
            // autres opérations qui l'utilisent)
            PointFile pf;
            if (!pf.load(path)) {
                QMessageBox::warning(this, "Fichier de points", "Impossible de lire : " + path);
                return;
            }
            PointFileEditorDialog dlg(this);
            dlg.setPointFile(pf);
            if (dlg.exec() != QDialog::Accepted) return;
            pf = dlg.pointFile();
            pf.filePath = path;
            if (!pf.save(path)) {
                QMessageBox::warning(this, "Fichier de points", "Impossible d'écrire : " + path);
                return;
            }
            m_editor->setPointFile(path);
            saveCurrentBlock();
            return;
        }

        PointFile pf;
        if (!pf.load(path)) {
            QMessageBox::warning(this, "Fichier de points", "Impossible de lire : " + path);
            return;
        }
        PointFileEditorDialog dlg(this);
        dlg.setPointFile(pf);
        if (dlg.exec() != QDialog::Accepted) return;
        pf = dlg.pointFile();
        pf.filePath = path;
        if (!pf.save(path)) {
            QMessageBox::warning(this, "Fichier de points", "Impossible d'écrire : " + path);
            return;
        }

        FraisageTrouData ftd = b.data.isEmpty() ? FraisageTrouData{} : fraisageTrouFromJson(b.data);
        ftd.fichierPoints = path;
        b.data = fraisageTrouToJson(ftd);
        clearDirectPositionFields(ftd);
        m_editor->loadFraisageTrou(ftd);
        saveCurrentBlock();
    });

    // dataChanged → save mémoire immédiat (fluide, zéro disque)
    // Signal DXF/STEP/IGES → ouvrir fichier + importer dans le ContourEditor actif
    connect(m_editor, &UnitEditor::openDxfImport, this, [this]{
        QString dir2 = m_currentPath.isEmpty() ? QDir::homePath() : QFileInfo(m_currentPath).absolutePath();
        QString path = QFileDialog::getOpenFileName(this,
                                                    "Importer fichier CAD", dir2,
                                                    "Fichiers CAD (*.dxf *.DXF *.step *.stp *.iges *.igs)"
                                                    ";;DXF (*.dxf);;STEP (*.step *.stp);;IGES (*.iges *.igs);;Tous (*)");
        if (path.isEmpty()) return;
        importCadFile(path);
    });
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(30);
    connect(m_saveTimer, &QTimer::timeout, this, &ProgrammePage::saveCurrentBlock);
    connect(m_editor, &UnitEditor::dataChanged,   this, [this]{ m_saveTimer->start(); });
    connect(m_editor, &UnitEditor::saveRequested, this, [this]{ m_saveTimer->start(); });
    // helpRequested → afficher le dessin d'aide
    connect(m_editor, &UnitEditor::helpRequested, this, &ProgrammePage::showHelp);

    // ── Colonne 3 : Cadre d'aide (dessins contextuels SVG) ──
    auto *helpPanel = new QWidget;
    helpPanel->setStyleSheet("QWidget {background:#EBEBEB;border-left:1px solid #808080;}");
    helpPanel->setMinimumWidth(200);  // minimum raisonnable, le stretch fait le reste
    auto *helpPanelLay = new QVBoxLayout(helpPanel);
    helpPanelLay->setContentsMargins(0,0,0,0); helpPanelLay->setSpacing(0);

    auto *helpHdr = new QLabel("  AIDE");
    helpHdr->setFixedHeight(36);
    helpHdr->setStyleSheet("QLabel{background:#EAEAEA;color:#404040;font-weight:bold;"
                           "font-size:6pt;font-weight:700;letter-spacing:3px;}");
    helpPanelLay->addWidget(helpHdr);

    m_helpFrame = new QFrame;
    m_helpFrame->setStyleSheet("QFrame{background:#EBEBEB;border:none;}");
    auto *helpLay = new QVBoxLayout(m_helpFrame);
    helpLay->setContentsMargins(8,8,8,8);
    helpLay->setSpacing(4);

    // Widget schéma pièce (affiché pour BlkForm)
    m_brutPreview = new BrutPreviewWidget;
    m_brutPreview->setVisible(false);
    helpLay->addWidget(m_brutPreview, 1);

    // Label image — prend tout l'espace disponible
    m_helpLabel = new QLabel;
    m_helpLabel->setAlignment(Qt::AlignCenter);
    m_helpLabel->setStyleSheet("background:transparent;");
    m_helpLabel->setWordWrap(true);
    m_helpLabel->setText("<span style='color:#808080;font-size:9pt;'>"
                         "Sélectionner un paramètre<br>pour voir l'aide</span>");
    helpLay->addWidget(m_helpLabel, 1);  // stretch=1 pour prendre la place

    // Dessin d'aide animé (remplace l'image fixe quand la clé a une animation)
    m_helpAnim = new HelpAnimWidget;
    m_helpAnim->setVisible(false);
    helpLay->addWidget(m_helpAnim, 1);

    // Label texte sous l'image
    m_helpTextLabel = new QLabel;
    m_helpTextLabel->setAlignment(Qt::AlignCenter);
    m_helpTextLabel->setWordWrap(true);
    m_helpTextLabel->setStyleSheet("color:#404040;font-size:10pt;font-weight:bold;background:transparent;");
    m_helpTextLabel->setVisible(false);
    helpLay->addWidget(m_helpTextLabel, 0);

    helpPanelLay->addWidget(m_helpFrame, 1);

    // Mettre à jour le schéma quand le BlkForm change
    if (m_blkForm) {
        connect(m_blkForm, &BlkFormWidget::dataChanged, this, [this]{
            if (m_brutPreview)
                m_brutPreview->setDimensions(
                    m_blkForm->xMinus(), m_blkForm->xPlus(),
                    m_blkForm->yMinus(), m_blkForm->yPlus(),
                    m_blkForm->zMinus(), m_blkForm->zPlus());
        });
        // Highlight de la face correspondant au champ qui a le focus
        connect(m_blkForm, &BlkFormWidget::axisFieldFocused,
                this, [this](int face){
                    if (!m_brutPreview) return;
                    using H = BrutPreviewWidget::HighlightFace;
                    static const H tbl[7] = {H::NoHighlight, H::Xmin, H::Xmax,
                                             H::Ymin, H::Ymax, H::Zmin, H::Zmax};
                    if (face >= 0 && face <= 6) m_brutPreview->setHighlight(tbl[face]);
                });
    }

    // ── Assemblage splitter 3 colonnes : liste | éditeur | aide ──
    // ── 3 colonnes figées à 1/3 chacune (non redimensionnables) ──
    // On abandonne le splitter au profit d\'un simple QHBoxLayout avec stretch=1
    // pour chaque colonne → largeur identique et aucun handle de redimensionnement.
    splitter->deleteLater();

    auto *cols = new QHBoxLayout;
    cols->setContentsMargins(0,0,0,0);
    cols->setSpacing(0);
    cols->addWidget(leftPanel,    3);  // colonne 1 : liste blocs ~400px
    cols->addWidget(m_rightStack, 5);  // colonne 2 : éditeur ~700px
    cols->addWidget(helpPanel,    4);  // colonne 3 : aide ~560px
    lay->addLayout(cols);
}

// ─────────────────────────────────────────────────────────────
//  showHelp — affiche le dessin SVG correspondant au paramètre
//  svgKey : nom de base sans extension (ex: "surep_lat")
//           cherché dans :/help/help_<key>.svg
// ─────────────────────────────────────────────────────────────
void ProgrammePage::showHelp(const QString &svgKey)
{
    if (!m_helpLabel || !m_helpFrame) return;

    static const QHash<QString,QString> tips = {
        {"longueur_largeur", "Longueur x Largeur de la poche (mm)"},
        {"diametre",         "Diametre de la poche (mm)"},
        {"profondeur",       "Profondeur totale depuis Z0 (mm)"},
        {"prof_passe",       "Profondeur par passe (mm)"},
        {"surep_lat",        "Surcote laterale pour la finition (mm)"},
        {"surep_fond",       "Surcote fond pour la finition (mm)"},
        {"surep_fin",        "Surep. finition (mm) : + laisse de la matiere, - enleve plus (correction de cote)"},
        {"rayon_angle",      "Rayon de coin (mm) - 0 = angle vif"},
        {"recouvrement",     "Recouvrement entre passes (%)"},
        {"mode_plongee",     "Mode de plongee : Helice / Rampe / Directe"},
        {"approche",         "Distance d approche au-dessus de Z0 (mm)"},
        {"mode_finition",    "Finition : passe au diametre nominal"},
        {"mode_ebauche",     "Ebauche : helice avec surep."},
        {"mode_eb_fin",      "Eb+Fin : ebauche puis finition"},
        // Motifs de positions (softkeys de l'onglet Position)
        {"sk_point",   "Point : une seule position X / Y"},
        {"sk_ligne",   "Ligne : positions regulierement espacees entre P1 et P2"},
        {"sk_grille",  "Grille : X/Y de depart, pas X / pas Y, nb de colonnes et de lignes"},
        {"sk_cercle",  "Cercle : centre, rayon, nb de positions, angles de depart et de fin"},
        {"sk_tableau", "Tableau : liste de positions saisies une par une"},
        // Dessins animés
        {"plongee_0",  "Helicoidale : descente en helice, la plus douce pour l'outil"},
        {"plongee_1",  "Rampe : descente en aller-retour selon l'angle de rampe de l'outil"},
        {"plongee_2",  "Directe : descente verticale, avance de plongee reduite"},
        {"fraisage_0", "En avalant : meilleur etat de surface, a privilegier"},
        {"fraisage_1", "En opposition : utile sur croute dure ou machine avec jeu"},
    };
    static const QHash<QString,QString> pngPaths = {
        {"mode_finition",  ":/unit/finition.png"},
        {"mode_ebauche",   ":/unit/ebauche.png"},
        {"mode_eb_fin",    ":/unit/ebauche finition.png"},
    };

    // Cacher l'animation si elle était affichée
    auto hideAnim = [this]() {
        if (m_helpAnim && m_helpAnim->isVisible()) {
            m_helpAnim->stop();
            m_helpAnim->setVisible(false);
            m_helpLabel->setVisible(true);
        }
    };

    // Helper : afficher image et/ou texte
    auto display = [this, hideAnim](const QPixmap &px, const QString &txt) {
        hideAnim();
        if (!px.isNull()) {
            int fw = m_helpLabel->width()  > 20 ? m_helpLabel->width()  : (m_helpFrame->width()  > 20 ? m_helpFrame->width()  - 16 : 380);
            int fh = m_helpLabel->height() > 20 ? m_helpLabel->height() : (m_helpFrame->height() > 20 ? m_helpFrame->height() - 60 : 280);
            m_helpLabel->setPixmap(px.scaled(fw, fh, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_helpLabel->setText(QString());
        } else {
            m_helpLabel->setPixmap(QPixmap());
            m_helpLabel->setText(!txt.isEmpty()
                ? QString("<span style='color:#404040;font-size:10pt;'>%1</span>").arg(txt)
                : "<span style='color:#808080;font-size:9pt;'>Selectionner un parametre<br>pour voir l'aide</span>");
        }
        if (m_helpTextLabel) {
            bool showTxt = !px.isNull() && !txt.isEmpty();
            m_helpTextLabel->setVisible(showTxt);
            if (showTxt) m_helpTextLabel->setText(txt);
        }
    };

    // Cle vide -> reset (sauf BlkForm)
    if (svgKey.isEmpty()) {
        const bool isBlkForm = (m_currentIndex >= 0 &&
                                m_currentIndex < m_blocks.size() &&
                                m_blocks[m_currentIndex].type == BlockType::BlkForm);
        if (isBlkForm) return;
        display(QPixmap(), QString());
        return;
    }

    // Dessin animé (pas sur la page BLK FORM qui affiche le schéma de la pièce)
    // Variante selon le cycle : poche circulaire -> dessins ronds
    QString animKey = svgKey;
    if (m_currentIndex >= 0 && m_currentIndex < m_blocks.size()
        && m_blocks[m_currentIndex].type == BlockType::PocheCirc) {
        if (animKey == "longueur_largeur") animKey = "diametre";
        // variante ronde si elle existe (surep_lat_circ, prof_passe_circ...)
        if (HelpAnimWidget::hasScene(animKey + "_circ")) animKey += "_circ";
    }
    // Poche contour : les champs du contournage montrent les dessins de poche
    if (m_currentIndex >= 0 && m_currentIndex < m_blocks.size()
        && m_blocks[m_currentIndex].type == BlockType::PocheContour) {
        static const QHash<QString,QString> versPoche = {
            {"cnt_profondeur", "profondeur"}, {"cnt_passe", "prof_passe"},
            {"cnt_surep_lat", "surep_lat"},   {"cnt_passe_fin", "surep_lat"} };
        animKey = versPoche.value(animKey, animKey);
    }
    if (m_helpAnim && HelpAnimWidget::hasScene(animKey)
        && !(m_brutPreview && m_brutPreview->isVisible())) {
        m_helpLabel->setVisible(false);
        m_helpAnim->setVisible(true);
        m_helpAnim->setScene(animKey);
        if (m_helpTextLabel) {
            const QString txt = tips.value(svgKey);
            m_helpTextLabel->setVisible(!txt.isEmpty());
            m_helpTextLabel->setText(txt);
        }
        return;
    }

    // Prefixe _png_ -> PNG par chemin direct
    if (svgKey.startsWith("_png_")) {
        display(QPixmap(svgKey.mid(5)), "");
        return;
    }

    // SVG d'aide
    const QString resPath = QString(":/help/icons/help_%1.svg").arg(svgKey);
    if (QFile::exists(resPath)) {
        QSvgRenderer renderer(resPath);
        if (renderer.isValid()) {
            int fw = m_helpFrame->width()  > 50 ? m_helpFrame->width()  - 24 : 400;
            int fh = m_helpFrame->height() > 50 ? m_helpFrame->height() - 80 : 300;
            QSize sz = renderer.defaultSize().isEmpty()
                       ? QSize(fw, fh)
                       : renderer.defaultSize().scaled(fw, fh, Qt::KeepAspectRatio);
            QPixmap px(sz); px.fill(Qt::transparent);
            QPainter p(&px);
            p.setRenderHint(QPainter::Antialiasing);
            renderer.render(&p); p.end();
            display(px, tips.value(svgKey, ""));
            return;
        }
    }

    // PNG par cle
    if (pngPaths.contains(svgKey)) {
        QPixmap px(pngPaths.value(svgKey));
        qDebug() << "[showHelp] PNG key=" << svgKey << "path=" << pngPaths.value(svgKey) << "null=" << px.isNull() << "size=" << px.size();
        display(px, tips.value(svgKey, ""));
        return;
    }

    // Texte seul
    const QString tip = tips.value(svgKey, QString(svgKey).replace("_"," "));
    display(QPixmap(), tip);
}

void ProgrammePage::newProgram()
{
    m_blocks.clear(); m_currentIndex=-1; m_nextId=1;
    m_currentPath.clear(); m_selectionMode=false; m_selectedRows.clear();
    BlockData blk; blk.id=m_nextId++; blk.type=BlockType::BlkForm; blk.name="BlkForm";
    m_blocks.append(blk);
    refreshList(); m_list->setCurrentRow(0);
}

// ─────────────────────────────────────────────────────────────
//  saveCurrentBlock — MÉMOIRE SEULEMENT (pas d'écriture disque)
//  1. Sérialise le bloc courant en mémoire
//  2. Met à jour ExecutionPage via gCodeReady
// ─────────────────────────────────────────────────────────────
void ProgrammePage::saveCurrentBlock()
{
    if (m_savingBlock) return;
    m_savingBlock = true;
    struct Guard { bool &b; ~Guard(){ b=false; } } g{m_savingBlock};

    qDebug() << "[saveCurrentBlock] index=" << m_currentIndex << "blocks.size=" << m_blocks.size();
    if (m_currentIndex < 0 || m_currentIndex >= m_blocks.size()) {
        qDebug() << "[saveCurrentBlock] SKIP index hors bornes";
        return;
    }
    BlockData &b = m_blocks[m_currentIndex];
    qDebug() << "[saveCurrentBlock] bloc id=" << b.id << "type=" << (int)b.type
             << "m_editor=" << (m_editor ? "ok" : "null")
             << "stackIdx=" << (m_rightStack ? m_rightStack->currentIndex() : -99);

    // ── 1. Sérialiser en mémoire ──────────────────────────────
    if (b.type == BlockType::BlkForm) {
        if (m_blkForm) {
            QJsonObject o;
            // Onglet 1 : Dimensions
            o["xMinus"]=m_blkForm->xMinus(); o["xPlus"]=m_blkForm->xPlus();
            o["yMinus"]=m_blkForm->yMinus(); o["yPlus"]=m_blkForm->yPlus();
            o["zMinus"]=m_blkForm->zMinus(); o["zPlus"]=m_blkForm->zPlus();
            // Onglet 2 : Paramètres généraux (hérités par les blocs)
            o["origineDefinie"]= m_blkForm->origineDefinie();
            o["origine"]       = m_blkForm->origine();
            o["zSautBride"]    = m_blkForm->zSautBride();
            o["zLiaison"]      = m_blkForm->zLiaison();
            o["zApproche"]     = m_blkForm->zApproche();
            o["avanceLiaison"] = m_blkForm->avanceLiaison();
            o["m25RetourOrigine"] = m_blkForm->m25RetourOrigine();
            b.data = o;
        }
    } else if (b.type == BlockType::Trou
               || b.type == BlockType::Percage
               || b.type == BlockType::PercageDebourr
               || b.type == BlockType::Pointage
               || b.type == BlockType::Taraudage
               || b.type == BlockType::Alesage) {
        if (m_editor && m_rightStack && m_rightStack->currentIndex() == 1) {
            TrouData td = m_editor->saveTrou();
            b.data = trouToJson(td);
        }
    } else if (b.type == BlockType::FraisageTrou) {
        if (m_editor && m_rightStack && m_rightStack->currentIndex() == 1) {
            FraisageTrouData ftd = m_editor->saveFraisageTrou();
            b.data = fraisageTrouToJson(ftd);
        }
    } else if (b.type == BlockType::Contour
               || b.type == BlockType::PocheContour
               || b.type == BlockType::Surfacage) {
        if (m_editor && m_rightStack && m_rightStack->currentIndex() == 1) {
            ContourData cd = m_editor->saveContour();
            b.data = contourToJson(cd);
            if (!cd.contourName.isEmpty())
                b.name = cd.contourName;
        }
    } else {
        if (m_editor && m_rightStack && m_rightStack->currentIndex() == 1) {
            PocheData pd = m_editor->save();
            b.data = pocheToJson(pd);
        }
    }

    // Mettre à jour le label (dimensions) — toujours, même si data inchangé
    if (m_currentIndex >= 0 && m_currentIndex < m_list->count()) {
        if (auto *item = m_list->item(m_currentIndex))
            item->setText(labelForBlock(m_blocks[m_currentIndex], m_currentIndex + 1));
    }

    // G-code régénéré seulement via saveFiles() (bouton Enr enc)
}

// ─────────────────────────────────────────────────────────────
//  loadEncFile
// ─────────────────────────────────────────────────────────────
void ProgrammePage::loadEncFile(const QString &filePath)
{
    CrashHandler::log(QString("loadEncFile: %1").arg(filePath));
    qDebug() << "[loadEncFile] path=" << filePath;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "[loadEncFile] ERREUR ouverture fichier";
        newProgram(); return;
    }
    QByteArray raw = f.readAll(); f.close();
    m_currentPath = filePath;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug() << "[loadEncFile] ERREUR JSON:" << err.errorString();
        newProgram(); return;
    }

    QJsonObject root   = doc.object();
    QJsonArray  blocks = root.value("blocks").toArray();
    m_blocks.clear(); m_currentIndex=-1; m_nextId=1;
    m_selectionMode=false; m_selectedRows.clear();

    bool hasBlkForm = false;
    for (const QJsonValue &v : std::as_const(blocks)) {
        QJsonObject obj = v.toObject();
        BlockData b;
        b.id   = obj.value("id").toInt(m_nextId);
        b.name = obj.value("name").toString();
        b.data = obj.value("data").toObject();
        QString t = obj.value("type").toString();
        if      (t=="BlkForm")       { b.type=BlockType::BlkForm; hasBlkForm=true; }
        else if (t=="PocheRect")     b.type=BlockType::PocheRect;
        else if (t=="PocheCirc")     b.type=BlockType::PocheCirc;
        else if (t=="Poche")         b.type=BlockType::PocheRect;   // rétrocompat
        else if (t=="Percage")       b.type=BlockType::Percage;
        else if (t=="PercageDebourr")b.type=BlockType::PercageDebourr;
        else if (t=="Pointage")      b.type=BlockType::Pointage;
        else if (t=="Taraudage")     b.type=BlockType::Taraudage;
        else if (t=="Alesage")       b.type=BlockType::Alesage;
        else if (t=="Trou")          b.type=BlockType::Percage;     // rétrocompat
        else if (t=="Contour")       b.type=BlockType::Contour;
        else if (t=="PocheContour")  b.type=BlockType::PocheContour;
        else if (t=="FraisageTrou")  b.type=BlockType::FraisageTrou;
        else if (t=="Surfacage")     b.type=BlockType::Surfacage;
        else if (t=="Rainure")       b.type=BlockType::Rainure;
        else if (t=="RainureCirc")   b.type=BlockType::RainureCirc;
        else if (t=="TenonRect")     b.type=BlockType::TenonRect;
        else if (t=="TenonCirc")     b.type=BlockType::TenonCirc;
        else                         { b.type=BlockType::BlkForm; hasBlkForm=true; }
        m_nextId = qMax(m_nextId, b.id+1);
        m_blocks.append(b);
    }

    if (!hasBlkForm) {
        BlockData blk; blk.id=m_nextId++; blk.type=BlockType::BlkForm; blk.name="BlkForm";
        m_blocks.prepend(blk);
    }

    refreshList();
    if (!m_blocks.isEmpty()) m_list->setCurrentRow(0);
    // ExecutionPage mise à jour seulement via bouton Enr enc.
    // La simulation, elle, reçoit le programme tout de suite.
    {
        const QString baseName = QFileInfo(m_currentPath).baseName();
        emit simGCodeReady(m_blocks, buildGCode(m_blocks, baseName.isEmpty() ? "Programme" : baseName),
                           QFileInfo(m_currentPath).fileName());
    }
}

// ─────────────────────────────────────────────────────────────
//  saveEncFile
//  NE PAS appeler saveCurrentBlock() ici — évite la récursion

// ─────────────────────────────────────────────────────────────
//  saveFiles — sauvegarde .enc + .nc sur disque avec overlay
//  Appelé depuis MainWindow lors du changement de page
// ─────────────────────────────────────────────────────────────
void ProgrammePage::saveFiles()
{
    // Sérialiser le bloc actif si valide
    if (m_currentIndex >= 0 && m_currentIndex < m_blocks.size())
        saveCurrentBlock();

    // Afficher l'overlay "Sauvegarde..."
    if (!m_saveOverlay) {
        m_saveOverlay = new QLabel(this);
        m_saveOverlay->setAlignment(Qt::AlignCenter);
        m_saveOverlay->setStyleSheet(
            "QLabel{"
            "  background:rgba(14,18,22,0.88);"
            "  color:#000000;"
            "  font-family:'IBM Plex Mono',monospace;"
            "  font-size:10pt; font-weight:700;"
            "  border:1px solid #3a8f34;"
            "  border-radius:6px;"
            "  padding:8px 20px;"
            "}");
        m_saveOverlay->setFixedSize(200, 44);
    }
    // Centrer dans la page
    m_saveOverlay->move((width()-200)/2, (height()-44)/2);
    m_saveOverlay->setText("  Sauvegarde...");
    m_saveOverlay->show();
    m_saveOverlay->raise();

    // Écriture disque — si pas de chemin, ouvre dialogue "Enregistrer sous"
    saveEncFile(m_currentPath);

    // Masquer après 1.2s
    QTimer::singleShot(1200, this, [this](){
        if (m_saveOverlay) m_saveOverlay->hide();
    });
}

// ─────────────────────────────────────────────────────────────
bool ProgrammePage::saveEncFile(const QString &filePath)
{
    // NE PAS appeler saveCurrentBlock() ici — évite la récursion via gCodeReady
    QString path = filePath.isEmpty() ? m_currentPath : filePath;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, "Enregistrer le programme",
            QString(), "Programmes EazyNC (*.enc);;Tous les fichiers (*)");
        if (path.isEmpty()) return false;
        if (!path.endsWith(".enc", Qt::CaseInsensitive)) path += ".enc";
    }

    QJsonArray arr;
    for (const BlockData &b : std::as_const(m_blocks)) {
        QJsonObject obj;
        obj["id"]   = b.id;
        obj["name"] = b.name;
        obj["data"] = b.data;
        switch (b.type) {
        case BlockType::BlkForm:        obj["type"]="BlkForm";       break;
        case BlockType::PocheRect:      obj["type"]="PocheRect";     break;
        case BlockType::PocheCirc:      obj["type"]="PocheCirc";     break;
        case BlockType::Poche:          obj["type"]="PocheRect";     break;
        case BlockType::Percage:        obj["type"]="Percage";       break;
        case BlockType::PercageDebourr: obj["type"]="PercageDebourr";break;
        case BlockType::Pointage:       obj["type"]="Pointage";      break;
        case BlockType::Taraudage:      obj["type"]="Taraudage";     break;
        case BlockType::Alesage:        obj["type"]="Alesage";       break;
        case BlockType::Trou:           obj["type"]="Percage";       break;
        case BlockType::Contour:        obj["type"]="Contour";       break;
        case BlockType::PocheContour:   obj["type"]="PocheContour";  break;
        case BlockType::FraisageTrou:   obj["type"]="FraisageTrou";  break;
        case BlockType::Surfacage:      obj["type"]="Surfacage";     break;
        case BlockType::Rainure:        obj["type"]="Rainure";       break;
        case BlockType::RainureCirc:    obj["type"]="RainureCirc";   break;
        case BlockType::TenonRect:      obj["type"]="TenonRect";     break;
        case BlockType::TenonCirc:      obj["type"]="TenonCirc";     break;
        }
        arr.append(obj);
    }
    QJsonObject root;
    root["version"] = 1;
    root["blocks"]  = arr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    m_currentPath = path;
    qDebug() << "[EazyNC] .enc écrit :" << path;

    // Générer le G-code UNE SEULE FOIS sur le thread principal
    const QString baseName = QFileInfo(m_currentPath).baseName();
    QVector<GCodeLine> generatedLines = buildGCode(m_blocks, baseName);
    const QString pathCopy = m_currentPath;
    const QVector<BlockData> blocksCopy = m_blocks;   // pour les sous-fichiers

    // Écrire le .nc en arrière-plan (évite freeze UI)
    // Le token évite que deux sauvegardes rapides écrivent dans le mauvais ordre.
    const int myToken = s_ncWriteToken.fetchAndAddRelaxed(1) + 1;
    QThreadPool::globalInstance()->start([generatedLines, pathCopy, blocksCopy, myToken](){
        if (s_ncWriteToken.loadRelaxed() != myToken) {
            qDebug() << "[EazyNC] writeNc annulé (double sauvegarde), token=" << myToken;
            return;
        }
        writeNcFile(generatedLines, pathCopy);
        // Écrire les sous-fichiers .cnc des contours dans le même dossier
        writeContourSubFiles(blocksCopy, pathCopy);
    });

    // Mettre à jour ExecutionPage avec le même G-code
    const QString name = QFileInfo(m_currentPath).fileName();
    emit gCodeReady(m_blocks, generatedLines, name);

    // ── Overlay confirmation ─────────────────────────────────
    if (!m_saveOverlay) {
        m_saveOverlay = new QLabel(this);
        m_saveOverlay->setAlignment(Qt::AlignCenter);
        m_saveOverlay->setStyleSheet(
            "QLabel{"
            "  background:rgba(14,18,22,0.92);"
            "  color:#000000;"
            "  font-family:'IBM Plex Mono',monospace;"
            "  font-size:10pt; font-weight:700;"
            "  border:1px solid #3a8f34;"
            "  border-radius:6px;"
            "  padding:8px 20px;"
            "}");
    }
    const int nb = m_blocks.size();
    const QString msg = QString("  ✓  %1 enregistre  ·  %2 bloc%3")
                            .arg(QFileInfo(m_currentPath).fileName())
                            .arg(nb).arg(nb > 1 ? QLatin1String("s") : QLatin1String(""));
    m_saveOverlay->setText(msg);
    m_saveOverlay->adjustSize();
    m_saveOverlay->setFixedHeight(44);
    m_saveOverlay->move((width() - m_saveOverlay->width()) / 2,
                        (height() - 44) / 2);
    m_saveOverlay->show();
    m_saveOverlay->raise();
    QTimer::singleShot(2000, this, [this](){
        if (m_saveOverlay) m_saveOverlay->hide();
    });

    return true;
}

// ─────────────────────────────────────────────────────────────
//  onBlockSelected
// ─────────────────────────────────────────────────────────────
void ProgrammePage::onBlockSelected(int index)
{
    saveCurrentBlock();  // sauvegarder le bloc courant avant de changer
    if (index < 0 || index >= m_blocks.size()) return;
    m_currentIndex = index;
    if (m_selectionMode) setSelRangeTo(index);   // la selection suit le curseur (clic ou fleches)
    showBlockInEditor(index);
    updateSelectionHighlight();
    // Le clic de la liste inverse ensuite l'etat de la ligne : on repasse apres lui
    if (m_selectionMode) QTimer::singleShot(0, this, [this]{ updateSelectionHighlight(); });
}

// ─────────────────────────────────────────────────────────────
//  showBlockInEditor
// ─────────────────────────────────────────────────────────────
void ProgrammePage::showBlockInEditor(int index)
{
    CrashHandler::log(QString("showBlockInEditor index=%1 type=%2")
                          .arg(index).arg((int)m_blocks.value(index).type));
    if (index < 0 || index >= m_blocks.size()) return;
    const BlockData &b = m_blocks[index];

    // Par défaut : cacher le schéma 3D (réaffiché seulement pour BlkForm)
    if (m_brutPreview) m_brutPreview->setVisible(false);
    if (m_helpAnim)    { m_helpAnim->stop(); m_helpAnim->setVisible(false); }
    if (m_helpLabel)   m_helpLabel->setVisible(true);

    // Transmettre les valeurs héritées du BLK FORM à l\'UnitEditor
    // (pour que les badges G soient correctement colorés et que les
    //  champs affichent la valeur héritée quand le flag local=false)
    if (m_editor && m_blkForm) {
        m_editor->setBlkFormHerited(
            m_blkForm->origineDefinie(),
            m_blkForm->origine(),
            m_blkForm->zSautBride(),
            m_blkForm->zLiaison(),
            m_blkForm->zApproche(),
            m_blkForm->avanceLiaison());
    }

    if (b.type == BlockType::BlkForm) {
        CrashHandler::log("BlkForm: setCurrentIndex(0)...");
        m_rightStack->setCurrentIndex(0);
        CrashHandler::log("BlkForm: setCurrentIndex OK");
        if (!b.data.isEmpty()) {
            CrashHandler::log("BlkForm: setValues...");
            // Copie locale + signaux bloques : sinon le 1er setValue declenche
            // saveCurrentBlock qui ecrase Z approche / saut de bride / liaison
            // du fichier avec les valeurs encore affichees (programme precedent).
            const QJsonObject bd = b.data;
            const QSignalBlocker blkSignals(m_blkForm);
            m_blkForm->setValues(
                bd["xMinus"].toDouble(-5),  bd["xPlus"].toDouble(200),
                bd["yMinus"].toDouble(-5),  bd["yPlus"].toDouble(150),
                bd["zMinus"].toDouble(-50), bd["zPlus"].toDouble(0));
            // Paramètres généraux (avec valeurs par défaut si champ absent)
            m_blkForm->setParamsGlobaux(
                bd["origineDefinie"].toBool(false),
                bd["origine"].toInt(0),
                bd["zSautBride"].toDouble(50.0),
                bd["zLiaison"].toDouble(5.0),
                bd["zApproche"].toDouble(2.0),
                bd["avanceLiaison"].toDouble(0.0),
                bd["m25RetourOrigine"].toBool(true));
            CrashHandler::log("BlkForm: setValues OK");
        }
        // Afficher le schéma 3D de la pièce dans la colonne aide
        if (m_brutPreview) {
            m_brutPreview->setDimensions(
                m_blkForm->xMinus(), m_blkForm->xPlus(),
                m_blkForm->yMinus(), m_blkForm->yPlus(),
                m_blkForm->zMinus(), m_blkForm->zPlus());
            m_brutPreview->setVisible(true);
            if (m_helpLabel) m_helpLabel->setVisible(false);
        }
    } else if (b.type == BlockType::Contour || b.type == BlockType::PocheContour) {
        CrashHandler::log("Contour: setCurrentIndex(1)...");
        m_rightStack->setCurrentIndex(1);
        CrashHandler::log("Contour: setCurrentIndex OK");
        CrashHandler::log("Contour: load ContourData...");
        ContourData d = b.data.isEmpty() ? ContourData{} : contourFromJson(b.data);
        d.typeCycle = (b.type == BlockType::PocheContour) ? 1 : 0;
        m_editor->load(d);
        CrashHandler::log("Contour: load OK");
        // Peupler b.data si vide (nouveau bloc)
        if (b.data.isEmpty() && m_currentIndex >= 0 && m_currentIndex < m_blocks.size()) {
            m_blocks[m_currentIndex].data = contourToJson(m_editor->saveContour());
            if (m_currentIndex < m_list->count())
                if (auto *item = m_list->item(m_currentIndex))
                    item->setText(labelForBlock(m_blocks[m_currentIndex], m_currentIndex+1));
        }
    } else {
        CrashHandler::log(QString("Poche/Trou: setCurrentIndex(1) type=%1...").arg((int)b.type));
        m_rightStack->setCurrentIndex(1);
        CrashHandler::log("Poche/Trou: setCurrentIndex OK");
        // Types perçage → TrouData
        const bool isTrouType = (b.type==BlockType::Trou
                                 || b.type==BlockType::Percage
                                 || b.type==BlockType::PercageDebourr
                                 || b.type==BlockType::Pointage
                                 || b.type==BlockType::Taraudage
                                 || b.type==BlockType::Alesage);
        const bool isFraisageTrou = (b.type == BlockType::FraisageTrou);
        if (isFraisageTrou) {
            FraisageTrouData ftd = b.data.isEmpty() ? FraisageTrouData{} : fraisageTrouFromJson(b.data);
            m_editor->setBlockMode(UnitEditor::FraisageTrou);
            m_editor->loadFraisageTrou(ftd);
        } else if (isTrouType) {
            CrashHandler::log("Percage/Trou: load...");
            TrouData d = b.data.isEmpty() ? TrouData{} : trouFromJson(b.data);
            // Forcer le cycle selon le type
            if (b.type==BlockType::PercageDebourr) d.cycle = 1;
            else if (b.type==BlockType::Taraudage)  d.cycle = 3;
            else if (b.type==BlockType::Alesage)    d.cycle = 4;
            else if (b.type==BlockType::Pointage)   d.cycle = 5;
            m_editor->setBlockMode(UnitEditor::Trou);
            m_editor->load(d);
            CrashHandler::log("Percage: load OK");
        } else {
            // Types poche/fraisage → PocheData
            CrashHandler::log("Poche/fraisage: load...");
            PocheData d = b.data.isEmpty() ? PocheData{} : pocheFromJson(b.data);
            // Forcer isCirculaire selon le type
            if (b.type==BlockType::PocheCirc || b.type==BlockType::RainureCirc
                || b.type==BlockType::TenonCirc) d.isCirculaire = true;
            else if (b.type==BlockType::PocheRect || b.type==BlockType::Rainure
                     || b.type==BlockType::TenonRect) d.isCirculaire = false;
            // setBlockMode PocheRect ou PocheCirc : cache le radio, fixe la visibilité
            const UnitEditor::BlockMode bm =
                (b.type==BlockType::PocheCirc) ? UnitEditor::PocheCirc : UnitEditor::PocheRect;
            m_editor->setBlockMode(bm);
            m_editor->load(d);
            CrashHandler::log("Poche: load OK");
        }
        // Toujours forcer une sauvegarde immédiate après load :
        // - peuple b.data si vide (nouveau bloc)
        // - garantit que le label affiche les dimensions correctes
        if (m_currentIndex >= 0 && m_currentIndex < m_blocks.size()) {
            BlockData &bw = m_blocks[m_currentIndex];
            const bool isTrouType2 = (bw.type==BlockType::Trou
                                      || bw.type==BlockType::Percage
                                      || bw.type==BlockType::PercageDebourr
                                      || bw.type==BlockType::Pointage
                                      || bw.type==BlockType::Taraudage
                                      || bw.type==BlockType::Alesage);
            const bool isFraisageTrou2 = (bw.type == BlockType::FraisageTrou);
            if (isFraisageTrou2)
                bw.data = fraisageTrouToJson(m_editor->saveFraisageTrou());
            else if (isTrouType2)
                bw.data = trouToJson(m_editor->saveTrou());
            else
                bw.data = pocheToJson(m_editor->save());
            if (m_currentIndex < m_list->count())
                if (auto *item = m_list->item(m_currentIndex))
                    item->setText(labelForBlock(bw, m_currentIndex + 1));
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  emitGCodeReady
// ─────────────────────────────────────────────────────────────
void ProgrammePage::emitGCodeReady()
{
    const QString baseName = m_currentPath.isEmpty()
    ? "Programme"
    : QFileInfo(m_currentPath).baseName();

    QVector<GCodeLine> lines = buildGCode(m_blocks, baseName);

    const QString name = m_currentPath.isEmpty()
                             ? "Programme"
                             : QFileInfo(m_currentPath).fileName();
    emit gCodeReady(m_blocks, lines, name);
}

// ─────────────────────────────────────────────────────────────
//  Helpers liste
// ─────────────────────────────────────────────────────────────
QString ProgrammePage::labelForBlock(const BlockData &b, int pos) const
{
    // pos = numéro d'ordre dans la liste (1-based), toujours à jour
    const QString num = (pos >= 0) ? QString("[%1]").arg(pos) : QString("[?]");
    switch (b.type) {
    case BlockType::BlkForm: {
        if (!b.data.isEmpty()) {
            const QJsonObject &o = b.data;
            double sx = o["xPlus"].toDouble() - o["xMinus"].toDouble();
            double sy = o["yPlus"].toDouble() - o["yMinus"].toDouble();
            double sz = qAbs(o["zMinus"].toDouble());
            if (sx > 0 || sy > 0)
                return QString("%1  Matiere  %2\u00d7%3  ep%4")
                    .arg(num,
                         QString::number(sx,'f',1),
                         QString::number(sy,'f',1),
                         QString::number(sz,'f',1));
        }
        return QString("%1  Matiere").arg(num);
    }
    case BlockType::PocheRect:
    case BlockType::Poche: {
        QString dim;
        if (!b.data.isEmpty()) {
            const QJsonObject &o = b.data;
            dim = QString("  %1×%2  Z%3")
                      .arg(QString::number(o["longueur"].toDouble(),'f',1),
                           QString::number(o["largeur"].toDouble(),'f',1),
                           QString::number(o["profondeur"].toDouble(),'f',1));
        }
        return QString("%1  Poche rect.%2").arg(num, dim);
    }
    case BlockType::PocheCirc: {
        QString dim;
        if (!b.data.isEmpty()) {
            const QJsonObject &o = b.data;
            dim = QString("  Ø%1  Z%2")
                      .arg(o["diametre"].toDouble(),0,'f',1)
                      .arg(o["profondeur"].toDouble(),0,'f',1);
        }
        return QString("%1  Poche circ.%2").arg(num, dim);
    }
    case BlockType::FraisageTrou: {
        QString dim;
        if (!b.data.isEmpty())
            dim = QString("  Ø%1  Z%2")
                      .arg(b.data["diametreFini"].toDouble(),0,'f',1)
                      .arg(b.data["profondeur"].toDouble(),0,'f',1);
        return QString("%1  Fraisage trou%2").arg(num, dim);
    }
    case BlockType::Percage:
    case BlockType::Trou: {
        QString dim;
        if (!b.data.isEmpty()) {
            const QJsonObject &o = b.data;
            dim = QString("  Ø%1  Z%2")
                      .arg(QString::number(o["diametre"].toDouble(),'f',1),
                           QString::number(o["profondeur"].toDouble(),'f',1));
        }
        return QString("%1  Percage%2").arg(num, dim);
    }
    case BlockType::PercageDebourr: {
        QString dim;
        if (!b.data.isEmpty()) {
            const QJsonObject &o = b.data;
            dim = QString("  Ø%1  Z%2")
                      .arg(o["diametre"].toDouble(),0,'f',1)
                      .arg(o["profondeur"].toDouble(),0,'f',1);
        }
        return QString("%1  Perc. debourr.%2").arg(num, dim);
    }
    case BlockType::Pointage: {
        QString dim;
        if (!b.data.isEmpty())
            dim = QString("  Ø%1").arg(b.data["diametre"].toDouble(),0,'f',1);
        return QString("%1  Pointage%2").arg(num, dim);
    }
    case BlockType::Taraudage: {
        QString dim;
        if (!b.data.isEmpty())
            dim = QString("  Ø%1  Z%2")
                      .arg(b.data["diametre"].toDouble(),0,'f',1)
                      .arg(b.data["profondeur"].toDouble(),0,'f',1);
        return QString("%1  Taraudage%2").arg(num, dim);
    }
    case BlockType::Alesage: {
        QString dim;
        if (!b.data.isEmpty())
            dim = QString("  Ø%1  Z%2")
                      .arg(b.data["diametre"].toDouble(),0,'f',1)
                      .arg(b.data["profondeur"].toDouble(),0,'f',1);
        return QString("%1  Alesage%2").arg(num, dim);
    }
    case BlockType::Surfacage: {
        QString dim;
        if (!b.data.isEmpty())
            dim = QString("  %1×%2")
                      .arg(b.data["longueur"].toDouble(),0,'f',1)
                      .arg(b.data["largeur"].toDouble(),0,'f',1);
        return QString("%1  Surfacage%2").arg(num, dim);
    }
    case BlockType::Rainure: {
        QString dim;
        if (!b.data.isEmpty())
            dim = QString("  %1×%2  Z%3")
                      .arg(b.data["longueur"].toDouble(),0,'f',1)
                      .arg(b.data["largeur"].toDouble(),0,'f',1)
                      .arg(b.data["profondeur"].toDouble(),0,'f',1);
        return QString("%1  Rainure%2").arg(num, dim);
    }
    case BlockType::RainureCirc: {
        QString dim;
        if (!b.data.isEmpty())
            dim = QString("  Ø%1  Z%2")
                      .arg(b.data["diametre"].toDouble(),0,'f',1)
                      .arg(b.data["profondeur"].toDouble(),0,'f',1);
        return QString("%1  Rainure circ.%2").arg(num, dim);
    }
    case BlockType::TenonRect: {
        QString dim;
        if (!b.data.isEmpty())
            dim = QString("  %1×%2  Z%3")
                      .arg(b.data["longueur"].toDouble(),0,'f',1)
                      .arg(b.data["largeur"].toDouble(),0,'f',1)
                      .arg(b.data["profondeur"].toDouble(),0,'f',1);
        return QString("%1  Tenon rect.%2").arg(num, dim);
    }
    case BlockType::TenonCirc: {
        QString dim;
        if (!b.data.isEmpty())
            dim = QString("  Ø%1  Z%2")
                      .arg(b.data["diametre"].toDouble(),0,'f',1)
                      .arg(b.data["profondeur"].toDouble(),0,'f',1);
        return QString("%1  Tenon circ.%2").arg(num, dim);
    }
    case BlockType::Contour: {
        QString info;
        if (!b.data.isEmpty()) {
            QString name = b.data["contourName"].toString();
            QString file = b.data["contourFile"].toString();
            double prof  = b.data["profondeur"].toDouble();
            // Afficher le nom du fichier .cnt en priorité, sinon le nom du contour
            if (!file.isEmpty())
                info = QString("  %1").arg(QFileInfo(file).baseName());
            else if (!name.isEmpty())
                info = QString("  %1").arg(name);
            if (prof > 0)
                info += QString("  Z%1mm").arg(QString::number(prof,'f',1));
        }
        return QString("%1  Contour%2").arg(num).arg(info.isEmpty() ? " — définir contour" : info);
    }
    case BlockType::PocheContour: {
        QString info;
        if (!b.data.isEmpty()) {
            const QString file = b.data["contourFile"].toString();
            if (!file.isEmpty()) info = "  " + QFileInfo(file).baseName();
            int nIl = b.data["ilotFiles"].toArray().size();
            if (b.data.contains("pocList")) {
                nIl = 0;
                for (const auto &v : b.data["pocList"].toArray()) if (v.toObject()["ilot"].toBool()) ++nIl;
            }
            if (nIl > 0) info += QString("  +%1 îlot%2").arg(nIl).arg(nIl > 1 ? "s" : "");
            const double prof = b.data["profondeur"].toDouble();
            if (prof > 0) info += QString("  Z%1mm").arg(QString::number(prof,'f',1));
        }
        return QString("%1  Poche contour%2").arg(num).arg(info.isEmpty() ? " — définir contour" : info);
    }
    }
    return QString("%1  ?").arg(num);
}

void ProgrammePage::refreshList()
{
    const int savedRow = m_list->currentRow();
    m_list->blockSignals(true); m_list->clear();
    for (int i = 0; i < m_blocks.size(); ++i) {
        auto *item = new QListWidgetItem(blockIcon(m_blocks[i].type),
                                         labelForBlock(m_blocks[i], i + 1));
        item->setSizeHint(QSize(0,72));
        m_list->addItem(item);
    }
    m_list->blockSignals(false);
    if (m_list->count()>0) m_list->setCurrentRow(qBound(0,savedRow,m_list->count()-1));
    updateSelectionHighlight();
}

void ProgrammePage::updateSelectionHighlight()
{
    // Pendant la selection : plusieurs lignes surlignees en orange (facon TNC)
    static const QString base =
        "QListWidget{background:#FFFFFF;border:none;outline:none;}"
        "QListWidget::item{"
        "  border-bottom:1px solid #D0D0D0;"
        "  border-left:3px solid transparent;"
        "  padding:10px 14px; color:#000000;"
        "  font-size:10pt;}"
        "QListWidget::item:hover{background:#EBEBEB;}";
    const QString sel = m_selectionMode
        ? "QListWidget::item:selected{background:#FFD080;border-left:3px solid #E07000;color:#000000;}"
        : "QListWidget::item:selected{background:#D6E8FF;border-left:3px solid #0000FF;color:#0000FF;}";
    m_list->setStyleSheet(base + sel);
    m_list->blockSignals(true);
    if (m_selectionMode) {
        m_list->setSelectionMode(QAbstractItemView::MultiSelection);
        for (int i=0; i<m_list->count(); ++i)
            if (QListWidgetItem *it = m_list->item(i)) it->setSelected(m_selectedRows.contains(i));
    } else {
        m_list->setSelectionMode(QAbstractItemView::SingleSelection);
        for (int i=0; i<m_list->count(); ++i)
            if (QListWidgetItem *it = m_list->item(i)) it->setSelected(i == m_currentIndex);
    }
    m_list->blockSignals(false);
}

void ProgrammePage::addBlock(BlockData b)
{
    int pos = (m_currentIndex>=0) ? m_currentIndex+1 : m_blocks.size();
    pos = qMax(pos, 1);
    m_blocks.insert(pos, b); refreshList(); m_list->setCurrentRow(pos);
}

// ─── Insertions ──────────────────────────────────────────────
void ProgrammePage::insertBlkForm()
{
    for (int i=0; i<m_blocks.size(); ++i)
        if (m_blocks[i].type==BlockType::BlkForm) { m_list->setCurrentRow(i); return; }
    BlockData b; b.id=m_nextId++; b.type=BlockType::BlkForm; b.name="BlkForm";
    m_blocks.prepend(b); refreshList(); m_list->setCurrentRow(0);
}
void ProgrammePage::insertPocheRect()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::PocheRect;      b.name="Poche rect.";    addBlock(b); }
void ProgrammePage::insertPocheCirc()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::PocheCirc;      b.name="Poche circ.";    addBlock(b); }
void ProgrammePage::insertFraisageTrou()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::FraisageTrou;   b.name="Fraisage trou";  addBlock(b); }
void ProgrammePage::insertPercage()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::Percage;        b.name="Percage";         addBlock(b); }
void ProgrammePage::insertPercageDebourr()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::PercageDebourr; b.name="Perc. debourr."; addBlock(b); }
void ProgrammePage::insertPointage()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::Pointage;       b.name="Pointage";        addBlock(b); }
void ProgrammePage::insertTaraudage()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::Taraudage;      b.name="Taraudage";       addBlock(b); }
void ProgrammePage::insertAlesage()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::Alesage;        b.name="Alesage";         addBlock(b); }
void ProgrammePage::insertContour()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::Contour;        b.name="Contour";         addBlock(b); }
void ProgrammePage::insertPocheContour()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::PocheContour;   b.name="Poche contour";   addBlock(b); }
void ProgrammePage::insertSurfacage()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::Surfacage;      b.name="Surfacage";       addBlock(b); }
void ProgrammePage::insertRainure()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::Rainure;        b.name="Rainure";         addBlock(b); }
void ProgrammePage::insertRainureCirc()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::RainureCirc;    b.name="Rainure circ.";   addBlock(b); }
void ProgrammePage::insertTenonRect()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::TenonRect;      b.name="Tenon rect.";     addBlock(b); }
void ProgrammePage::insertTenonCirc()
{ BlockData b; b.id=m_nextId++; b.type=BlockType::TenonCirc;      b.name="Tenon circ.";     addBlock(b); }

// ─── Copier / Coller / Couper / Supprimer ────────────────────
//  Facon TNC : la selection peut couvrir plusieurs blocs (du bloc de
//  depart jusqu'au curseur). Le BLK FORM n'est jamais copie ni coupe.
QVector<int> ProgrammePage::selectedRowsSorted() const
{
    QVector<int> rows;
    if (m_selectionMode && !m_selectedRows.isEmpty()) {
        for (int r : m_selectedRows) rows << r;
    } else if (m_currentIndex >= 0) {
        rows << m_currentIndex;
    }
    std::sort(rows.begin(), rows.end());
    QVector<int> out;
    for (int r : rows)
        if (r >= 0 && r < m_blocks.size() && m_blocks[r].type != BlockType::BlkForm) out << r;
    return out;
}
void ProgrammePage::copyBlock()
{
    const QVector<int> rows = selectedRowsSorted();
    if (rows.isEmpty()) return;
    m_clip.clear();
    for (int r : rows) m_clip << m_blocks[r];
    m_clipboard = m_clip.first(); m_clipValid = true;
    emit clipboardChanged(true);
}
void ProgrammePage::pasteBlock()
{
    if (m_clip.isEmpty()) return;
    saveCurrentBlock();
    int pos = (m_currentIndex >= 0) ? m_currentIndex + 1 : m_blocks.size();
    pos = qBound(1, pos, int(m_blocks.size()));
    for (const BlockData &src : std::as_const(m_clip)) {
        BlockData b = src; b.id = m_nextId++;
        m_blocks.insert(pos++, b);
    }
    refreshList();
    m_list->setCurrentRow(pos - 1);
}
void ProgrammePage::cutBlock()
{
    const QVector<int> rows = selectedRowsSorted();
    if (rows.isEmpty()) return;
    saveCurrentBlock();
    m_clip.clear();
    for (int r : rows) m_clip << m_blocks[r];
    m_clipboard = m_clip.first(); m_clipValid = true;
    for (int i = rows.size() - 1; i >= 0; --i) m_blocks.removeAt(rows[i]);
    m_selectedRows.clear();
    refreshList();
    if (m_list->count() > 0) m_list->setCurrentRow(qBound(0, rows.first() - 1, m_list->count() - 1));
    emit clipboardChanged(true);
}
void ProgrammePage::deleteBlock()
{
    if(m_currentIndex<0||m_currentIndex>=m_blocks.size()) return;
    if(m_blocks[m_currentIndex].type==BlockType::BlkForm) return;
    m_blocks.removeAt(m_currentIndex); refreshList();
    if(m_list->count()>0) m_list->setCurrentRow(qBound(0,m_currentIndex-1,m_list->count()-1));
}
void ProgrammePage::insertBlock()
{
    if(m_currentIndex<0) return;
    BlockData b; b.id=m_nextId++; b.type=BlockType::Poche; b.name="Poche";
    int pos=qMax(1,m_currentIndex);
    m_blocks.insert(pos,b); refreshList(); m_list->setCurrentRow(pos);
}

// ─── Mode sélection ──────────────────────────────────────────
void ProgrammePage::setSelRangeTo(int row)
{
    m_selectedRows.clear();
    if (m_selAnchor < 0) m_selAnchor = row;
    const int a = qMin(m_selAnchor, row), b = qMax(m_selAnchor, row);
    for (int i = a; i <= b; ++i) m_selectedRows.insert(i);
}
void ProgrammePage::beginSelection()
{ m_selectionMode=true; m_selAnchor=m_currentIndex; m_selectedRows.clear(); if(m_currentIndex>=0) m_selectedRows.insert(m_currentIndex); updateSelectionHighlight(); }
void ProgrammePage::cancelSelection()
{ m_selectionMode=false; m_selAnchor=-1; m_selectedRows.clear(); updateSelectionHighlight(); }
void ProgrammePage::selectNextBlock()
{ int n=m_currentIndex+1; if(n>=m_blocks.size()) return; m_list->setCurrentRow(n); }
void ProgrammePage::selectPrevBlock()
{ int p=m_currentIndex-1; if(p<0) return; m_list->setCurrentRow(p); }

// ─── Clavier ─────────────────────────────────────────────────
void ProgrammePage::keyPressEvent(QKeyEvent *event)
{
    switch(event->key()) {
    case Qt::Key_Up:     selectPrevBlock(); break;
    case Qt::Key_Down:   selectNextBlock(); break;
    case Qt::Key_Escape: if(m_selectionMode) cancelSelection(); break;
    default: QWidget::keyPressEvent(event);
    }
}