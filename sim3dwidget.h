#pragma once
#ifndef SIM3DWIDGET_H
#define SIM3DWIDGET_H
// ═══════════════════════════════════════════════════════════════
//  Sim3DWidget v4
//  Nouveautés vs v3 :
//    – GCodeParserFreeCADCompatible (classe locale dans .cpp)
//      G17/18/19, G20/21, G54-G59, G80/81/83, G90/91, G93/94
//    – Brut STEP via OpenCASCADE (optionnel : CONFIG += WITH_OCC)
//    – buildToolShape()  : fraise OCC depuis ToolDatabase
//    – applyToolCut()    : BRepAlgoAPI_Cut segment par segment
//    – applyAllOccCuts() : enlèvement complet hors-ligne
//    – triangulateOccShape() → m_occMesh → drawMeshGPU()
//    – Heightmap conservée comme fallback si pas de STEP
//
//  Activation OCC dans EazyNC.pro :
//
//    contains(CONFIG, WITH_OCC) {
//        OCC_INC  = C:/opencascade-7.9.3-vc14-64/inc
//        OCC_LIB  = C:/opencascade-7.9.3-vc14-64/win64/vc14/lib
//        INCLUDEPATH += $$OCC_INC
//        LIBS += -L$$OCC_LIB
//        # Kernel + Math + Geometry
//        LIBS += $$OCC_LIB/TKernel.lib
//        LIBS += $$OCC_LIB/TKMath.lib
//        LIBS += $$OCC_LIB/TKG2d.lib
//        LIBS += $$OCC_LIB/TKG3d.lib
//        LIBS += $$OCC_LIB/TKGeomBase.lib
//        LIBS += $$OCC_LIB/TKGeomAlgo.lib
//        # BRep + Topology
//        LIBS += $$OCC_LIB/TKBRep.lib
//        LIBS += $$OCC_LIB/TKTopAlgo.lib
//        LIBS += $$OCC_LIB/TKShHealing.lib
//        # Primitives + Mesh
//        LIBS += $$OCC_LIB/TKPrim.lib
//        LIBS += $$OCC_LIB/TKMesh.lib
//        # Boolean operations (BRepAlgoAPI_Cut/Fuse)
//        LIBS += $$OCC_LIB/TKBO.lib
//        LIBS += $$OCC_LIB/TKBool.lib
//        # STEP reader (OCC 7.9.3 : TKDESTEP remplace TKSTEP)
//        LIBS += $$OCC_LIB/TKDESTEP.lib
//        LIBS += $$OCC_LIB/TKXSBase.lib
//        LIBS += $$OCC_LIB/TKDE.lib
//        LIBS += $$OCC_LIB/TKDECascade.lib
//        DEFINES += WITH_OCC
//        # DLLs OCC à copier dans le dossier build :
//        # C:\opencascade-7.9.3-vc14-64\win64\vc14\bin\TK*.dll
//    }
// ═══════════════════════════════════════════════════════════════

// ── OCC — pImpl (forward declaration uniquement) ──────────────
// Tous les includes OCC sont dans sim3dwidget.cpp uniquement.
// Évite la pollution des macros Windows/CRT (S_NORMAL, etc.)
// dans tout le projet via la chaîne simulationpage.h → sim3dwidget.h.
struct OccData;
class QOpenGLShaderProgram;

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMatrix4x4>
#include <QVector3D>
#include <QPointF>
#include <QVector>
#include <QTimer>
#include <QElapsedTimer>
#include <QtMath>
#include <QMutex>
#include <atomic>
#include <QFuture>
#include <QFutureWatcher>
#include <QRectF>
#include <QtConcurrent/QtConcurrent>
class QToolButton; class QSlider; class QLabel;
#include "gcodeengine.h"
#include "tooldatabase.h"

