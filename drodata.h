#pragma once
#include <QString>

struct DroData
{
    double x  = 0.0, y  = 0.0, z  = 0.0;   // Work / Local
    double mx = 0.0, my = 0.0, mz = 0.0;   // Machine
    double rx = 0.0, ry = 0.0, rz = 0.0;   // Distance to go

    int spindleOverride = 100;
    int feedOverride    = 100;
    int spindleSpeed    = 0;
    int feedrate        = 0;

    int    tool       = 0;
    int    hOffset    = 0;
    double hOffsetAmt = 0.0;
    int    dOffset    = 0;
    double dOffsetAmt = 0.0;

    QString state    = "";
    QString jobName  = "";
    bool feedHold    = false;
    bool jogActive   = false;
};
