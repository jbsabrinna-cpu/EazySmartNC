#pragma once
#include <QObject>
#include <QString>

class CentroidStateWrapper : public QObject
{
    Q_OBJECT
public:
    explicit CentroidStateWrapper(QObject *parent = nullptr);

    void load(const QString &dllName); // ignoré, garde pour compatibilité
    bool isLoaded() const;

    // Vitesses
    double getFeedrate();
    int    getFeedOverride();
    double getSpindleSpeed();
    double getHighRangeSpindleSpeedMax();
    double getHighRangeSpindleSpeedMin();

    // DRO machine / locale
    double getMachineDro(int axis);
    double getLocalDro(int axis);

    // États machine
    int  getMoveMode();
    int  getPositioningMode();
    bool getJogState();
    int  getFeedHoldState();
    int  getUnitsDefault();

    // G-code
    int     refreshActiveGCodes();
    int     getActiveGCodesCount();
    QString getActiveGCode(int index);
    int     refreshGCodeDisplay();
    int     getGCodeDisplayCount();
    QString getGCodeDisplayLine(int index);
    bool    getCurrentLineInfo(int &line, int &program, int &stack);

    // Job
    QString getJobName();
    QString getFullPathJobName();

    // Outil
    int     getToolNumber();
    QString getToolDescription();
    int     getCurrentHeightOffsetNumber();
    double  getCurrentHeightOffsetAmount();
    double  getHeightOffsetAmountByIndex(int H);
    double  getDiameterOffsetAmount();
    int     getToolHNumber();
    int     getToolDNumber();
    int     getToolSpindleSpeed();
    int     getToolBin();

    // Messages
    QString getLastMessage();
};
