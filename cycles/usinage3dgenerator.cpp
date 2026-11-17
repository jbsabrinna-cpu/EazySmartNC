#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  usinage3dgenerator.cpp — voir usinage3dgenerator.h
// ═══════════════════════════════════════════════════════════════
#include "usinage3dgenerator.h"
#include "../cadimporter.h"
#include "../tooldatabase.h"
#include "../clipper2/clipper.h"
#include "../contourelement.h"
#include "../uniteditor.h"
#include "contourgenerator.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QRegularExpression>

#include <QJsonArray>
#include <QMap>
#include <QFileInfo>
#include <QtMath>
#include <QDebug>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

using namespace Clipper2Lib;

PathsD versClip(const Contours2D &cs)
{
    PathsD r;
    for (const Contour2D &c : cs) {
        PathD p;
        for (const QPointF &q : c) p.push_back(PointD(q.x(), q.y()));
        if (p.size() >= 3) r.push_back(p);
    }
    return r;
}

// Bords de contours fermés, coupés par une zone : morceaux ouverts
PathsD bordsDans(const PathsD &fermes, const PathsD &zone)
{
    PathsD ouverts;
    for (PathD p : fermes) {
        if (p.size() < 3) continue;
        p.push_back(p.front());                 // refermer en chemin ouvert
        ouverts.push_back(p);
    }
    ClipperD c(4);
    c.AddOpenSubject(ouverts);
    c.AddClip(zone);
    PathsD ferm, ouv;
    c.Execute(ClipType::Intersection, FillRule::NonZero, ferm, ouv);
    PathsD res;
    for (const PathD &p : ouv) if (p.size() >= 2) res.push_back(p);
    return res;
}

double longueur(const PathD &p)
{
    double L = 0;
    for (size_t i = 1; i < p.size(); ++i) L += std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
    return L;
}

struct Outil {
    int T = 0; double R = 0; int dents = 0; bool boule = false; QString nom;
};

Outil lireOutil(const QString &t)
{
    Outil o;
    bool ok = false;
    o.T = t.trimmed().toInt(&ok);
    if (!ok || o.T <= 0) { o.T = 0; return o; }
    if (const ToolData *td = ToolDatabase::instance().toolByT(o.T)) {
        o.R = td->Routil; o.dents = td->nbDents; o.nom = td->description;
        o.boule = (td->toolType == ToolData::Boule);
    }
    return o;
}

double tr(const QJsonArray &a, int i) { return a.size() > i ? a[i].toDouble() : 0.0; }

// Douglas-Peucker sur une suite de points 3D (garde les extrémités)
void simplifier(const QVector<QVector3D> &in, double tol, QVector<QVector3D> &out)
{
    const int n = in.size();
    if (n <= 2) { out = in; return; }
    QVector<bool> garde(n, false);
    garde[0] = garde[n - 1] = true;
    QVector<QPair<int, int>> pile{{0, n - 1}};
    while (!pile.isEmpty()) {
        auto [a, b] = pile.takeLast();
        const QVector3D A = in[a], B = in[b], AB = B - A;
        const float L2 = AB.lengthSquared();
        double dMax = -1; int iMax = -1;
        for (int i = a + 1; i < b; ++i) {
            const QVector3D AP = in[i] - A;
            const float t = L2 > 1e-12f ? qBound(0.0f, QVector3D::dotProduct(AP, AB) / L2, 1.0f) : 0.0f;
            const double d = (AP - AB * t).length();
            if (d > dMax) { dMax = d; iMax = i; }
        }
        if (dMax > tol && iMax > 0) { garde[iMax] = true; pile << qMakePair(a, iMax) << qMakePair(iMax, b); }
    }
    out.clear();
    for (int i = 0; i < n; ++i) if (garde[i]) out << in[i];
}

} // namespace

// ═══════════════════════════════════════════════════════════════
void Orientation3D::appliquer(CadDocument &doc) const
{
    if (identite()) return;
    auto tourne = [this](float *v) {
        const QVector3D q = versMachine(QVector3D(v[0], v[1], v[2]));
        v[0] = q.x(); v[1] = q.y(); v[2] = q.z();
    };
    for (int i = 0; i + 5 < doc.mesh.size(); i += 6) { tourne(doc.mesh.data() + i); tourne(doc.mesh.data() + i + 3); }
    for (int i = 0; i + 2 < doc.edges3d.size(); i += 3) tourne(doc.edges3d.data() + i);
    for (int i = 0; i + 2 < doc.centres3d.size(); i += 3) tourne(doc.centres3d.data() + i);
    // un quart de tour peut inverser l'ordre des sommets : on garde les
    // normales (déjà tournées), l'ordre n'est pas utilisé par les calculs
    for (int k = 0; k < 3; ++k) { doc.bbMin[k] = 1e30f; doc.bbMax[k] = -1e30f; }
    for (int i = 0; i + 5 < doc.mesh.size(); i += 6)
        for (int k = 0; k < 3; ++k) { doc.bbMin[k] = qMin(doc.bbMin[k], doc.mesh[i + k]); doc.bbMax[k] = qMax(doc.bbMax[k], doc.mesh[i + k]); }
}

