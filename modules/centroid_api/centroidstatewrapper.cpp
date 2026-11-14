#include "centroidstatewrapper.h"
#include <QDebug>
#include <QStringList>

CentroidStateWrapper::CentroidStateWrapper(QObject *parent) : QObject(parent) {}

// ═══════════════════════════════════════════════════════════════════
//  VERSION AVEC API CENTROID
// ═══════════════════════════════════════════════════════════════════
#ifdef WITH_CENTROID_API

#include "../../CentroidWrapper.h"

#define CNC_SUCCESS 27

void CentroidStateWrapper::load(const QString &dllName)
{
    Q_UNUSED(dllName)
    qDebug() << "CentroidStateWrapper: utilise CentroidWrapper.dll (déjà connectée)";
}

bool CentroidStateWrapper::isLoaded() const
{
    return CNC_IsConstructed() != 0;
}

// ── Vitesses ──────────────────────────────────────────────────────
double CentroidStateWrapper::getFeedrate()
{ double v = 0.0; CNC_State_GetFeedrate(&v); return v; }

int CentroidStateWrapper::getFeedOverride()
{ int v = 0; CNC_State_GetFeedrateOverride(&v); return v; }

double CentroidStateWrapper::getSpindleSpeed()
{ double v = 0.0; CNC_State_GetSpindleSpeed(&v); return v; }

double CentroidStateWrapper::getHighRangeSpindleSpeedMax()
{ double v = 0.0; CNC_State_GetHighRangeSpindleSpeed(0, &v); return v; }

double CentroidStateWrapper::getHighRangeSpindleSpeedMin()
{ double v = 0.0; CNC_State_GetHighRangeSpindleSpeed(1, &v); return v; }

// ── DRO ───────────────────────────────────────────────────────────
double CentroidStateWrapper::getMachineDro(int axis)
{
    double pos[8] = {0}; int count = 0;
    int rc = CNC_State_GetCurrentMachinePosition(pos, &count);
    if (rc != CNC_SUCCESS || axis >= count) return 0.0;
    return pos[axis];
}

double CentroidStateWrapper::getLocalDro(int axis)
{
    double pos[8] = {0}; int count = 0;
    int rc = CNC_State_GetCurrentLocalPosition(pos, &count);
    if (rc != CNC_SUCCESS || axis >= count) return 0.0;
    return pos[axis];
}

// ── États machine ─────────────────────────────────────────────────
int  CentroidStateWrapper::getMoveMode()
{ int v = -1; CNC_State_GetMoveMode(&v); return v; }

int  CentroidStateWrapper::getPositioningMode()
{ int v = -1; CNC_State_GetPositioningMode(&v); return v; }

bool CentroidStateWrapper::getJogState()
{ int v = 0; CNC_State_GetJogState(&v); return v != 0; }

int  CentroidStateWrapper::getFeedHoldState()
{ int v = -1; CNC_State_GetFeedHoldState(&v); return v; }

int  CentroidStateWrapper::getUnitsDefault()
{ int v = 0; CNC_State_GetUnitsOfMeasureDefault(&v); return v; }

// ── G-code ────────────────────────────────────────────────────────
static QStringList s_activeGCodes;
static QStringList s_gcodeDisplay;

int CentroidStateWrapper::refreshActiveGCodes()
{
    char buf[1024] = {0};
    int rc = CNC_State_GetActiveGCodes(buf, sizeof(buf));
    s_activeGCodes.clear();
    if (rc == CNC_SUCCESS)
        s_activeGCodes = QString::fromLatin1(buf).split("_", Qt::SkipEmptyParts);
    return s_activeGCodes.size();
}

int     CentroidStateWrapper::getActiveGCodesCount()  { return s_activeGCodes.size(); }
QString CentroidStateWrapper::getActiveGCode(int i)
{ return (i >= 0 && i < s_activeGCodes.size()) ? s_activeGCodes[i] : ""; }

int CentroidStateWrapper::refreshGCodeDisplay()
{
    char buf[2048] = {0};
    int rc = CNC_State_GetGCodeDisplay(buf, sizeof(buf));
    s_gcodeDisplay.clear();
    if (rc == CNC_SUCCESS)
        s_gcodeDisplay = QString::fromLatin1(buf).split("_", Qt::SkipEmptyParts);
    return s_gcodeDisplay.size();
}

int     CentroidStateWrapper::getGCodeDisplayCount()      { return s_gcodeDisplay.size(); }
QString CentroidStateWrapper::getGCodeDisplayLine(int i)
{ return (i >= 0 && i < s_gcodeDisplay.size()) ? s_gcodeDisplay[i] : ""; }

bool CentroidStateWrapper::getCurrentLineInfo(int &line, int &program, int &stack)
{
    int rc = CNC_State_GetCurrentLineInfo(&line, &program, &stack);
    return rc == CNC_SUCCESS;
}

// ── Job ───────────────────────────────────────────────────────────
QString CentroidStateWrapper::getJobName()
{ char buf[512] = {0}; CNC_State_GetJobNameCurrent(buf, sizeof(buf)); return QString::fromLatin1(buf); }

QString CentroidStateWrapper::getFullPathJobName()
{ char buf[1024] = {0}; CNC_State_GetFullPathJobNameCurrent(buf, sizeof(buf)); return QString::fromLatin1(buf); }

