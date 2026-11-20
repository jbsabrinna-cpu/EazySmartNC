#pragma once
#pragma execution_character_set("utf-8")
// ═══════════════════════════════════════════════════════════════
//  ExecutionPage — page Exécution
//
//  Gauche  : position (machine / pièce / reste), outil + F / S,
//            pupitre (départ cycle, arrêt avance, stop).
//  Centre  : programme en BLOCS EazyNC (fait / en cours / à venir,
//            opérations des blocs 3D) ou en G-CODE, avancement + temps.
//  Droite  : vue 3D en direct (matière usinée jusqu'à la ligne en cours).
//  Le jog, la broche et les corrections restent sur la page Mode manuel.
// ═══════════════════════════════════════════════════════════════
#include <QWidget>
#include <QVector>
#include <QElapsedTimer>
#include <QIcon>
#include "gcodeengine.h"
#include "programmepage.h"

class QLabel;
class QPushButton;
class QListWidget;
class QShowEvent;
class QListView;
class QStringListModel;
class QFrame;
class QStackedWidget;
class QProgressBar;
class Sim3DWidget;

class ExecutionPage : public QWidget
{
    Q_OBJECT
public:
    explicit ExecutionPage(QWidget *parent = nullptr);

    // Chargement : depuis la page Programme (blocs connus) ou un .nc du gestionnaire
    void offerProgram(const QVector<BlockData> &blocks, const QVector<GCodeLine> &gcode, const QString &name);
    void loadRawFile(const QString &path);

    // Mises à jour (minuterie de la fenêtre principale)
    void updateDro(double wx, double wy, double wz, double mx, double my, double mz,
                   double dtgx, double dtgy, double dtgz);
    void updateSpindle(int rpm, int spindleOvr);
    void updateFeed(double feed_mmmin, int feedOvr);
    void updateTool(int tNum, int hNum, const QString &nom = QString());
    void updateMessages(const QString &msg);
    void setLigneCourante(int ligne, bool enCours);      // ligne du .nc exécutée (1 = première)

    QString currentNcPath() const { return m_ncPath; }
    void    setNcPath(const QString &p) { m_ncPath = p; }
    void    setInfoCnc12(const QString &txt);           // « chargé dans CNC12 ✓ », erreur…

    // Touches du bas
    void basculerBlocsGcode();
    void basculerAgrandir3D();
    void montrerMessages();
    void allerALigne(int ligne);                         // fait défiler le G-code
    void chercherLigne(int ligne);                       // passe en vue G-code sur cette ligne

signals:
    void requestOpenFile();
    void cycleStartRequested();
    void feedHoldRequested();
    void cycleStopRequested();
    void resetRequested();

private:
    void construire();
    void majPositions();
    void chargerLignes(const QVector<GCodeLine> &lignes);   // G-code + 3D + découpage en blocs
    void majBlocs();
    void choisirVue(int v);                               // 0 blocs, 1 G-code

    // Position
    QLabel *m_ax[3] = {nullptr, nullptr, nullptr};
    QLabel *m_titrePos = nullptr;
    QPushButton *m_seg[3] = {nullptr, nullptr, nullptr};
    int m_modePos = 1;                                    // 0 machine, 1 pièce, 2 reste
    double m_val[3][3] = {};                              // [mode][axe]
    // Outil
    QLabel *m_lblOutil = nullptr, *m_lblF = nullptr, *m_lblS = nullptr, *m_lblFo = nullptr;
    QProgressBar *m_barF = nullptr;
    int m_feedOvr = 100;
    // Programme
    QPushButton *m_bBlocs = nullptr, *m_bGcode = nullptr;
    QLabel *m_lblProg = nullptr;
    QStackedWidget *m_pile = nullptr;
    QListWidget *m_listeBlocs = nullptr;
    QListView *m_listeGcode = nullptr;
    QStringListModel *m_modeleGcode = nullptr;
    struct Ligne { QFrame *f = nullptr; QLabel *st = nullptr; QProgressBar *pb = nullptr; int etat = -1; };
    QVector<Ligne> m_rangs;
    QVector<float> m_tCumul;                              // temps estimé cumulé (s) à chaque ligne
    QProgressBar *m_barProg = nullptr;
    QLabel *m_lblPct = nullptr, *m_lblTemps = nullptr;
    // Vue 3D
    QWidget *m_colG = nullptr, *m_colC = nullptr;
    Sim3DWidget *m_sim = nullptr;
    QPushButton *m_chMat = nullptr, *m_chOutil = nullptr, *m_chParc = nullptr;
    QLabel *m_lblMsg = nullptr, *m_lblPrep = nullptr;
    bool m_simCharge = false;                             // vue 3D construite pour ce programme
    void construire3D();
protected:
    void showEvent(QShowEvent *e) override;
private:

    struct Section { QString nom, detail; QIcon icone; int debut = 0, fin = 0; bool ebauche = false, op = false; };
    QVector<Section> m_sections;                          // blocs (et opérations 3D) avec leurs lignes
    QVector<BlockData> m_blocks;
    QVector<GCodeLine> m_lignes;
    QString m_ncPath, m_nomProg, m_infoCnc;
    QStringList m_messages;
    int  m_ligne = 0;
    bool m_enCours = false;
    QElapsedTimer m_chrono, m_dernier3D;
    qint64 m_tempsAvant = 0;                              // temps cumulé des périodes précédentes
};
