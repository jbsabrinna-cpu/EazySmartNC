#pragma execution_character_set("utf-8")
#include "cadview3d.h"
#include "cadimporter.h"
#include "decoupe3d.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QSurfaceFormat>
#include <cmath>
#include <QHash>
#include <algorithm>

// ── Shaders (GLSL 1.30, comme la simulation 3D) ─────────────────
static const char *kVert = R"GLSL(
#version 130
in vec3 aPos;
in vec3 aNor;
uniform mat4 uMVP;
uniform mat3 uNM;
out vec3 vN;
void main(){
    vN = normalize(uNM * aNor);
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

static const char *kFrag = R"GLSL(
#version 130
in vec3 vN;
uniform vec3  uCol;
uniform float uLignes;
uniform float uAlpha;
out vec4 fragColor;
void main(){
    if (uLignes > 1.5) { fragColor = vec4(uCol, uAlpha); return; }
    if (uLignes > 0.5) { fragColor = vec4(uCol, 1.0); return; }
    vec3 n = normalize(vN);
    if (!gl_FrontFacing) n = -n;
    vec3 L1 = normalize(vec3(-0.35, 0.55, 1.0));   // lumière principale
    vec3 L2 = normalize(vec3(0.6, -0.4, 0.5));     // lumière d'appoint
    float d = 0.55 * max(dot(n, L1), 0.0) + 0.22 * max(dot(n, L2), 0.0);
    vec3 H = normalize(L1 + vec3(0.0, 0.0, 1.0));
    float s = pow(max(dot(n, H), 0.0), 40.0) * 0.35;
    vec3 c = uCol * (0.30 + d) + vec3(s);
    fragColor = vec4(c, 1.0);
}
)GLSL";

CadView3D::CadView3D(QWidget *parent) : QOpenGLWidget(parent)
{
    QSurfaceFormat f = format();
    f.setDepthBufferSize(24);
    f.setSamples(4);
    setFormat(f);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 150);
}

CadView3D::~CadView3D()
{
    makeCurrent();
    m_vboTri.destroy();
    m_vboArete.destroy();
    m_vboCouches.destroy();
    m_vboSel.destroy();
    m_vboCand.destroy();
    m_vboLigSel.destroy();
    m_vboRep.destroy();
    m_vboCroix.destroy();
    m_vboAxes.destroy();
    m_vboAxTri.destroy();
    m_vboPtsB.destroy();
    m_vboCone.destroy();
    doneCurrent();
}

void CadView3D::setDocument(const CadDocument &doc)
{
    m_tri = doc.mesh;
    m_aretes.clear();
    m_aretes.reserve(doc.edges3d.size() * 2);
    for (int i = 0; i + 2 < doc.edges3d.size(); i += 3)          // normale factice
        m_aretes << doc.edges3d[i] << doc.edges3d[i + 1] << doc.edges3d[i + 2] << 0.f << 0.f << 1.f;
    m_nTri = m_tri.size() / 6;
    m_nArete = m_aretes.size() / 6;
    const QVector3D a(doc.bbMin[0], doc.bbMin[1], doc.bbMin[2]);
    const QVector3D b(doc.bbMax[0], doc.bbMax[1], doc.bbMax[2]);
    m_centre = (a + b) * 0.5f;
    m_rayon = qMax(1.0f, (b - a).length() * 0.5f);
    m_info = m_nTri > 0
        ? QString("%1 × %2 × %3 mm   —   %4 triangles")
              .arg(double(b.x() - a.x()), 0, 'f', 1).arg(double(b.y() - a.y()), 0, 'f', 1)
              .arg(double(b.z() - a.z()), 0, 'f', 1).arg(m_nTri / 3)
        : QString("Pas de modèle 3D (fichier DXF ou STEP vide)");
    // Faces planes horizontales (normale vers +Z) : candidates « fond »
    m_triFace = doc.meshFace;
    m_zFace.clear();
    {
        QMap<int, bool> plane; QMap<int, double> zs; QMap<int, int> nb;
        for (int t = 0; t < m_triFace.size() && (t + 1) * 18 <= m_tri.size(); ++t) {
            const int f = m_triFace[t];
            bool ok = plane.value(f, true);
            for (int k = 0; k < 3; ++k) {
                const float *v = m_tri.constData() + t * 18 + k * 6;
                if (v[5] < 0.999f) ok = false;
                zs[f] += v[2]; nb[f] += 1;
            }
            plane[f] = ok;
        }
        for (auto it = plane.cbegin(); it != plane.cend(); ++it)
            if (it.value() && nb[it.key()] > 0) m_zFace[it.key()] = zs[it.key()] / nb[it.key()];
    }
    m_choisies.clear();
    m_segChoisis.clear();
    m_segSurvol.clear();
    m_faceSurvol = -1;
    construireGraphe();
    m_centres.clear();
    for (int k = 0; k + 2 < doc.centres3d.size(); k += 3)
        m_centres << QVector3D(doc.centres3d[k], doc.centres3d[k + 1], doc.centres3d[k + 2]);
    // centres hors de l'encombrement de la pièce (grands arrondis) : inutiles
    {
        QVector<QVector3D> garde;
        for (const QVector3D &q : m_centres)
            if (q.x() >= doc.bbMin[0] - 0.01f && q.x() <= doc.bbMax[0] + 0.01f &&
                q.y() >= doc.bbMin[1] - 0.01f && q.y() <= doc.bbMax[1] + 0.01f &&
                q.z() >= doc.bbMin[2] - 0.01f && q.z() <= doc.bbMax[2] + 0.01f) garde << q;
        m_centres = garde;
        m_centresPiece = garde;
    }
    m_croix.clear();
    majSelection();
    m_aTeleverser = true;
    vueIso();
}

void CadView3D::setSelectionFaces(bool actif)
{
    m_selActive = actif;
    setMouseTracking(actif);
    majSelection();
    m_msgSel = actif ? QString::fromUtf8("Zone à usiner : cliquez une ARÊTE (toute la boucle est prise) ou des FACES. "
                                         "Elles passent en vert ; re-clic pour enlever.   (orange = fonds plats trouvés)")
                     : QString();
    update();
}

void CadView3D::setFacesChoisies(const QSet<int> &faces)
{
    m_choisies.clear();
    for (int f : faces) m_choisies.insert(f);
    majSelection();
    update();
}