// ───────────────────────────────────────────────────────────────
//  ToolInfo — géométrie simplifiée de l'outil pour la simulation
// ───────────────────────────────────────────────────────────────
struct ToolInfo {
    float r=3.f, len=30.f, r2=0.f;  // r=rayon, len=longueur utile, r2=rayon bec
    int   T=0;                      // r2 : 0=plate, 0<r2<r torique, r2>=r boule, -1 foret
    // Pointe conique (r2 = -1) : foret ou fraise à chanfreiner
    float coneTan = 1.6643f;        // tan(demi-angle) — foret 118° par défaut
    float tipR    = 0.f;            // rayon de la pointe tronquée (mm)
};

// ───────────────────────────────────────────────────────────────
//  Heightmap — grille Z(x,y), fallback si pas de STEP OCC
// ───────────────────────────────────────────────────────────────
struct Heightmap {
    int   res  = 64;
    std::vector<float> data;      // [i*res + j]  i=X, j=Y
    std::vector<int>   toolMap;   // outil qui a usiné chaque cellule (numéro T)
    float xMin=0,xMax=200,yMin=0,yMax=100,zTop=0,zBot=-50;

    // ── Zone modifiée depuis le dernier envoi à la carte graphique ──
    // (indices de cases, bornes incluses ; vide si dI1 < dI0)
    int dI0=0, dI1=-1, dJ0=0, dJ1=-1;
    void markAllDirty() { dI0=0; dI1=res-1; dJ0=0; dJ1=res-1; }
    void clearDirty()   { dI0=res; dI1=-1; dJ0=res; dJ1=-1; }
    bool hasDirty() const { return dI1>=dI0 && dJ1>=dJ0; }

    void  init(float x0,float x1,float y0,float y1,float zt,float zb);
    void  applyTool(float cx,float cy,float r,float z,int toolT=0);   // ancien tampon disque
    // Coupe d'un segment a→b en UNE passe : chaque case de la boîte
    // englobante est visitée une seule fois, avec le vrai profil d'outil.
    bool  cutSegment(const QVector3D &a,const QVector3D &b,const ToolInfo &ti,int toolT=0);  // true si matière enlevée
    float worldX(int i) const { return xMin+(xMax-xMin)*i/(res-1); }
    float worldY(int j) const { return yMin+(yMax-yMin)*j/(res-1); }
    float& at(int i,int j)       { return data[i*res+j]; }
    float  at(int i,int j) const { return data[i*res+j]; }
    float  safe(int i,int j) const {
        if(data.empty()||res<1) return zTop;
        int ii=i<0?0:(i>=res?res-1:i);
        int jj=j<0?0:(j>=res?res-1:j);
        return data[ii*res+jj];
    }
};

// ───────────────────────────────────────────────────────────────
//  MeshVertex — sommet triangulé OCC → OpenGL immediate mode
// ───────────────────────────────────────────────────────────────
struct MeshVertex {
    float x,y,z;      // position mm
    float nx,ny,nz;   // normale unitaire
    int   toolT = 0;  // numéro outil qui a usiné cette face (0=brut intact)
};

// ───────────────────────────────────────────────────────────────
//  PassColor / SimMove / StockData / ToolInfo
// ───────────────────────────────────────────────────────────────
enum class PassColor { Normal, EbaucheFinale, FinitionFinale };

struct SimMove {
    enum Kind { Rapid, Cut };
    Kind      kind      = Cut;
    float     zMid      = 0.f;
    int       toolT     = 0;
    int       lineIdx   = -1;
    PassColor passColor = PassColor::Normal;
    QVector<QVector3D> pts;     // coordonnées centre outil (compensées CRC)
    QVector<QVector3D> rawPts;  // coordonnées brutes G-code (avant CRC)
};

struct StockData {
    float xMin=0,xMax=200,yMin=0,yMax=100,zMin=-50,zMax=0;
    bool  valid=false;
};

// (ToolInfo est défini plus haut, avant Heightmap)