// ═══════════════════════════════════════════════════════════════
void Usinage3DGenerator::origineEtBrut(const QJsonObject &d, const double pMin[3], const double pMax[3],
                                       double o[3], double bMin[3], double bMax[3])
{
    if (d["brutMode"].toInt(0) == 0) {
        const double sx = d["brutSx"].toDouble(5.0), sy = d["brutSy"].toDouble(5.0);
        bMin[0] = pMin[0] - sx; bMax[0] = pMax[0] + sx;
        bMin[1] = pMin[1] - sy; bMax[1] = pMax[1] + sy;
        bMin[2] = pMin[2] - d["brutZm"].toDouble(3.0); bMax[2] = pMax[2] + d["brutZp"].toDouble(1.0);
    } else {
        const double cx = (pMin[0] + pMax[0]) / 2, cy = (pMin[1] + pMax[1]) / 2;
        const double L = d["brutL"].toDouble(220.0), W = d["brutW"].toDouble(70.0), H = d["brutH"].toDouble(15.0);
        bMin[0] = cx - L / 2; bMax[0] = cx + L / 2; bMin[1] = cy - W / 2; bMax[1] = cy + W / 2;
        if (d["brutZal"].toInt(0) == 1) { bMin[2] = pMin[2]; bMax[2] = bMin[2] + H; }   // pièce en bas du brut
        else { bMax[2] = pMax[2] + d["brutZp"].toDouble(1.0); bMin[2] = bMax[2] - H; }  // en haut (+ surép.)
    }
    // Méthode « point choisi en 3D » : chaque axe a sa référence
    //  valeur (point cliqué : sommet, centre de rayon, face), bmin/bmax
    //  (faces du brut), pmin/pmax (faces de la pièce), pmil/bmil (milieux)
    if (d["origMode"].toInt(0) == 1) {
        const QJsonArray pt = d["origPoint"].toArray(), rf = d["origRef3"].toArray();
        for (int k = 0; k < 3; ++k) {
            const QString tag = rf.size() == 3 ? rf[k].toString() : QString("valeur");
            if      (tag == "bmin") o[k] = bMin[k];
            else if (tag == "bmax") o[k] = bMax[k];
            else if (tag == "pmin") o[k] = pMin[k];
            else if (tag == "pmax") o[k] = pMax[k];
            else if (tag == "pmil") o[k] = (pMin[k] + pMax[k]) / 2;
            else if (tag == "bmil") o[k] = (bMin[k] + bMax[k]) / 2;
            else o[k] = pt.size() == 3 ? pt[k].toDouble() : (k == 2 ? bMax[2] : (bMin[k] + bMax[k]) / 2);
        }
        return;
    }
    const int pos = d["origPos"].toInt(7);
    const bool refBrut = d["origRef"].toInt(0) == 0;
    const double *rMin = refBrut ? bMin : pMin, *rMax = refBrut ? bMax : pMax;
    const int colX = (pos - 1) % 3, rowY = (pos - 1) / 3;
    o[0] = rMin[0] + colX * 0.5 * (rMax[0] - rMin[0]);
    o[1] = rMin[1] + rowY * 0.5 * (rMax[1] - rMin[1]);
    const int oz = d["origZ"].toInt(0);
    o[2] = oz == 0 ? bMax[2] : (oz == 1 ? pMax[2] : pMin[2]);
}