void CadView3D::majSelection()
{
    m_sel.clear();
    for (int t = 0; t < m_triFace.size() && (t + 1) * 18 <= m_tri.size(); ++t)
        if (m_choisies.contains(m_triFace[t]))
            for (int k = 0; k < 18; ++k) m_sel << m_tri[t * 18 + k];
    m_nSel = m_sel.size() / 6;
    // faces possibles (planes horizontales, non choisies) puis face survolée
    m_cand.clear();
    if (m_selActive) {
        for (int t = 0; t < m_triFace.size() && (t + 1) * 18 <= m_tri.size(); ++t) {
            const int f = m_triFace[t];
            if (m_zFace.contains(f) && !m_choisies.contains(f))
                for (int k = 0; k < 18; ++k) m_cand << m_tri[t * 18 + k];
        }
    }
    m_nCand = m_cand.size() / 6;
    if (m_selActive && m_faceSurvol >= 0 && m_segSurvol.isEmpty())
        for (int t = 0; t < m_triFace.size() && (t + 1) * 18 <= m_tri.size(); ++t)
            if (m_triFace[t] == m_faceSurvol)
                for (int k = 0; k < 18; ++k) m_cand << m_tri[t * 18 + k];
    m_nSurvol = m_cand.size() / 6 - m_nCand;
    // lignes : boucles choisies puis boucle survolée
    m_ligSel.clear();
    auto ajSeg = [this](int s) {
        for (int k : {m_segs[s].first, m_segs[s].second}) {
            const QVector3D &p = m_pts[k];
            m_ligSel << p.x() << p.y() << p.z() << 0.f << 0.f << 1.f;
        }
    };
    for (int s : m_segChoisis) ajSeg(s);
    m_nLigChoisi = m_ligSel.size() / 6;
    for (int s : m_segSurvol) ajSeg(s);
    m_nLigSurvol = m_ligSel.size() / 6 - m_nLigChoisi;
    m_aTeleverser = true;
}

// Rayon souris -> triangle le plus proche (Möller–Trumbore)
int CadView3D::facePointee(const QPoint &pos, QVector3D *impact) const
{
    const QMatrix4x4 inv = (projection() * vue()).inverted();
    const float x = 2.0f * pos.x() / qMax(1, width()) - 1.0f;
    const float y = 1.0f - 2.0f * pos.y() / qMax(1, height());
    const QVector3D o = inv.map(QVector3D(x, y, -1.0f));
    const QVector3D d = (inv.map(QVector3D(x, y, 1.0f)) - o).normalized();
    int best = -1; float tBest = 1e30f;
    const int nT = m_tri.size() / 18;
    for (int t = 0; t < nT; ++t) {
        const float *p = m_tri.constData() + t * 18;
        const QVector3D a(p[0], p[1], p[2]), b(p[6], p[7], p[8]), cc(p[12], p[13], p[14]);
        const QVector3D e1 = b - a, e2 = cc - a, h = QVector3D::crossProduct(d, e2);
        const float det = QVector3D::dotProduct(e1, h);
        if (std::fabs(det) < 1e-12f) continue;
        const float f = 1.0f / det;
        const QVector3D s = o - a;
        const float u = f * QVector3D::dotProduct(s, h);
        if (u < 0.0f || u > 1.0f) continue;
        const QVector3D q = QVector3D::crossProduct(s, e1);
        const float v = f * QVector3D::dotProduct(d, q);
        if (v < 0.0f || u + v > 1.0f) continue;
        const float tt = f * QVector3D::dotProduct(e2, q);
        if (tt > 0.0f && tt < tBest) { tBest = tt; best = t; }
    }
    if (impact && best >= 0) *impact = o + d * tBest;
    return best;
}

void CadView3D::setCouches(const QVector<Couche3D> &couches)
{
    m_couches.clear();
    m_plages.clear();
    int nPlanes = 0;
    for (int i = 0; i < couches.size(); ++i) {
        const Couche3D &c = couches[i];
        const int debut = int(m_couches.size() / 6);
        for (const Contour2D &ct : (c.aVider.isEmpty() ? c.ombre : c.aVider))
            for (int k = 0; k < ct.size(); ++k) {
                const QPointF &a = ct[k], &b = ct[(k + 1) % ct.size()];
                m_couches << float(a.x()) << float(a.y()) << float(c.z) << 0.f << 0.f << 1.f
                          << float(b.x()) << float(b.y()) << float(c.z) << 0.f << 0.f << 1.f;
            }
        const float t = couches.size() > 1 ? float(i) / float(couches.size() - 1) : 0.f;
        QVector3D col = c.facePlane ? QVector3D(1.0f, 0.62f, 0.15f)                 // face plane : orange
                                    : QVector3D(0.15f + 0.1f * t, 0.85f - 0.45f * t, 1.0f);   // cyan -> bleu
        if (c.facePlane) ++nPlanes;
        m_plages.append({debut, int(m_couches.size() / 6) - debut, col});
    }
    m_infoCouches = couches.isEmpty() ? QString()
        : QString::fromUtf8("   —   %1 couches (dont %2 sur fonds plats, en orange)")
              .arg(couches.size()).arg(nPlanes);
    m_aTeleverser = true;
    update();
}

void CadView3D::vueIso()    { m_lacet = -45; m_tangage = 55; cadrer(); }
void CadView3D::vueDessus() { m_lacet = 0;   m_tangage = 0;  cadrer(); }
void CadView3D::vueFace()   { m_lacet = 0;   m_tangage = 90; cadrer(); }
void CadView3D::vueDroite() { m_lacet = -90; m_tangage = 90; cadrer(); }
void CadView3D::cadrer()    { m_pan = QVector3D(); m_echelle = m_rayon * 1.05f; update(); }

void CadView3D::initializeGL()
{
    initializeOpenGLFunctions();
    m_prog.addShaderFromSourceCode(QOpenGLShader::Vertex, kVert);
    m_prog.addShaderFromSourceCode(QOpenGLShader::Fragment, kFrag);
    m_prog.bindAttributeLocation("aPos", 0);
    m_prog.bindAttributeLocation("aNor", 1);
    m_prog.link();
    m_vboTri.create();
    m_vboArete.create();
    m_vboCouches.create();
    m_vboSel.create();
    m_vboCand.create();
    m_vboLigSel.create();
    m_vboRep.create();
    m_vboCroix.create();
    m_vboAxes.create();
    m_vboAxTri.create();
    m_vboPtsB.create();
    m_vboCone.create();
    m_glPret = true;
    m_aTeleverser = true;
}

