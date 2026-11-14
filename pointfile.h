#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  pointfile.h — Fichier de points EazyNC (.pnt)
//  Équivalent du .PNT iTNC 530 / TNC 620, mais EN PLUS : un fichier
//  peut cumuler PLUSIEURS entrées de motifs différents (ex : une
//  grille + un cercle + un point isolé), chacune avec son propre
//  mode Point/Ligne/Grille/Cercle/Tableau. Les points de toutes les
//  entrées sont concaténés pour former la liste finale.
// ═══════════════════════════════════════════════════════════════
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QtMath>

// ───────────────────────────────────────────────────────────────
//  PointEntry — un point d'usinage (résultat final, après calcul)
// ───────────────────────────────────────────────────────────────
struct PointEntry
{
    double  x     = 0.0;
    double  y     = 0.0;
    double  z     = 0.0;
    bool    actif = true;

    QJsonObject toJson() const {
        QJsonObject o;
        o["x"] = x; o["y"] = y; o["z"] = z; o["actif"] = actif;
        return o;
    }
    static PointEntry fromJson(const QJsonObject &o) {
        return { o["x"].toDouble(), o["y"].toDouble(),
                 o["z"].toDouble(), o["actif"].toBool(true) };
    }
};

// ───────────────────────────────────────────────────────────────
//  PatternEntry — UN bloc de motif (Point/Ligne/Grille/Cercle/
//  Tableau) avec ses propres paramètres. Un PointFile en contient
//  une liste ; chaque entrée génère ses points, tous concaténés.
// ───────────────────────────────────────────────────────────────
struct PatternEntry
{
    enum Mode { ModePoint=0, ModeLigne=1, ModeGrille=2, ModeCercle=3, ModeTableau=4, ModeCadre=5 };
    Mode mode = ModePoint;

    // ── Point unique (mode Point) — UN SEUL X/Y/Z ─────────────
    double  ptX = 0, ptY = 0, ptZ = 0;

    // ── Ligne ──────────────────────────────────────────────────
    // ── Ligne — MÊME convention que le reste de l'appli (Poche etc.) :
    //  point de départ + pas (déplacement selon X) + nombre de points.
    //  PAS un point d'arrivée — c'était différent du reste, corrigé.
    double  ligneP1X = 0, ligneP1Y = 0;
    double  lignePas = 10;
    int     ligneNb  = 2;

    // ── Grille ─────────────────────────────────────────────────
    double  grilleOX = 0, grilleOY = 0, grilleOZ = 0;
    double  grillePasX = 10, grillePasY = 10;
    int     grilleNbX = 2, grilleNbY = 2;
    double  grilleAngle = 0;   // inclinaison (°) — 0 = alignée machine

    // ── Cadre — points sur le POURTOUR d'un rectangle (pas remplis) ─
    double  cadreOX = 0, cadreOY = 0, cadreOZ = 0;
    double  cadrePasX = 10, cadrePasY = 10;
    int     cadreNbX = 3, cadreNbY = 3;   // nb points par côté (coins inclus)
    double  cadreAngle = 0;    // inclinaison (°)

    // ── Cercle (rayon stocké en interne, affiché en Ø dans l'UI) ─
    double  cercleCX = 0, cercleCY = 0, cercleZ = 0;
    double  cercleRayon = 20;
    double  cercleAngDep = 0, cercleAngInc = 90;
    int     cercleNb = 4;

    // ── Tableau manuel (points saisis un par un) ─────────────
    QVector<PointEntry> tableauPts;

    QString label() const {
        switch (mode) {
        case ModePoint:   return QString("Point (%1, %2)").arg(ptX).arg(ptY);
        case ModeLigne:   return QString("Ligne — %1 pts").arg(ligneNb);
        case ModeGrille:  return QString("Grille %1×%2").arg(grilleNbX).arg(grilleNbY);
        case ModeCercle:  return QString("Cercle Ø%1 — %2 pts").arg(cercleRayon*2).arg(cercleNb);
        case ModeTableau: return QString("Tableau — %1 pts").arg(tableauPts.size());
        case ModeCadre:   return QString("Cadre %1×%2").arg(cadreNbX).arg(cadreNbY);
        }
        return {};
    }

