#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  cadimporter.h — Lecteur unifié DXF / STEP / IGES pour EazyNC
//
//  Formats supportés :
//    DXF  R12 → 2018 (parser maison, aucune dépendance)
//    STEP AP203/214/242 (OCC STEPControl_Reader — WITH_OCC)
//    IGES 5.3          (OCC IGESControl_Reader  — WITH_OCC)
//
//  Architecture pImpl : AUCUN include OCC dans ce .h
//  → toute la dépendance OCC est confinée dans cadimporter.cpp
// ═══════════════════════════════════════════════════════════════
#include <QString>
#include <QVector>
#include <QPointF>
#include <QRectF>
#include <QColor>
#include <QtMath>
#include <QJsonObject>
#include <QJsonArray>

// ───────────────────────────────────────────────────────────────
//  CadSegment — élément géométrique atomique
// ───────────────────────────────────────────────────────────────
struct CadSegment
{
    enum Type { Line, Arc, Circle };
    Type   type    = Line;

    // Line  : (x1,y1)→(x2,y2)
    // Arc   : centre=(x1,y1), rayon=radius, départ=angle1°, fin=angle2° (CCW)
    // Circle: centre=(x1,y1), rayon=radius (angle1=0, angle2=360)
    double x1=0, y1=0;
    double x2=0, y2=0;
    double radius = 0;
    double angle1 = 0;
    double angle2 = 360.0;
    bool   ccw    = true;   // sens arc (true = CCW = standard DXF)

    // Points de début/fin pour le chaînage
    QPointF startPt() const;
    QPointF endPt()   const;

    // Conversion arc → polyligne (arcSegments = nb de segments par 360°)
    QVector<QPointF> toPolyline(int arcSegments = 64) const;

    // Longueur géométrique
    double length() const;
};

// ───────────────────────────────────────────────────────────────
//  CadContour — chaîne de segments connectés
// ───────────────────────────────────────────────────────────────
struct CadContour
{
    int                  id      = 0;
    QString              layer;
    QColor               color   = Qt::white;
    QVector<CadSegment>  segments;
    bool                 closed  = false;   // dernier endpoint ≈ premier startpoint

    QRectF               bbox()   const;
    double               length() const;

    // Conversion en polyligne dense (pour affichage et offset)
    QVector<QPointF>     toPolyline(int arcSegments = 64) const;

    // Teste si un point est à l'intérieur du contour (pour la détection d'îlots)
    // Utilise ray-casting — uniquement valide si closed == true
    bool contains(const QPointF& pt) const;

    // Diamètre approximatif (utile pour "filtre par diamètre" dans la visionneuse)
    double diameter() const;
};

// ───────────────────────────────────────────────────────────────
//  CadLayer — calque DXF
// ───────────────────────────────────────────────────────────────
struct CadLayer
{
    QString name    = "0";
    QColor  color   = Qt::white;
    bool    visible = true;
    bool    frozen  = false;

    // Couleur standard DXF (index 1-255) → QColor
    static QColor colorFromIndex(int idx);
};

// ───────────────────────────────────────────────────────────────
//  CadDocument — document CAD complet
// ───────────────────────────────────────────────────────────────
struct CadDocument
{
    QString             filePath;
    QString             format;    // "DXF", "STEP", "IGES"
    bool                valid = false;
    QString             error;

    QVector<CadLayer>   layers;
    QVector<CadContour> contours;
    QRectF              extents;   // boîte englobante de tous les contours

    // ── Modèle 3D (STEP / IGES uniquement) ──────────────────────
    //  mesh  : triangles, 6 floats par sommet (x y z nx ny nz)
    //  meshFace : n° de face B-rep de chaque triangle (sélection future)
    //  edges3d : segments des arêtes B-rep, 3 floats par point (paires)
    QVector<float>      mesh;
    QVector<int>        meshFace;
    QVector<float>      edges3d;
    QVector<float>      centres3d;   // centres des arcs / cercles (x y z), pour l'origine
    float               bbMin[3] = {0, 0, 0};
    float               bbMax[3] = {0, 0, 0};

    // Accès pratiques
    const CadLayer* layerByName(const QString& name) const;
    QVector<const CadContour*> contoursInLayer(const QString& layerName) const;
    QVector<const CadContour*> closedContours() const;