// ═══════════════════════════════════════════════════════════════
//  Plan d'ébauche : niveaux + ombre + zone à vider
// ═══════════════════════════════════════════════════════════════
Usinage3DPlan Usinage3DGenerator::plan(const QJsonObject &d, const CadDocument &doc, double rayonEb)
{
    Usinage3DPlan pl;
    if (doc.mesh.isEmpty()) { pl.erreur = "modele 3D vide ou introuvable"; return pl; }
    // Origine + brut recalculés depuis l'encombrement réel de la pièce
    {
        const double pMin[3] = {doc.bbMin[0], doc.bbMin[1], doc.bbMin[2]};
        const double pMax[3] = {doc.bbMax[0], doc.bbMax[1], doc.bbMax[2]};
        origineEtBrut(d, pMin, pMax, pl.origine, pl.brutMin, pl.brutMax);
    }

    double zBas = d["zBasModele"].toDouble(0.0);
    const bool zone = !d["contoursZone"].toArray().isEmpty() || !d["facesZone"].toArray().isEmpty();
    if (!zone && std::fabs(zBas) < 1e-9) zBas = doc.bbMin[2];      // défaut : dessous de la pièce
    pl.zFin = zBas - d["decoupeSous"].toDouble(0.5);
    const double zHaut = pl.brutMax[2];
    const double passe = qMax(0.05, d["passeEb"].toDouble(2.0));
    const double sFond = d["surepFond"].toDouble(0.2);
    const double sLat  = d["surepLat"].toDouble(0.3);

    // Fonds plats : faces B-rep dont toutes les normales sont vers +Z
    QMap<int, bool> plane; QMap<int, double> zs; QMap<int, int> nb; QMap<int, double> aireF;
    for (int t = 0; t < doc.meshFace.size() && (t + 1) * 18 <= doc.mesh.size(); ++t) {
        const int f = doc.meshFace[t];
        const float *p = doc.mesh.constData() + t * 18;
        bool ok = plane.value(f, true);
        for (int k = 0; k < 3; ++k) { if (p[k * 6 + 5] < 0.999f) ok = false; zs[f] += p[k * 6 + 2]; nb[f] += 1; }
        aireF[f] += 0.5 * std::fabs((p[6] - p[0]) * (p[13] - p[1]) - (p[12] - p[0]) * (p[7] - p[1]));
        plane[f] = ok;
    }
    // Surfaçage : s'il y a du brut au-dessus de la pièce, on descend
    // jusqu'au dessus de la pièce + surép. de fond (niveau « surfaçage »),
    // puis les couches continuent au pas de passe à partir de là.
    QVector<QPair<double, bool>> niv;
    const double zSurf = doc.bbMax[2] + sFond;
    double zDep = zHaut;
    if (zHaut > zSurf + 1e-3) {
        for (double z = zHaut - passe; z > zSurf + 1e-6; z -= passe) niv << qMakePair(z, false);
        niv << qMakePair(zSurf, false);
        zDep = zSurf;
    }
    for (double z = zDep - passe; z > pl.zFin + 1e-6; z -= passe) niv << qMakePair(z, false);
    niv << qMakePair(pl.zFin, false);
    for (auto it = plane.cbegin(); it != plane.cend(); ++it) {
        if (!it.value() || aireF[it.key()] < 1.0) continue;
        const double zf = zs[it.key()] / nb[it.key()] + sFond;
        if (zf >= zHaut - 1e-6 || zf <= pl.zFin + 1e-6) continue;
        bool deja = false;
        for (const auto &n : niv) if (n.second && std::fabs(n.first - zf) < 1e-3) deja = true;
        if (deja) continue;
        for (int k = niv.size() - 1; k >= 0; --k)
            if (!niv[k].second && std::fabs(niv[k].first - zf) < 0.3 && k != niv.size() - 1) niv.remove(k);
        niv << qMakePair(zf, true);
    }
    std::sort(niv.begin(), niv.end(), [](const auto &a, const auto &b){ return a.first > b.first; });
    pl.couches = Decoupe3D::couchesAuxNiveaux(doc.mesh, niv);

    // Limite : le brut, ou la zone choisie agrandie (dans le brut)
    const Contours2D brut{ Contour2D{ QPointF(pl.brutMin[0], pl.brutMin[1]), QPointF(pl.brutMax[0], pl.brutMin[1]),
                                      QPointF(pl.brutMax[0], pl.brutMax[1]), QPointF(pl.brutMin[0], pl.brutMax[1]) } };
    pl.limite = brut;
    const QJsonArray cz = d["contoursZone"].toArray();
    if (!cz.isEmpty()) {
        Contours2D z2;
        for (const QJsonValue &ct : cz) {
            Contour2D c;
            for (const QJsonValue &p : ct.toArray()) c << QPointF(p.toArray()[0].toDouble(), p.toArray()[1].toDouble());
            if (c.size() >= 3) z2 << c;
        }
        pl.limite = Decoupe3D::intersection(brut, Decoupe3D::decaler(z2, 2.0 * rayonEb + sLat + 1.0));
    }
    for (Couche3D &c : pl.couches) c.aVider = Decoupe3D::zoneAVider(pl.limite, c.ombre, sLat);
    return pl;
}

// ═══════════════════════════════════════════════════════════════
Usinage3DGenerator::Usinage3DGenerator(const QJsonObject &d, double approche, double sautBride)
    : m_d(d), m_approche(approche), m_sautBride(sautBride) {}

namespace {

// Enveloppe convexe (chaîne monotone)
Contour2D enveloppe(QVector<QPointF> p)
{
    std::sort(p.begin(), p.end(), [](const QPointF &a, const QPointF &b){
        return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y()); });
    if (p.size() < 3) return p;
    auto cr = [](const QPointF &o, const QPointF &a, const QPointF &b){
        return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x()); };
    QVector<QPointF> h(2 * p.size());
    int k = 0;
    for (int i = 0; i < p.size(); ++i) { while (k >= 2 && cr(h[k - 2], h[k - 1], p[i]) <= 0) --k; h[k++] = p[i]; }
    for (int i = p.size() - 2, t = k + 1; i >= 0; --i) { while (k >= t && cr(h[k - 2], h[k - 1], p[i]) <= 0) --k; h[k++] = p[i]; }
    h.resize(k - 1);
    return h;
}

