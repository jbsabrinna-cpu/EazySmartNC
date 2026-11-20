#pragma once
#include <QObject>
#include <QString>

// Valeurs passées à CNCPipe::Dro::DroCoordinates (conseil d'Andrew :
// des noms plutôt que des chiffres, pour ne jamais envoyer n'importe quoi)
enum DroType { DRO_TRAVAIL = 0, DRO_RESTE_A_PARCOURIR = 2 };
enum AxeCnc  { AXE_X = 0, AXE_Y = 1, AXE_Z = 2 };

class CentroidBridgeWrapper : public QObject
{
    Q_OBJECT
public:
    explicit CentroidBridgeWrapper(QObject *parent = nullptr);
    void init();
    bool isLoaded() const;

    // DRO
    double getWorkDro(int axis);
    double getMachineDro(int axis);
    double getDroDTG(int axis);

    // Origine pièce (WCS)
    // Définit la coordonnée pièce de l'axe à la valeur donnée
    // (utilise CNC_Wcs_SetWorkpieceLocation)
    void setWorkOffset(int axis, double value);

    // MDI — envoie une commande G-code immédiatement
    // (utilise CNC_Job_RunCommand)
    void sendMdiCommand(const QString &cmd);

    // Charger un fichier programme dans CNC12 (brut, sans contrôle)
    bool loadFile(const QString &path);

    // Envoi sûr : fichier présent, aucun programme en cours, pas de renvoi
    // inutile. Renvoie false + message si refusé. Le chargement est confirmé
    // ensuite par programmeCharge() == chemin (voir MainWindow).
    bool envoyerProgramme(const QString &path, QString *erreur = nullptr);
    bool programmeEnCours();              // CNC12 exécute un programme
    QString programmeCharge();            // chemin complet du programme chargé dans CNC12
    static QString cheminCnc(const QString &path);   // forme comparée / envoyée

    // Messages de CNC12 non marqués « traités » : la DLL Centroid les garde
    // (~1000 max, conseil d'Andrew Peters) -> on vide régulièrement.
    void viderMessages();
    int  dernierCodeLoad() const { return m_codeLoad; }

    // VCP — code > 0 = press, code < 0 = release
    void handleVcpButtonRaw(int code);

private:
    bool loaded = false;
    int  m_codeLoad = 0;
    // DRO lus une fois par rafraîchissement (au lieu d'un appel par axe)
    bool lireDro(int type, double *pos, int &n);
    double m_dro[3][8] = {};
    int    m_nDro[3] = {};
    qint64 m_tDro[3] = {-1000, -1000, -1000};
};
