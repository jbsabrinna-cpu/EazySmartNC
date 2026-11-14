#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  contourgenerator.cpp
//
//  Cycle CONTOURNAGE (usinage exterieur). Code repris a
//  l'identique de l'ancien GCodeEngine::generateContour, adapte
//  a l'architecture Strategy (heritage de BlockGenerator).
//
//  Seules les signatures et l'acces aux donnees (m_d) ont
//  change. La logique d'usinage et la gestion du sens
//  (modeFraisage) sont INCHANGEES, y compris les portions
//  marquees "TEST" : la stabilisation du sens se fera dans une
//  etape ulterieure, sur ce fichier isole.
// ═══════════════════════════════════════════════════════════════
#include "contourgenerator.h"
#include "../contourelement.h"
#include "../tooldatabase.h"
#include "../clipper2utils.h"
#include "../pointfile.h"

#include <QtMath>
#include <QDebug>
#include <algorithm>   // std::reverse

static QVector<QPointF> offsetPolyline(const QVector<QPointF> &pts,
                                       double delta, bool closed)
{
    if (pts.size() < 3 || qAbs(delta) < 1e-6) return pts;

    // Préparer le path Clipper : enlever doublon final si fermé
    Path2D profil;
    profil.reserve(pts.size());
    for (const QPointF &p : pts) profil.append(p);
    if (closed && profil.size() >= 2 &&
        std::hypot(profil.last().x()-profil.first().x(),
                   profil.last().y()-profil.first().y()) < 1e-3) {
        profil.removeLast();
    }
    if (profil.size() < 3) return pts;

    auto signedAreaMath = [](const Path2D &p) -> double {
        double a = 0.0;
        int n = p.size();
        for (int i = 0; i < n; ++i) {
            int j = (i + 1) % n;
            a += p[i].x() * p[j].y() - p[j].x() * p[i].y();
        }
        return a * 0.5;
    };

    auto extractBest = [&](const Paths2D &res) -> Path2D {
        if (res.isEmpty()) return Path2D();
        Path2D best = res.first();
        double bestArea = qAbs(signedAreaMath(best));
        for (int i = 1; i < res.size(); ++i) {
            double a = qAbs(signedAreaMath(res[i]));
            if (a > bestArea) { best = res[i]; bestArea = a; }
        }
        return best;
    };

    // Stratégie robuste : on essaie les deux orientations (CCW et CW)
    // et on garde celle qui produit un offset VERS L'EXTERIEUR (= aire plus grande
    // que le contour original) pour delta > 0. Independant de la convention interne
    // de Clipper2 (qui peut varier selon version Y up/down).
    const double aireOriginale = qAbs(signedAreaMath(profil));

    Path2D ccw = profil;
    if (signedAreaMath(ccw) < 0) std::reverse(ccw.begin(), ccw.end());
    Path2D cw = ccw;
    std::reverse(cw.begin(), cw.end());

    // joinType = 1 (Miter) : les coins exterieurs restent VIFS.
    //  Choix : avec Round, Clipper arrondit chaque coin, et au
    //  point d'attaque le raccordement entree/contour passe par
    //  cet arrondi -> changement de direction brutal (a-coup).
    //  En Miter, les segments droits restent droits jusqu'au
    //  coin : l'entree/sortie tangente se raccorde proprement.
    Paths2D res1 = Clipper2Utils::offset(ccw, delta, 1);
    Paths2D res2 = Clipper2Utils::offset(cw,  delta, 1);

    Path2D best1 = extractBest(res1);
    Path2D best2 = extractBest(res2);
    double a1 = best1.isEmpty() ? -1.0 : qAbs(signedAreaMath(best1));
    double a2 = best2.isEmpty() ? -1.0 : qAbs(signedAreaMath(best2));

    // Pour delta > 0 (expansion) : on veut l'aire plus grande que l'original
    // Pour delta < 0 (rétrécissement) : on veut l'aire plus petite
    Path2D best;
    if (delta > 0) {
        bool ok1 = a1 > aireOriginale * 1.001;
        bool ok2 = a2 > aireOriginale * 1.001;
        if (ok1 && ok2)      best = (a1 > a2) ? best1 : best2;
        else if (ok1)        best = best1;
        else if (ok2)        best = best2;
        else                 best = (a1 > a2) ? best1 : best2; // fallback
    } else {
        bool ok1 = (a1 >= 0) && (a1 < aireOriginale * 0.999);
        bool ok2 = (a2 >= 0) && (a2 < aireOriginale * 0.999);
        if (ok1 && ok2)      best = (a1 < a2) ? best1 : best2;
        else if (ok1)        best = best1;
        else if (ok2)        best = best2;
        else                 best = (a1 < a2) ? best1 : best2;
    }

    if (best.size() < 3) return QVector<QPointF>();

    // Forcer le sens de sortie a etre coherent avec le profil d'entree
    bool origCCW = signedAreaMath(profil) > 0;
    bool bestCCW = signedAreaMath(best) > 0;
    if (origCCW != bestCCW) std::reverse(best.begin(), best.end());

    // Refermer (Clipper2 ne duplique pas le 1er point en fin)
    if (closed && !best.isEmpty()) best.append(best.first());

    return best;
}

// ─────────────────────────────────────────────────────────────────────
//  alignClosedPathStart — réordonne une polyligne fermée pour qu'elle
//  commence au point le plus proche de "anchor".
// ─────────────────────────────────────────────────────────────────────
static QVector<QPointF> alignClosedPathStart(const QVector<QPointF> &pts,
                                             QPointF anchor)
{
    if (pts.size() < 3) return pts;
    QVector<QPointF> work = pts;
    bool wasClosed = false;
    if (work.size() >= 2 &&
        std::hypot(work.last().x()-work.first().x(),
                   work.last().y()-work.first().y()) < 1e-3) {
        work.removeLast();
        wasClosed = true;
    }
    int    bestIdx  = 0;
    double bestDist = std::numeric_limits<double>::max();
    for (int i = 0; i < work.size(); ++i) {
        double d = std::hypot(work[i].x()-anchor.x(), work[i].y()-anchor.y());
        if (d < bestDist) { bestDist = d; bestIdx = i; }
    }
    if (bestIdx > 0) std::rotate(work.begin(), work.begin()+bestIdx, work.end());
    if (wasClosed) work.append(work.first());
    return work;
}

// ─────────────────────────────────────────────────────────────────────
//  computeContourPath — discrétise + nettoie le contour brut depuis .cnt
//  Retourne (path, closed)
// ─────────────────────────────────────────────────────────────────────
// Distance d'un point a une polyligne (fermee si closed)
static double distToPolyline(const QVector<QPointF> &pl, const QPointF &q, bool closed = true)
{
    const int n = pl.size();
    if (n < 2) return 1e30;
    double best = 1e30;
    const int m = closed ? n : n - 1;
    for (int i = 0; i < m; ++i) {
        const QPointF &a = pl[i], &b = pl[(i + 1) % n];
        const double dx = b.x()-a.x(), dy = b.y()-a.y();
        const double L2 = dx*dx + dy*dy;
        double t = L2 > 1e-12 ? ((q.x()-a.x())*dx + (q.y()-a.y())*dy) / L2 : 0.0;
        t = qBound(0.0, t, 1.0);
        best = qMin(best, std::hypot(q.x()-(a.x()+t*dx), q.y()-(a.y()+t*dy)));
    }
    return best;
}

static QVector<QPointF> computeContourPath(const ContourFile &cf, bool &closed)
{
    QVector<QPointF> path;
    auto segs = cf.toSegments(64);
    for (const auto &s : segs) {
        if (s.pts.isEmpty()) continue;
        int start = path.isEmpty() ? 0 : 1;
        for (int k = start; k < s.pts.size(); ++k) path << s.pts[k];
    }
    QVector<QPointF> cleaned;
    for (const auto &p : path) {
        if (cleaned.isEmpty() ||
            std::hypot(p.x()-cleaned.last().x(), p.y()-cleaned.last().y()) > 1e-3)
            cleaned << p;
    }
    path = cleaned;
    closed = (path.size() >= 2 &&
              std::hypot(path.last().x()-path.first().x(),
                         path.last().y()-path.first().y()) < 1e-3);
    return path;
}


// ─────────────────────────────────────────────────────────────
//  generate — point d'entree du cycle contournage.
//  Ici l'appel outil et le retrait sont integres DANS
//  contourBody() (comme dans l'ancien generateContour), donc
//  generate() ne fait que deleguer.
// ─────────────────────────────────────────────────────────────
QVector<GCodeLine> ContourGenerator::generate(int blkId)
{
    return contourBody(blkId);
}