    QVector<PointEntry> generate() const {
        QVector<PointEntry> pts;
        // Rotation d'un point (dx,dy) relatif à l'origine, par angle (°)
        auto rotate = [](double dx, double dy, double angDeg) {
            double rad = qDegreesToRadians(angDeg);
            double c = qCos(rad), s = qSin(rad);
            return QPointF(dx*c - dy*s, dx*s + dy*c);
        };
        switch (mode) {
        case ModePoint:
            pts.append({ptX, ptY, ptZ, true});
            break;
        case ModeLigne:
            for (int i = 0; i < qMax(ligneNb, 1); ++i)
                pts.append({ligneP1X + i*lignePas, ligneP1Y, 0, true});
            break;
        case ModeGrille:
            for (int iy = 0; iy < grilleNbY; ++iy) {
                for (int ix = 0; ix < grilleNbX; ++ix) {
                    QPointF r = rotate(ix*grillePasX, iy*grillePasY, grilleAngle);
                    pts.append({grilleOX + r.x(), grilleOY + r.y(), grilleOZ, true});
                }
            }
            break;
        case ModeCercle: {
            double ang = cercleAngDep;
            for (int i = 0; i < cercleNb; ++i) {
                double rad = qDegreesToRadians(ang);
                pts.append({cercleCX + cercleRayon*qCos(rad),
                           cercleCY + cercleRayon*qSin(rad),
                           cercleZ, true});
                ang += cercleAngInc;
            }
            break;
        }
        case ModeTableau:
            for (const auto &p : tableauPts)
                if (qAbs(p.x) > 1e-9 || qAbs(p.y) > 1e-9) pts.append(p);
            break;
        case ModeCadre: {
            // Points sur le POURTOUR uniquement (pas remplis à l'intérieur)
            int nx = qMax(cadreNbX, 2), ny = qMax(cadreNbY, 2);
            for (int iy = 0; iy < ny; ++iy) {
                for (int ix = 0; ix < nx; ++ix) {
                    bool bord = (ix == 0 || ix == nx-1 || iy == 0 || iy == ny-1);
                    if (!bord) continue;  // saute l'intérieur
                    QPointF r = rotate(ix*cadrePasX, iy*cadrePasY, cadreAngle);
                    pts.append({cadreOX + r.x(), cadreOY + r.y(), cadreOZ, true});
                }
            }
            break;
        }
        }
        return pts;
    }

    static QString modeIcon(Mode m) {
        switch (m) {
        case ModePoint:   return ":/unit/point.png";
        case ModeLigne:   return ":/unit/ligne.png";
        case ModeGrille:  return ":/unit/grille.png";
        case ModeCercle:  return ":/unit/cercle.png";
        case ModeTableau: return ":/unit/smartnc_unit_pattern.png";
        case ModeCadre:   return ":/unit/cadre.png";
        }
        return {};
    }
    static QString modeName(Mode m) {
        switch (m) {
        case ModePoint:   return "Point";
        case ModeLigne:   return "Ligne";
        case ModeGrille:  return "Grille";
        case ModeCercle:  return "Cercle";
        case ModeTableau: return "Tableau";
        case ModeCadre:   return "Cadre";
        }
        return {};
    }

