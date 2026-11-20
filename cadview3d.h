#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  CadView3D — vue 3D ombrée d'un modèle STEP / IGES
//  Rotation : clic gauche glissé   Déplacement : clic droit / milieu
//  Zoom : molette                   Cadrer : double-clic
//  Vues : touches 1 (dessus) 2 (face) 3 (droite) 0 (isométrique)
// ═══════════════════════════════════════════════════════════════
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QMatrix4x4>
#include <QVector3D>
#include <QPointF>
#include <QPoint>
#include <QSet>
#include <QMap>

struct CadDocument;
struct Couche3D;

class CadView3D : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit CadView3D(QWidget *parent = nullptr);
    ~CadView3D() override;

    void setDocument(const CadDocument &doc);
    // Couches d'ébauche (orange, fonds plats jaunes) et de finition (vert) ; vide = effacer
    void setCouches(const QVector<Couche3D> &ebauche);
    void setCouches(const QVector<Couche3D> &ebauche, const QVector<Couche3D> &finition);
    void setAffichage(bool ebauche, bool finition, bool pave);   // cases « Afficher »
    // Pavé de surfaçage : contour XY de la zone, de z0 (dessus pièce) à z1 (dessus brut)
    void setPave(const QVector<QPointF> &contour, double z0, double z1);
    void vueIso();
    void vueDessus();
    void vueFace();
    void vueDroite();
    void cadrer();

    // ── Sélection de la zone à usiner (usinage 3D) ─────────────
    //  clic simple (sans glisser) :
    //   - près d'une arête -> toute sa boucle (contour) est prise
    //   - sur une face     -> la face est prise
    //  re-clic : enlevé. La sélection passe en vert.
    void setSelectionFaces(bool actif);
    void setFacesChoisies(const QSet<int> &faces);
    QSet<int> facesChoisies() const { return m_choisies; }
    // Cavités exclues (rouge) : clic sur une face en mode « exclure »
    void setModeExclure(bool on);
    void setFacesExclues(const QSet<int> &f);
    QSet<int> facesExclues() const { return m_exclues; }
    double zFace(int face) const { return m_zFace.value(face, 0.0); }
    QList<double> zFondsPlats() const { return m_zFace.values(); }   // faces planes horizontales
    // contours choisis : boucles ordonnées de points 3D
    QVector<QVector<QVector3D>> contoursChoisis() const;
    void setContoursChoisis(const QVector<QVector<QVector3D>> &cts);   // retrouve les arêtes
    double zBasSelection() const;
    // origine pièce (trièdre) + brut (boîte), en coordonnées du STEP
    void setRepere(const QVector3D &origine, const QVector3D &brutMin, const QVector3D &brutMax);
    // idem + boîte d'encombrement de la pièce (orange)
    void setRepere(const QVector3D &origine, const QVector3D &brutMin, const QVector3D &brutMax,
                   const QVector3D &pieceMin, const QVector3D &pieceMax);
    // Choix d'un point (origine) : clic sur un sommet (aimanté) ou sur une face
    void setChoixPoint(bool actif);
    // Sens des axes machine (directions dans le repère du STEP) : le trièdre
    // les suit et la face + de chaque axe de la boîte pièce prend sa couleur
    void setAxesMachine(const QVector3D &ax, const QVector3D &ay, const QVector3D &az);
    // Mise en place (page STEP) : 0 rien, 1 sens des axes (clic face boîte pièce),
    // 2 origine sur un point de la boîte du brut, 3 origine sur un point de la pièce
    void setModeMiseEnPlace(int mode);
    void setVoirAxes(bool v);        // faces translucides + flèches des axes          // point le plus bas de la sélection (1e30 si vide)
    bool   selectionVide() const { return m_choisies.isEmpty() && m_segChoisis.isEmpty(); }

