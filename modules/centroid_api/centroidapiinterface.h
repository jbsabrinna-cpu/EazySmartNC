#ifndef CENTROIDAPIINTERFACE_H
#define CENTROIDAPIINTERFACE_H

#include <QObject>
#include <QThread>
#include <QtConcurrent>
#include "../../drodata.h"  // ← chemin relatif correct depuis modules/centroid_api

class CentroidAPIInterface : public QObject
{
    Q_OBJECT

public:
    explicit CentroidAPIInterface(QObject* parent = nullptr);
    void start();
    void stop();

signals:
    void droUpdated(const DroData& data);  // ← DroData maintenant reconnu
    void machineStateChanged(int state);

private:
    void readLoop();
    bool running = false;
};

#endif // CENTROIDAPIINTERFACE_H
