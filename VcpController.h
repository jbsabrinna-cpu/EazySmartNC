#pragma once
#include <QObject>
#include <QElapsedTimer>
#include "centroidbridgewrapper.h"

class VcpController : public QObject
{
    Q_OBJECT

public:
    explicit VcpController(QObject *parent = nullptr);

    void setApi(CentroidBridgeWrapper *a);

    // Jog axes
    void jogXPlus(bool pressed);
    void jogXMinus(bool pressed);
    void jogYPlus(bool pressed);
    void jogYMinus(bool pressed);
    void jogZPlus(bool pressed);
    void jogZMinus(bool pressed);

    // Jog modes
    void toggleIncCont();
    void setX1();
    void setX10();
    void setX100();
    void toggleRapid();
    void toggleMPG();

    // Feed override
    void feedPlus(bool pressed);
    void feedMinus(bool pressed);
    void feedPreset100();
    void feedPreset75();
    void feedPreset50();
    void feedPreset25();
    void feedHold();

    // Spindle override
    void spindlePlus(bool pressed);
    void spindleMinus(bool pressed);
    void spindlePreset100();

    // Spindle
    void spindleCW();
    void spindleCCW();
    void spindleStop();
    void spindleStart();

    void toggleManAuto();

    // Cycle
    void cycleStart();
    void cycleStop();
    void reset();

private:
    void momentary(int code);
    void handleJog(bool pressed, int eventCode);

    CentroidBridgeWrapper *api = nullptr;

    // Jog logic
    QElapsedTimer jogTimer;
    bool incMode = false;
    int incStep = 1; // X1 par défaut
};
