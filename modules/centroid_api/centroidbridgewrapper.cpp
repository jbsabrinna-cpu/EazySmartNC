#include "CentroidBridgeWrapper.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>

CentroidBridgeWrapper::CentroidBridgeWrapper(QObject *parent) : QObject(parent) {}

bool CentroidBridgeWrapper::isLoaded() const { return loaded; }

// ═══════════════════════════════════════════════════════════════════
//  VERSION AVEC API CENTROID (WITH_CENTROID_API défini dans cmake)
// ═══════════════════════════════════════════════════════════════════
#ifdef WITH_CENTROID_API

#include "../../CentroidWrapper.h"

// ReturnCode::SUCCESS de l'API Centroid : 30 depuis CNC12 v5.4x (Centroid a
// ajouté ERROR_REPORT_PATH_* avant SUCCESS, qui valait 27 dans les anciennes
// versions). EazyNC doit utiliser la CentroidAPI.dll livrée avec CNC12.
#define CNC_SUCCESS 30

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

bool CentroidBridgeWrapper::lireDro(int type, double *pos, int &n)
{
    const int k = type == DRO_RESTE_A_PARCOURIR ? 2 : 0;
    const qint64 t = QDateTime::currentMSecsSinceEpoch();
    if (t - m_tDro[k] > 40) {                  // un appel API par rafraîchissement
        int count = 0;
        if (CNC_Dro_GetDroPositions(type, m_dro[k], &count) != CNC_SUCCESS) { m_nDro[k] = 0; m_tDro[k] = t; return false; }
        m_nDro[k] = qBound(0, count, 8);
        m_tDro[k] = t;
    }
    n = m_nDro[k];
    for (int i = 0; i < n; ++i) pos[i] = m_dro[k][i];
    return n > 0;
}

double CentroidBridgeWrapper::getWorkDro(int axis)
{
    if (!loaded || axis < 0 || axis >= 8) return 0.0;
    double positions[8] = {0}; int count = 0;
    if (!lireDro(DRO_TRAVAIL, positions, count) || axis >= count) return 0.0;
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
    if (!loaded || axis < 0 || axis >= 8) return 0.0;
    double positions[8] = {0}; int count = 0;
    if (!lireDro(DRO_RESTE_A_PARCOURIR, positions, count) || axis >= count) return 0.0;
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

QString CentroidBridgeWrapper::cheminCnc(const QString &path)
{
    return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}

bool CentroidBridgeWrapper::programmeEnCours()
{
    if (!loaded) return false;
    int r = 0;
    if (CNC_State_IsJobRunning(&r) != CNC_SUCCESS) return false;
    return r != 0;
}

QString CentroidBridgeWrapper::programmeCharge()
{
    if (!loaded) return QString();
    char buf[1024] = {0};
    if (CNC_State_GetFullPathJobNameCurrent(buf, int(sizeof buf)) != CNC_SUCCESS) return QString();
    const QByteArray ba(buf);
    QString s = QString::fromUtf8(ba);
    if (s.contains(QChar::ReplacementCharacter)) s = QString::fromLocal8Bit(ba);
    return s.trimmed();
}

bool CentroidBridgeWrapper::envoyerProgramme(const QString &path, QString *erreur)
{
    auto refus = [&](const QString &m) { if (erreur) *erreur = m; qDebug() << "[BRIDGE] envoi refusé :" << m; return false; };
    if (!loaded) return refus(QString::fromUtf8("CNC12 n'est pas connecté"));
    const QFileInfo fi(path);
    if (!fi.exists()) return refus(QString::fromUtf8("Fichier introuvable : %1").arg(cheminCnc(path)));
    if (programmeEnCours()) return refus(QString::fromUtf8("Un programme tourne dans CNC12 : envoi impossible"));
    const QString cible = cheminCnc(path);
    const QByteArray ba = cible.toLocal8Bit();
    qDebug() << "[BRIDGE] CNC_Job_Load" << cible << fi.size() << "octets";
    // Code retour : 30 observé alors que CNC12 charge bien le programme ->
    // on ne s'y fie pas ; la vraie preuve est programmeCharge() == chemin.
    m_codeLoad = CNC_Job_Load(ba.constData());
    qDebug() << "[BRIDGE] CNC_Job_Load rc=" << m_codeLoad;
    return true;
}

void CentroidBridgeWrapper::viderMessages()
{
    if (!loaded) return;
    CNC_ClearUnhandledMessages();
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
QString CentroidBridgeWrapper::cheminCnc(const QString &path) { return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()); }
bool CentroidBridgeWrapper::programmeEnCours() { return false; }
QString CentroidBridgeWrapper::programmeCharge() { return QString(); }
bool CentroidBridgeWrapper::envoyerProgramme(const QString &, QString *erreur)
{ if (erreur) *erreur = QString::fromUtf8("API Centroid absente (mode autonome)"); return false; }
void CentroidBridgeWrapper::viderMessages() {}
bool CentroidBridgeWrapper::lireDro(int, double *, int &n) { n = 0; return false; }

#endif // WITH_CENTROID_API