signals:
    void facesChanged();
    void pointChoisi(const QVector3D &p);
    // clic sur une face d'une boîte : boite 0 = brut, 1 = pièce ; axe 0 X 1 Y 2 Z ; plus = face +
    void faceBoiteChoisie(int boite, int axe, bool plus);
    // point de la boîte du brut : indices 0 min, 1 milieu, 2 max par axe du STEP
    void pointBoiteChoisi(int ix, int iy, int iz);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void televerser();
    QMatrix4x4 rotation() const;
    QMatrix4x4 vue() const;
    QMatrix4x4 projection() const;
    void dessinerTriedre(QPainter &p);
    int  facePointee(const QPoint &pos, QVector3D *impact = nullptr) const;   // n° de triangle, -1 sinon
    int  sommetPointe(const QPoint &pos) const;    // n° de sommet d'arête visible à < 10 px, -1 sinon
    int  aretePointee(const QPoint &pos) const;    // n° de segment d'arête à < 7 px, -1 sinon
    QVector<int> boucle(int seg) const;            // segments de la boucle de ce segment
    void construireGraphe();
    void majSelection();

    QOpenGLShaderProgram m_prog;
    QOpenGLBuffer m_vboTri{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer m_vboArete{QOpenGLBuffer::VertexBuffer};
    QVector<float> m_tri, m_aretes;
    QOpenGLBuffer m_vboCouches{QOpenGLBuffer::VertexBuffer};
    QVector<float> m_couches;
    struct Plage { int debut, nb; QVector3D col; int type; };   // type 0 ébauche, 1 finition
    bool m_voirEb = true, m_voirFin = true, m_voirPave = true;
    QVector<float> m_pave;
    QOpenGLBuffer m_vboPave{QOpenGLBuffer::VertexBuffer};
    QVector<Plage> m_plages;
    QString m_infoCouches;
    int  m_nTri = 0, m_nArete = 0;
    bool m_aTeleverser = false;
    bool m_glPret = false;

    QVector3D m_centre{0, 0, 0};
    float m_rayon = 100.0f;          // demi-diagonale du modèle
    float m_lacet = -45.0f;          // rotation autour de Z (degrés)
    float m_tangage = 55.0f;         // inclinaison depuis la vue de dessus
    float m_echelle = 1.0f;          // demi-hauteur visible (mm)
    QVector3D m_pan{0, 0, 0};
    QPoint m_dernier;
    QString m_info;

    QVector<int> m_triFace;                // n° de face B-rep de chaque triangle
    QMap<int, double> m_zFace;             // faces planes horizontales -> Z
    bool m_selActive = false;
    QSet<int> m_choisies;
    QSet<int> m_exclues;
    bool m_modeExclure = false;
    int  m_nExcl = 0;
    QOpenGLBuffer m_vboSel{QOpenGLBuffer::VertexBuffer};
    QVector<float> m_sel;
    int m_nSel = 0;
    QOpenGLBuffer m_vboCand{QOpenGLBuffer::VertexBuffer};   // faces possibles + survol
    QVector<float> m_cand;
    int m_nCand = 0, m_nSurvol = 0;                          // [0,nCand) possibles, puis survol
    int m_faceSurvol = -1;
    // graphe des arêtes : points uniques + segments
    QVector<QVector3D> m_pts;
    QVector<QPair<int,int>> m_segs;
    QVector<QVector<int>> m_segsDuPt;
    QSet<int> m_segChoisis;
    QVector<int> m_segSurvol;
    QOpenGLBuffer m_vboLigSel{QOpenGLBuffer::VertexBuffer};
    QVector<float> m_ligSel;
    int m_nLigChoisi = 0, m_nLigSurvol = 0;
    QOpenGLBuffer m_vboRep{QOpenGLBuffer::VertexBuffer};
    QVector<float> m_rep;                  // brut (24 pts) puis X, Y, Z (2 pts chacun)
    bool m_aRepere = false;
    bool m_aBoitePiece = false;
    bool m_choixPoint = false;
    QVector<QVector3D> m_centres;          // centres des arcs (aimantés en mode point)
    QVector<QVector3D> m_centresPiece;     // centres dans l'encombrement de la pièce
    QVector3D m_bMin, m_bMax, m_pMin, m_pMax;
    QVector3D m_axX{1, 0, 0}, m_axY{0, 1, 0}, m_axZ{0, 0, 1};
    QOpenGLBuffer m_vboAxes{QOpenGLBuffer::VertexBuffer};
    QVector<float> m_axesLig;              // faces + colorées : 3 groupes de 12 points
    void majFacesAxes();
    int  m_mep = 0;                        // mode de mise en place
    bool m_voirAxes = true;
    QOpenGLBuffer m_vboAxTri{QOpenGLBuffer::VertexBuffer};
    QVector<float> m_axesTri;              // faces translucides : 3 groupes de 6 points
    QOpenGLBuffer m_vboPtsB{QOpenGLBuffer::VertexBuffer};
    QVector<float> m_ptsBoite;             // 27 croix (6 points chacune)
    QVector<QVector3D> m_ptsB;
    void majPointsBoite();
    // Cônes (pointes de flèches) : faces X Y Z puis trièdre X Y Z
    QOpenGLBuffer m_vboCone{QOpenGLBuffer::VertexBuffer};
    QVector<float> m_conesFace, m_conesTri, m_cones;
    int m_nCone = 0;                       // sommets par cône
    void ajouterCone(QVector<float> &v, const QVector3D &base, const QVector3D &pointe, float rayon);
    void majCones();
    QOpenGLBuffer m_vboCroix{QOpenGLBuffer::VertexBuffer};
    QVector<float> m_croix;
    QPoint m_appui;
    QString m_msgSel;
};