// Dessin d'une couche : UN contour extérieur (les trous sont ignorés,
// la matière y reste). Plusieurs morceaux -> on les relie (fermeture
// du Ø outil), sinon enveloppe : on laisse plus de matière, jamais moins.
Contour2D dessinCouche(const Contours2D &ombre, double rOutil)
{
    PathsD ext;
    for (const PathD &p : versClip(ombre)) {
        double a = 0;
        for (size_t i = 0, n = p.size(); i < n; ++i) a += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
        if (a > 0) ext.push_back(p);                       // extérieurs (CCW)
    }
    if (ext.empty()) return {};
    if (ext.size() > 1) {
        const double g = rOutil + 0.5;
        PathsD f = InflatePaths(InflatePaths(ext, g, JoinType::Round, EndType::Polygon, 2.0, 4),
                                -g, JoinType::Round, EndType::Polygon, 2.0, 4);
        PathsD fe;
        for (const PathD &p : f) {
            double a = 0;
            for (size_t i = 0, n = p.size(); i < n; ++i) a += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
            if (a > 0) fe.push_back(p);
        }
        ext = fe;
    }
    Contour2D r;
    if (ext.size() == 1) {
        for (const PointD &q : SimplifyPath(ext[0], 0.01, true)) r << QPointF(q.x, q.y);
    } else {
        QVector<QPointF> tous;
        for (const PathD &p : ext) for (const PointD &q : p) tous << QPointF(q.x, q.y);
        r = enveloppe(tous);
    }
    return r;
}

// Garder les mouvements d'un cycle Contour (l'outil est appelé une
// seule fois pour tout le bloc : pas de T M6 / S / M5 / M9 à chaque couche)
void ajouterMouvements(QVector<GCodeLine> &out, const QVector<GCodeLine> &cyc, double decalZ = 0.0)
{
    // Le cycle Contour travaille sous Z0 : il est calculé avec Z0 = dessus
    // du brut, puis tous les Z sont décalés de « decalZ » (repère pièce).
    static const QRegularExpression reZ("Z(-?\\d+(?:\\.\\d*)?)");
    for (GCodeLine l : cyc) {
        if (std::fabs(decalZ) > 1e-9 && !l.code.startsWith("(")) {
            QString code; int pos = 0;
            auto it = reZ.globalMatch(l.code);
            while (it.hasNext()) {
                const auto m = it.next();
                code += l.code.mid(pos, m.capturedStart() - pos);
                code += "Z" + QString::number(m.captured(1).toDouble() + decalZ, 'f', 3);
                pos = m.capturedEnd();
            }
            code += l.code.mid(pos);
            l.code = code;
        }
        const QString c = l.code.trimmed();
        if (l.type == GCodeLine::ToolCall || l.type == GCodeLine::Spindle || l.type == GCodeLine::MFunction) continue;
        if (c.startsWith("G4 P") || c.startsWith("( Appel outil")) continue;
        if (l.type == GCodeLine::Blank) continue;
        out << l;
    }
}

} // namespace

// Mémoire du résultat : le G-code du bloc est gardé sur disque ; s'il n'a
// rien de changé (bloc, fichier STEP, outils), il est repris tel quel.
static const char *U3D_VERSION = "u3d-14";

QVector<GCodeLine> Usinage3DGenerator::generate(int blkId)
{
    const QString fichier0 = m_d["fichier"].toString();
    QByteArray cle = QJsonDocument(m_d).toJson(QJsonDocument::Compact);
    cle += QByteArray::number(m_approche) + "|" + QByteArray::number(m_sautBride) + "|" + U3D_VERSION;
    cle += "|" + QByteArray::number(QFileInfo(fichier0).lastModified().toMSecsSinceEpoch());
    for (const char *k : {"outilEb", "outilFin"}) {
        const Outil o = lireOutil(m_d[QString::fromLatin1(k)].toString());
        cle += "|" + QByteArray::number(o.T) + ":" + QByteArray::number(o.R, 'f', 4) + ":" +
               QByteArray::number(o.dents) + ":" + QByteArray::number(int(o.boule));
    }
    const QString dosCache = QDir::tempPath() + "/eazync_u3d";
    QDir().mkpath(dosCache);
    const QString fCache = dosCache + "/cache_" +
        QString::fromLatin1(QCryptographicHash::hash(cle, QCryptographicHash::Md5).toHex()) + ".txt";
    {
        QFile f(fCache);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QVector<GCodeLine> out;
            QTextStream ts(&f);
            ts.setEncoding(QStringConverter::Utf8);
            while (!ts.atEnd()) {
                const QStringList p = ts.readLine().split(QChar(0x1F));
                if (p.size() < 3) continue;
                GCodeLine l; l.type = GCodeLine::Type(p[0].toInt()); l.code = p[1]; l.comment = p[2]; l.blockId = blkId;
                out << l;
            }
            if (!out.isEmpty()) { qDebug() << "[U3D] G-code repris de la memoire" << fCache; return out; }
        }
    }
    QVector<GCodeLine> out = calculer(blkId);
    QFile f(fCache);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        for (const GCodeLine &l : out)
            ts << int(l.type) << QChar(0x1F) << l.code << QChar(0x1F) << l.comment << "\n";
    }
    return out;
}