// ═══════════════════════════════════════════════════════════════
//  Sim3DWidget
// ═══════════════════════════════════════════════════════════════
class Sim3DWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit Sim3DWidget(QWidget *parent = nullptr);
    ~Sim3DWidget() override;

    // ── Stock / G-code ────────────────────────────────────────
    void setStock(const StockData &s);
    void setLines(const QVector<GCodeLine> &lines, double rDefault=3.0);
    void buildMovesOnly(const QVector<GCodeLine> &lines, double rDefault=3.0);
    void clear();

    // ── Brut STEP OCC ─────────────────────────────────────────
    bool setStockFromStep(const QString &path);
    bool setStockFromBox(const StockData &s);   // crée boîte OCC depuis StockData   // retourne false si OCC absent/invalide
    void clearOccShape();                          // reset brut au shape initial
    void applyAllOccCuts();                        // calcul CSG hors-ligne (lourd)
    QString occInfo() const;
    bool isOccMode() const { return m_useOcc; }

    // ── Résolution heightmap ──────────────────────────────────
    // r <= 0 : résolution automatique selon la taille du brut (~0,15 mm/case)
    void setResolution(int r) {
        if(r<=0){ m_autoRes=true; return; }
        m_autoRes=false; m_hmapRes=qBound(32,r,2048); applyResolution();
    }
    int  resolution() const   { return m_hmapRes; }
    QString resolutionInfo() const;

    // ── Animation ─────────────────────────────────────────────
    void play();
    void pause();
    void resetSim();
    bool isPlaying() const { return m_playing; }
    void setSpeed(int v);

    // ── Lecture type magnétoscope (barre blanche dans la vue) ──
    void seekToMove(int idx);        // positionne la simu au début du move idx
    void seekToLine(int lineIdx);    // idem, depuis une ligne de G-code
    void stepForward();              // finit le déplacement en cours
    void stepBackward();             // revient au début du déplacement
    void goToStart();
    void goToEnd();
    void fastForward();              // x2 → x4 → x8 → x16 → x2
    void rewind();                   // rembobine x1 → x2 → x4 → x8
    void togglePlayPause();

    // ── Affichage ─────────────────────────────────────────────
    void setShowRapids(bool v)       { m_showRapids=v; update(); }
    void setShowToolPath(bool v)     { m_showPath=v;   update(); }
    void setStockTransparent(bool v) { m_stockTransp=v;update(); }
    // ── Opacité variable du brut (0.0 = invisible → 1.0 = plein) ──
    // Remplace l'ancien bouton plein/transparent. Utilise le pipeline
    // heightmap dans tous les cas (plus d'OCC qui n'était pas synchro).
    void setStockAlpha(float a) {
        m_stockAlpha = qBound(0.0f, a, 0.975f);  // max 97.5%
        m_useOcc = false;
        { QMutexLocker lk(&m_occMeshMutex); m_occMesh.clear(); }
        m_hmapVBODirty = true;  // re-générer le VBO avec les nouveaux alphas
        update();
    }
    float stockAlpha() const { return m_stockAlpha; }
    // Compat : ancien bouton bool plein/transparent → mappé sur alpha
    void setStockOpaque(bool v) { setStockAlpha(v ? 0.975f : 0.15f); }
    void setShowStock(bool v)        { m_showStock=v;  update(); }
    void setShowTool(bool v)         { m_showTool=v;   update(); }
    void onRebuildHeightmap() {
        pause();
        if(m_stock.valid){
            m_hmap.res=m_hmapRes;
            m_hmap.init(m_stock.xMin,m_stock.xMax,m_stock.yMin,m_stock.yMax,
                        m_stock.zMax,m_stock.zMin);
        }
        buildHeightmapAsync();
    }

    // ── Caméra ────────────────────────────────────────────────
    void fitAll();
    // Zoom maxi : ~1,5 micron par pixel (la loupe descend à 1 micron par case)
    float minZoomDist() const { return qMax(0.5f, 0.0015f * float(qMax(1, height())) / (m_ortho ? 0.70f : 0.443f)); }
    void zoomIn()  { m_dist=qMax(minZoomDist(),m_dist*0.80f); rebuildProjection(); update(); }
    void zoomOut() { m_dist=qMax(minZoomDist(),m_dist*1.25f); rebuildProjection(); update(); }
    void setOrtho(bool on) { m_ortho=on; rebuildProjection(); updateTransportUi(); update(); }
    bool isOrtho() const   { return m_ortho; }
    enum ViewPreset { ViewIso, ViewTop, ViewFront, ViewLeft, ViewRight };
    void viewPreset(ViewPreset v);

    // ── Mesure ────────────────────────────────────────────────
    void setMeasureMode(bool on) {
        m_measureMode=on;
        if(!on){ m_measurePts.clear(); m_faceRegion.clear(); m_faceType=0; }
        setMouseTracking(on);            // lecture X/Y/Z au survol, sans cliquer (comme la TNC)
        m_hoverValid = false; updateHoverLabel();
        update();
    }
    bool isMeasureMode()  const  { return m_measureMode; }
    void clearMeasure()          { m_measurePts.clear(); m_faceRegion.clear(); m_faceType=0; update(); }