    // Îlots : retourne les contours fermés contenus dans un contour parent
    QVector<int> islandsOf(int contourId) const;
};

// ───────────────────────────────────────────────────────────────
//  ContourCadData — paramètres d'usinage pour bloc Contour CAD
//  Utilisé par GCodeEngine::generateContourCad()
// ───────────────────────────────────────────────────────────────
struct ContourCadData
{
    // ── Fichier source ────────────────────────────────────────
    QString              filePath;
    QVector<QString>     selectedLayers;     // calques à usiner
    QVector<int>         selectedContourIds; // IDs de CadContour sélectionnés
    QVector<CadContour>  contours;           // contours effectifs (rempli par CadImporter)

    // ── Outil ─────────────────────────────────────────────────
    QString toolRef;
    bool    toolByName  = false;
    double  spindleVal  = 0;
    bool    spindleByVc = false;
    double  descenteVal = 0;
    int     descenteMode= 0;
    double  avanceVal   = 0;
    int     avanceMode  = 0;
    double  avanceFinVal= 0;
    int     avanceFinMode=0;
    bool    brocheCW    = true;
    QString fonctionM1, fonctionM2;
    double  DL = 0;

    // ── Mode d'usinage ────────────────────────────────────────
    enum ModeUsinage { Poche = 0, Profil = 1, Gravure = 2 };
    ModeUsinage modeUsinage = Poche;

    // ── Paramètres géométriques ───────────────────────────────
    double profondeur  = 10.0;
    double profPasse   = 2.0;
    double surepLat    = 0.0;
    double surepProf   = 0.0;
    double passeFinition = 0.0;
    int    modeFraisage  = 0;  // 0=avalant, 1=opposition

    // ── Positionnement ────────────────────────────────────────
    double origineX  = 0, origineY  = 0, origineZ  = 0;
    double approche  = 2.0;
    double sautBride = 50.0;

    // ── Sérialisation JSON ────────────────────────────────────
    QJsonObject toJson() const;
    static ContourCadData fromJson(const QJsonObject& o);
};

// ───────────────────────────────────────────────────────────────
//  CadImporter — classe principale de lecture
// ───────────────────────────────────────────────────────────────
class CadImporter
{
public:
    // Charge automatiquement selon l'extension (.dxf / .stp / .step / .igs / .iges)
    static CadDocument load(const QString& filePath);

    // Charge un seul format explicitement
    static CadDocument loadDxf (const QString& filePath);
    static CadDocument loadStep(const QString& filePath);  // nécessite WITH_OCC
    static CadDocument loadIges(const QString& filePath);  // nécessite WITH_OCC

    // Retourne les extensions supportées (pour QFileDialog)
    static QString fileFilter();

private:
    // ── DXF ──────────────────────────────────────────────────
    struct DxfEntity {
        QString type;   // "LINE","ARC","CIRCLE","LWPOLYLINE","POLYLINE","SPLINE"
        QString layer;
        int     color  = 256;  // 256=BYLAYER
        // Géométrie brute
        double  x1=0,y1=0,z1=0, x2=0,y2=0,z2=0;
        double  cx=0,cy=0,cz=0;   // centre arc/cercle
        double  radius=0, angle1=0, angle2=360;
        int     flags=0;
        // Sommets LWPOLYLINE
        struct Vertex { double x=0,y=0,z=0,bulge=0; };
        QVector<Vertex> vertices;
        // Points SPLINE
        QVector<QPointF> splineCtrl;
        int     splineDegree = 3;
    };

    static QVector<DxfEntity> parseDxfEntities(const QString& filePath,
                                               QVector<CadLayer>& layersOut);
    static QVector<CadContour> chainEntities(const QVector<DxfEntity>& entities,
                                             const QVector<CadLayer>& layers,
                                             double tolerance = 0.01);
    static CadContour entityToContour(const DxfEntity& e, int id,
                                      const QVector<CadLayer>& layers);
    static QRectF computeExtents(const QVector<CadContour>& contours);

    // ── OCC STEP/IGES (implémentation dans .cpp derrière #ifdef WITH_OCC) ──
    static CadDocument occLoad(const QString& filePath, const QString& format);
};
