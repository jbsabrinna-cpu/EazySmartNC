#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  usinage3dgenerator.h — bloc « Usinage 3D » (STEP / IGES)
//
//  ÉBAUCHE  : couches Z (passe d'ébauche + fonds plats), à chaque
//             couche des passes qui tournent autour de la pièce, du
//             bord du brut vers la pièce (entrée dans le vide,
//             engagement = Ø × (1 − recouvrement)).
//             Garde-fou : là où l'outil ne passe pas, la matière
//             reste ; la finition le sait (passes étagées).
//  FINITION : balayage aller-retour (pas latéral) avec l'outil de
//             finition (boule ou plate), en descente « drop-cutter »
//             sur la pièce. Passes étagées de la profondeur de passe
//             de finition pour reprendre l'escalier d'ébauche.
//
//  Calcul dans le repère du STEP, sortie G-code dans le repère
//  pièce (origine choisie dans le bloc).
// ═══════════════════════════════════════════════════════════════
#include "blockgenerator.h"
#include "../decoupe3d.h"
#include <QJsonObject>

struct CadDocument;
#include <QJsonArray>
#include <QVector3D>

// Sens des axes machine par rapport au modèle (quarts de tour) :
//  ligne k = direction (dans le repère du STEP) de l'axe machine k (X, Y, Z).
//  machine = M · modèle ; modèle = Mᵀ · machine
struct Orientation3D
{
    int m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    static Orientation3D depuis(const QJsonArray &a) {
        Orientation3D o;
        if (a.size() == 9) for (int i = 0; i < 9; ++i) o.m[i / 3][i % 3] = a[i].toInt();
        return o;
    }
    QJsonArray json() const { QJsonArray a; for (int i = 0; i < 9; ++i) a.append(m[i / 3][i % 3]); return a; }
    bool identite() const { for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) if (m[i][j] != (i == j)) return false; return true; }
    QVector3D versMachine(const QVector3D &p) const {
        return QVector3D(m[0][0]*p.x() + m[0][1]*p.y() + m[0][2]*p.z(),
                         m[1][0]*p.x() + m[1][1]*p.y() + m[1][2]*p.z(),
                         m[2][0]*p.x() + m[2][1]*p.y() + m[2][2]*p.z()); }
    QVector3D versModele(const QVector3D &p) const {
        return QVector3D(m[0][0]*p.x() + m[1][0]*p.y() + m[2][0]*p.z(),
                         m[0][1]*p.x() + m[1][1]*p.y() + m[2][1]*p.z(),
                         m[0][2]*p.x() + m[1][2]*p.y() + m[2][2]*p.z()); }
    QVector3D axe(int k) const { return QVector3D(m[k][0], m[k][1], m[k][2]); }   // dans le repère du STEP
    // Tourne tout le document (maillage, normales, arêtes, centres) + encombrement
    void appliquer(CadDocument &doc) const;
};

// Plan d'ébauche partagé avec la page du bloc (aperçu des couches)
struct Usinage3DPlan
{
    QVector<Couche3D> couches;     // ombre + zone à vider (repère STEP)
    Contours2D limite;             // brut ou zone choisie
    double origine[3] = {0, 0, 0};
    double brutMin[3] = {0, 0, 0};
    double brutMax[3] = {0, 0, 0};
    double zFin = 0.0;             // Z bas − découpe (repère STEP)
    QString erreur;
};

class Usinage3DGenerator : public BlockGenerator
{
public:
    Usinage3DGenerator(const QJsonObject &d, double approche, double sautBride);
    QVector<GCodeLine> generate(int blkId) override;

    // Couches d'ébauche (niveaux + ombre + zone à vider)
    //  rayonEb : rayon de l'outil d'ébauche (pour agrandir la zone choisie)
    static Usinage3DPlan plan(const QJsonObject &d, const CadDocument &doc, double rayonEb);

    // Origine pièce + brut (repère du STEP) à partir des réglages
    //  origPos/origRef/origZ/brutMode/brutSx.. et de l'encombrement de la pièce.
    //  Partagé avec l'onglet « Modèle 3D » du BLK FORM.
    static void origineEtBrut(const QJsonObject &d, const double pMin[3], const double pMax[3],
                              double origine[3], double brutMin[3], double brutMax[3]);

private:
    QVector<GCodeLine> calculer(int blkId);    // calcul complet (sans la mémoire)
    QJsonObject m_d;
    double m_approche = 2.0;
    double m_sautBride = 50.0;
};