QVector<GCodeLine> ContourGenerator::contourBody(int blkId) const
{
    const ContourData &d = m_d;
    QVector<GCodeLine> out;

    // ── 0. Validation ─────────────────────────────────────────
    if (d.contourFile.isEmpty()) {
        out << comment("CONTOUR SKIP : aucun fichier .cnt defini", blkId);
        return out;
    }
    if (d.profondeur <= 0 || d.profPasse <= 0) {
        out << comment("CONTOUR SKIP : profondeur/passe invalide", blkId);
        return out;
    }

    // ── 1. Charger le contour ────────────────────────────────
    ContourFile cf;
    if (!cf.load(d.contourFile)) {
        out << comment(QString("CONTOUR SKIP : lecture %1 echouee")
                           .arg(d.contourFile), blkId);
        return out;
    }
    if (cf.elements.isEmpty()) {
        out << comment("CONTOUR SKIP : contour vide", blkId);
        return out;
    }

    // ── 2. Rayon outil et numéro T ───────────────────────────
    double rOutil = 0.0;
    int    tNum   = 0;
    int    nbDentsOutil = 1;   // pour les avances en Fz (mm/dent)
    {
        QString t = d.toolRef.trimmed();
        if (t.startsWith('T', Qt::CaseInsensitive)) t = t.mid(1);
        bool ok = false; int ti = t.toInt(&ok);
        if (ok && ti > 0) {
            tNum = ti;
            const ToolData *td = ToolDatabase::instance().toolByT(ti);
            if (td && td->Routil > 0) rOutil = td->Routil;
            if (td && td->nbDents > 0) nbDentsOutil = td->nbDents;
        }
    }
    if (rOutil < 0.1 || tNum <= 0) {
        out << comment(QString("ERREUR : outil T%1 absent de la table")
                           .arg(d.toolRef), blkId);
        return out;
    }

    // ── 3. Discrétiser et calculer trajectoires offset ───────
    bool closed = false;
    QVector<QPointF> path = computeContourPath(cf, closed);
    if (path.size() < 2) {
        out << comment("CONTOUR SKIP : contour invalide", blkId);
        return out;
    }

    // ── Sens d'usinage ───────────────────────────────────────
    //  REGLE FONDAMENTALE :
    //   - Le contour est dessine dans le sens AVALANT.
    //   - Mode avalant   (modeFraisage==0) : on usine le path
    //     tel qu'il est dessine.
    //   - Mode opposition(modeFraisage==1) : on inverse le sens
    //     de parcours du path.
    //  C'est le SEUL endroit ou le sens est decide. offsetPolyline
    //  preserve le sens, et emitContour / trajFin / la finition
    //  heritent tous de ce path. AUCUNE autre inversion ailleurs.
    if (d.modeFraisage == 1) {
        if (closed) {
            QPointF firstPt = path.first();
            bool hadClose = (path.size() >= 2 &&
                             std::hypot(path.last().x()-firstPt.x(),
                                        path.last().y()-firstPt.y()) < 1e-3);
            if (hadClose) path.removeLast();
            std::reverse(path.begin(), path.end());
            int newFirst = path.indexOf(firstPt);
            if (newFirst > 0)
                std::rotate(path.begin(), path.begin()+newFirst, path.end());
            if (hadClose) path.append(path.first());
        } else {
            std::reverse(path.begin(), path.end());
        }
    }

    // Trajectoire ébauche (avec surepLat).
    // Plus de G41/G42 : on offsette TOUJOURS vers l'EXTERIEUR du contour
    // tel que dessine. Le sens du contour (CCW/CW) est gere dans
    // offsetPolyline qui force CCW avant offset Clipper.
    // Cas standard : contour = piece a usiner par l'exterieur (fraise dehors).
    // ── Position de l'outil (R0 / RL / RR) ────────────────────
    //  0 = Exterieur : comportement historique (offset vers l'exterieur)
    //  1 = R0  : centre outil SUR la ligne (gorge, rainure, gravure)
    //  2 = RL  : outil a gauche du sens de parcours
    //  3 = RR  : outil a droite du sens de parcours
    //  RL/RR : gauche/droite traduits en interieur/exterieur selon le
    //  sens du contour ferme (CCW : gauche = interieur).
    const int posOutil = qBound(0, d.positionOutil, 3);
    double sideSign = 1.0;
    if (posOutil == 1) sideSign = 0.0;
    else if (posOutil >= 2) {
        double area = 0.0;
        for (int i = 0; i + 1 < path.size(); ++i)
            area += path[i].x()*path[i+1].y() - path[i+1].x()*path[i].y();
        const bool ccw  = area > 0;
        const bool left = (posOutil == 2);
        sideSign = (left == ccw) ? -1.0 : 1.0;
        if (!closed) {
            out << comment("RL/RR : contour ouvert non gere, outil sur la ligne", blkId);
            sideSign = 0.0;
        }
    }
    double offsetEb = sideSign * (rOutil + d.DR + d.surepLat);

    // Trajectoire finition (sans surepLat) - utilisée seulement si doFinition
    double offsetFin = sideSign * (rOutil + d.DR);

    QVector<QPointF> trajEb = (qAbs(offsetEb) < 1e-6)
                                  ? path
                                  : offsetPolyline(path, offsetEb, closed);
    QVector<QPointF> trajFin = (qAbs(offsetFin) < 1e-6)
                                   ? path
                                   : offsetPolyline(path, offsetFin, closed);

    if (trajEb.isEmpty() || trajFin.isEmpty()) {
        out << comment("ERREUR : offset impossible (surepaisseur trop grande)", blkId);
        return out;
    }
    if (closed) {
        if (qAbs(offsetEb)  > 1e-6) trajEb  = alignClosedPathStart(trajEb,  path.first());
        if (qAbs(offsetFin) > 1e-6) trajFin = alignClosedPathStart(trajFin, path.first());
    }

    // ── 4. Lambda computeEntreeSortie sur trajectoire offset ──
    //  IMPORTANT : la tangente d'entree/sortie ne doit PAS etre
    //  calculee sur le point immediatement voisin (p[1] / p[n-2]).
    //  Pres d'un coin, Clipper2 (offset Round) insere des micro-
    //  facettes d'arrondi : le voisin immediat est sur l'arrondi,
    //  pas sur le vrai segment droit -> tangente faussee.
    //  Solution : on prend un point situe un peu plus loin sur le
    //  path, au-dela de la zone d'arrondi (distance ~ rayon
    //  d'arrondi = offset outil). On obtient ainsi la tangente du
    //  VRAI segment du contour.
    auto computeEntreeSortie = [&](const QVector<QPointF> &p,
                                   QPointF &approchePt, QString &entreeCode, bool &entIsArc,
                                   QPointF &sortiePt,   QString &sortieCode, bool &sorIsArc) {
        QPointF startPt = p.first();
        QPointF endPt   = closed ? startPt : p.last();

        // ── tangenteDepuis ───────────────────────────────────
        //  IDEE CLE : on calcule la direction tangente sur le
        //  CONTOUR DESSINE (path), PAS sur la trajectoire offset.
        //  Sur le contour dessine, les coins sont nets : pas
        //  d'arrondi Clipper, donc la direction des cotes est
        //  exacte. On reporte ensuite cette direction sur le
        //  point d'accroche offset.
        //
        //  parametres :
        //   refPt : point (sur l'offset) dont on cherche la
        //           tangente -> on localise le point le plus
        //           proche sur 'path'
        //   sens  : +1 cote qui PART de ce point (entree)
        //           -1 cote qui ARRIVE a ce point (sortie)
        //  retour : direction unitaire du segment droit, orientee
        //           dans le sens 'sens'.
        auto tangenteDepuis = [&](const QPointF &refPt, int sens) -> QPointF {
            const QVector<QPointF> &src = path;   // contour dessine
            int n = src.size();
            if (n < 2) return QPointF(1, 0);

            // path ferme : dernier point = premier (doublon).
            int nEff = n;
            if (closed && n >= 2 &&
                std::hypot(src.last().x()-src.first().x(),
                           src.last().y()-src.first().y()) < 1e-6)
                nEff = n - 1;

            auto at = [&](int i) -> QPointF {
                if (closed) return src[(i % nEff + nEff) % nEff];
                return src[qBound(0, i, n-1)];
            };

            // 1) Trouver l'indice du point de 'path' le plus proche
            //    de refPt.
            int idxStart = 0; double dMin = 1e18;
            for (int i = 0; i < nEff; ++i) {
                double dd = std::hypot(at(i).x()-refPt.x(),
                                       at(i).y()-refPt.y());
                if (dd < dMin) { dMin = dd; idxStart = i; }
            }

            auto pas = [&](int ia, int ib) -> QPointF {
                return QPointF(at(ib).x()-at(ia).x(),
                               at(ib).y()-at(ia).y());
            };

            // 2) Sur le contour dessine, on suit le segment droit.
            //    Les arcs sont discretises (micro-segments), les
            //    lignes droites sont de vrais longs segments. On
            //    cumule tant que la direction est stable ; on
            //    retient le 1er groupe dont la longueur depasse le
            //    seuil (= vrai cote, pas un bout d'arc discretise).
            const double seuilLong = qMax(2.0 * rOutil, 2.0);
            const double cosSeuil  = 0.985;   // ~10 deg

            int idx = idxStart;
            int guard = 0;
            while (guard++ < 2*nEff) {
                QPointF d0 = pas(idx, idx+sens);
                double l0 = std::hypot(d0.x(), d0.y());
                if (l0 < 1e-9) { idx += sens; continue; }
                QPointF refDir(d0.x()/l0, d0.y()/l0);

                double longCumul = l0;
                int idxNext = idx + sens;
                int steps = 0;
                while (steps++ < 2*nEff) {
                    QPointF dd = pas(idxNext, idxNext+sens);
                    double ll = std::hypot(dd.x(), dd.y());
                    if (ll < 1e-9) { idxNext += sens; continue; }
                    double dot = (dd.x()*refDir.x()+dd.y()*refDir.y())/ll;
                    if (dot < cosSeuil) break;
                    longCumul += ll;
                    idxNext += sens;
                }
                if (longCumul >= seuilLong)
                    return refDir;          // vrai cote trouve
                idx = idxNext;              // groupe court -> on saute
            }
            // Repli : direction du 1er pas non nul.
            QPointF d0 = pas(idxStart, idxStart+sens);
            double l0 = std::hypot(d0.x(), d0.y());
            if (l0 < 1e-9) return QPointF(1, 0);
            return QPointF(d0.x()/l0, d0.y()/l0);
        };

        // tangIn : direction du cote qui PART du point de depart.
        QPointF tangIn = tangenteDepuis(startPt, +1);

        // tangOut : direction du cote qui ARRIVE au point final.
        //  tangenteDepuis(-1) donne la direction "vers l'arriere" ;
        //  on l'inverse pour l'orienter dans le sens du parcours.
        QPointF tangOut;
        {
            QPointF tBack = tangenteDepuis(endPt, -1);
            tangOut = QPointF(-tBack.x(), -tBack.y());
        }

        // perpOut = perpendiculaire vers HORS matière.
        // Plus de G41/G42 : on detecte le sens de la trajectoire pour savoir
        // de quel cote est l'exterieur (= hors matiere).
        // Convention : matière = intérieur du contour dessiné.
        //   - Trajectoire CCW (sens trigo) : extérieur = à GAUCHE du parcours
        //   - Trajectoire CW (sens horaire) : extérieur = à DROITE du parcours
        // Calcul du sens du contour offsete (somme algebrique des cross products).
        auto computeIsCCW = [&]() -> bool {
            if (p.size() < 3) return true;
            double sum = 0.0;
            int n = p.size();
            for (int i = 0; i < n; ++i) {
                int j = (i + 1) % n;
                sum += (p[j].x() - p[i].x()) * (p[j].y() + p[i].y());
            }
            // sum < 0 => CCW (en convention Y vers le haut)
            return sum < 0;
        };
        const bool trajIsCCW = computeIsCCW();

        auto perpOut = [&](QPointF tang) -> QPointF {
            // CCW (contour anti-horaire) : matiere a gauche, exterieur a DROITE
            //    -> perp =  (y, -x)
            // CW  (contour horaire) : matiere a droite, exterieur a GAUCHE
            //    -> perp = (-y, x)
            const QPointF n = trajIsCCW ? QPointF( tang.y(), -tang.x())
                                        : QPointF(-tang.y(),  tang.x());
            return (sideSign < 0) ? -n : n;
        };
        QPointF perpEnt = perpOut(tangIn);
        QPointF perpExt = perpOut(tangOut);

        // Sens d'arc d'approche/sortie tangent (TEST - inverse) :
        //   - Trajectoire CCW (extérieur à gauche) : arc d'entrée CW = G2
        //   - Trajectoire CW  (extérieur à droite) : arc d'entrée CCW = G3
        const QString gArc = trajIsCCW ? "G2" : "G3";

        // Helper local : direction "hors matiere" en un point du contour
        //  offset, en tenant compte du COIN local (bissectrice exterieure
        //  pour les saillants, normale moyenne sinon).
        //  tIn, tOut = tangentes entrante et sortante en ce point.
        //  Retourne un vecteur unitaire pointant VERS LE BRUT (= hors matiere).
        auto outDirContourCoin = [&](QPointF tIn, QPointF tOut) -> QPointF {
            // Normale "vers le brut" (= cote matiere a enlever).
            // Matiere a enlever = exterieur du contour ferme dessine.
            //  CW (avalant)   : matiere a GAUCHE de la traj -> (-ty, tx)
            //  CCW (oppos.)   : matiere a DROITE de la traj -> ( ty,-tx)
            // (Convention coherente avec emitContour ouvert)
            auto normOut = [&](QPointF t) -> QPointF {
                const QPointF n = trajIsCCW ? QPointF( t.y(), -t.x())
                                            : QPointF(-t.y(),  t.x());
                return (sideSign < 0) ? -n : n;     // outil dedans : la matiere est dedans
            };
            QPointF nIn  = normOut(tIn);
            QPointF nOut = normOut(tOut);
            QPointF biss(nIn.x() + nOut.x(), nIn.y() + nOut.y());
            double l = std::hypot(biss.x(), biss.y());
            if (l < 1e-6) return nIn;
            biss = QPointF(biss.x()/l, biss.y()/l);
            // Si rentrant (creux), biss peut pointer mauvais cote
            double dotN = biss.x()*nIn.x() + biss.y()*nIn.y();
            if (dotN < 0) return nIn;
            return biss;
        };
        const QPointF inDir  = outDirContourCoin(tangIn,  tangIn);    // sur startPt
        const QPointF outDir = outDirContourCoin(tangOut, tangOut);   // sur endPt

        // Entrée
        const int t = qBound(0, d.typeEntree, 2);
        const double pp = qMax(0.0, d.paramEntree);
        if (t == 1 && pp > 1e-3) {
            // Arc tangent rayon paramEntree, depuis le brut
            double R = pp;
            QPointF perp1(-tangIn.y(),  tangIn.x());
            QPointF perp2( tangIn.y(), -tangIn.x());
            double dot1 = perp1.x()*inDir.x() + perp1.y()*inDir.y();
            double dot2 = perp2.x()*inDir.x() + perp2.y()*inDir.y();
            bool useP1 = dot1 >= dot2;
            QPointF perp = useP1 ? perp1 : perp2;
            QString gArcIn = useP1 ? "G3" : "G2";
            QPointF cen(startPt.x() + perp.x()*R, startPt.y() + perp.y()*R);
            approchePt = QPointF(cen.x() - tangIn.x()*R,
                                 cen.y() - tangIn.y()*R);
            double iVal = cen.x() - approchePt.x();
            double jVal = cen.y() - approchePt.y();
            entreeCode = QString("%1 X%2 Y%3 I%4 J%5")
                             .arg(gArcIn, fc(startPt.x()), fc(startPt.y()),
                                  fc(iVal), fc(jVal));
            entIsArc = true;
        } else if (t == 2) {
            // Ligne tangente vers le brut
            double dt1 =  tangIn.x()*inDir.x() + tangIn.y()*inDir.y();
            // approche = recul depuis startPt en sens contraire (donc tang projete sur outDir<0)
            QPointF dir = (dt1 >= 0) ? QPointF(-tangIn.x(),-tangIn.y()) : tangIn;
            approchePt = QPointF(startPt.x() + dir.x()*pp,
                                 startPt.y() + dir.y()*pp);
            entreeCode = QString("G1 X%1 Y%2").arg(fc(startPt.x()), fc(startPt.y()));
            entIsArc = false;
        } else {
            // Perpendiculaire au cote du brut
            double pSeg = (pp > 1e-3) ? pp : qMax(2.0, rOutil);
            approchePt = QPointF(startPt.x() + inDir.x()*pSeg,
                                 startPt.y() + inDir.y()*pSeg);
            // La plongee ne doit jamais entamer une paroi : on recule le point
            // tant qu'il est a moins d'un rayon outil du contour dessine.
            const double rMin = qAbs(rOutil + d.DR) - 0.01;
            for (int k = 0; k < 12 && pSeg > 0.05
                            && distToPolyline(path, approchePt, closed) < rMin; ++k) {
                pSeg *= 0.6;
                approchePt = QPointF(startPt.x() + inDir.x()*pSeg,
                                     startPt.y() + inDir.y()*pSeg);
            }
            entreeCode = QString("G1 X%1 Y%2").arg(fc(startPt.x()), fc(startPt.y()));
            entIsArc = false;
        }

        // Securite entree (tous types) : si l'approche ou l'arc entame une
        // paroi, on repasse en entree perpendiculaire raccourcie.
        {
            const double rMin = qAbs(rOutil + d.DR) - 0.01;
            bool badE = (sideSign != 0.0) && distToPolyline(path, approchePt, closed) < rMin;
            if (!badE && entIsArc && sideSign != 0.0) {
                const QPointF mid((approchePt.x()+startPt.x())*0.5, (approchePt.y()+startPt.y())*0.5);
                badE = distToPolyline(path, mid, closed) < rMin;
            }
            if (badE) {
                double pSeg = qMax(2.0, rOutil);
                for (int k = 0; k < 14; ++k) {
                    approchePt = QPointF(startPt.x() + inDir.x()*pSeg, startPt.y() + inDir.y()*pSeg);
                    if (distToPolyline(path, approchePt, closed) >= rMin || pSeg < 0.05) break;
                    pSeg *= 0.6;
                }
                entreeCode = QString("G1 X%1 Y%2").arg(fc(startPt.x()), fc(startPt.y()));
                entIsArc = false;
            }
        }

        // Sortie
        const int t2 = qBound(0, d.typeSortie, 2);
        const double pp2 = qMax(0.0, d.paramSortie);
        // outDir est calcule plus haut (bissectrice du coin local au contour).
        // Pour TOUS les types de sortie, on s'aligne sur cette direction
        // qui pointe hors matiere meme aux rentrants serres.
        if (t2 == 1 && pp2 > 1e-3) {
            // Arc tangent rayon paramSortie, vers le brut
            double R = pp2;
            // Centre de l'arc : depuis endPt, perpendiculaire a tangOut,
            // du cote du brut (= dans la direction de outDir).
            QPointF perp1(-tangOut.y(),  tangOut.x());
            QPointF perp2( tangOut.y(), -tangOut.x());
            double dot1 = perp1.x()*outDir.x() + perp1.y()*outDir.y();
            double dot2 = perp2.x()*outDir.x() + perp2.y()*outDir.y();
            bool useP1 = dot1 >= dot2;
            QPointF perp = useP1 ? perp1 : perp2;
            // useP1 (perp = (-ty, tx)) -> tangent CCW -> G3
            // useP2 (perp = ( ty,-tx)) -> tangent CW  -> G2
            QString gArcOut = useP1 ? "G3" : "G2";
            QPointF cen(endPt.x() + perp.x()*R, endPt.y() + perp.y()*R);
            sortiePt = QPointF(cen.x() + tangOut.x()*R,
                               cen.y() + tangOut.y()*R);
            double iVal = cen.x() - endPt.x();
            double jVal = cen.y() - endPt.y();
            sortieCode = QString("%1 X%2 Y%3 I%4 J%5")
                             .arg(gArcOut, fc(sortiePt.x()), fc(sortiePt.y()),
                                  fc(iVal), fc(jVal));
            sorIsArc = true;
        } else if (t2 == 2) {
            // Ligne tangente : la sortie PROLONGE le mouvement de
            // l'outil dans le sens de la trajectoire. Une sortie
            // tangente continue tout droit -> on suit tangOut tel
            // quel, SANS l'inverser (l'inverser ferait repartir
            // l'outil en arriere, vers la matiere).
            // tangOut = direction d'arrivee au point final, deja
            // orientee dans le sens du parcours.
            sortiePt = QPointF(endPt.x() + tangOut.x()*pp2,
                               endPt.y() + tangOut.y()*pp2);
            sortieCode = QString("G1 X%1 Y%2").arg(fc(sortiePt.x()), fc(sortiePt.y()));
            sorIsArc = false;
        } else {
            // Perpendiculaire : direction vers le bord du brut le plus proche
            double pSeg = (pp2 > 1e-3) ? pp2 : qMax(2.0, rOutil);
            sortiePt = QPointF(endPt.x() + outDir.x()*pSeg,
                               endPt.y() + outDir.y()*pSeg);
            const double rMin = qAbs(rOutil + d.DR) - 0.01;
            for (int k = 0; k < 12 && pSeg > 0.05
                            && distToPolyline(path, sortiePt, closed) < rMin; ++k) {
                pSeg *= 0.6;
                sortiePt = QPointF(endPt.x() + outDir.x()*pSeg,
                                   endPt.y() + outDir.y()*pSeg);
            }
            sortieCode = QString("G1 X%1 Y%2").arg(fc(sortiePt.x()), fc(sortiePt.y()));
            sorIsArc = false;
        }
        // Securite sortie (tous types)
        {
            const double rMin = qAbs(rOutil + d.DR) - 0.01;
            bool badS = (sideSign != 0.0) && distToPolyline(path, sortiePt, closed) < rMin;
            if (!badS && sorIsArc && sideSign != 0.0) {
                const QPointF mid((sortiePt.x()+endPt.x())*0.5, (sortiePt.y()+endPt.y())*0.5);
                badS = distToPolyline(path, mid, closed) < rMin;
            }
            if (badS) {
                double pSeg = qMax(2.0, rOutil);
                for (int k = 0; k < 14; ++k) {
                    sortiePt = QPointF(endPt.x() + outDir.x()*pSeg, endPt.y() + outDir.y()*pSeg);
                    if (distToPolyline(path, sortiePt, closed) >= rMin || pSeg < 0.05) break;
                    pSeg *= 0.6;
                }
                sortieCode = QString("G1 X%1 Y%2").arg(fc(sortiePt.x()), fc(sortiePt.y()));
                sorIsArc = false;
            }
        }
    };

    // Entree/sortie de la FINITION PAROI : calculee sur trajFin.
    // (L'ebauche-contour gere ses propres approches/sorties
    //  hors-brut dans emitContour ; elle n'utilise donc pas
    //  computeEntreeSortie.)
    QPointF apprFin, sortFin;
    QString entCodeFin, sorCodeFin;
    bool    entArcFin, sorArcFin;
    computeEntreeSortie(trajFin, apprFin, entCodeFin, entArcFin,
                        sortFin, sorCodeFin, sorArcFin);

    // ── 5. En-tête + outil ───────────────────────────────────
    out << blank();
    out << comment(QString("CONTOUR : %1  prof=%2mm  passe=%3mm")
                       .arg(d.contourName).arg(fc(d.profondeur)).arg(fc(d.profPasse)),
                   blkId);
    out << emitToolBlock(d.toolRef, d.toolByName,
                         d.spindleVal, d.spindleByVc,
                         0.0,                 // diametre : non utilise
                         d.brocheCW,
                         d.fonctionM1, d.fonctionM2, blkId);

    const QString fTrav = avanceStr(d.avanceVal,    d.avanceMode,   d.spindleVal, nbDentsOutil);
    double fDescVal = (d.descenteVal > 0) ? d.descenteVal : d.avanceVal;
    const QString fDesc = avanceStr(fDescVal,        d.descenteMode, d.spindleVal, nbDentsOutil);

    // ── 6. (Plus de sous-fichier .cnc - trajectoires inline) ─

    // ── 7. Rayons (info G10) ─────────────────────────────────
    const double rEbauche  = rOutil + d.DR + d.surepLat;
    const double rFinition = rOutil + d.DR;
    // Pour le contour, on fait UNE passe de finition au Z final
    // si surepLat > 0 (la finition reprend la matiere laissee par l'ebauche).
    // Le champ passeFinition est ignore (specifique aux poches).
    const bool   doFinition = (d.surepLat > 1e-6);

    out << line(GCodeLine::Rapid, "G0 Z"+fc(d.sautBride), "Plan saut bride", blkId);

    // ── 8. Lambda emitPass (NC en G40, trajectoire inline) ──
    //  Reçoit la trajectoire à exécuter (déjà offsetée par Clipper)
    //  et l'écrit en G1 inline dans le NC. Plus aucun M98.
    auto emitPass = [&](double zNext, const QString &fAv, double rayonD,
                        const QPointF &appr, const QString &entCode, bool entArc,
                        const QPointF &sort, const QString &sorCode, bool sorArc,
                        const QVector<QPointF> &traj) {
        // Plus de G10 D R : il ecrasait le diametre de l'outil dans la
        // table de CNC12. La trajectoire est deja decalee par EazyNC.
        out << comment(QString("Trajectoire centre outil - decalage %1 mm")
                           .arg(fc(rayonD)), blkId);
        // G0 X Y au point d'approche
        out << line(GCodeLine::Rapid,
                    QString("G0 X%1 Y%2").arg(fc(appr.x()), fc(appr.y())),
                    "Position approche", blkId);
        out << line(GCodeLine::Rapid, "G0 Z"+fc(d.approche), "Approche Z", blkId);
        // R0/RL/RR : la plongee se fait DANS la matiere (gorge) -> jamais en G0
        if (d.plongeeRapide && posOutil == 0) {
            out << line(GCodeLine::Rapid, "G0 Z"+fc(zNext), "Plongee rapide", blkId);
        } else {
            out << line(GCodeLine::Linear,
                        QString("G1 Z%1 %2").arg(fc(zNext), fDesc),
                        "Plongee F descente", blkId);
        }
        // Entrée matière
        out << line(entArc ? GCodeLine::Arc : GCodeLine::Linear,
                    entCode + " " + fAv,
                    d.typeEntree==1 ? "Arc entree" :
                        d.typeEntree==2 ? "Entree ligne" : "Entree perpendiculaire",
                    blkId);
        // Trajectoire INLINE en G1 (calculee par Clipper, plus de M98)
        // On commence a l'index 1 car traj[0] = startPt = deja atteint par l'entree.
        for (int i = 1; i < traj.size(); ++i) {
            out << line(GCodeLine::Linear,
                        QString("G1 X%1 Y%2").arg(fc(traj[i].x()), fc(traj[i].y())),
                        "", blkId);
        }
        Q_UNUSED(sort);
        out << line(sorArc ? GCodeLine::Arc : GCodeLine::Linear,
                    sorCode,
                    d.typeSortie==1 ? "Arc sortie" :
                        d.typeSortie==2 ? "Sortie ligne" : "Sortie perpendiculaire",
                    blkId);
        out << line(GCodeLine::Rapid, "G0 Z"+fc(d.sautBride), "Retrait", blkId);
    };

    // ── 9. Mode d'usinage : decider quelles phases generer ────
    //  modeUsin :
    //   0 = Ebauche + Finition (par defaut, comportement complet)
    //   1 = Ebauche seule (pas de finition au Z final)
    //   2 = Finition seule (pas de multi-passes ebauche, juste 1 passe)
    // ── 9a. R0 / RL / RR : passes en Z le long du trajet ─────────
    //  Pas d'ebauche « zone par zone » du brut (reservee au profil
    //  exterieur). On descend par passes de profPasse jusqu'au fond,
    //  puis une finition au Z final si demandee.
    if (posOutil != 0) {
        const QString fFinG = avanceStr(
            d.avanceFinition > 0 ? d.avanceFinition : d.avanceVal,
            d.avanceFinMode, d.spindleVal, nbDentsOutil);
        const bool finG = (d.modeUsin == 0 || d.modeUsin == 2);
        const bool ebG  = (d.modeUsin == 0 || d.modeUsin == 1);
        const double zEnd = -d.profondeur;
        const double zEb  = (finG && ebG) ? -(d.profondeur - d.surepProf) : zEnd;
        QPointF apprEb, sortEb; QString entEb, sorEb; bool entArcEb = false, sorArcEb = false;
        computeEntreeSortie(trajEb, apprEb, entEb, entArcEb, sortEb, sorEb, sorArcEb);
        out << blank();
        out << comment(QString("-- %1 : passes de %2 mm --")
                           .arg(posOutil == 1 ? "R0 SUR LA LIGNE"
                                : posOutil == 2 ? "RL A GAUCHE" : "RR A DROITE")
                           .arg(fc(d.profPasse)), blkId);
        if (ebG) {
            double z = 0.0; int n = 0;
            while (z > zEb + 1e-6) {
                z = qMax(z - d.profPasse, zEb);
                ++n;
                out << comment(QString("Passe %1  Z=%2").arg(n).arg(fc(z)), blkId);
                emitPass(z, fTrav, qAbs(offsetEb),
                         apprEb, entEb, entArcEb, sortEb, sorEb, sorArcEb, trajEb);
            }
        }
        if (finG && (!ebG || d.surepLat > 1e-6 || d.surepProf > 1e-6)) {
            out << blank();
            out << comment("-- FINITION (Z final) --", blkId);
            emitPass(zEnd, fFinG, qAbs(offsetFin),
                     apprFin, entCodeFin, entArcFin,
                     sortFin, sorCodeFin, sorArcFin, trajFin);
        }
        out << emitRetrait(d.sautBride, blkId);
        return out;
    }

    const bool doEbauche  = (d.modeUsin == 0 || d.modeUsin == 1);
    const bool doFinitionPhase = doFinition && (d.modeUsin == 0 || d.modeUsin == 2);

    // ── 10. Phase ÉBAUCHE ZONE PAR ZONE ──────────────────────────
    //
    //  Strategie :
    //   1) On calcule les ZONES MATIERE = brut MOINS (contour + offsetMin)
    //      Chaque zone disjointe est traitee independamment (les coins
    //      separes du brut deviennent des poches a vider).
    //   2) Pour chaque offset croissant du contour (du plus loin vers
    //      le plus proche du contour), on coupe la polyligne fermee
    //      par le rectangle brut -> on obtient des morceaux ouverts qui
    //      mordent dans la matiere (la fraise entre/ressort par les
    //      bords du brut).
    //   3) Chaque morceau est range dans la zone qui contient son
    //      milieu. Au sein d'une zone, on emet les morceaux du plus
    //      eloigne du contour (cote brut) au plus proche (cote piece).
    //   4) Sens d'usinage : la polyligne d'offset est forcee CCW
    //      (= avalant pour contournage externe, matiere a droite).
    //      En mode opposition (modeFraisage=1), on inverse chaque
    //      morceau pour avoir matiere a gauche du mouvement.
    const double zFinal = -(d.profondeur - d.surepProf);
    double zCur     = 0.0;
    int    passNum  = 0;

    // ── 9bis. Conditions d'execution ──────────────────────────
    //  needSegments = on doit calculer les morceaux d'usinage
    //  La "finition fond" rejoue les passes au Z FINAL pour racler la
    //  surcote en fond.
    //
    //  Active toujours dans :
    //   - modeUsin=0 (ebauche+finition) : ebauche descend a -prof+surepProf
    //     puis finition fond racle a -prof
    //   - modeUsin=2 (finition seule)   : pas d'ebauche dans CE bloc, mais
    //     un bloc precedent l'a faite avec un autre outil. On racle le
    //     fond restant (surepProf) AVANT la passe paroi.
    //
    //  Ordre garanti dans tous les cas : ebauche -> finition fond -> paroi.
    const bool doFinitionFond =
        (d.modeUsin == 0 || d.modeUsin == 2) && d.surepProf > 1e-6;
    const bool needSegments = doEbauche || doFinitionFond;

    if (needSegments) {
        out << blank();
        out << blank();
        if (doEbauche)
            out << comment("-- EBAUCHE ZONE PAR ZONE --", blkId);
        else
            out << comment("-- FINITION FOND SEULE (mode 2) --", blkId);

        if (!d.blkValid) {
            out << comment("ERREUR ebauche : BLK FORM invalide ou absent", blkId);
        } else {
            // ── 10.1 Brut + parametres ────────────────────────
            const double brutMinX = d.blkXmin;
            const double brutMaxX = d.blkXmax;
            const double brutMinY = d.blkYmin;
            const double brutMaxY = d.blkYmax;
            const double cx = (brutMinX + brutMaxX) * 0.5;
            const double cy = (brutMinY + brutMaxY) * 0.5;
            const double lx = brutMaxX - brutMinX;
            const double ly = brutMaxY - brutMinY;
            Path2D brutRect = Clipper2Utils::makeRect(cx, cy, lx, ly, 0.0);
            Paths2D brutPaths; brutPaths << brutRect;

            const int    stepPct = qBound(10, d.stepOverPct, 95);
            const double stepOver = 2.0 * rOutil * stepPct / 100.0;
            const double offsetMin = rOutil + d.DR + d.surepLat;

            // offsetMax = distance max d'un point du contour aux coins
            // du brut. Au-dela, la polyligne d'offset est forcement
            // entierement hors brut -> aucun morceau utile.
            double offsetMax = 0;
            for (const QPointF &p : path) {
                double d1 = std::hypot(p.x() - brutMinX, p.y() - brutMinY);
                double d2 = std::hypot(p.x() - brutMaxX, p.y() - brutMinY);
                double d3 = std::hypot(p.x() - brutMinX, p.y() - brutMaxY);
                double d4 = std::hypot(p.x() - brutMaxX, p.y() - brutMaxY);
                double dMax = qMax(qMax(d1, d2), qMax(d3, d4));
                if (dMax > offsetMax) offsetMax = dMax;
            }
            offsetMax += stepOver;

            // ── 10.1bis Mode "Coins uniquement" (rest machining) ──
            //  Si strategieFinition = 1 ET toolRefEbauche definit un
            //  outil PLUS GROS que l'outil actuel, on suppose qu'il a
            //  deja nettoye tout ce qu'il pouvait. L'outil actuel ne
            //  s'occupe que des rayons concaves qu'il n'a pas pu faire.
            //  Concretement : on plafonne offsetMax pour ne generer que
            //  les passes situees dans l'anneau laisse par le grand outil.
            double rOutilPrec = 0.0;
            {
                QString tEb = d.toolRefEbauche.trimmed();
                if (tEb.startsWith('T', Qt::CaseInsensitive)) tEb = tEb.mid(1);
                bool ok = false; int tiEb = tEb.toInt(&ok);
                if (ok && tiEb > 0) {
                    const ToolData *tdEb = ToolDatabase::instance().toolByT(tiEb);
                    if (tdEb && tdEb->Routil > 0) rOutilPrec = tdEb->Routil;
                }
            }
            const bool restMach = (d.strategieFinition == 1)
                                && (rOutilPrec > rOutil + 1e-3);
            if (restMach) {
                double offsetMaxAng = rOutilPrec + d.DR + d.surepLat + stepOver;
                if (offsetMaxAng < offsetMax)
                    offsetMax = offsetMaxAng;
                out << comment(QString("Resmat : outil ebauche T%1 (R=%2), "
                                       "offsetMax plafonne a %3")
                                   .arg(d.toolRefEbauche)
                                   .arg(fc(rOutilPrec)).arg(fc(offsetMax)),
                               blkId);
            } else if (d.strategieFinition == 1) {
                out << comment("Resmat ignore : toolRefEbauche invalide ou plus petit "
                               "que l'outil actuel", blkId);
            }

            // ── 10.2 Path sans doublon de fermeture ───────────
            Path2D pathNoClose = path;
            if (closed && pathNoClose.size() >= 2 &&
                std::hypot(pathNoClose.last().x()-pathNoClose.first().x(),
                           pathNoClose.last().y()-pathNoClose.first().y()) < 1e-3)
                pathNoClose.removeLast();

            // ── 10.3 Calcul des passes : 2 phases avec CLIP par brut ──
            //  Algo :
            //   - On itere des offsets croissants du contour : offsetMin,
            //     offsetMin+ae, offsetMin+2*ae, ... ou ae = stepOver.
            //   - Chaque offset est CLIPPE par le brut elargi de rOutil.
            //     -> on garde les portions DEDANS (fermees ou ouvertes).
            //   - Une portion FERMEE   = boucle qui suit le contour
            //                            entierement dans le brut.
            //   - Une portion OUVERTE  = arc parallele au contour qui
            //                            rentre/sort par les bords du brut.
            //   - On accumule jusqu'a ce qu'aucun offset ne reste DANS
            //     le brut elargi (= matiere entierement nettoyee de ce
            //     cote).
            //   - La zone matiere RESIDUELLE (= ce qui n'a pas pu etre
            //     atteint par les offsets) est traitee en MODE POCHE
            //     concentrique.
            //   - Execution : POCHE d'abord (residu), puis CONTOURNAGE
            //     (toutes les portions clippees, de la plus eloignee du
            //     contour a la plus proche).
            const double ae = stepOver;

            // Brut elargi de rOutil (fraise peut deborder)
            Path2D brutBig = Clipper2Utils::makeRect(cx, cy,
                                                     lx + 2.0 * rOutil,
                                                     ly + 2.0 * rOutil, 0.0);
            Paths2D brutBigPaths; brutBigPaths << brutBig;

            // ── 10.3.1 Helpers de clip (Liang-Barsky par rectangle) ──
            //  Le brut elargi est un rectangle, donc clip rectangulaire
            //  efficace par Liang-Barsky parametrique.
            const double bxMin = brutMinX - rOutil;
            const double bxMax = brutMaxX + rOutil;
            const double byMin = brutMinY - rOutil;
            const double byMax = brutMaxY + rOutil;
            auto inBrutBig = [&](const QPointF &p) -> bool {
                return p.x() >= bxMin - 1e-6 && p.x() <= bxMax + 1e-6
                    && p.y() >= byMin - 1e-6 && p.y() <= byMax + 1e-6;
            };
            auto clipParam = [&](const QPointF &a, const QPointF &b,
                                  double &tEnt, double &tExt) -> bool {
                tEnt = 0.0; tExt = 1.0;
                double dx = b.x() - a.x();
                double dy = b.y() - a.y();
                auto clip1 = [&](double p, double q) -> bool {
                    if (qAbs(p) < 1e-12) return q >= 0;
                    double t = q / p;
                    if (p < 0) { if (t > tExt) return false; if (t > tEnt) tEnt = t; }
                    else       { if (t < tEnt) return false; if (t < tExt) tExt = t; }
                    return true;
                };
                if (!clip1(-dx, a.x() - bxMin)) return false;
                if (!clip1( dx, bxMax - a.x())) return false;
                if (!clip1(-dy, a.y() - byMin)) return false;
                if (!clip1( dy, byMax - a.y())) return false;
                return tEnt < tExt - 1e-9;
            };

            // Clippe une polyligne FERMEE (premier != dernier) par le brut
            // elargi. Renvoie une liste de morceaux : si tout est dedans
            // -> 1 polyligne fermee. Si decoupage -> N polylignes ouvertes.
            // Si tout est dehors -> liste vide.
            // Resultat : chaque path renvoye porte un flag implicite :
            //   isLoop = (path.size>=3 && first==last avec tolerance)
            auto clipOffsetToBrut = [&](const Path2D &closedPoly) -> Paths2D {
                Paths2D outPaths;
                int n = closedPoly.size();
                if (n < 3) return outPaths;

                // Cas 1 : tout dedans -> boucle fermee complete
                bool allIn = true;
                for (const QPointF &p : closedPoly)
                    if (!inBrutBig(p)) { allIn = false; break; }
                if (allIn) {
                    Path2D loop = closedPoly;
                    loop << loop.first();
                    outPaths << loop;
                    return outPaths;
                }

                // Cas 2 : decoupage. Demarrer depuis un vertex HORS brut.
                int startIdx = -1;
                for (int i = 0; i < n; ++i)
                    if (!inBrutBig(closedPoly[i])) { startIdx = i; break; }
                if (startIdx < 0) return outPaths; // ne devrait pas arriver

                Path2D current;
                for (int i = 0; i < n; ++i) {
                    int idx     = (startIdx + i)     % n;
                    int nextIdx = (startIdx + i + 1) % n;
                    const QPointF &a = closedPoly[idx];
                    const QPointF &b = closedPoly[nextIdx];
                    double tEnt, tExt;
                    if (!clipParam(a, b, tEnt, tExt)) {
                        if (current.size() >= 2) outPaths << current;
                        current.clear();
                        continue;
                    }
                    QPointF pEnt(a.x() + tEnt*(b.x()-a.x()),
                                 a.y() + tEnt*(b.y()-a.y()));
                    QPointF pExt(a.x() + tExt*(b.x()-a.x()),
                                 a.y() + tExt*(b.y()-a.y()));
                    if (tEnt > 1e-9) {
                        if (current.size() >= 2) outPaths << current;
                        current.clear();
                        current << pEnt;
                    }
                    if (tExt < 1.0 - 1e-9) {
                        current << pExt;
                        if (current.size() >= 2) outPaths << current;
                        current.clear();
                    } else {
                        current << b;
                    }
                }
                if (current.size() >= 2) outPaths << current;
                return outPaths;
            };

            // ── 10.3.2 Collecte des passes de contournage ──
            //  Chaque "passe" peut etre une boucle fermee OU une polyligne
            //  ouverte qui rentre/sort par les bords du brut.
            //  On itere offsets CROISSANTS : on s'arrete quand le clip
            //  est vide (= offset entierement hors brut elargi).
            struct ContourPass {
                double  offset;   // distance au contour
                Path2D  path;     // points (boucle fermee ou ouverte)
                bool    isLoop;   // true = boucle fermee
            };
            QVector<ContourPass> passesContour;

            double dCur = offsetMin;
            double dDernierContour = offsetMin;
            int safetyContour = 0;
            while (safetyContour++ < 500) {
                QVector<QPointF> off = offsetPolyline(pathNoClose, dCur, true);
                if (off.size() < 3) break;
                Path2D offP;
                int nOff = off.size();
                if (nOff >= 2 &&
                    std::hypot(off.last().x()-off.first().x(),
                               off.last().y()-off.first().y()) < 1e-3)
                    --nOff;
                for (int i = 0; i < nOff; ++i) offP << off[i];
                if (offP.size() < 3) break;

                Paths2D parts = clipOffsetToBrut(offP);
                if (parts.isEmpty()) break;   // plus rien dans le brut

                for (const Path2D &piece : parts) {
                    if (piece.size() < 2) continue;
                    bool isLoop = (piece.size() >= 3 &&
                        std::hypot(piece.last().x()-piece.first().x(),
                                   piece.last().y()-piece.first().y()) < 1e-3);
                    ContourPass cp;
                    cp.offset = dCur;
                    cp.path   = piece;
                    cp.isLoop = isLoop;
                    passesContour << cp;
                }
                dDernierContour = dCur;
                dCur += ae;
            }

            // ── 10.3.3 Zone matiere RESIDUELLE pour la phase poche ──
            //  Zone = (brut+rOutil) MOINS (contour+dDernierContour).
            //  = parties du brut elargi qui n'ont jamais ete couvertes
            //    par les offsets clippes (= coins isoles trop loin du
            //    contour pour qu'un offset y arrive).
            Paths2D zonePolygons;
            {
                Paths2D contourPlusReserve =
                    Clipper2Utils::offset(pathNoClose, dDernierContour, 1);
                zonePolygons = Clipper2Utils::difference(brutBigPaths,
                                                        contourPlusReserve);
            }
            const int nZones = zonePolygons.size();

            if (passesContour.isEmpty() && nZones == 0) {
                out << comment("EBAUCHE SKIP : rien a usiner", blkId);
            } else {
                // ── 10.4 Helpers : direction hors brut + emission passe ──
                auto outDirForBrutEdge = [&](const QPointF &p) -> QPointF {
                    double dXmin = qAbs(p.x() - brutMinX);
                    double dXmax = qAbs(p.x() - brutMaxX);
                    double dYmin = qAbs(p.y() - brutMinY);
                    double dYmax = qAbs(p.y() - brutMaxY);
                    double mn = qMin(qMin(dXmin, dXmax), qMin(dYmin, dYmax));
                    if      (mn == dXmin) return QPointF(-1, 0);
                    else if (mn == dXmax) return QPointF( 1, 0);
                    else if (mn == dYmin) return QPointF( 0,-1);
                    else                  return QPointF( 0, 1);
                };
                auto distToBrut = [&](const QPointF &p) -> double {
                    double dXmin = qAbs(p.x() - brutMinX);
                    double dXmax = qAbs(p.x() - brutMaxX);
                    double dYmin = qAbs(p.y() - brutMinY);
                    double dYmax = qAbs(p.y() - brutMaxY);
                    return qMin(qMin(dXmin, dXmax), qMin(dYmin, dYmax));
                };

                // Sens des passes concentriques (poche).
                //  - avalant     : path CW  -> cw = true
                //  - opposition  : path CCW -> cw = false
                const bool cw = (d.modeFraisage == 0);

                // ── Direction sortante locale au contour ──────────────
                //  Pour un point d'une polyligne offset (pass) avec tangentes
                //  entrante (depuis le point precedent) et sortante (vers le
                //  point suivant), on calcule la direction qui s'eloigne du
                //  contour :
                //   - Coin SAILLANT (la pliure pointe vers la matiere a
                //     enlever) -> bissectrice exterieure du coin
                //   - Coin RENTRANT (creux dans la piece) -> normale moyenne
                //   - Segment droit (tangentes alignees) -> normale au segment
                //  Convention : path est dans le sens CW si avalant, CCW
                //  sinon. Pour un contour ferme avec matiere INTERIEURE...
                //  Mais ici matiere = EXTERIEURE (autour du contour ferme).
                //  La direction "hors matiere a enlever" = vers la piece
                //  finale = PAS ce qu'on veut. On veut "vers le brut" = OPPOSE.
                //
                //  En pratique : on calcule la normale qui pointe vers
                //  l'EXTERIEUR du contour (= vers le brut), perpendiculaire
                //  a la tangente. Convention :
                //   - path CW (avalant)  : normale = (-ty,  tx)
                //   - path CCW (oppos.)  : normale = ( ty, -tx)
                //  Si on est sur un coin, on combine les deux normales.
                auto outDirContourRaw = [&](const Path2D &pass, int idx) -> QPointF {
                    int n = pass.size();
                    if (n < 2) return QPointF(0, 1);
                    // Tangente entrante : pass[idx-1] -> pass[idx]
                    QPointF tIn(0,0), tOut(0,0);
                    if (idx > 0) {
                        tIn = QPointF(pass[idx].x() - pass[idx-1].x(),
                                      pass[idx].y() - pass[idx-1].y());
                        double l = std::hypot(tIn.x(), tIn.y());
                        if (l > 1e-9) tIn = QPointF(tIn.x()/l, tIn.y()/l);
                    }
                    if (idx < n - 1) {
                        tOut = QPointF(pass[idx+1].x() - pass[idx].x(),
                                       pass[idx+1].y() - pass[idx].y());
                        double l = std::hypot(tOut.x(), tOut.y());
                        if (l > 1e-9) tOut = QPointF(tOut.x()/l, tOut.y()/l);
                    }
                    // Si premier ou dernier point, utiliser une seule tangente
                    if (tIn.x() == 0 && tIn.y() == 0) tIn = tOut;
                    if (tOut.x() == 0 && tOut.y() == 0) tOut = tIn;

                    // Normale pointant vers l'EXTERIEUR du contour ferme
                    //  (= vers la matiere a enlever, vers le brut)
                    //  Pour un polygone vu de dessus :
                    //   - parcours CW  -> exterieur a GAUCHE  -> (-ty,  tx)
                    //   - parcours CCW -> exterieur a DROITE -> ( ty, -tx)
                    auto normOut = [&](QPointF t) -> QPointF {
                        return cw ? QPointF(-t.y(),  t.x())
                                  : QPointF( t.y(), -t.x());
                    };
                    QPointF nIn  = normOut(tIn);
                    QPointF nOut = normOut(tOut);

                    // Combinaison : bissectrice exterieure = moyenne
                    QPointF biss(nIn.x() + nOut.x(), nIn.y() + nOut.y());
                    double l = std::hypot(biss.x(), biss.y());
                    if (l < 1e-6) {
                        // Coin tres aigu / inverse : fallback sur nIn
                        return nIn;
                    }
                    biss = QPointF(biss.x()/l, biss.y()/l);

                    // Detection coin RENTRANT (creux) :
                    //  z = tIn × tOut. Pour path CW, coin saillant <=> z<0
                    //  Pour path CCW, coin saillant <=> z>0
                    double cross = tIn.x()*tOut.y() - tIn.y()*tOut.x();
                    bool isSaillant = cw ? (cross < -1e-6) : (cross > 1e-6);
                    if (!isSaillant) {
                        // Rentrant : la bissectrice peut pointer vers la
                        // matiere finale -> on retombe sur la normale moyenne
                        // (qui pointe vers le brut)
                        // Comme nIn et nOut pointent deja vers le brut, leur
                        // moyenne normalisee est OK. C'est ce qu'on calcule
                        // deja (biss). Mais pour ne pas creuser dans la
                        // piece, on garde simplement la bissectrice.
                        // Note : pour un rentrant TRES marque, biss est
                        // l'oppose de ce qu'on veut. Fallback : nIn.
                        // On detecte ce cas par : biss . nIn < 0
                        double dotN = biss.x()*nIn.x() + biss.y()*nIn.y();
                        if (dotN < 0) return nIn;
                    }
                    return biss;
                };
                // Distance signee a la piece (contour dessine) : <0 dedans, >0 dehors
                auto distPiece = [&](const QPointF &q) -> double {
                    const int n = path.size();
                    if (n < 2) return 0.0;
                    double best = 1e30; bool inside = false;
                    for (int i = 0, j = n - 1; i < n; j = i++) {
                        const QPointF &a = path[j], &b = path[i];
                        if (((a.y() > q.y()) != (b.y() > q.y())) &&
                            (q.x() < (b.x()-a.x())*(q.y()-a.y())/(b.y()-a.y()) + a.x()))
                            inside = !inside;
                        const double dx = b.x()-a.x(), dy = b.y()-a.y();
                        const double L2 = dx*dx + dy*dy;
                        double t = L2 > 1e-12 ? ((q.x()-a.x())*dx + (q.y()-a.y())*dy) / L2 : 0.0;
                        t = qBound(0.0, t, 1.0);
                        best = qMin(best, std::hypot(q.x()-(a.x()+t*dx), q.y()-(a.y()+t*dy)));
                    }
                    return inside ? -best : best;
                };
                // Direction de sortie : quel que soit le sens de parcours de la
                // passe (dessin, avalant, opposition), elle doit s'ELOIGNER de la piece.
                auto outDirContour = [&](const Path2D &pass, int idx) -> QPointF {
                    QPointF r = outDirContourRaw(pass, idx);
                    if (idx < 0 || idx >= pass.size()) return r;
                    const QPointF p = pass[idx];
                    const double e = qMax(0.5, rOutil * 0.5);
                    if (distPiece(p - r*e) > distPiece(p + r*e)) r = -r;
                    return r;
                };

                // Lambda : emet une passe de contournage
                //  - cp.isLoop = true  -> boucle fermee, on cherche le point
                //                         le plus proche du brut comme depart,
                //                         on entre hors brut, on suit la boucle.
                //  - cp.isLoop = false -> polyligne ouverte qui rentre/sort
                //                         par les bords du brut. On entre au
                //                         1er point hors brut, on suit, on
                //                         ressort hors brut au dernier point.
                auto emitContour = [&](const ContourPass &cp, double zNext) {
                    if (cp.path.size() < 2) return;
                    Path2D pass = cp.path;

                    if (cp.isLoop) {
                        // Retirer doublon de fermeture
                        if (pass.size() >= 2 &&
                            std::hypot(pass.last().x()-pass.first().x(),
                                       pass.last().y()-pass.first().y()) < 1e-6)
                            pass.removeLast();
                        if (pass.size() < 3) return;
                        // REGLE : on usine dans le sens du contour dessine.
                        // 'pass' vient de offsetPolyline(path) qui preserve
                        // ce sens -> on ne force PAS d'orientation absolue.
                        // Trouver point le plus proche du brut comme depart
                        int idxMin = 0; double dMin = 1e9;
                        for (int k = 0; k < pass.size(); ++k) {
                            double dd = distToBrut(pass[k]);
                            if (dd < dMin) { dMin = dd; idxMin = k; }
                        }
                        if (idxMin > 0)
                            std::rotate(pass.begin(),
                                        pass.begin()+idxMin, pass.end());
                        // Refermer la boucle
                        pass.append(pass.first());
                    } else {
                        // Polyligne ouverte (contournage le long du profil).
                        // REGLE : on usine dans le sens du contour dessine.
                        // offsetPolyline preserve ce sens -> on ne touche
                        // a rien, 'pass' garde l'ordre herite.
                    }

                    QPointF startPt = pass.first();
                    QPointF outDirS = outDirContour(pass, 0);
                    const double pE = qMax(d.paramEntree, qMax(2.0, rOutil));
                    QPointF appPt(startPt.x() + outDirS.x()*pE,
                                  startPt.y() + outDirS.y()*pE);

                    // Approche + plongee
                    out << line(GCodeLine::Rapid,
                                QString("G0 X%1 Y%2").arg(fc(appPt.x()), fc(appPt.y())),
                                "Approche hors brut", blkId);
                    out << line(GCodeLine::Rapid, "G0 Z"+fc(d.approche),
                                "Plan approche Z", blkId);
                    if (d.plongeeRapide)
                        out << line(GCodeLine::Rapid, "G0 Z"+fc(zNext),
                                    "Plongee Z hors brut", blkId);
                    else
                        out << line(GCodeLine::Linear,
                                    QString("G1 Z%1 %2").arg(fc(zNext), fDesc),
                                    "Plongee F descente", blkId);
                    out << line(GCodeLine::Linear,
                                QString("G1 X%1 Y%2 %3")
                                    .arg(fc(startPt.x()), fc(startPt.y()), fTrav),
                                "Entree matiere", blkId);
                    for (int i = 1; i < pass.size(); ++i) {
                        out << line(GCodeLine::Linear,
                                    QString("G1 X%1 Y%2")
                                        .arg(fc(pass[i].x()), fc(pass[i].y())),
                                    "", blkId);
                    }
                    // Si polyligne ouverte : sortie hors brut au dernier point
                    if (!cp.isLoop) {
                        QPointF endPt = pass.last();
                        QPointF outDirE = outDirContour(pass, pass.size() - 1);
                        const double pS = qMax(d.paramSortie, qMax(2.0, rOutil));
                        QPointF sortiePt(endPt.x() + outDirE.x()*pS,
                                         endPt.y() + outDirE.y()*pS);
                        out << line(GCodeLine::Linear,
                                    QString("G1 X%1 Y%2")
                                        .arg(fc(sortiePt.x()), fc(sortiePt.y())),
                                    "Sortie hors brut", blkId);
                    }
                    out << line(GCodeLine::Rapid, "G0 Z"+fc(d.sautBride),
                                "Retrait", blkId);
                };

                // Lambda : emet une passe poche (boucle fermee) avec
                // plongee hors brut si possible, sinon plongee verticale.
                const double seuilProche = qMax(1.0, rOutil * 0.6);
                auto emitPoche = [&](const Path2D &passIn, double zNext) {
                    if (passIn.size() < 3) return;
                    Path2D pass = passIn;
                    if (pass.size() >= 2 &&
                        std::hypot(pass.last().x()-pass.first().x(),
                                   pass.last().y()-pass.first().y()) < 1e-6)
                        pass.removeLast();
                    if (pass.size() < 3) return;
                    pass = cw ? Clipper2Utils::ensureCW(pass)
                              : Clipper2Utils::ensureCCW(pass);
                    // Cherche point le plus proche du brut
                    int idxMin = 0; double dMin = 1e9;
                    for (int k = 0; k < pass.size(); ++k) {
                        double dd = distToBrut(pass[k]);
                        if (dd < dMin) { dMin = dd; idxMin = k; }
                    }
                    if (idxMin > 0)
                        std::rotate(pass.begin(),
                                    pass.begin()+idxMin, pass.end());
                    pass.append(pass.first());
                    QPointF startPt = pass.first();

                    if (dMin <= seuilProche) {
                        // Entree hors brut (cote pres du brut)
                        // Bissectrice du coin local pour eviter de manger
                        // les rayons du contour.
                        QPointF outDir = outDirContour(pass, 0);
                        const double pE = qMax(d.paramEntree, qMax(2.0, rOutil));
                        QPointF appPt(startPt.x() + outDir.x()*pE,
                                      startPt.y() + outDir.y()*pE);
                        out << line(GCodeLine::Rapid,
                                    QString("G0 X%1 Y%2").arg(fc(appPt.x()), fc(appPt.y())),
                                    "Approche hors brut", blkId);
                        out << line(GCodeLine::Rapid, "G0 Z"+fc(d.approche),
                                    "Plan approche Z", blkId);
                        if (d.plongeeRapide)
                            out << line(GCodeLine::Rapid, "G0 Z"+fc(zNext),
                                        "Plongee Z hors brut", blkId);
                        else
                            out << line(GCodeLine::Linear,
                                        QString("G1 Z%1 %2").arg(fc(zNext), fDesc),
                                        "Plongee F descente", blkId);
                        out << line(GCodeLine::Linear,
                                    QString("G1 X%1 Y%2 %3")
                                        .arg(fc(startPt.x()), fc(startPt.y()), fTrav),
                                    "Entree matiere", blkId);
                    } else {
                        // Plongee matiere (verticale pour l'instant, rampe a venir)
                        out << line(GCodeLine::Rapid,
                                    QString("G0 X%1 Y%2")
                                        .arg(fc(startPt.x()), fc(startPt.y())),
                                    "Position plongee matiere", blkId);
                        out << line(GCodeLine::Rapid, "G0 Z"+fc(d.approche),
                                    "Plan approche Z", blkId);
                        out << line(GCodeLine::Linear,
                                    QString("G1 Z%1 %2").arg(fc(zNext), fDesc),
                                    "Plongee matiere (vertical)", blkId);
                    }
                    for (int i = 1; i < pass.size(); ++i) {
                        out << line(GCodeLine::Linear,
                                    QString("G1 X%1 Y%2")
                                        .arg(fc(pass[i].x()), fc(pass[i].y())),
                                    "", blkId);
                    }
                    out << line(GCodeLine::Rapid, "G0 Z"+fc(d.sautBride),
                                "Retrait", blkId);
                };

                // ── 10.5 Pre-calcul passes POCHE par zone ─────────
                //  Passes concentriques dans chaque zone matiere residuelle.
                //  concentricPasses retourne CENTRE -> PAROI ; on inverse
                //  pour avoir PAROI -> CENTRE (attaque par les bords d'abord).
                struct PochePass { int zoneIdx; Path2D path; };
                QVector<PochePass> pochePasses;
                for (int z = 0; z < nZones; ++z) {
                    Paths2D pp = Clipper2Utils::concentricPasses(
                        zonePolygons[z], rOutil, d.DR, stepPct);
                    for (int idx = pp.size() - 1; idx >= 0; --idx) {
                        if (pp[idx].size() < 3) continue;
                        PochePass x; x.zoneIdx = z; x.path = pp[idx];
                        pochePasses << x;
                    }
                }

                // ── 10.6 Boucle des passes Z (EBAUCHE) ────────────
                out << comment(QString("%1 zone(s) poche, %2 passe(s) poche, "
                                       "%3 passe(s) contour")
                                   .arg(nZones)
                                   .arg(pochePasses.size())
                                   .arg(passesContour.size()), blkId);

                if (doEbauche) {
                    while (zCur > zFinal - 1e-6) {
                        ++passNum;
                        const double zNext = qMax(zCur - d.profPasse, zFinal);
                        if (qAbs(zNext - zCur) < 1e-9) break;
                        out << blank();
                        out << comment(QString("Passe Z %1  Z=%2")
                                           .arg(passNum).arg(fc(zNext)), blkId);

                        // ── Phase 1 : POCHE (gros volumes d'abord) ──
                        if (!pochePasses.isEmpty()) {
                            out << comment(QString("Phase 1 POCHE (%1 passe(s))")
                                               .arg(pochePasses.size()), blkId);
                            for (const PochePass &pp : std::as_const(pochePasses))
                                emitPoche(pp.path, zNext);
                        }

                        // ── Phase 2 : CONTOURNAGE (pelage du contour) ──
                        if (!passesContour.isEmpty()) {
                            out << comment(QString("Phase 2 CONTOURNAGE (%1 passe(s))")
                                               .arg(passesContour.size()), blkId);
                            // Ordre : du PLUS LOIN du contour vers le PLUS PROCHE
                            for (int i = passesContour.size() - 1; i >= 0; --i)
                                emitContour(passesContour[i], zNext);
                        }

                        zCur = zNext;
                        if (zCur <= zFinal + 1e-6) break;
                    }
                }

                // ── 10.7 FINITION FOND (poche + contour a Z final) ──
                if (doFinitionFond) {
                    const double zFond = -d.profondeur;
                    out << blank();
                    out << comment("-- FINITION FOND (passes a Z final) --",
                                   blkId);
                    if (!pochePasses.isEmpty()) {
                        out << comment(QString("Finition fond poche (%1 passe(s))")
                                           .arg(pochePasses.size()), blkId);
                        for (const PochePass &pp : std::as_const(pochePasses))
                            emitPoche(pp.path, zFond);
                    }
                    if (!passesContour.isEmpty()) {
                        out << comment(QString("Finition fond contournage (%1 passe(s))")
                                           .arg(passesContour.size()), blkId);
                        for (int i = passesContour.size() - 1; i >= 0; --i)
                            emitContour(passesContour[i], zFond);
                    }
                }
            }
        }
    } else {
        // Cas : pas de calcul de segments (ex: modeUsin 2 + Contour complet,
        // ou modeUsin 1 sans ebauche, etc.) -> on saute directement aux
        // phases de finition paroi (11) si applicable.
        out << blank();
        out << comment("-- PAS DE CALCUL DE SEGMENTS (passe directe a finition paroi) --",
                       blkId);
    }

    // ── 11. Phase FINITION PAROI (1 passe le long du contour) ─
    //  Skipped quand strategieFinition = 1 (coins uniquement) :
    //  la passe contour a deja ete faite par l'outil precedent.
    if (doFinitionPhase && d.strategieFinition != 1) {
        const QString fFin = avanceStr(
            d.avanceFinition > 0 ? d.avanceFinition : d.avanceVal,
            d.avanceFinMode, d.spindleVal, nbDentsOutil);
        out << blank();
        out << comment("-- FINITION PAROI (passe contour) --", blkId);
        emitPass(-d.profondeur, fFin, rFinition,
                 apprFin, entCodeFin, entArcFin,
                 sortFin, sorCodeFin, sorArcFin,
                 trajFin);
    } else if (doFinitionPhase && d.strategieFinition == 1) {
        out << blank();
        out << comment("-- FINITION PAROI SKIP (strategie Coins uniquement) --", blkId);
    }

    // ── 12. Retrait final ─────────────────────────────────────
    out << emitRetrait(d.sautBride, blkId);
    return out;
}