void CadView3D::televerser()
{
    m_vboTri.bind();
    m_vboTri.allocate(m_tri.constData(), int(m_tri.size() * sizeof(float)));
    m_vboArete.bind();
    m_vboArete.allocate(m_aretes.constData(), int(m_aretes.size() * sizeof(float)));
    m_vboCouches.bind();
    m_vboCouches.allocate(m_couches.constData(), int(m_couches.size() * sizeof(float)));
    m_vboSel.bind();
    m_vboSel.allocate(m_sel.constData(), int(m_sel.size() * sizeof(float)));
    m_vboCand.bind();
    m_vboCand.allocate(m_cand.constData(), int(m_cand.size() * sizeof(float)));
    m_vboLigSel.bind();
    m_vboLigSel.allocate(m_ligSel.constData(), int(m_ligSel.size() * sizeof(float)));
    m_vboRep.bind();
    m_vboRep.allocate(m_rep.constData(), int(m_rep.size() * sizeof(float)));
    m_vboCroix.bind();
    m_vboCroix.allocate(m_croix.constData(), int(m_croix.size() * sizeof(float)));
    m_vboAxes.bind();
    m_vboAxes.allocate(m_axesLig.constData(), int(m_axesLig.size() * sizeof(float)));
    m_vboAxTri.bind();
    m_vboAxTri.allocate(m_axesTri.constData(), int(m_axesTri.size() * sizeof(float)));
    m_vboPtsB.bind();
    m_vboPtsB.allocate(m_ptsBoite.constData(), int(m_ptsBoite.size() * sizeof(float)));
    m_vboCone.bind();
    m_vboCone.allocate(m_cones.constData(), int(m_cones.size() * sizeof(float)));
    m_aTeleverser = false;
}

void CadView3D::resizeGL(int, int) {}

QMatrix4x4 CadView3D::rotation() const
{
    QMatrix4x4 r;
    r.rotate(-m_tangage, 1, 0, 0);
    r.rotate(m_lacet, 0, 0, 1);
    return r;
}

QMatrix4x4 CadView3D::vue() const
{
    QMatrix4x4 v;
    v.translate(-m_pan);
    v.translate(0, 0, -3.0f * m_rayon);
    v *= rotation();
    v.translate(-m_centre);
    return v;
}

QMatrix4x4 CadView3D::projection() const
{
    const float a = float(width()) / qMax(1, height());
    QMatrix4x4 p;
    p.ortho(-m_echelle * a, m_echelle * a, -m_echelle, m_echelle, 0.01f * m_rayon, 8.0f * m_rayon);
    return p;
}