QVector<GCodeLine> Usinage3DGenerator::calculer(int blkId)
{
    QVector<GCodeLine> out;
    const QString fichier = m_d["fichier"].toString();
    out << blank();
    out << comment("===== USINAGE 3D : " + QFileInfo(fichier).fileName() + " =====", blkId);
    if (fichier.isEmpty()) { out << comment("!! aucun fichier 3D", blkId); return out; }

    CadDocument docBrut = CadImporter::load(fichier);
    // Sens des axes choisi dans le BLK FORM : tout est calculé dans le repère machine
    const Orientation3D orient = Orientation3D::depuis(m_d["orient"].toArray());
    if (!orient.identite()) {
        orient.appliquer(docBrut);
        QJsonArray cz;
        double zMin = 1e30;
        for (const QJsonValue &ct : m_d["contoursZone"].toArray()) {
            QJsonArray pa;
            for (const QJsonValue &p : ct.toArray()) {
                const QJsonArray a = p.toArray();
                const QVector3D q = orient.versMachine(QVector3D(a[0].toDouble(), a[1].toDouble(), a[2].toDouble()));
                pa.append(QJsonArray{q.x(), q.y(), q.z()});
                zMin = qMin(zMin, double(q.z()));
            }
            cz.append(pa);
        }
        m_d["contoursZone"] = cz;
        m_d["zBasModele"] = cz.isEmpty() ? double(docBrut.bbMin[2]) : zMin;
    }
    const CadDocument &doc = docBrut;
    const int mode = m_d["modeUsin"].toInt(0);              // 0 éb+fin, 1 éb, 2 fin
    const Outil oEb = lireOutil(m_d["outilEb"].toString());
    const Outil oFin = lireOutil(m_d["outilFin"].toString());
    const Usinage3DPlan pl = plan(m_d, doc, oEb.R);
    if (!pl.erreur.isEmpty()) { out << comment("!! " + pl.erreur, blkId); return out; }

    const double ox = pl.origine[0], oy = pl.origine[1], oz = pl.origine[2];
    const double zSecu = pl.brutMax[2] - oz + m_approche;       // repère pièce
    const double zDessus = pl.brutMax[2] - oz;                  // dessus du brut, repère pièce
    const QString dossier = QDir::tempPath() + "/eazync_u3d";
    QDir().mkpath(dossier);

    // Contour de base commun aux couches (brut, avances, outil...)
    auto base = [&](const Outil &ot, const QString &suf) {
        ContourData cd;
        cd.toolRef      = QString::number(ot.T);
        cd.spindleVal   = m_d["s" + suf].toDouble();
        cd.spindleByVc  = m_d["modeS" + suf].toInt(0) == 1;
        cd.avanceVal    = m_d["avance" + suf].toDouble();
        cd.avanceMode   = m_d["modeAvance" + suf].toInt(0);
        cd.avanceFinition = cd.avanceVal;
        cd.avanceFinMode  = cd.avanceMode;
        cd.descenteVal  = m_d["avPlongee" + suf].toDouble();
        cd.descenteMode = m_d["modeAvPlongee" + suf].toInt(0);
        cd.approche     = m_approche;      // au-dessus du brut (Z0 du cycle = dessus du brut)
        cd.sautBride    = m_approche;      // pas de remontée à 50 mm entre couches
        cd.positionOutil = 0;              // extérieur auto : ébauche depuis le brut
        cd.blkValid = true;
        cd.blkXmin = pl.brutMin[0] - ox; cd.blkXmax = pl.brutMax[0] - ox;
        cd.blkYmin = pl.brutMin[1] - oy; cd.blkYmax = pl.brutMax[1] - oy;
        cd.surepProf = 0.0;
        return cd;
    };
    // Écrit le dessin d'une couche en .cnt (repère pièce)
    auto ecrireDessin = [&](const Contour2D &ct, const QString &nom) -> QString {
        ContourFile cf;
        cf.name = nom;
        for (int i = 0; i <= ct.size(); ++i) {
            const QPointF &p = ct[i % ct.size()];
            ContourElement e; e.type = ContourElement::L;
            e.x2 = p.x() - ox; e.y2 = p.y() - oy;
            cf.elements << e;
        }
        const QString chemin = dossier + "/" + nom + ".cnt";
        cf.save(chemin);
        return chemin;
    };
    auto appelOutil = [&](const Outil &ot, const QString &suf) {
        out << emitToolBlock(QString::number(ot.T), false, m_d["s" + suf].toDouble(),
                             m_d["modeS" + suf].toInt(0) == 1, 2.0 * ot.R, true, "", "", blkId);
        out << line(GCodeLine::Rapid, "G0 Z" + fc(zSecu), "Hauteur de securite", blkId);
    };

    // ─────────────── ÉBAUCHE : une couche = un dessin = une passe ───────────────
    if (mode == 0 || mode == 1) {
        if (oEb.T <= 0 || oEb.R <= 0) {
            out << comment("!! ebauche : outil d'ebauche absent ou sans rayon dans le tableau", blkId);
        } else {
            const double sLat = m_d["surepLat"].toDouble(0.3);
            out << comment(QString("EBAUCHE  T%1 %2  %3 couches  surep. lat %4 / fond %5")
                           .arg(oEb.T).arg(oEb.nom).arg(pl.couches.size())
                           .arg(fc(sLat), fc(m_d["surepFond"].toDouble())), blkId);
            appelOutil(oEb, "Eb");
            int n = 0;
            for (const Couche3D &c : pl.couches) {
                ++n;
                const double z = c.z - oz;
                out << comment(QString("--- Couche %1/%2  Z%3%4 ---").arg(n).arg(pl.couches.size())
                               .arg(fc(z), c.facePlane ? "  (fond plat)" : ""), blkId);
                Contour2D ct = dessinCouche(c.ombre, oEb.R);
                ContourData cd = base(oEb, "Eb");
                cd.modeUsin    = 1;                          // ébauche seule
                cd.surepLat    = sLat;
                cd.stepOverPct = 100 - m_d["recouvrement"].toInt(40);
                cd.profondeur  = zDessus - z;                // profondeur depuis le dessus du brut
                cd.profPasse   = cd.profondeur;              // une seule passe, à ce Z
                cd.plongeeRapide = true;                     // entrée par le bord du brut (dans le vide)
                if (ct.size() < 3) {
                    // rien de la pièce au-dessus de ce Z : on vide le brut entier
                    const double cx = (pl.brutMin[0] + pl.brutMax[0]) / 2, cy = (pl.brutMin[1] + pl.brutMax[1]) / 2;
                    ct = Contour2D{QPointF(cx - 0.05, cy - 0.05), QPointF(cx + 0.05, cy - 0.05),
                                   QPointF(cx + 0.05, cy + 0.05), QPointF(cx - 0.05, cy + 0.05)};
                }
                cd.contourName = QString("u3d_eb_%1").arg(n, 2, 10, QChar('0'));
                cd.contourFile = ecrireDessin(ct, cd.contourName);
                ContourGenerator gen(cd);
                ajouterMouvements(out, gen.generate(blkId), zDessus);
            }
            out << line(GCodeLine::Rapid, "G0 Z" + fc(zSecu), "Fin ebauche", blkId);
        }
    }

    // ─────────────── FINITION : un dessin par couche fine, contournage ───────────────
    //  Fraise boule : le centre de la boule doit rester hors de
    //     ∪ ombre(z + h) ⊕ ρ(h),  ρ(h) = √(2rh − h²),  h ∈ [0, r]
    //  (la boule touche une pente plus haut que sa pointe). Le bord de cette
    //  zone est le CHEMIN DU CENTRE : usiné « sur la ligne » (R0).
    //  Fraise plate : ombre(z) ⊕ r.
    if (mode == 0 || mode == 2) {
        if (oFin.T <= 0 || oFin.R <= 0) {
            out << comment("!! finition : outil de finition absent ou sans rayon dans le tableau", blkId);
        } else {
            const double passeFin = qMax(0.05, m_d["passeFin"].toDouble(0.5));
            const double ecartMax = qMax(0.02, m_d["pasFin"].toDouble(0.3));
            const double r = oFin.R;
            QVector<double> hs, rho;                             // profil de l'outil
            if (oFin.boule) {
                for (int j = 0; j <= 8; ++j) { const double h = r * j / 8.0; hs << h; rho << std::sqrt(qMax(0.0, 2 * r * h - h * h)); }
            } else { hs << 0.0; rho << r; }

            // ── Carte des hauteurs de la pièce H, puis hauteur de la pointe T ──
            //  T(x,y) = plus haute position de la pointe de l'outil centré en
            //  (x,y) sans toucher la pièce (« drop-cutter »). Le chemin du centre
            //  à une hauteur zt est la courbe de niveau T = zt. Calcul fait UNE fois.
            const double res = 0.25;
            const double gx0 = doc.bbMin[0] - r - 2 * res, gy0 = doc.bbMin[1] - r - 2 * res;
            const int nx = int((doc.bbMax[0] - doc.bbMin[0] + 2 * r + 4 * res) / res) + 2;
            const int ny = int((doc.bbMax[1] - doc.bbMin[1] + 2 * r + 4 * res) / res) + 2;
            const float VIDE = -1e9f;
            QVector<float> H(nx * ny, VIDE), T(nx * ny, VIDE);
            for (int t = 0; t + 17 < doc.mesh.size(); t += 18) {
                const float *p = doc.mesh.constData() + t;
                const double ax = p[0], ay = p[1], az = p[2], bx = p[6], by = p[7], bz = p[8], cx = p[12], cy = p[13], cz = p[14];
                const double den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
                if (std::fabs(den) < 1e-12) continue;
                const int i0 = qMax(0, int((qMin(ax, qMin(bx, cx)) - gx0) / res)), i1 = qMin(nx - 1, int((qMax(ax, qMax(bx, cx)) - gx0) / res) + 1);
                const int j0 = qMax(0, int((qMin(ay, qMin(by, cy)) - gy0) / res)), j1 = qMin(ny - 1, int((qMax(ay, qMax(by, cy)) - gy0) / res) + 1);
                for (int jj = j0; jj <= j1; ++jj) for (int ii = i0; ii <= i1; ++ii) {
                    const double x = gx0 + ii * res, y = gy0 + jj * res;
                    const double l1 = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / den;
                    const double l2 = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / den;
                    const double l3 = 1 - l1 - l2;
                    if (l1 < -1e-6 || l2 < -1e-6 || l3 < -1e-6) continue;
                    const float z = float(l1 * az + l2 * bz + l3 * cz);
                    if (z > H[jj * nx + ii]) H[jj * nx + ii] = z;
                }
            }
            {
                struct K { int di, dj; float dz; };
                QVector<K> noyau;
                const int kr = int(r / res) + 1;
                for (int dj = -kr; dj <= kr; ++dj) for (int di = -kr; di <= kr; ++di) {
                    const double dd = std::hypot(di * res, dj * res);
                    if (dd > r) continue;
                    noyau << K{di, dj, float(oFin.boule ? (std::sqrt(r * r - dd * dd) - r) : 0.0)};
                }
                for (int jj = 0; jj < ny; ++jj) for (int ii = 0; ii < nx; ++ii) {
                    const float h = H[jj * nx + ii];
                    if (h <= VIDE) continue;
                    for (const K &k : noyau) {                         // « tampon » de l'outil
                        const int a = ii + k.di, b = jj + k.dj;
                        if (a < 1 || b < 1 || a >= nx - 1 || b >= ny - 1) continue;
                        float &tv = T[b * nx + a];
                        if (h + k.dz > tv) tv = h + k.dz;
                    }
                }
            }
            // Courbe de niveau T = zt (carrés marchants) -> contours fermés,
            // zone interdite à gauche (extérieurs dans le sens trigo)
            QMap<qint64, PathsD> cache;
            auto chemin = [&](double zt) -> PathsD {
                const qint64 cle = qRound64(zt * 1e4);
                auto itc = cache.constFind(cle);
                if (itc != cache.constEnd()) return itc.value();
                const float L = float(zt);
                auto dedans = [&](int a, int b) { return T[b * nx + a] > L; };
                auto pt = [&](qint64 e) -> PointD {                  // point sur une arête
                    const bool vert = e & 1; const qint64 n = e >> 1;
                    const int a = int(n % nx), b = int(n / nx);
                    const int a2 = vert ? a : a + 1, b2 = vert ? b + 1 : b;
                    const float v1 = T[b * nx + a], v2 = T[b2 * nx + a2];
                    double t = (std::fabs(v2 - v1) > 1e-9f) ? (L - v1) / double(v2 - v1) : 0.5;
                    t = qBound(0.0, t, 1.0);
                    return PointD(gx0 + (a + (a2 - a) * t) * res, gy0 + (b + (b2 - b) * t) * res);
                };
                QHash<qint64, qint64> suivant;
                for (int b = 0; b + 1 < ny; ++b) for (int a = 0; a + 1 < nx; ++a) {
                    const int cas = (dedans(a, b) ? 1 : 0) | (dedans(a + 1, b) ? 2 : 0)
                                  | (dedans(a + 1, b + 1) ? 4 : 0) | (dedans(a, b + 1) ? 8 : 0);
                    if (cas == 0 || cas == 15) continue;
                    const qint64 B = 2 * (qint64(b) * nx + a), Tt = 2 * (qint64(b + 1) * nx + a);
                    const qint64 Lf = 2 * (qint64(b) * nx + a) + 1, R = 2 * (qint64(b) * nx + a + 1) + 1;
                    auto seg = [&](qint64 de, qint64 vers) { suivant.insert(de, vers); };
                    switch (cas) {
                    case 1: seg(B, Lf); break;   case 2: seg(R, B); break;
                    case 3: seg(R, Lf); break;   case 4: seg(Tt, R); break;
                    case 5: seg(B, R); seg(Tt, Lf); break;          // col : régions reliées (prudent)
                    case 6: seg(Tt, B); break;   case 7: seg(Tt, Lf); break;
                    case 8: seg(Lf, Tt); break;  case 9: seg(B, Tt); break;
                    case 10: seg(Lf, B); seg(R, Tt); break;
                    case 11: seg(R, Tt); break;  case 12: seg(Lf, R); break;
                    case 13: seg(B, R); break;   case 14: seg(Lf, B); break;
                    }
                }
                PathsD ext;
                while (!suivant.isEmpty()) {
                    const qint64 depart = suivant.begin().key();
                    PathD boucle;
                    qint64 e = depart;
                    for (int garde = 0; garde < 10000000; ++garde) {
                        auto it = suivant.find(e);
                        if (it == suivant.end()) break;
                        boucle.push_back(pt(e));
                        const qint64 n = it.value();
                        suivant.erase(it);
                        e = n;
                        if (e == depart) break;
                    }
                    if (boucle.size() >= 3 && Area(boucle) > 0)       // extérieurs ; trous : matière laissée
                        ext.push_back(SimplifyPath(boucle, 0.01, true));
                }
                cache.insert(cle, ext);
                return ext;
            };
            // Écart à plat trop grand ? (test rapide Clipper) : la passe basse
            // doit tenir dans la passe haute agrandie de « écart max »
            auto tropEcarte = [&](const PathsD &haut, const PathsD &bas) -> bool {
                if (bas.empty() || haut.empty()) return false;
                const PathsD g = InflatePaths(haut, ecartMax, JoinType::Round, EndType::Polygon, 2.0, 3);
                const PathsD dif = Difference(bas, g, FillRule::NonZero, 3);
                double a = 0;
                for (const PathD &p : dif) a += std::fabs(Area(p));
                return a > 0.05;
            };

            // Couches : pas maximum = passe de finition, puis couches ajoutées
            // tant que l'écart à plat dépasse « écart max » (dessus peu pentu)
            QVector<double> zf;
            for (double z = doc.bbMax[2] - passeFin; z > pl.zFin + 1e-6; z -= passeFin) zf << z;
            zf << pl.zFin;
            {
                QVector<double> res;
                double zHautPrec = doc.bbMax[2];                  // au-dessus : rien
                for (double zb : zf) {
                    QVector<QPair<double, double>> pile{{zHautPrec, zb}};
                    QVector<double> ajout;
                    int garde = 0;
                    while (!pile.isEmpty() && garde++ < 60) {
                        auto [za, zc] = pile.takeLast();
                        if (za - zc < 0.1) { ajout << zc; continue; }    // pas mini ~0,06 mm
                        const PathsD ha = (za >= doc.bbMax[2] - 1e-9) ? PathsD() : chemin(za);
                        if (tropEcarte(ha, chemin(zc))) { const double zm = (za + zc) / 2; pile << qMakePair(zm, zc) << qMakePair(za, zm); }
                        else ajout << zc;
                    }
                    std::sort(ajout.begin(), ajout.end(), std::greater<double>());
                    for (double z : ajout) if (res.isEmpty() || std::fabs(res.last() - z) > 1e-6) res << z;
                    zHautPrec = zb;
                }
                zf = res;
            }

            out << comment(QString("FINITION  T%1 %2 (%3)  %4 couches : pas max %5 mm, ecart max %6 mm, chemin du centre calcule")
                           .arg(oFin.T).arg(oFin.nom, oFin.boule ? "boule" : "plate")
                           .arg(zf.size()).arg(fc(passeFin), fc(ecartMax)), blkId);
            appelOutil(oFin, "Fin");
            double SFin = m_d["sFin"].toDouble();
            if (m_d["modeSFin"].toInt(0) == 1 && oFin.R > 0) SFin = SFin * 1000.0 / (M_PI * 2.0 * oFin.R);
            const QString FFin  = avanceStr(m_d["avanceFin"].toDouble(), m_d["modeAvanceFin"].toInt(0), SFin, oFin.dents);
            const QString FpFin = avanceStr(m_d["avPlongeeFin"].toDouble(), m_d["modeAvPlongeeFin"].toInt(0), SFin, oFin.dents);
            int n = 0;
            for (double zt : zf) {
                ++n;
                const double z = zt - oz;
                if (z >= zDessus - 1e-6) continue;
                int k = 0;
                for (const PathD &p : chemin(zt)) {
                    Contour2D ct;
                    for (auto it = p.rbegin(); it != p.rend(); ++it) ct << QPointF(it->x, it->y);   // sens horaire = avalant en M3
                    if (ct.size() < 3) continue;
                    ++k;
                    out << comment(QString("--- Finition %1/%2  Z%3%4 ---").arg(n).arg(zf.size()).arg(fc(z))
                                   .arg(k > 1 ? QString("  morceau %1").arg(k) : QString()), blkId);
                    // Passe émise directement : descente PILE sur le chemin (sûr :
                    // l'outil y touche juste la pièce), tour complet, remontée.
                    // Pas d'entrée/sortie tangente (elle peut rentrer dans la pièce).
                    const double zLib = qMin(zSecu, z + m_d["passeEb"].toDouble(2.0) + 1.0);
                    out << line(GCodeLine::Rapid, "G0 X" + fc(ct[0].x() - ox) + " Y" + fc(ct[0].y() - oy), "", blkId);
                    out << line(GCodeLine::Rapid, "G0 Z" + fc(zLib), "", blkId);
                    out << line(GCodeLine::Linear, "G1 Z" + fc(z) + " " + FpFin, "Descente sur le chemin", blkId);
                    for (int q = 1; q <= ct.size(); ++q) {
                        const QPointF &pq = ct[q % ct.size()];
                        out << line(GCodeLine::Linear, "G1 X" + fc(pq.x() - ox) + " Y" + fc(pq.y() - oy)
                                                       + (q == 1 ? " " + FFin : QString()), "", blkId);
                    }
                    out << line(GCodeLine::Rapid, "G0 Z" + fc(zSecu), "", blkId);
                }
            }
        }
    }
    out << emitRetrait(m_sautBride, blkId);
    return out;
}
