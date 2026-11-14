#include "centroidapiinterface.h"
#include <QDebug>
#include <QFuture>

CentroidAPIInterface::CentroidAPIInterface(QObject* parent)
    : QObject(parent)
{
}

void CentroidAPIInterface::start()
{
#ifdef WITH_CENTROID_API
    if (running)
        return;

    running = true;

    QFuture<void> f = QtConcurrent::run([this]() {
        readLoop();
    });
    Q_UNUSED(f)
#else
    qDebug() << "[API STUB] start() ignoré — mode standalone";
#endif
}

void CentroidAPIInterface::stop()
{
    running = false;
}

void CentroidAPIInterface::readLoop()
{
#ifdef WITH_CENTROID_API
    // TODO version intégrée :
    // - Abonner au gestionnaire d'événements CentroidAPI
    // - Marquer chaque MessageReceivedEventArgs.Handled = true
    //   pour éviter l'accumulation dans la pile LIFO (~1000 msg max)
    // - Remplacer le polling ci-dessous par les vrais appels API

    while (running)
    {
        DroData data;
        data.x = 0.0;
        data.y = 0.0;
        data.z = 0.0;

        emit droUpdated(data);
        emit machineStateChanged(1);

        QThread::msleep(50);
    }
#else
    // Stub : boucle inactive
    Q_UNUSED(this)
#endif
}