void CadView3D::paintGL()
{
    if (m_aTeleverser && m_glPret) televerser();
    glClearColor(0.075f, 0.082f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    if (m_nTri > 0 && m_prog.bind()) {
        const QMatrix4x4 mv = vue();
        m_prog.setUniformValue("uMVP", projection() * mv);
        m_prog.setUniformValue("uNM", mv.normalMatrix());

        // Faces (décalées pour que les arêtes restent visibles)
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);
        m_vboTri.bind();
        m_prog.enableAttributeArray(0);
        m_prog.enableAttributeArray(1);
        m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
        m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
        m_prog.setUniformValue("uCol", QVector3D(0.70f, 0.74f, 0.80f));
        m_prog.setUniformValue("uLignes", 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, m_nTri);
        glDisable(GL_POLYGON_OFFSET_FILL);

        // Faces possibles (orange pâle) et face sous la souris (jaune vif)
        if (m_nCand + m_nSurvol > 0) {
            glDepthFunc(GL_LEQUAL);
            m_vboCand.bind();
            m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
            m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
            if (m_nCand > 0) {
                m_prog.setUniformValue("uCol", QVector3D(0.95f, 0.72f, 0.45f));
                glDrawArrays(GL_TRIANGLES, 0, m_nCand);
            }
            if (m_nSurvol > 0) {
                m_prog.setUniformValue("uCol", QVector3D(1.0f, 0.95f, 0.25f));
                glDrawArrays(GL_TRIANGLES, m_nCand, m_nSurvol);
            }
            glDepthFunc(GL_LESS);
        }
        // Faces choisies comme fonds : vert, par-dessus la pièce
        if (m_nSel > 0) {
            glDepthFunc(GL_LEQUAL);
            m_vboSel.bind();
            m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
            m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
            m_prog.setUniformValue("uCol", QVector3D(0.25f, 0.85f, 0.35f));
            glDrawArrays(GL_TRIANGLES, 0, m_nSel);
            glDepthFunc(GL_LESS);
        }

        // Arêtes
        if (m_nArete > 0) {
            m_vboArete.bind();
            m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
            m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
            m_prog.setUniformValue("uCol", QVector3D(0.10f, 0.11f, 0.13f));
            m_prog.setUniformValue("uLignes", 1.0f);
            glDrawArrays(GL_LINES, 0, m_nArete);
        }
        // Couches : contours de l'ombre à chaque Z, visibles à travers la pièce
        if (!m_plages.isEmpty()) {
            glDisable(GL_DEPTH_TEST);
            glLineWidth(2.0f);
            m_vboCouches.bind();
            m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
            m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
            m_prog.setUniformValue("uLignes", 1.0f);
            for (const Plage &pl : m_plages) {
                if (pl.nb <= 0) continue;
                m_prog.setUniformValue("uCol", pl.col);
                glDrawArrays(GL_LINES, pl.debut, pl.nb);
            }
            glLineWidth(1.0f);
            glEnable(GL_DEPTH_TEST);
        }
        // Brut (cyan) et origine pièce (X rouge, Y vert, Z bleu)
        if (m_aRepere && m_rep.size() >= 30 * 6) {
            glDisable(GL_DEPTH_TEST);
            m_vboRep.bind();
            m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
            m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
            m_prog.setUniformValue("uLignes", 1.0f);
            glLineWidth(1.5f);
            m_prog.setUniformValue("uCol", m_mep == 1 ? QVector3D(0.38f, 0.40f, 0.44f)     // brut grisé
                                                      : QVector3D(0.35f, 0.85f, 0.95f));
            glDrawArrays(GL_LINES, 0, 24);
            glLineWidth(4.0f);
            m_prog.setUniformValue("uCol", QVector3D(1.0f, 0.25f, 0.25f)); glDrawArrays(GL_LINES, 24, 2);
            m_prog.setUniformValue("uCol", QVector3D(0.3f, 1.0f, 0.3f));   glDrawArrays(GL_LINES, 26, 2);
            m_prog.setUniformValue("uCol", QVector3D(0.35f, 0.55f, 1.0f)); glDrawArrays(GL_LINES, 28, 2);
            if (m_aBoitePiece && m_rep.size() >= 54 * 6) {            // encombrement pièce : orange
                glLineWidth(1.5f);
                m_prog.setUniformValue("uCol", QVector3D(1.0f, 0.6f, 0.15f));
                glDrawArrays(GL_LINES, 30, 24);
            }
            glLineWidth(1.0f);
            glEnable(GL_DEPTH_TEST);
        }
        // Faces + des axes machine : translucides + flèches (rouge X, vert Y, bleu Z)
        if (m_aBoitePiece && m_voirAxes && m_axesTri.size() >= 18 * 6) {
            const QVector3D cols[3] = {QVector3D(1.0f, 0.25f, 0.25f), QVector3D(0.3f, 1.0f, 0.3f), QVector3D(0.35f, 0.55f, 1.0f)};
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            m_vboAxTri.bind();
            m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
            m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
            m_prog.setUniformValue("uLignes", 2.0f);
            m_prog.setUniformValue("uAlpha", 0.38f);
            for (int k = 0; k < 3; ++k) { m_prog.setUniformValue("uCol", cols[k]); glDrawArrays(GL_TRIANGLES, k * 6, 6); }
            glDisable(GL_BLEND);
            if (m_axesLig.size() >= 3 * 2 * 6) {                     // tiges des flèches
                glLineWidth(3.5f);
                m_vboAxes.bind();
                m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
                m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
                m_prog.setUniformValue("uLignes", 1.0f);
                for (int k = 0; k < 3; ++k) { m_prog.setUniformValue("uCol", cols[k]); glDrawArrays(GL_LINES, k * 2, 2); }
                glLineWidth(1.0f);
            }
            glEnable(GL_DEPTH_TEST);
        }
        // Cônes des flèches : par-dessus le modèle, éclairés
        if (m_nCone > 0 && !m_cones.isEmpty()) {
            const QVector3D cols[3] = {QVector3D(1.0f, 0.25f, 0.25f), QVector3D(0.3f, 1.0f, 0.3f), QVector3D(0.35f, 0.55f, 1.0f)};
            glClear(GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            m_vboCone.bind();
            m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
            m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
            m_prog.setUniformValue("uLignes", 0.0f);
            const int nFace = m_conesFace.size() / 6 / m_nCone, nTri = m_conesTri.size() / 6 / m_nCone;
            const bool voirFace = m_aBoitePiece && m_voirAxes;
            for (int i = 0; i < nFace + nTri; ++i) {
                if (i < nFace && !voirFace) continue;
                if (i >= nFace && !m_aRepere) continue;
                m_prog.setUniformValue("uCol", cols[(i < nFace ? i : i - nFace) % 3]);
                glDrawArrays(GL_TRIANGLES, i * m_nCone, m_nCone);
            }
        }
        // Points de la boîte du brut (origine) : croix cyan
        if ((m_mep == 2 || m_mep == 4) && !m_ptsBoite.isEmpty()) {
            glDisable(GL_DEPTH_TEST);
            glLineWidth(2.5f);
            m_vboPtsB.bind();
            m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
            m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
            m_prog.setUniformValue("uLignes", 1.0f);
            m_prog.setUniformValue("uCol", m_mep == 4 ? QVector3D(1.0f, 0.65f, 0.2f) : QVector3D(0.55f, 0.95f, 1.0f));
            glDrawArrays(GL_LINES, 0, m_ptsBoite.size() / 6);
            glLineWidth(1.0f);
            glEnable(GL_DEPTH_TEST);
        }
        // Centres des rayons (mode choix de point) : croix jaunes
        if (m_choixPoint && !m_croix.isEmpty()) {
            glDisable(GL_DEPTH_TEST);
            glLineWidth(2.0f);
            m_vboCroix.bind();
            m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
            m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
            m_prog.setUniformValue("uLignes", 1.0f);
            m_prog.setUniformValue("uCol", QVector3D(1.0f, 0.9f, 0.1f));
            glDrawArrays(GL_LINES, 0, m_croix.size() / 6);
            glLineWidth(1.0f);
            glEnable(GL_DEPTH_TEST);
        }
        // Boucles d'arêtes choisies (vert) et survolée (jaune), épaisses, par-dessus
        if (m_nLigChoisi + m_nLigSurvol > 0) {
            glDisable(GL_DEPTH_TEST);
            glLineWidth(4.0f);
            m_vboLigSel.bind();
            m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
            m_prog.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));
            m_prog.setUniformValue("uLignes", 1.0f);
            if (m_nLigChoisi > 0) {
                m_prog.setUniformValue("uCol", QVector3D(0.2f, 0.95f, 0.3f));
                glDrawArrays(GL_LINES, 0, m_nLigChoisi);
            }
            if (m_nLigSurvol > 0) {
                m_prog.setUniformValue("uCol", QVector3D(1.0f, 0.95f, 0.2f));
                glDrawArrays(GL_LINES, m_nLigChoisi, m_nLigSurvol);
            }
            glLineWidth(1.0f);
            glEnable(GL_DEPTH_TEST);
        }
        m_prog.disableAttributeArray(0);
        m_prog.disableAttributeArray(1);
        m_prog.release();
    }
    glDisable(GL_DEPTH_TEST);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    dessinerTriedre(p);
    if (!m_msgSel.isEmpty()) {
        QFont fs = p.font(); fs.setPointSize(12); fs.setBold(true); p.setFont(fs);
        p.setPen(QColor(120, 230, 140));
        p.drawText(QRect(12, 10, width() - 24, 26), Qt::AlignLeft | Qt::AlignVCenter, m_msgSel);
    }
    p.setPen(QColor(200, 205, 215));
    QFont f = p.font(); f.setPointSize(9); p.setFont(f);
    p.drawText(QRect(10, height() - 26, width() - 20, 20), Qt::AlignRight | Qt::AlignVCenter,
               m_info + m_infoCouches + QString::fromUtf8("   |   gauche : tourner   droit : déplacer   molette : zoom   "
                                          "double-clic : cadrer   1 dessus  2 face  3 droite  0 iso"));
}

void CadView3D::dessinerTriedre(QPainter &p)
{
    const QMatrix4x4 r = rotation();
    const QPointF o(50, height() - 60);
    const struct { QVector3D v; QColor c; const char *n; } ax[3] = {
        {m_axX, QColor(230, 60, 60), "X"},          // axes machine choisis
        {m_axY, QColor(60, 200, 80), "Y"},
        {m_axZ, QColor(70, 140, 255), "Z"} };
    QFont f = p.font(); f.setBold(true); f.setPointSize(10); p.setFont(f);
    for (const auto &a : ax) {
        const QVector3D d = r.map(a.v);
        const QPointF dir(d.x(), -d.y());
        const QPointF e = o + dir * 32;
        const QPointF b = o + dir * 22;
        p.setPen(QPen(a.c, 2.5));
        p.drawLine(o, b);
        const double lg = std::hypot(dir.x(), dir.y());
        if (lg > 0.05) {                                  // pointe en cône (triangle plein)
            const QPointF nrm(-dir.y() / lg * 5.0, dir.x() / lg * 5.0);
            QPolygonF tri; tri << e << b + nrm << b - nrm;
            p.setBrush(a.c); p.setPen(Qt::NoPen);
            p.drawPolygon(tri);
            p.setBrush(Qt::NoBrush);
        }
        p.setPen(QPen(a.c, 2.5));
        p.drawText(e + QPointF(d.x() * 8 - 4, -d.y() * 8 + 4), a.n);
    }
}