// ── Outil ─────────────────────────────────────────────────────────
int CentroidStateWrapper::getToolNumber()
{ int v = -1; CNC_Tool_GetToolNumber(&v); return v; }

QString CentroidStateWrapper::getToolDescription()
{ char buf[256] = {0}; CNC_Tool_GetToolDescription(buf, sizeof(buf)); return QString::fromLatin1(buf); }

int    CentroidStateWrapper::getCurrentHeightOffsetNumber()
{ int v = -1; CNC_Tool_GetCurrentHeightOffsetNumber(&v); return v; }

double CentroidStateWrapper::getCurrentHeightOffsetAmount()
{ double v = 0.0; CNC_Tool_GetHeightOffsetAmount(&v); return v; }

double CentroidStateWrapper::getHeightOffsetAmountByIndex(int H)
{ double v = 0.0; CNC_Tool_GetHeightOffsetAmountByH(H, &v); return v; }

double CentroidStateWrapper::getDiameterOffsetAmount()
{ double v = 0.0; CNC_Tool_GetDiameterOffsetAmount(&v); return v; }

int CentroidStateWrapper::getToolHNumber()
{ int v = -1; CNC_Tool_GetToolHNumber(&v); return v; }

int CentroidStateWrapper::getToolDNumber()
{ int v = -1; CNC_Tool_GetToolDNumber(&v); return v; }

int CentroidStateWrapper::getToolSpindleSpeed()
{ int v = 0; CNC_Tool_GetToolSpindleSpeed(&v); return v; }

int CentroidStateWrapper::getToolBin()
{ int v = -1; CNC_Tool_GetToolBin(&v); return v; }

// ── Messages ──────────────────────────────────────────────────────
QString CentroidStateWrapper::getLastMessage()
{
    char buf[2048] = {0};
    int rc = CNC_MessageWindow_GetMessages(buf, sizeof(buf));
    if (rc != CNC_SUCCESS || buf[0] == '\0') return QString();
    QString all = QString::fromLatin1(buf);
    QStringList parts = all.split("`", Qt::SkipEmptyParts);
    return parts.isEmpty() ? QString() : parts.last();
}

// ═══════════════════════════════════════════════════════════════════
//  VERSION STUB — sans DLL Centroid
//  Toutes les fonctions retournent des valeurs neutres.
//  Activer WITH_CENTROID_API dans cmake pour la version intégrée.
// ═══════════════════════════════════════════════════════════════════
#else

void    CentroidStateWrapper::load(const QString &) {}
bool    CentroidStateWrapper::isLoaded() const      { return false; }

double  CentroidStateWrapper::getFeedrate()                     { return 0.0; }
int     CentroidStateWrapper::getFeedOverride()                 { return 100; }
double  CentroidStateWrapper::getSpindleSpeed()                 { return 0.0; }
double  CentroidStateWrapper::getHighRangeSpindleSpeedMax()     { return 0.0; }
double  CentroidStateWrapper::getHighRangeSpindleSpeedMin()     { return 0.0; }

double  CentroidStateWrapper::getMachineDro(int)    { return 0.0; }
double  CentroidStateWrapper::getLocalDro(int)      { return 0.0; }

int     CentroidStateWrapper::getMoveMode()         { return 0; }
int     CentroidStateWrapper::getPositioningMode()  { return 0; }
bool    CentroidStateWrapper::getJogState()         { return false; }
int     CentroidStateWrapper::getFeedHoldState()    { return 0; }
int     CentroidStateWrapper::getUnitsDefault()     { return 1; } // mm

int     CentroidStateWrapper::refreshActiveGCodes()     { return 0; }
int     CentroidStateWrapper::getActiveGCodesCount()    { return 0; }
QString CentroidStateWrapper::getActiveGCode(int)       { return {}; }
int     CentroidStateWrapper::refreshGCodeDisplay()     { return 0; }
int     CentroidStateWrapper::getGCodeDisplayCount()    { return 0; }
QString CentroidStateWrapper::getGCodeDisplayLine(int)  { return {}; }
bool    CentroidStateWrapper::getCurrentLineInfo(int &line, int &program, int &stack)
{ line = 0; program = 0; stack = 0; return false; }

QString CentroidStateWrapper::getJobName()          { return {}; }
QString CentroidStateWrapper::getFullPathJobName()  { return {}; }

int     CentroidStateWrapper::getToolNumber()                   { return 0; }
QString CentroidStateWrapper::getToolDescription()              { return {}; }
int     CentroidStateWrapper::getCurrentHeightOffsetNumber()    { return 0; }
double  CentroidStateWrapper::getCurrentHeightOffsetAmount()    { return 0.0; }
double  CentroidStateWrapper::getHeightOffsetAmountByIndex(int) { return 0.0; }
double  CentroidStateWrapper::getDiameterOffsetAmount()         { return 0.0; }
int     CentroidStateWrapper::getToolHNumber()                  { return 0; }
int     CentroidStateWrapper::getToolDNumber()                  { return 0; }
int     CentroidStateWrapper::getToolSpindleSpeed()             { return 0; }
int     CentroidStateWrapper::getToolBin()                      { return 0; }

QString CentroidStateWrapper::getLastMessage()      { return {}; }

#endif // WITH_CENTROID_API