signals:
    void progressChanged(int pct);
    void animProgressChanged(int pct);
    void playingChanged(bool playing);
    void currentLineChanged(int lineIdx);
    void measureResult(const QString &html);
    void faceDetected(const QString &html);
    void occProgressChanged(int pct);   // progression applyAllOccCuts

protected:
    void initializeGL()                  override;
    void resizeGL(int w,int h)           override;
    void paintGL()                       override;
    void mousePressEvent(QMouseEvent*)   override;
    void mouseMoveEvent(QMouseEvent*)    override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*)        override;
    void leaveEvent(QEvent*)             override;

private:
    // ── Stock / heightmap ─────────────────────────────────────
    StockData          m_stock;
    Heightmap          m_hmap;
    double             m_rDefault = 3.0;

    // ── OCC — pImpl ──────────────────────────────────────────────
    OccData*            m_occ     = nullptr;
    bool                m_useOcc  = false;
    QVector<MeshVertex> m_occMesh;
    QMutex              m_occMeshMutex;

    // ── Moves ─────────────────────────────────────────────────
    QVector<SimMove>   m_moves;
    QVector<QVector3D> m_ebaucheContour;
    QVector<QVector3D> m_finitionContour;

    // ── Animation ─────────────────────────────────────────────
    QTimer   *m_animTimer  = nullptr;
    bool      m_playing    = false;
    int       m_speed      = 10;
    int       m_curMove    = 0;
    int       m_curSeg     = 0;
    float     m_curSegT    = 0.f;
    float     m_stepMM     = 1.f;
    QVector3D m_toolPos;
    ToolInfo  m_toolInfo;
    int       m_lastEmittedLine = -1;

    // ── Heightmap incrémentale ────────────────────────────────
    // Position du dernier point d'outil appliqué sur m_hmap.
    // Permet de creuser le segment exact entre 2 ticks d'animation
    // sans rejouer toute l'historique (synchro parfaite et fluide).
    QVector3D m_lastIncrPos;
    bool      m_lastIncrValid = false;
    int       m_lastIncrToolT = -1;
    // Throttle du rebuild VBO pendant la simu : un VBO contient ~65k
    // vertices à 256², le rebuilder à chaque tick (30 FPS) = 2M
    // vertices/sec. On limite à 20 Hz pendant la simu pour garder de
    // la marge GPU et CPU. Le visuel reste très synchro.
    QElapsedTimer m_vboLastBuild;
    static constexpr int kVboMinIntervalMs = 50;

    // ── Résolution heightmap ──────────────────────────────────
    // 256×256 = rendu fin (passage à l'incrémental permet ça sans coût
    // de simu ; seul le rebuild VBO est plus coûteux mais peu fréquent)
    int   m_hmapRes = 1024; // résolution heightmap (R×R cellules)
    bool  m_autoRes = true; // résolution calculée depuis la taille du brut
    // Deux grilles (méthode VerdierCAM) : une légère pendant l'animation
    // (~0,25 mm par case), la fine (m_hmapRes) pour la pièce à l'arrêt.
    int   m_animRes  = 820;
    int   m_buildRes = 1024;   // résolution du prochain calcul en tâche de fond
    // Déplacement en cours à creuser jusqu'à l'outil (pause / reprise)
    int       m_bpMove = -1, m_bpSeg = 0;
    QVector3D m_bpPos;
    void  applyResolution();
    QFutureWatcher<void> *m_hmapWatcher  = nullptr;
    std::atomic<int>      m_hmapProgress {0};
    QTimer               *m_progressTimer= nullptr;
    QTimer               *m_hmapRefreshTimer = nullptr;  // rebuild périodique pendant animation
    bool                  m_hmapPending  = false;

    // ── Cache normales heightmap (évite recalcul Sobel à chaque frame) ──
    std::vector<QVector3D> m_normalCache;   // taille R×R, recalculé si m_hmapNormalsDirty
    bool                   m_hmapNormalsDirty = true;
    void rebuildNormalCache();

    // ── Double buffer heightmap (évite race condition thread pool ↔ GL) ──
    // buildHeightmapSync construit dans m_hmapBack (thread pool)
    // puis swap atomique → m_hmap (main thread) protégé par m_hmapMutex
    Heightmap          m_hmapBack;          // buffer de construction (thread pool)
    QMutex             m_hmapMutex;         // protège m_hmap contre accès concurrent

    // ── VBO heightmap (remplace immediate mode) ───────────────────
    // Rebuild quand : données changent, résolution change, mode opaque change.
    // Gain massif sur grandes pièces : upload GPU 1×/changement, pas chaque frame.
    GLuint m_hmapVBO       = 0;      // buffer pos+normal+couleur (DYNAMIC_DRAW)
    bool   m_hmapVBODirty  = true;   // doit être reconstruit
    int    m_hmapVBORes    = 0;      // résolution au moment de la création
    bool   m_hmapVBOOpaque = false;  // mode opaque au moment de la création
    int    m_hmapTotalCount = 0;    // nb vertices dans le VBO (surface + murs)
    // ── Plages séparées pour 2 passes de rendu ──
    // Le VBO est organisé en 2 sections contiguës :
    //   [0 .. m_hmapBrutCount[          → top du brut (cellules non usinées)
    //   [m_hmapBrutCount .. m_hmapTotalCount[ → matière (cellules usinées + murs)
    // Permet de dessiner le brut avec opacité variable et la matière
    // toujours opaque dans 2 glDrawArrays distincts.
    int    m_hmapBrutCount  = 0;
    void   rebuildHeightmapVBO();    // upload VBO (appelé depuis drawHeightmap/Solid)

    // ── Rendu GPU de la heightmap (méthode « jeu vidéo ») ──────────
    //  La carte de hauteurs vit dans une texture float sur la carte
    //  graphique. Seule la zone touchée par l'outil est renvoyée à
    //  chaque image (glTexSubImage2D). Une grille fixe est soulevée
    //  par le vertex shader ; l'éclairage est calculé au pixel.
    //  Si les shaders ne compilent pas, on retombe sur l'ancien rendu.
    QOpenGLShaderProgram *m_hmProg = nullptr;
    bool   m_gpuTried = false, m_gpuOk = false;
    GLuint m_hmTex = 0;          int m_hmTexRes = 0;
    GLuint m_hmToolTex = 0;      // numéro d'outil par case (couleur par outil)
    GLuint m_hmMeshVBO = 0;      int m_hmMeshG  = 0;
    GLuint m_hmIdxFull = 0;      int m_hmIdxFullCount = 0;
    GLuint m_hmIdxLod  = 0;      int m_hmIdxLodCount  = 0;
    GLuint m_hmIdxLod4 = 0;      int m_hmIdxLod4Count = 0;   // 1 nœud sur 4 (vue de loin)
    bool   initHeightmapGPU();
    void   uploadHeightmapTexture();      // appelé sous m_hmapMutex
    void   buildHeightmapMesh(int G);
    void   drawHeightmapGPU();

    // ── Loupe : zone visible recalculée en grille fine quand on zoome ──
    Heightmap         m_loupe;
    bool              m_lpValid = false;
    bool              m_lpUploaded = false;
    QRectF            m_lpRegion;                 // zone XY couverte (mm)
    int               m_lpVerBuilt = -1;          // version de matière de la loupe
    std::atomic<int>  m_matVer{0};                // change à chaque modif de la pièce
    std::atomic<int>  m_lpGen{0};
    QFuture<void>     m_lpFuture;
    QTimer           *m_lpTimer = nullptr;
    QVector<float>    m_lpLastCam, m_lpBuiltCam;
    GLuint m_lpTex = 0, m_lpToolTex = 0, m_lpVBO = 0, m_lpIBO = 0;
    int    m_lpMeshG = 0, m_lpIdxCount = 0;
    QVector<float> camKey() const;
    void   computeLoupe();
    void   cancelLoupe();

    // ── Couleur de la fraise : rouge dans la matière, verte dans le vide ──
    QElapsedTimer m_lastCutClock;            // dernière fois que la fraise a enlevé de la matière
    bool   m_rapidCollision = false, m_rapidCollisionShown = false;
    bool   toolBelowSurface(const QVector3D &p, int toolT);

    // ── Pause automatique pendant la rotation à la souris ─────────
    bool   m_mousePaused = false;

    // ── Chronométrage (sortie de débogage, 1×/seconde) ────────────
    qint64        m_perfCutNs = 0, m_perfUploadNs = 0, m_perfDrawNs = 0;
    qint64        m_perfPaintNs = 0, m_perfTickNs = 0, m_perfPathNs = 0;
    int           m_perfFrames = 0, m_perfTicks = 0;
    QElapsedTimer m_perfClock;

    // ── Display list GL pour le parcours exécuté (perf i5-4570T) ────
    GLuint  m_dlDone     = 0;      // display list parcours fait
    int     m_dlDoneUpTo = -1;     // m_curMove au moment de la dernière compilation
    bool    m_dlDirty    = true;   // à recompiler
    void    rebuildDoneList();     // compile m_dlDone jusqu'à m_curMove

    // ── Flags affichage ───────────────────────────────────────
    bool m_showRapids=false, m_showPath=false, m_stockTransp=false, m_hmapRealtime=true, m_stockOpaque=false;
    float m_stockAlpha = 0.975f; // 0=invisible, ≥0.975=plein (plein par défaut)
    bool m_showStock=true,   m_showTool=true;

    // ── Mesure ────────────────────────────────────────────────
    bool               m_measureMode = false;
    QVector<QVector3D> m_measurePts;
    QVector<QVector3D> m_faceRegion;
    int                m_faceType = 0;

    // ── Caméra ────────────────────────────────────────────────
    float      m_azimuth=225.f, m_elevation=35.f, m_dist=500.f;
    bool       m_ortho=false;   // perspective douce par défaut (FOV 30°)
    QVector3D  m_pivot;
    QPoint     m_lastMouse;
    bool       m_orbiting=false, m_panning=false;
    QMatrix4x4 m_proj, m_view;

    // ── Méthodes privées — simulation ─────────────────────────
    void buildMoves(const QVector<GCodeLine> &lines, double rDefault);
    void buildHeightmapAsync(int upTo = -2, int res = -1);  // -2 = auto
    void buildHeightmapSync(int upTo=-1);  // upTo=-1 = tous les moves
    // Enlève la matière incrémentalement entre la dernière position
    // appliquée et curPos (sample dense pour ne sauter aucune cellule).
    void applyToolIncremental(const QVector3D &curPos, int toolT);
    void rebuildCamera();
    void rebuildProjection();
    void advanceAnimation();
    void emitAnimStatus(bool force);
    QElapsedTimer m_uiEmitClock;

    // ── Méthodes privées — dessin ─────────────────────────────
    void drawStock();
    void drawHeightmap();
    void drawHeightmapWireframe();
    void drawHeightmapSolid();  // mode plein — opaque avec couleurs par outil
    void drawToolPath();
    void drawContours();
    void drawTool();
    void drawAxes();
    void drawCylinder(float r, float len);
    void drawDisk(float r);
    void drawMeshGPU();          // mesh OCC triangulé → GL_TRIANGLES

    // ── Marching Squares — extraction d'iso-contours lisses ──────
    // Retourne les segments [P0,P1,P0,P1,...] en coordonnées XY monde,
    // pour la ligne de niveau Z=zSeuil de la heightmap.
    // Format plat (paires de QPointF) car on a aussi à gérer les cas
    // ambigus où un même carré produit 2 segments.
    QVector<QPointF> extractIsoContour(float zSeuil) const;

    // ── Méthodes privées — OCC ────────────────────────────────
    void triangulateOccShape(int toolT=0);  // BRepMesh → m_occMesh (flat, thread-safe)
    void applyToolCut(float cx, float cy, float r, float z);


    // ── Utilitaires géométrie ─────────────────────────────────
    ToolInfo resolveToolInfo(int T) const;
    static bool parseParam(const QString &code, QChar name, double &out);
    static QVector<QVector3D> tessArc(QVector3D from, QVector3D to,
                                      double cx, double cy, bool cw, int steps=64);
    static QVector<QVector3D> tessArcR(QVector3D from, QVector3D to,
                                       double R, bool cw, int steps=64);
    // Arc plan-aware G17/G18/G19 (ci,cj,ck = I,J,K du G-code)
    static QVector<QVector3D> tessArcPlane(QVector3D from, QVector3D to,
                                           double ci, double cj, double ck,
                                           bool cw, int plane, int steps=64);
    static QVector3D colorForZ(float z, float zMin, float zMax);

    // ── Mesure 3D ─────────────────────────────────────────────
    QVector3D pickSurface(const QPoint &screenPt) const;

    // ── Mesure au survol (façon TNC 640) ─────────────────────
    bool      m_hoverValid = false;
    QVector3D m_hoverPt, m_hoverN;
    int       m_hoverT = 0;
    QLabel   *m_hoverLbl = nullptr;     // bandeau X~ Y~ Z~ + outil
    void updateHover(const QPoint &sp);
    void updateHoverLabel();
    void drawHoverProbe();              // cercle pointillé + palpeur posé sur la matière
    void      drawMeasurePoints();
    QString   formatMeasure() const;
    void      detectFace(const QVector3D &clickPt);
    QString   analyzeFaceRegion() const;
    void      drawFaceHighlight();

    // ── Barre de lecture (overlay) ────────────────────────────
    QWidget     *m_tpBar    = nullptr;
    QSlider     *m_tpSlider = nullptr;
    QToolButton *m_tpPlay   = nullptr;
    QToolButton *m_tpPersp  = nullptr;
    QLabel      *m_tpLbl    = nullptr;
    QTimer      *m_rwTimer  = nullptr;   // rembobinage
    QTimer      *m_fineTimer= nullptr;   // grille fine après une recherche
    int    m_ffMul = 1;                  // multiplicateur avance rapide
    int    m_rwMul = 0;                  // >0 : rembobinage actif
    float  m_stepBase = 1.f;             // pas d'avance (vitesse curseur)
    bool   m_sliderDrag = false;
    bool   m_buildPending = false;       // un calcul attend la fin du précédent
    int    m_pendingRes = 0;
    bool   m_needSyncRebuild = false;    // position changée hors animation
    QElapsedTimer m_rwBuildClock;
    void buildTransportBar();
    void positionTransportBar();
    void updateTransportUi();
    void requestBuildCurrent(int res);   // calcule la pièce à la position courante
    void launchBuildCurrent(int res);
    void stopRewind(bool refine);
    bool retreat(float dist);            // recule l'outil le long du trajet
    void onRewindTick();

    // ── Calculs de pièce : annulation immédiate + points de sauvegarde ──
    std::atomic<bool> m_buildCancel{false};
    void stopBuild();                    // interrompt le calcul en cours
    struct HmCkpt { int next = 0; Heightmap hm; };   // pièce après les moves [0, next)
    QVector<HmCkpt>   m_ckpts;
    QMutex            m_ckptMutex;
    std::atomic<int>  m_ckptGen{0};
    QFuture<void>     m_ckptFuture;
    int               m_ckptRes = 0;
    Heightmap         m_finalFine;           // pièce finie, grille fine (affichage instantané de « Fin »)
    bool              m_finalFineValid = false;
    void startCheckpoints();
    void invalidateCheckpoints();
protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;   // clic direct dans la barre de temps

private slots:
    void onAnimTick();
};

#endif // SIM3DWIDGET_H