void CadView3D::mousePressEvent(QMouseEvent *e) { m_dernier = e->pos(); m_appui = e->pos(); setFocus(); }

void CadView3D::mouseReleaseEvent(QMouseEvent *e)
{
    const bool clic = e->button() == Qt::LeftButton && (e->pos() - m_appui).manhattanLength() <= 4;
    // Sens des axes : face de la boîte pièce sous la souris
    if (m_mep == 1 && clic && m_aBoitePiece) {
        const QMatrix4x4 inv = (projection() * vue()).inverted();
        const float x = 2.0f * e->pos().x() / qMax(1, width()) - 1.0f;
        const float y = 1.0f - 2.0f * e->pos().y() / qMax(1, height());
        const QVector3D o = inv.map(QVector3D(x, y, -1.0f));
        const QVector3D d = (inv.map(QVector3D(x, y, 1.0f)) - o).normalized();
        float tBest = 1e30f; int aB = -1; bool pB = false;
        for (int ax = 0; ax < 3; ++ax) for (int sg = 0; sg < 2; ++sg) {
            const float plan = sg ? m_pMax[ax] : m_pMin[ax];
            if (std::fabs(d[ax]) < 1e-9f) continue;
            const float t = (plan - o[ax]) / d[ax];
            if (t <= 0 || t >= tBest) continue;
            const QVector3D q = o + d * t;
            const int a1 = (ax + 1) % 3, a2 = (ax + 2) % 3;
            if (q[a1] < m_pMin[a1] - 1e-4f || q[a1] > m_pMax[a1] + 1e-4f) continue;
            if (q[a2] < m_pMin[a2] - 1e-4f || q[a2] > m_pMax[a2] + 1e-4f) continue;
            tBest = t; aB = ax; pB = sg;
        }
        if (aB >= 0) { emit faceBoiteChoisie(1, aB, pB); update(); }
        return;
    }
    // Origine sur la boîte du brut : croix la plus proche
    if ((m_mep == 2 || m_mep == 4) && clic) {
        const QMatrix4x4 mvp = projection() * vue();
        int best = -1; double dB = 22.0;
        for (int i = 0; i < m_ptsB.size(); ++i) {
            const QVector3D n = mvp.map(m_ptsB[i]);
            const double sx = (n.x() + 1.0) * 0.5 * width(), sy = (1.0 - n.y()) * 0.5 * height();
            const double dd = std::hypot(sx - e->pos().x(), sy - e->pos().y());
            if (dd < dB) { dB = dd; best = i; }
        }
        if (best >= 0) emit pointBoiteChoisi(best % 3, (best / 3) % 3, best / 9);
        update();
        return;
    }
    if (m_mep == 1 || m_mep == 2 || m_mep == 4) return;
    // clic simple (pas de glissé) en mode sélection -> choisir / enlever la face
    if (m_choixPoint && e->button() == Qt::LeftButton && (e->pos() - m_appui).manhattanLength() <= 4) {
        // 1) centre d'un rayon (croix jaune), 2) sommet, 3) point sur face,
        // 4) face d'une boîte (brut / pièce) -> un seul axe
        const QMatrix4x4 mvp = projection() * vue();
        auto ecran = [&](const QVector3D &q) {
            const QVector3D n = mvp.map(q);
            return QPointF((n.x() + 1.0) * 0.5 * width(), (1.0 - n.y()) * 0.5 * height());
        };
        int iC = -1; double dC = 12.0;
        for (int i = 0; i < m_centres.size(); ++i) {
            const QPointF sp = ecran(m_centres[i]);
            const double d = std::hypot(sp.x() - e->pos().x(), sp.y() - e->pos().y());
            if (d < dC) { dC = d; iC = i; }
        }
        QVector3D p; QString quoi;
        if (iC >= 0) { p = m_centres[iC]; quoi = "centre de rayon"; }
        else {
            const int s = sommetPointe(e->pos());
            if (s >= 0) { p = m_pts[s]; quoi = "sommet"; }
            else if (facePointee(e->pos(), &p) >= 0) quoi = "sur face";
        }
        if (!quoi.isEmpty()) {
            m_msgSel = QString::fromUtf8("Point (%1) : X %2  Y %3  Z %4").arg(quoi)
                           .arg(p.x(), 0, 'f', 3).arg(p.y(), 0, 'f', 3).arg(p.z(), 0, 'f', 3);
            emit pointChoisi(p);
            update();
            return;
        }
        // Faces des boîtes : rayon souris -> face la plus proche
        if (m_aBoitePiece && m_mep == 0) {
            const QMatrix4x4 inv = mvp.inverted();
            const float x = 2.0f * e->pos().x() / qMax(1, width()) - 1.0f;
            const float y = 1.0f - 2.0f * e->pos().y() / qMax(1, height());
            const QVector3D o = inv.map(QVector3D(x, y, -1.0f));
            const QVector3D d = (inv.map(QVector3D(x, y, 1.0f)) - o).normalized();
            float tBest = 1e30f; int bB = -1, aB = -1; bool pB = false;
            const QVector3D mins[2] = {m_bMin, m_pMin}, maxs[2] = {m_bMax, m_pMax};
            for (int bo = 0; bo < 2; ++bo) for (int ax = 0; ax < 3; ++ax) for (int sg = 0; sg < 2; ++sg) {
                const float plan = sg ? maxs[bo][ax] : mins[bo][ax];
                if (std::fabs(d[ax]) < 1e-9f) continue;
                const float t = (plan - o[ax]) / d[ax];
                if (t <= 0 || t >= tBest) continue;
                const QVector3D q = o + d * t;
                const int a1 = (ax + 1) % 3, a2 = (ax + 2) % 3;
                if (q[a1] < mins[bo][a1] - 1e-4f || q[a1] > maxs[bo][a1] + 1e-4f) continue;
                if (q[a2] < mins[bo][a2] - 1e-4f || q[a2] > maxs[bo][a2] + 1e-4f) continue;
                tBest = t; bB = bo; aB = ax; pB = sg;
            }
            if (bB >= 0) {
                m_msgSel = QString::fromUtf8("Face %1 %2%3 du %4").arg(QChar("XYZ"[aB])).arg(QChar("XYZ"[aB]))
                               .arg(pB ? "+" : "−").arg(bB == 0 ? "brut" : "pièce");
                m_msgSel = QString::fromUtf8("Face %1%2 du %3").arg(QChar("XYZ"[aB])).arg(pB ? "+" : "−")
                               .arg(bB == 0 ? "brut" : "pièce");
                emit faceBoiteChoisie(bB, aB, pB);
                update();
            }
        }
        return;
    }
    if (!m_selActive || e->button() != Qt::LeftButton) return;
    if ((e->pos() - m_appui).manhattanLength() > 4) return;
    const int s = aretePointee(e->pos());
    if (s >= 0) {
        const QVector<int> b = boucle(s);
        const bool deja = m_segChoisis.contains(s);
        for (int x : b) { if (deja) m_segChoisis.remove(x); else m_segChoisis.insert(x); }
    } else {
        const int t = facePointee(e->pos());
        if (t < 0 || t >= m_triFace.size()) return;
        const int f = m_triFace[t];
        if (m_choisies.contains(f)) m_choisies.remove(f); else m_choisies.insert(f);
    }
    const int nb = contoursChoisis().size();
    const double zb = zBasSelection();
    m_msgSel = selectionVide() ? QString::fromUtf8("Rien de choisi — cliquez une arête ou des faces.")
        : QString::fromUtf8("Zone : %1 contour(s), %2 face(s)   —   Z bas %3")
              .arg(nb).arg(m_choisies.size()).arg(zb, 0, 'f', 3);
    majSelection();
    emit facesChanged();
    update();
}

