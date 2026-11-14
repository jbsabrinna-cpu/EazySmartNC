#include "CentroidBridgeWrapper.h"
#include <QDebug>

CentroidBridgeWrapper::CentroidBridgeWrapper(QObject *parent) : QObject(parent) {}

bool CentroidBridgeWrapper::isLoaded() const { return loaded; }

// ═══════════════════════════════════════════════════════════════════
//  VERSION AVEC API CENTROID (WITH_CENTROID_API défini dans cmake)
// ═══════════════════════════════════════════════════════════════════
#ifdef WITH_CENTROID_API

#include "../../CentroidWrapper.h"

#define CNC_SUCCESS 27

void CentroidBridgeWrapper::init()
{
    qDebug() << "[BRIDGE] Appel CNC_Connect(1, 5)...";
    int rc = CNC_Connect(1, 5);
    qDebug() << "[BRIDGE] CNC_Connect rc=" << rc;
    if (rc != 0) { qDebug() << "[BRIDGE] ECHEC connexion"; return; }
    int constructed = CNC_IsConstructed();
    qDebug() << "[BRIDGE] CNC_IsConstructed=" << constructed;
    if (!constructed) { qDebug() << "[BRIDGE] ECHEC IsConstructed"; return; }
    loaded = true;
    qDebug() << "[BRIDGE] Connecte a CNC12";
}

double CentroidBridgeWrapper::getWorkDro(int axis)
{
    if (!loaded) return 0.0;
    double positions[8] = {0}; int count = 0;
    int rc = CNC_Dro_GetDroPositions(0, positions, &count);
    if (rc != CNC_SUCCESS || axis >= count) return 0.0;
    return positions[axis];
}

double CentroidBridgeWrapper::getMachineDro(int axis)
{
    if (!loaded) return 0.0;
    double positions[8] = {0}; int count = 0;
    int rc = CNC_State_GetCurrentMachinePosition(positions, &count);
    if (rc != CNC_SUCCESS || axis >= count) return 0.0;
    return positions[axis];
}

double CentroidBridgeWrapper::getDroDTG(int axis)
{
    if (!loaded) return 0.0;
    double positions[8] = {0}; int count = 0;
    int rc = CNC_Dro_GetDroPositions(2, positions, &count);
    if (rc != CNC_SUCCESS || axis >= count) return 0.0;
    return positions[axis];
}

void CentroidBridgeWrapper::setWorkOffset(int axis, double value)
{
    if (!loaded) return;
    int rc = CNC_Wcs_SetWorkpieceLocation(axis, value);
    if (rc != CNC_SUCCESS)
        qDebug() << "[BRIDGE] setWorkOffset axis=" << axis << "val=" << value << "rc=" << rc;
}

void CentroidBridgeWrapper::sendMdiCommand(const QString &cmd)
{
    if (!loaded) return;
    QByteArray ba = cmd.toLatin1();
    int rc = CNC_Job_RunCommand(ba.constData(), 0);
    if (rc != CNC_SUCCESS)
        qDebug() << "[BRIDGE] sendMdiCommand:" << cmd << "rc=" << rc;
}

bool CentroidBridgeWrapper::loadFile(const QString &path)
{
    if (!loaded) return false;
    QByteArray ba = path.toLocal8Bit();
    int rc = CNC_Job_Load(ba.constData());
    if (rc != CNC_SUCCESS)
        qDebug() << "[BRIDGE] loadFile:" << path << "rc=" << rc;
    return rc == CNC_SUCCESS;
}

void CentroidBridgeWrapper::handleVcpButtonRaw(int code)
{
    if (!loaded) return;
    if (code > 0) CNC_Plc_SetSkinEventState(code, 1);
    else          CNC_Plc_SetSkinEventState(-code, 0);
}

// ═══════════════════════════════════════════════════════════════════
//  VERSION STUB — sans DLL Centroid
//  Toutes les fonctions retournent des valeurs neutres.
//  Activer WITH_CENTROID_API dans cmake pour la version intégrée.
// ═══════════════════════════════════════════════════════════════════
#else

void CentroidBridgeWrapper::init()
{
    loaded = false;
    qDebug() << "[BRIDGE STUB] Mode standalone — CentroidAPI désactivée";
}

double CentroidBridgeWrapper::getWorkDro(int)    { return 0.0; }
double CentroidBridgeWrapper::getMachineDro(int) { return 0.0; }
double CentroidBridgeWrapper::getDroDTG(int)     { return 0.0; }

void CentroidBridgeWrapper::setWorkOffset(int axis, double value)
{ qDebug() << "[BRIDGE STUB] setWorkOffset ignoré axis=" << axis << "val=" << value; }

void CentroidBridgeWrapper::sendMdiCommand(const QString &cmd)
{ qDebug() << "[BRIDGE STUB] sendMdiCommand ignoré:" << cmd; }

bool CentroidBridgeWrapper::loadFile(const QString &path)
{ qDebug() << "[BRIDGE STUB] loadFile ignoré:" << path; return false; }

void CentroidBridgeWrapper::handleVcpButtonRaw(int) {}

#endif // WITH_CENTROID_API