    QJsonObject toJson() const {
        QJsonObject o;
        o["mode"] = (int)mode;
        o["ptX"]=ptX; o["ptY"]=ptY; o["ptZ"]=ptZ;
        o["ligneP1X"]=ligneP1X; o["ligneP1Y"]=ligneP1Y;
        o["lignePas"]=lignePas; o["ligneNb"]=ligneNb;
        o["grilleOX"]=grilleOX; o["grilleOY"]=grilleOY; o["grilleOZ"]=grilleOZ;
        o["grillePasX"]=grillePasX; o["grillePasY"]=grillePasY;
        o["grilleNbX"]=grilleNbX; o["grilleNbY"]=grilleNbY;
        o["grilleAngle"]=grilleAngle;
        o["cadreOX"]=cadreOX; o["cadreOY"]=cadreOY; o["cadreOZ"]=cadreOZ;
        o["cadrePasX"]=cadrePasX; o["cadrePasY"]=cadrePasY;
        o["cadreNbX"]=cadreNbX; o["cadreNbY"]=cadreNbY; o["cadreAngle"]=cadreAngle;
        o["cercleCX"]=cercleCX; o["cercleCY"]=cercleCY; o["cercleZ"]=cercleZ;
        o["cercleRayon"]=cercleRayon;
        o["cercleAngDep"]=cercleAngDep; o["cercleAngInc"]=cercleAngInc; o["cercleNb"]=cercleNb;
        QJsonArray arr;
        for (const auto &p : tableauPts) arr << p.toJson();
        o["tableauPts"] = arr;
        return o;
    }
    static PatternEntry fromJson(const QJsonObject &o) {
        PatternEntry e;
        e.mode = (Mode)o["mode"].toInt();
        e.ptX=o["ptX"].toDouble(); e.ptY=o["ptY"].toDouble(); e.ptZ=o["ptZ"].toDouble();
        e.ligneP1X=o["ligneP1X"].toDouble(); e.ligneP1Y=o["ligneP1Y"].toDouble();
        e.lignePas=o["lignePas"].toDouble(10);
        e.ligneNb=o["ligneNb"].toInt(2);
        e.grilleOX=o["grilleOX"].toDouble(); e.grilleOY=o["grilleOY"].toDouble();
        e.grilleOZ=o["grilleOZ"].toDouble();
        e.grillePasX=o["grillePasX"].toDouble(10); e.grillePasY=o["grillePasY"].toDouble(10);
        e.grilleNbX=o["grilleNbX"].toInt(2); e.grilleNbY=o["grilleNbY"].toInt(2);
        e.grilleAngle=o["grilleAngle"].toDouble(0);
        e.cadreOX=o["cadreOX"].toDouble(); e.cadreOY=o["cadreOY"].toDouble();
        e.cadreOZ=o["cadreOZ"].toDouble();
        e.cadrePasX=o["cadrePasX"].toDouble(10); e.cadrePasY=o["cadrePasY"].toDouble(10);
        e.cadreNbX=o["cadreNbX"].toInt(3); e.cadreNbY=o["cadreNbY"].toInt(3);
        e.cadreAngle=o["cadreAngle"].toDouble(0);
        e.cercleCX=o["cercleCX"].toDouble(); e.cercleCY=o["cercleCY"].toDouble();
        e.cercleZ=o["cercleZ"].toDouble(); e.cercleRayon=o["cercleRayon"].toDouble(20);
        e.cercleAngDep=o["cercleAngDep"].toDouble(); e.cercleAngInc=o["cercleAngInc"].toDouble(90);
        e.cercleNb=o["cercleNb"].toInt(4);
        for (const auto &v : o["tableauPts"].toArray())
            e.tableauPts << PointEntry::fromJson(v.toObject());
        return e;
    }
};

// ───────────────────────────────────────────────────────────────
//  PointFile — fichier complet : NOM + liste d'entrées (motifs)
//  empilées. generate() concatène les points de TOUTES les entrées.
// ───────────────────────────────────────────────────────────────
struct PointFile
{
    // Rétrocompatibilité : anciens fichiers .pnt à UN SEUL mode
    using Mode = PatternEntry::Mode;
    static constexpr Mode ModePoint   = PatternEntry::ModePoint;
    static constexpr Mode ModeLigne   = PatternEntry::ModeLigne;
    static constexpr Mode ModeGrille  = PatternEntry::ModeGrille;
    static constexpr Mode ModeCercle  = PatternEntry::ModeCercle;
    static constexpr Mode ModeTableau = PatternEntry::ModeTableau;
    static QString modeIcon(Mode m) { return PatternEntry::modeIcon(m); }
    static QString modeName(Mode m) { return PatternEntry::modeName(m); }

    QString                name;
    QString                filePath;
    QVector<PatternEntry>  entries;   // motifs empilés
    QVector<PointEntry>    points;    // résultat concaténé (après generate())

    void generate() {
        points.clear();
        for (const auto &e : entries)
            points += e.generate();
    }

    int activeCount() const {
        int n = 0;
        for (const auto &p : points) if (p.actif) ++n;
        return n;
    }

    QJsonObject toJson() const {
        QJsonObject o;
        o["name"] = name;
        QJsonArray arr;
        for (const auto &e : entries) arr << e.toJson();
        o["entries"] = arr;
        return o;
    }
    static PointFile fromJson(const QJsonObject &o) {
        PointFile f;
        f.name = o["name"].toString();
        // Rétrocompatibilité : ancien format à un seul mode (pas de
        // tableau "entries") → on le convertit en UNE entrée.
        if (o.contains("entries")) {
            for (const auto &v : o["entries"].toArray())
                f.entries << PatternEntry::fromJson(v.toObject());
        } else if (o.contains("mode")) {
            f.entries << PatternEntry::fromJson(o);
        }
        f.generate();
        return f;
    }

    bool save(const QString &path = {}) {
        QString p = path.isEmpty() ? filePath : path;
        if (p.isEmpty()) return false;
        if (!p.endsWith(".pnt", Qt::CaseInsensitive)) p += ".pnt";
        filePath = p;
        generate();
        QFile f(p);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(QJsonDocument(toJson()).toJson());
        return true;
    }

    bool load(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError) return false;
        *this = fromJson(doc.object());
        filePath = path;
        return true;
    }
};