void CadView3D::mouseMoveEvent(QMouseEvent *e)
{
    if (m_selActive && e->buttons() == Qt::NoButton) {
        // survol : la face plane sous la souris s'allume en jaune
        const int s = aretePointee(e->pos());
        QVector<int> b = s >= 0 ? boucle(s) : QVector<int>();
        int f = -1;
        if (b.isEmpty()) {
            const int t = facePointee(e->pos());
            f = (t >= 0 && t < m_triFace.size()) ? m_triFace[t] : -1;
        }
        if (f != m_faceSurvol || b != m_segSurvol) {
            m_faceSurvol = f; m_segSurvol = b; majSelection(); update();
        }
        return;
    }
    const QPoint d = e->pos() - m_dernier;
    m_dernier = e->pos();
    if (e->buttons() & Qt::LeftButton) {
        m_lacet += d.x() * 0.4f;
        m_tangage = qBound(0.0f, m_tangage - d.y() * 0.4f, 180.0f);
    } else if (e->buttons() & (Qt::RightButton | Qt::MiddleButton)) {
        const float k = 2.0f * m_echelle / qMax(1, height());
        m_pan += QVector3D(-d.x() * k, d.y() * k, 0);
    }
    update();
}

void CadView3D::mouseDoubleClickEvent(QMouseEvent *) { cadrer(); }

void CadView3D::wheelEvent(QWheelEvent *e)
{
    const float f = std::pow(0.88f, e->angleDelta().y() / 120.0f);
    m_echelle = qBound(m_rayon * 0.01f, m_echelle * f, m_rayon * 20.0f);
    update();
}

void CadView3D::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_1: vueDessus(); break;
    case Qt::Key_2: vueFace();   break;
    case Qt::Key_3: vueDroite(); break;
    case Qt::Key_0: vueIso();    break;
    default: QOpenGLWidget::keyPressEvent(e);
    }
}

// ── Graphe des arêtes : points fusionnés (tolérance 1 µm) ──────
void CadView3D::construireGraphe()
{
    m_pts.clear(); m_segs.clear(); m_segsDuPt.clear();
    QHash<qint64, int> index;
    auto cle = [](const QVector3D &p) -> qint64 {
        const qint64 x = qRound64(p.x() * 1000.0), y = qRound64(p.y() * 1000.0), z = qRound64(p.z() * 1000.0);
        return (x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL);
    };
    auto pt = [&](const QVector3D &p) -> int {
        const qint64 k = cle(p);
        auto it = index.constFind(k);
        if (it != index.constEnd() && (m_pts[it.value()] - p).lengthSquared() < 1e-5f) return it.value();
        m_pts << p; m_segsDuPt << QVector<int>();
        index.insert(k, m_pts.size() - 1);
        return m_pts.size() - 1;
    };
    for (int i = 0; i + 11 < m_aretes.size(); i += 12) {
        const QVector3D a(m_aretes[i], m_aretes[i + 1], m_aretes[i + 2]);
        const QVector3D b(m_aretes[i + 6], m_aretes[i + 7], m_aretes[i + 8]);
        const int ia = pt(a), ib = pt(b);
        if (ia == ib) continue;
        m_segs << qMakePair(ia, ib);
        m_segsDuPt[ia] << m_segs.size() - 1;
        m_segsDuPt[ib] << m_segs.size() - 1;
    }
}

int CadView3D::aretePointee(const QPoint &pos) const
{
    if (m_segs.isEmpty()) return -1;
    const QMatrix4x4 mvp = projection() * vue();
    const float W = float(width()), H = float(height());
    auto ecran = [&](const QVector3D &p) {
        const QVector3D n = mvp.map(p);
        return QPointF((n.x() + 1.0f) * 0.5f * W, (1.0f - n.y()) * 0.5f * H);
    };
    const QPointF m(pos);
    int best = -1; double dBest = 7.0;
    for (int s = 0; s < m_segs.size(); ++s) {
        const QPointF a = ecran(m_pts[m_segs[s].first]), b = ecran(m_pts[m_segs[s].second]);
        const QPointF ab = b - a;
        const double L2 = ab.x() * ab.x() + ab.y() * ab.y();
        double t = L2 > 1e-9 ? ((m.x() - a.x()) * ab.x() + (m.y() - a.y()) * ab.y()) / L2 : 0.0;
        t = qBound(0.0, t, 1.0);
        const QPointF q = a + ab * t - m;
        const double d = std::sqrt(q.x() * q.x() + q.y() * q.y());
        if (d < dBest) { dBest = d; best = s; }
    }
    // l'arête doit être visible : pas cachée derrière une face plus proche
    if (best >= 0) {
        const int t = facePointee(pos);
        if (t >= 0) {
            const QMatrix4x4 v = vue();
            const float *p = m_tri.constData() + t * 18;
            const float zFace = v.map(QVector3D(p[0], p[1], p[2])).z();
            const float zA = qMax(v.map(m_pts[m_segs[best].first]).z(), v.map(m_pts[m_segs[best].second]).z());
            if (zA < zFace - 0.05f * m_rayon) return -1;
        }
    }
    return best;
}

// Boucle d'un segment : si l'arête est horizontale, toutes les arêtes reliées
// dans le même plan Z ; sinon la chaîne qui continue sans embranchement.
QVector<int> CadView3D::boucle(int seg) const
{
    QVector<int> res;
    if (seg < 0 || seg >= m_segs.size()) return res;
    const float za = m_pts[m_segs[seg].first].z(), zb = m_pts[m_segs[seg].second].z();
    const bool plat = std::fabs(za - zb) < 0.01f;
    QSet<int> vu; QVector<int> pile{seg}; vu.insert(seg);
    while (!pile.isEmpty()) {
        const int s = pile.takeLast();
        res << s;
        for (int k : {m_segs[s].first, m_segs[s].second}) {
            const QVector<int> &voisins = m_segsDuPt[k];
            if (!plat && voisins.size() != 2) continue;          // embranchement : on s'arrête
            for (int n : voisins) {
                if (vu.contains(n)) continue;
                if (plat && (std::fabs(m_pts[m_segs[n].first].z() - za) > 0.01f ||
                             std::fabs(m_pts[m_segs[n].second].z() - za) > 0.01f)) continue;
                vu.insert(n); pile << n;
            }
        }
    }
    std::sort(res.begin(), res.end());
    return res;
}

