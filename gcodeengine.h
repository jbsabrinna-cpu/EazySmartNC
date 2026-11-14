#pragma once
#pragma execution_character_set("utf-8")

#include <QStringList>
#include <QJsonObject>
#include <QVector>
#include <QPointF>
#include <QPair>
#include "uniteditor.h"

// ═══════════════════════════════════════════════════════════════
//  GCodeLine
// ═══════════════════════════════════════════════════════════════
struct GCodeLine {
    enum Type {
        Comment,
        ToolCall,
        Spindle,
        Rapid,
        Linear,
        Arc,
        Cycle,
        MFunction,
        Motion,
        TCommand,
        Other,
        Blank,
    };

    Type    type    = Blank;
    QString code;
    QString comment;
    int     blockId = 0;
};

// ═══════════════════════════════════════════════════════════════
//  PercageData
// ═══════════════════════════════════════════════════════════════
struct PercageData {
    enum Mode { Foret = 0, FraisageHelicoidale = 1 };
    Mode    mode         = Foret;

    QString toolRef;
    bool    toolByName   = false;
    double  spindleVal   = 0;
    bool    spindleByVc  = false;
    double  avancePerc   = 0;
    int     avanceMode   = 0;
    double  avanceTrav   = 0;
    int     avanceTravMode = 0;
    QString fonctionM1;
    QString fonctionM2;
    double  DL = 0, DR = 0;
    bool    brocheCW     = true;

    double  diametreOutil = 0;
    double  diametreTrou  = 0;
    double  profondeur    = 0;
    bool    traversant    = false;

    int     cycle         = 0;
    double  degagement    = 3.0;

    // ── Perçage profond — cycle unique, 0 = fonction desactivee ──
    double  profPasse           = 0.0;   // increment de plongee (obligatoire)
    double  reductionPasse      = 0.0;   // reduction par increment (0 = constante)
    double  passeMin            = 0.0;   // passe minimale (plancher reduction)
    double  profBriseCopeaux    = 0.0;   // profondeur brise-copeaux (0 = off)
    double  retraitBriseCopeaux = 0.2;   // recul casse-copeau (mm)
    double  avancePlongee       = 0.0;   // avance de plongee
    double  avancePrepos        = 0.0;   // avance de pre-positionnement
    double  avanceRetrait       = 0.0;   // avance de retrait
    double  tempoFond           = 0.0;   // temporisation en fond (s)

    double  pasHelicoidale= 1.5;
    double  passeFinition = 0.1;

    double  approche      = 2.0;
    double  sautBride     = 50.0;
    bool    positionnementG0 = true;
    double  positionnementF  = 0;

    int     positionMode  = 0;
    QString fichierPoints;
    double  ligneP1X=0, ligneP1Y=0, ligneP2X=0, ligneP2Y=0;
    int     ligneNbPts    = 2;
    double  grilleOX=0, grilleOY=0, grillePasX=0, grillePasY=0;
    int     grilleNbX=1, grilleNbY=1;
    double  cercleCX=0, cercleCY=0, cercleRayon=0;
    int     cercleNbPts   = 4;
    double  cercleAngDep  = 0, cercleAngFin = 360;
};

// ═══════════════════════════════════════════════════════════════
//  GCodeEngine
// ═══════════════════════════════════════════════════════════════
class GCodeEngine
{
public:
    GCodeEngine() = default;

    QVector<GCodeLine> generatePoche      (const PocheData   &d, int blockId = 0) const;
    QVector<GCodeLine> generatePercage    (const PercageData &d, int blockId = 0) const;
    QVector<GCodeLine> generateContour    (const ContourData &d, int blockId = 0) const;
    QVector<GCodeLine> generateContourSubFile(const ContourData &d, int blockId = 0) const;
    QVector<GCodeLine> generateProgram    (const QString &programName, bool m25RetourOrigine = true) const;
    QVector<GCodeLine> generateBlkForm    (const QJsonObject &data, int blockId = 0) const;
    QVector<GCodeLine> generateEndProgram (int unused = 0, bool m25RetourOrigine = true) const;

    static QString     toText(const QVector<GCodeLine> &lines);

    QStringList validatePercage(const PercageData &d) const;
    QStringList validatePoche  (const PocheData   &d) const;

private:
    static QString   f(double v, int dec = 3);
    static QString   fc(double v);
    static GCodeLine line(GCodeLine::Type t, const QString &code,
                          const QString &comment = {}, int blkId = 0);
    static GCodeLine blank();
    static GCodeLine comment(const QString &txt, int blkId = 0);
    static QString   avanceStr(double val, int mode, double S = 0.0, int nbDents = 0);

    QVector<GCodeLine> toolBlock(const QString &toolRef, bool byName,
                                 double spindleVal, bool byVc,
                                 bool cw, const QString &fm1, const QString &fm2,
                                 double DL, int blkId) const;

    QVector<QPointF>   computePositions     (const PercageData &d) const;
    QVector<QPointF>   computePositionsPoche(const PocheData   &d) const;

    QVector<GCodeLine> pocheRect    (const PocheData   &d, int blkId) const;
    QVector<GCodeLine> pocheCirc    (const PocheData   &d, int blkId) const;
    QVector<GCodeLine> percageForet (const PercageData &d, int blkId) const;
    QVector<GCodeLine> percageHelico(const PercageData &d, int blkId) const;
    QVector<GCodeLine> retrait      (double sautBride, int blkId) const;

    // ── Restauration DR/DL en fin de programme ────────────────────────
    // mutable car modifiés dans des méthodes const
    mutable QVector<QPair<int,double>> m_drRestores; // {T, Routil*2 original}
    mutable QVector<QPair<int,double>> m_dlRestores; // {T, Long original}
};
