#pragma once
#include <QObject>
#include <QString>

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

    // Charger un fichier programme dans CNC12
    bool loadFile(const QString &path);

    // VCP — code > 0 = press, code < 0 = release
    void handleVcpButtonRaw(int code);

private:
    bool loaded = false;
};