// Contours choisis, ordonnés (une liste de points par boucle)
QVector<QVector<QVector3D>> CadView3D::contoursChoisis() const
{
    QVector<QVector<QVector3D>> out;
    QSet<int> reste = m_segChoisis;
    while (!reste.isEmpty()) {
        int s = *reste.begin(); reste.remove(s);
        QVector<QVector3D> ch{m_pts[m_segs[s].first], m_pts[m_segs[s].second]};
        int bout = m_segs[s].second;
        bool avance = true;
        while (avance) {
            avance = false;
            for (int n : m_segsDuPt[bout]) {
                if (!reste.contains(n)) continue;
                reste.remove(n);
                bout = (m_segs[n].first == bout) ? m_segs[n].second : m_segs[n].first;
                ch << m_pts[bout];
                avance = true;
                break;
            }
        }
        out << ch;
    }
    return out;
}

void CadView3D::setContoursChoisis(const QVector<QVector<QVector3D>> &cts)
{
    m_segChoisis.clear();
    for (const auto &ct : cts)
        for (int i = 0; i + 1 < ct.size(); ++i)
            for (int s = 0; s < m_segs.size(); ++s) {
                const QVector3D &a = m_pts[m_segs[s].first], &b = m_pts[m_segs[s].second];
                if (((a - ct[i]).lengthSquared() < 1e-4f && (b - ct[i + 1]).lengthSquared() < 1e-4f) ||
                    ((b - ct[i]).lengthSquared() < 1e-4f && (a - ct[i + 1]).lengthSquared() < 1e-4f)) {
                    m_segChoisis.insert(s); break;
                }
            }
    majSelection();
    update();
}

double CadView3D::zBasSelection() const
{
    double z = 1e30;
    for (int s : m_segChoisis)
        z = qMin(z, double(qMin(m_pts[m_segs[s].first].z(), m_pts[m_segs[s].second].z())));
    for (int t = 0; t < m_triFace.size() && (t + 1) * 18 <= m_tri.size(); ++t)
        if (m_choisies.contains(m_triFace[t]))
            for (int k = 0; k < 3; ++k) z = qMin(z, double(m_tri[t * 18 + k * 6 + 2]));
    return z;
}

void CadView3D::setRepere(const QVector3D &o, const QVector3D &a, const QVector3D &b)
{
    m_rep.clear();
    m_aBoitePiece = false;
    auto P = [this](float x, float y, float z) { m_rep << x << y << z << 0.f << 0.f << 1.f; };
    const float xs[2] = {a.x(), b.x()}, ys[2] = {a.y(), b.y()}, zs[2] = {a.z(), b.z()};
    for (int j = 0; j < 2; ++j) for (int k = 0; k < 2; ++k) {          // 12 arêtes de la boîte
        P(xs[0], ys[j], zs[k]); P(xs[1], ys[j], zs[k]);
        P(xs[j], ys[0], zs[k]); P(xs[j], ys[1], zs[k]);
        P(xs[j], ys[k], zs[0]); P(xs[j], ys[k], zs[1]);
    }
    const float L = qMax(5.0f, m_rayon * 0.25f);
    m_conesTri.clear();
    for (const QVector3D &ax : {m_axX, m_axY, m_axZ}) {      // trièdre = axes machine
        P(o.x(), o.y(), o.z());
        const QVector3D q = o + ax * (L * 0.78f);
        P(q.x(), q.y(), q.z());
        ajouterCone(m_conesTri, q, o + ax * L, L * 0.09f);
    }
    majCones();
    m_aRepere = true;
    m_aTeleverser = true;
    update();
}

void CadView3D::setRepere(const QVector3D &o, const QVector3D &a, const QVector3D &b,
                          const QVector3D &pa, const QVector3D &pb)
{
    setRepere(o, a, b);
    m_bMin = a; m_bMax = b; m_pMin = pa; m_pMax = pb;
    // Centres des rayons : sur la pièce + projetés sur le dessus de la boîte
    // pièce et sur le dessus du brut (croix jaunes, cliquables)
    {
        QVector<QVector3D> proj;
        for (const QVector3D &q : m_centresPiece) {
            proj << q << QVector3D(q.x(), q.y(), pb.z()) << QVector3D(q.x(), q.y(), b.z());
        }
        m_centres = proj;
        m_croix.clear();
        const float t = qMax(0.6f, m_rayon * 0.012f);
        auto P = [this](const QVector3D &p) { m_croix << p.x() << p.y() << p.z() << 0.f << 0.f << 1.f; };
        for (const QVector3D &q : m_centres) {
            P(q - QVector3D(t, 0, 0)); P(q + QVector3D(t, 0, 0));
            P(q - QVector3D(0, t, 0)); P(q + QVector3D(0, t, 0));
        }
    }
    auto P = [this](float x, float y, float z) { m_rep << x << y << z << 0.f << 0.f << 1.f; };
    const float xs[2] = {pa.x(), pb.x()}, ys[2] = {pa.y(), pb.y()}, zs[2] = {pa.z(), pb.z()};
    for (int j = 0; j < 2; ++j) for (int k = 0; k < 2; ++k) {
        P(xs[0], ys[j], zs[k]); P(xs[1], ys[j], zs[k]);
        P(xs[j], ys[0], zs[k]); P(xs[j], ys[1], zs[k]);
        P(xs[j], ys[k], zs[0]); P(xs[j], ys[k], zs[1]);
    }
    m_aBoitePiece = true;
    majFacesAxes();
    majPointsBoite();
    m_aTeleverser = true;
    update();
}

void CadView3D::setAxesMachine(const QVector3D &ax, const QVector3D &ay, const QVector3D &az)
{
    m_axX = ax; m_axY = ay; m_axZ = az;
    majFacesAxes();
    m_aTeleverser = true;
    update();
}

// Face + de chaque axe machine sur la boîte pièce : 2 triangles translucides
// + une flèche (tige + 4 traits de pointe) partant du centre de la face
void CadView3D::majFacesAxes()
{
    m_axesLig.clear();
    m_axesTri.clear();
    m_conesFace.clear();
    if (!m_aBoitePiece) return;
    float taille = 0;
    for (int k = 0; k < 3; ++k) taille = qMax(taille, m_pMax[k] - m_pMin[k]);
    const float L = qMax(8.0f, taille * 0.30f);
    auto P = [](QVector<float> &v, const QVector3D &p) { v << p.x() << p.y() << p.z() << 0.f << 0.f << 1.f; };
    for (const QVector3D &n : {m_axX, m_axY, m_axZ}) {
        int a = 0; for (int k = 1; k < 3; ++k) if (std::fabs(n[k]) > std::fabs(n[a])) a = k;
        const float plan = n[a] > 0 ? m_pMax[a] : m_pMin[a];
        const int a1 = (a + 1) % 3, a2 = (a + 2) % 3;
        auto C = [&](int i, int j) { QVector3D q; q[a] = plan; q[a1] = i ? m_pMax[a1] : m_pMin[a1]; q[a2] = j ? m_pMax[a2] : m_pMin[a2]; return q; };
        P(m_axesTri, C(0,0)); P(m_axesTri, C(1,0)); P(m_axesTri, C(1,1));
        P(m_axesTri, C(0,0)); P(m_axesTri, C(1,1)); P(m_axesTri, C(0,1));
        QVector3D c; c[a] = plan; c[a1] = 0.5f * (m_pMin[a1] + m_pMax[a1]); c[a2] = 0.5f * (m_pMin[a2] + m_pMax[a2]);
        const QVector3D dir = n.normalized();
        const QVector3D pt = c + dir * L, b = c + dir * (L * 0.72f);
        QVector3D u, w; u[a1] = L * 0.12f; w[a2] = L * 0.12f;
        Q_UNUSED(u); Q_UNUSED(w);
        P(m_axesLig, c);  P(m_axesLig, b);
        ajouterCone(m_conesFace, b, pt, L * 0.10f);
    }
    majCones();
}

// Cône plein (côtés + base), normales pour l'éclairage
void CadView3D::ajouterCone(QVector<float> &v, const QVector3D &base, const QVector3D &pointe, float rayon)
{
    const int N = 20;
    const QVector3D d = (pointe - base).normalized();
    QVector3D u = QVector3D::crossProduct(d, std::fabs(d.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(1, 0, 0)).normalized();
    const QVector3D w = QVector3D::crossProduct(d, u);
    const float h = (pointe - base).length();
    auto S = [&v](const QVector3D &p, const QVector3D &n) { v << p.x() << p.y() << p.z() << n.x() << n.y() << n.z(); };
    for (int i = 0; i < N; ++i) {
        const float a0 = 2.0f * 3.14159265f * i / N, a1 = 2.0f * 3.14159265f * (i + 1) / N;
        const QVector3D r0 = u * std::cos(a0) + w * std::sin(a0), r1 = u * std::cos(a1) + w * std::sin(a1);
        const QVector3D p0 = base + r0 * rayon, p1 = base + r1 * rayon;
        const QVector3D n0 = (r0 * h + d * rayon).normalized(), n1 = (r1 * h + d * rayon).normalized();
        S(pointe, (n0 + n1).normalized()); S(p0, n0); S(p1, n1);
        S(base, -d); S(p1, -d); S(p0, -d);
    }
    m_nCone = N * 6;
}

void CadView3D::majCones()
{
    m_cones = m_conesFace;
    m_cones += m_conesTri;
    m_aTeleverser = true;
}

// 27 points de la boîte du brut (coins, milieux d'arêtes, centres de faces, centre)
void CadView3D::majPointsBoite()
{
    m_ptsB.clear(); m_ptsBoite.clear();
    if (!m_aRepere) return;
    const float t = qMax(0.8f, m_rayon * 0.02f);
    auto P = [this](const QVector3D &p) { m_ptsBoite << p.x() << p.y() << p.z() << 0.f << 0.f << 1.f; };
    for (int iz = 0; iz < 3; ++iz) for (int iy = 0; iy < 3; ++iy) for (int ix = 0; ix < 3; ++ix) {
        const int id[3] = {ix, iy, iz};
        QVector3D q;
        const QVector3D &mn = m_mep == 4 ? m_pMin : m_bMin, &mx = m_mep == 4 ? m_pMax : m_bMax;
        for (int k = 0; k < 3; ++k) q[k] = mn[k] + 0.5f * id[k] * (mx[k] - mn[k]);
        m_ptsB << q;
        P(q - QVector3D(t, 0, 0)); P(q + QVector3D(t, 0, 0));
        P(q - QVector3D(0, t, 0)); P(q + QVector3D(0, t, 0));
        P(q - QVector3D(0, 0, t)); P(q + QVector3D(0, 0, t));
    }
}

void CadView3D::setModeMiseEnPlace(int mode)
{
    m_mep = mode;
    m_choixPoint = (mode == 3);
    m_msgSel = mode == 1 ? QString::fromUtf8("Sens des axes : bouton X+ / Y+ / Z+ puis clic sur une face de la boîte orange")
             : mode == 2 ? QString::fromUtf8("Origine : cliquez une croix de la boîte du brut (coin, milieu, centre)")
             : mode == 4 ? QString::fromUtf8("Origine : cliquez une croix orange de l'encombrement pièce, ou le pavé")
             : mode == 3 ? QString::fromUtf8("Origine : cliquez un SOMMET, un CENTRE de rayon (croix jaune) ou un point d'une face")
             : QString();
    majPointsBoite();
    m_aTeleverser = true;
    update();
}

void CadView3D::setVoirAxes(bool v) { m_voirAxes = v; update(); }

void CadView3D::setChoixPoint(bool actif)
{
    m_choixPoint = actif;
    m_msgSel = actif ? QString::fromUtf8("Origine : cliquez un CENTRE de rayon (croix jaune), un SOMMET, un point de la pièce, "
                                         "ou une FACE du brut / de la pièce (un seul axe). Boutons en bas.")
                     : QString();
    update();
}

int CadView3D::sommetPointe(const QPoint &pos) const
{
    if (m_pts.isEmpty()) return -1;
    const QMatrix4x4 mvp = projection() * vue();
    const float W = float(width()), H = float(height());
    int best = -1; double dBest = 10.0;
    for (int i = 0; i < m_pts.size(); ++i) {
        const QVector3D n = mvp.map(m_pts[i]);
        const double sx = (n.x() + 1.0) * 0.5 * W, sy = (1.0 - n.y()) * 0.5 * H;
        const double d = std::hypot(sx - pos.x(), sy - pos.y());
        if (d < dBest) { dBest = d; best = i; }
    }
    if (best >= 0) {                           // visible : pas caché derrière une face plus proche
        QVector3D hit;
        if (facePointee(pos, &hit) >= 0) {
            const QMatrix4x4 v = vue();
            if (v.map(m_pts[best]).z() < v.map(hit).z() - 0.05f * m_rayon) return -1;
        }
    }
    return best;
}
