// CentroidWrapper.cpp
// Pont CLR entre CentroidAPI (.NET) et code natif C++
// Genere automatiquement - EazyNC Project

#include "pch.h"
#include "CentroidWrapper.h"
#include <msclr/marshal_cppstd.h>
#include <string>

using namespace System;
using namespace System::Runtime::InteropServices;
using namespace CentroidAPI;
using namespace msclr::interop;

// Instance globale du pipe - utilise gcroot pour eviter l'erreur de variable statique managee
#include <gcroot.h>
static gcroot<CNCPipe^> g_pipe;
static bool g_pipeInitialized = false;

// Callback pour les evenements temps reel
static CNC_EventCallback g_eventCallback = nullptr;

// ============================================================
// HELPER - Convertir String^ en char*
// ============================================================
static void StringToChar(String^ src, char* dest, int bufferSize)
{
    if (src == nullptr || dest == nullptr) return;
    std::string str = marshal_as<std::string>(src);
    strncpy_s(dest, bufferSize, str.c_str(), _TRUNCATE);
}

// ============================================================
// CONNEXION
// ============================================================
extern "C" int CNC_Connect(int useVcpPipe, int timeout)
{
    try {
        System::Diagnostics::Debug::WriteLine("[CNC] CNC_Connect called, timeout=" + timeout.ToString());
        g_pipe = gcnew CNCPipe(true, timeout);
        bool constructed = g_pipe->IsConstructed();
        System::Diagnostics::Debug::WriteLine("[CNC] IsConstructed=" + constructed.ToString());
        if (!constructed) {
            g_pipeInitialized = false;
            System::Diagnostics::Debug::WriteLine("[CNC] FAIL: pipe not constructed");
            return -1;
        }
        g_pipeInitialized = true;
        System::Diagnostics::Debug::WriteLine("[CNC] SUCCESS: pipe ready");
        return 0;
    }
    catch (Exception^ ex) {
        System::Diagnostics::Debug::WriteLine("[CNC] EXCEPTION in Connect: " + ex->Message);
        g_pipeInitialized = false;
        return -1;
    }
}

extern "C" void CNC_Disconnect()
{
    try {
        if (g_pipeInitialized) {
            g_pipe->StopListening();
            g_pipeInitialized = false;
        }
    }
    catch (...) {}
}

extern "C" int CNC_IsConstructed()
{
    if (!g_pipeInitialized) return 0;
    return g_pipe->IsConstructed() ? 1 : 0;
}

// ============================================================
// SYNCHRONISATION TEMPS REEL
// ============================================================

// Classe pour recevoir les evenements de CNC12
ref class EventListener
{
public:
    void OnEvent(CNCPipe::InboundComm::CommPacket packet)
    {
        if (g_eventCallback == nullptr) return;
        try {
            double positions[8] = { 0 };
            if (packet.Positions != nullptr) {
                for (int i = 0; i < 8 && i < packet.Positions->Length; i++)
                    positions[i] = packet.Positions[i];
            }
            std::string msg = marshal_as<std::string>(packet.Message != nullptr ? packet.Message : "");
            g_eventCallback(
                (int)packet.CommunicationType,
                (int)packet.Flag,
                positions,
                packet.LineNumber,
                packet.StackLevel,
                msg.c_str(),
                packet.Ticks
            );
        }
        catch (...) {}
    }
};

static gcroot<EventListener^> g_listener;

extern "C" void CNC_SetEventCallback(CNC_EventCallback callback)
{
    g_eventCallback = callback;
}

extern "C" void CNC_StartListening()
{
    if (!g_pipeInitialized) return;
    try {
        g_listener = gcnew EventListener();
        g_pipe->StartListening();
    }
    catch (...) {}
}

extern "C" void CNC_StopListening()
{
    if (!g_pipeInitialized) return;
    try {
        g_pipe->StopListening();
    }
    catch (...) {}
}

extern "C" void CNC_ClearUnhandledMessages()
{
    if (!g_pipeInitialized) return;
    try { g_pipe->ClearUnhandledMessages(); }
    catch (...) {}
}

// ============================================================
// INBOUND COMM
// ============================================================
extern "C" int CNC_InboundComm_ChangeDroType(int dro_coordinates)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->inbound_communications->ChangeDroType((CNCPipe::Dro::DroCoordinates)dro_coordinates); }
    catch (...) { return -1; }
}

extern "C" int CNC_InboundComm_ChangeJobInfoType(int type)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->inbound_communications->ChangeJobInfoType((CNCPipe::InboundComm::JobInfoType)type); }
    catch (...) { return -1; }
}

// ============================================================
// AXIS
// ============================================================
extern "C" int CNC_Axis_GetAccelTime(int axis, double* accel_time)
{
    if (!g_pipeInitialized || accel_time == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->axis->GetAccelTime((CNCPipe::Axes)axis, val);
        *accel_time = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetAccelTime(int axis, double time)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetAccelTime((CNCPipe::Axes)axis, time); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetDeadstartVelocity(int axis, double* velocity)
{
    if (!g_pipeInitialized || velocity == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->axis->GetDeadstartVelocity((CNCPipe::Axes)axis, val);
        *velocity = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetDeadstartVelocity(int axis, double velocity)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetDeadstartVelocity((CNCPipe::Axes)axis, velocity); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetDeltaVMax(int axis, double* delta_v_max)
{
    if (!g_pipeInitialized || delta_v_max == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->axis->GetDeltaVMax((CNCPipe::Axes)axis, val);
        *delta_v_max = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetDeltaVMax(int axis, double delta_v_max)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetDeltaVMax((CNCPipe::Axes)axis, delta_v_max); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetRate(int axis, int jog_rate, double* rate)
{
    if (!g_pipeInitialized || rate == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->axis->GetRate((CNCPipe::Axes)axis, (CNCPipe::Axis::Rate)jog_rate, val);
        *rate = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetRate(int axis, int rate_type, double rate)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetRate((CNCPipe::Axes)axis, (CNCPipe::Axis::Rate)rate_type, rate); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetLabel(int axis, char* label)
{
    if (!g_pipeInitialized || label == nullptr) return -1;
    try {
        wchar_t val; int rc = (int)g_pipe->axis->GetLabel((CNCPipe::Axes)axis, val);
        *label = (char)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetLabel(int axis, char label)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetLabel((CNCPipe::Axes)axis, label); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetLashComp(int axis, double* lash_comp)
{
    if (!g_pipeInitialized || lash_comp == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->axis->GetLashComp((CNCPipe::Axes)axis, val);
        *lash_comp = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetLashComp(int axis, double value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetLashComp((CNCPipe::Axes)axis, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetScrewPitch(int axis, double* screw_pitch)
{
    if (!g_pipeInitialized || screw_pitch == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->axis->GetScrewPitch((CNCPipe::Axes)axis, val);
        *screw_pitch = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetScrewPitch(int axis, double value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetScrewPitch((CNCPipe::Axes)axis, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetLimit(int axis, int direction, double* limit)
{
    if (!g_pipeInitialized || limit == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->axis->GetLimit((CNCPipe::Axes)axis, (CNCPipe::Axis::Direction)direction, val);
        *limit = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetLimit(int axis, int direction, double value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetLimit((CNCPipe::Axes)axis, (CNCPipe::Axis::Direction)direction, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetHomeLimit(int axis, int direction, double* home_limit)
{
    if (!g_pipeInitialized || home_limit == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->axis->GetHomeLimit((CNCPipe::Axes)axis, (CNCPipe::Axis::Direction)direction, val);
        *home_limit = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetHomeLimit(int axis, int direction, double value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetHomeLimit((CNCPipe::Axes)axis, (CNCPipe::Axis::Direction)direction, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetAxisReversal(int axis, int* is_reversed)
{
    if (!g_pipeInitialized || is_reversed == nullptr) return -1;
    try {
        bool val; int rc = (int)g_pipe->axis->GetAxisReversal((CNCPipe::Axes)axis, val);
        *is_reversed = val ? 1 : 0; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetAxisReversal(int axis, int is_reversed)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetAxisReversal((CNCPipe::Axes)axis, is_reversed != 0); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetCountsPerTurn(int axis, double* counts)
{
    if (!g_pipeInitialized || counts == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->axis->GetCountsPerTurn((CNCPipe::Axes)axis, val);
        *counts = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetCountsPerTurn(int axis, double value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetCountsPerTurn((CNCPipe::Axes)axis, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetTravelLimit(int axis, int direction, double* travel_limit)
{
    if (!g_pipeInitialized || travel_limit == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->axis->GetTravelLimit((CNCPipe::Axes)axis, (CNCPipe::Axis::Direction)direction, val);
        *travel_limit = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetTravelLimit(int axis, int direction, double value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetTravelLimit((CNCPipe::Axes)axis, (CNCPipe::Axis::Direction)direction, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetScalesInput(int axis, int* encoder_number)
{
    if (!g_pipeInitialized || encoder_number == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->axis->GetScalesInput((CNCPipe::Axes)axis, val);
        *encoder_number = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetScalesInput(int axis, int value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetScalesInput((CNCPipe::Axes)axis, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetScalesEnable(int axis, int* enabled)
{
    if (!g_pipeInitialized || enabled == nullptr) return -1;
    try {
        bool val; int rc = (int)g_pipe->axis->GetScalesEnable((CNCPipe::Axes)axis, val);
        *enabled = val ? 1 : 0; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetScalesEnable(int axis, int enabled)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetScalesEnable((CNCPipe::Axes)axis, enabled != 0); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetScalesCounts(int axis, double* counts)
{
    if (!g_pipeInitialized || counts == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->axis->GetScalesCounts((CNCPipe::Axes)axis, val);
        *counts = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetScalesCounts(int axis, double value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetScalesCounts((CNCPipe::Axes)axis, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetScalesVelocity(int axis, double* velocity)
{
    if (!g_pipeInitialized || velocity == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->axis->GetScalesVelocity((CNCPipe::Axes)axis, val);
        *velocity = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetScalesVelocity(int axis, double value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetScalesVelocity((CNCPipe::Axes)axis, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetScalesDeadband(int axis, int* deadband)
{
    if (!g_pipeInitialized || deadband == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->axis->GetScalesDeadband((CNCPipe::Axes)axis, val);
        *deadband = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_SetScalesDeadband(int axis, int value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->axis->SetScalesDeadband((CNCPipe::Axes)axis, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Axis_GetPower(int axis, int* power_state)
{
    if (!g_pipeInitialized || power_state == nullptr) return -1;
    try {
        CNCPipe::Axis::MotorPower val;
        int rc = (int)g_pipe->axis->GetPower((CNCPipe::Axes)axis, val);
        *power_state = (int)val; return rc;
    }
    catch (...) { return -1; }
}

// ============================================================
// CSR
// ============================================================
extern "C" int CNC_CSR_GetAngle(double* angle)
{
    if (!g_pipeInitialized || angle == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->csr->GetAngle(val);
        *angle = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_CSR_GetAngleWcs(int wcs, double* angle)
{
    if (!g_pipeInitialized || angle == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->csr->GetAngle(wcs, val);
        *angle = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_CSR_SetAngle(double angle)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->csr->SetAngle(angle); }
    catch (...) { return -1; }
}

extern "C" int CNC_CSR_SetAngleWcs(int wcs, double angle)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->csr->SetAngle(wcs, angle); }
    catch (...) { return -1; }
}

extern "C" int CNC_CSR_Disable()
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->csr->DisableCSR(); }
    catch (...) { return -1; }
}

extern "C" int CNC_CSR_Reenable()
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->csr->ReenableCSR(); }
    catch (...) { return -1; }
}

// ============================================================
// DRO
// ============================================================

// Helper : parse un double depuis une string DRO
static double ParseDroString(String^ s)
{
    if (String::IsNullOrWhiteSpace(s)) return 0.0;
    double val = 0.0;
    if (Double::TryParse(s,
        System::Globalization::NumberStyles::Float | System::Globalization::NumberStyles::AllowThousands,
        System::Globalization::CultureInfo::InvariantCulture, val))
        return val;
    if (Double::TryParse(s, val))
        return val;
    return 0.0;
}

extern "C" int CNC_Dro_GetDroPositions(int dro_coordinates, double* positions, int* count)
{
    if (!g_pipeInitialized || positions == nullptr || count == nullptr) return -1;
    try {
        // LOCAL et MACHINE via state (comme CncControlBridge)
        if (dro_coordinates == 0 || dro_coordinates == 1) {
            System::Globalization::CultureInfo^ savedCulture = System::Threading::Thread::CurrentThread->CurrentCulture;
            System::Threading::Thread::CurrentThread->CurrentCulture = System::Globalization::CultureInfo::InvariantCulture;
            array<double>^ pos = nullptr;
            int rc = -1;
            if (dro_coordinates == 0)
                rc = (int)g_pipe->state->GetCurrentLocalPosition(pos);
            else
                rc = (int)g_pipe->state->GetCurrentMachinePosition(pos);
            System::Threading::Thread::CurrentThread->CurrentCulture = savedCulture;
            if (pos == nullptr || pos->Length == 0) return -1;
            *count = pos->Length;
            for (int i = 0; i < pos->Length; i++) positions[i] = pos[i];
            return rc;
        }
        // DRO_DISTANCE_TO_GO = 2 via dro->GetDro avec strings
        array<Tuple<String^, String^, String^>^>^ arr = nullptr;
        int rc = (int)g_pipe->dro->GetDro((CNCPipe::Dro::DroCoordinates)dro_coordinates, arr);
        if (arr == nullptr) return -1;
        *count = arr->Length;
        for (int i = 0; i < arr->Length; i++)
            positions[i] = arr[i] != nullptr ? ParseDroString(arr[i]->Item2) : 0.0;
        return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Dro_SetDroCoordinates(int dro_coordinates)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->dro->SetDroCoordinates((CNCPipe::Dro::DroCoordinates)dro_coordinates); }
    catch (...) { return -1; }
}

// ============================================================
// JOB
// ============================================================
extern "C" int CNC_Job_Load(const char* path)
{
    if (!g_pipeInitialized || path == nullptr) return -1;
    try { return (int)g_pipe->job->Load(gcnew String(path)); }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_LoadWithDir(const char* path, const char* working_dir)
{
    if (!g_pipeInitialized || path == nullptr || working_dir == nullptr) return -1;
    try { return (int)g_pipe->job->Load(gcnew String(path), gcnew String(working_dir)); }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_RunCommand(const char* command, int require_cycle_start)
{
    if (!g_pipeInitialized || command == nullptr) return -1;
    try { return (int)g_pipe->job->RunCommand(gcnew String(command), require_cycle_start != 0); }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_RunCommandWithDir(const char* command, const char* working_dir, int require_cycle_start)
{
    if (!g_pipeInitialized || command == nullptr || working_dir == nullptr) return -1;
    try { return (int)g_pipe->job->RunCommand(gcnew String(command), gcnew String(working_dir), require_cycle_start != 0); }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_CancelExecution()
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->job->CancelExecution(); }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_ContinueExecution()
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->job->ContinueExecution(); }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_GetRepeatState(int* repeat_state)
{
    if (!g_pipeInitialized || repeat_state == nullptr) return -1;
    try {
        bool val; int rc = (int)g_pipe->job->GetJobRepeatState(val);
        *repeat_state = val ? 1 : 0; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_SetRepeatState(int state)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->job->SetJobRepeatState(state != 0); }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_RefreshGraph(int refresh_rtg)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->job->RefreshGraph(refresh_rtg != 0); }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_GetPartCount(int* part_count)
{
    if (!g_pipeInitialized || part_count == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->job->GetPartCount(val);
        *part_count = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_GetPartNumber(int* part_number)
{
    if (!g_pipeInitialized || part_number == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->job->GetPartNumber(val);
        *part_number = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_GetCurrentlyRunningJob(char* job_name, int bufferSize)
{
    if (!g_pipeInitialized || job_name == nullptr) return -1;
    try {
        String^ val; int rc = (int)g_pipe->job->GetCurrentlyRunningJob(val);
        StringToChar(val, job_name, bufferSize); return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_GetUserSystemVariable(int variable_number, double* value)
{
    if (!g_pipeInitialized || value == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->job->GetUserSystemVariable(variable_number, val);
        *value = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_GetUserStringSystemVariable(int variable_number, char* value, int bufferSize)
{
    if (!g_pipeInitialized || value == nullptr) return -1;
    try {
        String^ val; int rc = (int)g_pipe->job->GetUserStringSystemVariable(variable_number, val);
        StringToChar(val, value, bufferSize); return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_GetSystemVariable(int variable_number, double* value)
{
    if (!g_pipeInitialized || value == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->job->GetSystemVariable(variable_number, val);
        *value = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_GetSystemVariableStr(int variable_number, char* value, int bufferSize)
{
    if (!g_pipeInitialized || value == nullptr) return -1;
    try {
        String^ val; int rc = (int)g_pipe->job->GetSystemVariable(variable_number, val);
        StringToChar(val, value, bufferSize); return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_SetSystemVariable(int variable_number, double value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->job->SetSystemVariable(variable_number, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Job_SetSystemVariableStr(int variable_number, const char* value)
{
    if (!g_pipeInitialized || value == nullptr) return -1;
    try { return (int)g_pipe->job->SetSystemVariable(variable_number, gcnew String(value)); }
    catch (...) { return -1; }
}

// ============================================================
// MESSAGE WINDOW
// ============================================================
extern "C" int CNC_MessageWindow_GetMessages(char* messages, int bufferSize)
{
    if (!g_pipeInitialized || messages == nullptr) return -1;
    try {
        array<String^>^ msgs = nullptr;
        int rc = (int)g_pipe->message_window->GetMessages(msgs);
        if (msgs == nullptr) return rc;
        String^ joined = String::Join("`", msgs);
        if (joined != nullptr)
            StringToChar(joined, messages, bufferSize);
        return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_MessageWindow_AddMessage(const char* message, int backgroundColor, int textColor)
{
    if (!g_pipeInitialized || message == nullptr) return -1;
    try { return (int)g_pipe->message_window->AddMessage(gcnew String(message), backgroundColor, textColor); }
    catch (...) { return -1; }
}

// ============================================================
// PARAMETER
// ============================================================
extern "C" int CNC_Parameter_GetMachineParameterValue(int parameter_num, double* value)
{
    if (!g_pipeInitialized || value == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->parameter->GetMachineParameterValue(parameter_num, val);
        *value = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Parameter_SetMachineParameter(int parameter_num, double value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->parameter->SetMachineParameter(parameter_num, value); }
    catch (...) { return -1; }
}

// ============================================================
// PLC
// ============================================================
extern "C" int CNC_Plc_GetInputState(int bitNumber, int* state)
{
    if (!g_pipeInitialized || state == nullptr) return -1;
    try {
        CNCPipe::Plc::IOState val;
        int rc = (int)g_pipe->plc->GetInputState(bitNumber, val);
        *state = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_GetOutputState(int bitNumber, int* state)
{
    if (!g_pipeInitialized || state == nullptr) return -1;
    try {
        CNCPipe::Plc::IOState val;
        int rc = (int)g_pipe->plc->GetOutputState(bitNumber, val);
        *state = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_GetMemoryState(int bitNumber, int* state)
{
    if (!g_pipeInitialized || state == nullptr) return -1;
    try {
        CNCPipe::Plc::IOState val;
        int rc = (int)g_pipe->plc->GetMemoryState(bitNumber, val);
        *state = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_GetMemoryStateBool(int bitNumber)
{
    if (!g_pipeInitialized) return 0;
    try { return g_pipe->plc->GetMemoryState(bitNumber) ? 1 : 0; }
    catch (...) { return 0; }
}

extern "C" int CNC_Plc_GetInputStateBool(int bitNumber)
{
    if (!g_pipeInitialized) return 0;
    try { return g_pipe->plc->GetInputState(bitNumber) ? 1 : 0; }
    catch (...) { return 0; }
}

extern "C" int CNC_Plc_GetOutputStateBool(int bitNumber)
{
    if (!g_pipeInitialized) return 0;
    try { return g_pipe->plc->GetOutputState(bitNumber) ? 1 : 0; }
    catch (...) { return 0; }
}

extern "C" int CNC_Plc_SetIoForceState(int ioBit, int bitType, int state)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->plc->SetIoForceState(ioBit, (CNCPipe::Plc::BitType)bitType, (CNCPipe::Plc::ForceState)state); }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_SetInputInversionState(int inputBit, int state)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->plc->SetInputInversionState(inputBit, (CNCPipe::Plc::InversionState)state); }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_SetInputForceState(int inputBit, int state)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->plc->SetInputForceState(inputBit, (CNCPipe::Plc::ForceState)state); }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_GetWordValue(int index, int* value)
{
    if (!g_pipeInitialized || value == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->plc->GetWordValue(index, val);
        *value = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_GetDoubleWordValue(int index, long long* value)
{
    if (!g_pipeInitialized || value == nullptr) return -1;
    try {
        long long val; int rc = (int)g_pipe->plc->GetDoubleWordValue(index, val);
        *value = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_GetFloatWordValue(int index, float* value)
{
    if (!g_pipeInitialized || value == nullptr) return -1;
    try {
        float val; int rc = (int)g_pipe->plc->GetFloatWordValue(index, val);
        *value = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_GetDoubleFloatWordValue(int index, double* value)
{
    if (!g_pipeInitialized || value == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->plc->GetDoubleFloatWordValue(index, val);
        *value = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_SetSkinningDataWord(int index, int value, int sendImmediately)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->plc->SetSkinningDataWord(index, value, sendImmediately != 0); }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_GetSkinningDataWord(int index, int* value)
{
    if (!g_pipeInitialized || value == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->plc->GetSkinningDataWord(index, val);
        *value = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_SetSkinningDataFloatWord(int index, float value, int sendImmediately)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->plc->SetSkinningDataFloatWord(index, value, sendImmediately != 0); }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_GetSkinningDataFloatWord(int index, float* value)
{
    if (!g_pipeInitialized || value == nullptr) return -1;
    try {
        float val; int rc = (int)g_pipe->plc->GetSkinningDataFloatWord(index, val);
        *value = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_SetSkinningDataDoubleFloatWord(int index, double value, int sendImmediately)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->plc->SetSkinningDataDoubleFloatWord(index, value, sendImmediately != 0); }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_GetSkinningDataDoubleFloatWord(int index, double* value)
{
    if (!g_pipeInitialized || value == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->plc->GetSkinningDataDoubleFloatWord(index, val);
        *value = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_SetSkinEventState(int eventNumber, int state)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->plc->SetSkinEventState(eventNumber, state); }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_GetSkinEventState(int skin_event, int* state)
{
    if (!g_pipeInitialized || state == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->plc->GetSkinEventState(skin_event, val);
        *state = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_GetPcSystemVariableBit(int bit, int* state)
{
    if (!g_pipeInitialized || state == nullptr) return -1;
    try {
        CNCPipe::Plc::IOState val;
        int rc = (int)g_pipe->plc->GetPcSystemVariableBit((CentroidAPI::PcToMpuSysVarBit)bit, val);
        *state = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Plc_GetPlcSystemVariableBit(int bit, int* state)
{
    if (!g_pipeInitialized || state == nullptr) return -1;
    try {
        CNCPipe::Plc::IOState val;
        int rc = (int)g_pipe->plc->GetPlcSystemVariableBit((CentroidAPI::MpuToPcSysVarBit)bit, val);
        *state = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" unsigned long long CNC_Plc_GetVcpLedStates()
{
    if (!g_pipeInitialized) return 0;
    try { return (unsigned long long)g_pipe->plc->GetVcpLedStates(); }
    catch (...) { return 0; }
}

// ============================================================
// STATE
// ============================================================
extern "C" int CNC_State_GetJogState(int* jog_state)
{
    if (!g_pipeInitialized || jog_state == nullptr) return -1;
    try {
        bool val; int rc = (int)g_pipe->state->GetJogState(val);
        *jog_state = val ? 1 : 0; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetFeedrate(double* feedrate)
{
    if (!g_pipeInitialized || feedrate == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->state->GetFeedrate(val);
        *feedrate = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetSpindleSpeed(double* spindle_speed)
{
    if (!g_pipeInitialized || spindle_speed == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->state->GetSpindleSpeed(val);
        *spindle_speed = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetFeedrateOverride(int* feedrate_override)
{
    if (!g_pipeInitialized || feedrate_override == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->state->GetFeedrateOverride(val);
        *feedrate_override = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetCurrentMachinePosition(double* positions, int* count)
{
    if (!g_pipeInitialized || positions == nullptr || count == nullptr) return -1;
    System::Globalization::CultureInfo^ savedCulture = System::Threading::Thread::CurrentThread->CurrentCulture;
    try {
        System::Threading::Thread::CurrentThread->CurrentCulture = System::Globalization::CultureInfo::InvariantCulture;
        array<double>^ pos = nullptr;
        int rc = (int)g_pipe->state->GetCurrentMachinePosition(pos);
        System::Threading::Thread::CurrentThread->CurrentCulture = savedCulture;
        if (pos == nullptr || pos->Length == 0) return -1;
        *count = pos->Length;
        for (int i = 0; i < pos->Length; i++) positions[i] = pos[i];
        return rc;
    }
    catch (...) {
        System::Threading::Thread::CurrentThread->CurrentCulture = savedCulture;
        return -1;
    }
}

extern "C" int CNC_State_GetCurrentLocalPosition(double* positions, int* count)
{
    if (!g_pipeInitialized || positions == nullptr || count == nullptr) return -1;
    System::Globalization::CultureInfo^ savedCulture = System::Threading::Thread::CurrentThread->CurrentCulture;
    try {
        System::Threading::Thread::CurrentThread->CurrentCulture = System::Globalization::CultureInfo::InvariantCulture;
        array<double>^ pos = nullptr;
        int rc = (int)g_pipe->state->GetCurrentLocalPosition(pos);
        System::Threading::Thread::CurrentThread->CurrentCulture = savedCulture;
        if (pos == nullptr || pos->Length == 0) return -1;
        *count = pos->Length;
        for (int i = 0; i < pos->Length; i++) positions[i] = pos[i];
        return rc;
    }
    catch (...) {
        System::Threading::Thread::CurrentThread->CurrentCulture = savedCulture;
        return -1;
    }
}

extern "C" int CNC_State_GetEncoderPosition(long long* positions, int* count)
{
    if (!g_pipeInitialized || positions == nullptr || count == nullptr) return -1;
    try {
        array<long long>^ pos;
        int rc = (int)g_pipe->state->GetEncoderPosition(pos);
        *count = pos->Length;
        for (int i = 0; i < pos->Length; i++) positions[i] = pos[i];
        return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetMoveMode(int* move_mode)
{
    if (!g_pipeInitialized || move_mode == nullptr) return -1;
    try {
        CNCPipe::State::MoveMode val;
        int rc = (int)g_pipe->state->GetMoveMode(val);
        *move_mode = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetPositioningMode(int* positioning_mode)
{
    if (!g_pipeInitialized || positioning_mode == nullptr) return -1;
    try {
        CNCPipe::State::PositioningMode val;
        int rc = (int)g_pipe->state->GetPositioningMode(val);
        *positioning_mode = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetFeedHoldState(int* feed_hold_state)
{
    if (!g_pipeInitialized || feed_hold_state == nullptr) return -1;
    try {
        CNCPipe::State::FeedHoldState val;
        int rc = (int)g_pipe->state->GetFeedHoldState(val);
        *feed_hold_state = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetMdiState(int* mdi_state)
{
    if (!g_pipeInitialized || mdi_state == nullptr) return -1;
    try {
        CNCPipe::State::MdiState val;
        int rc = (int)g_pipe->state->GetMdiState(val);
        *mdi_state = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetUnitsOfMeasureDefault(int* units)
{
    if (!g_pipeInitialized || units == nullptr) return -1;
    try {
        CNCPipe::State::UnitsOfMeasure val;
        int rc = (int)g_pipe->state->GetUnitsOfMeasureDefault(val);
        *units = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetUnitsOfMeasureDro(int* units)
{
    if (!g_pipeInitialized || units == nullptr) return -1;
    try {
        CNCPipe::State::UnitsOfMeasure val;
        int rc = (int)g_pipe->state->GetUnitsOfMeasureDro(val);
        *units = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_SetImperialUnits() { if (!g_pipeInitialized) return -1; try { return (int)g_pipe->state->SetImperialUnits(); } catch (...) { return -1; } }
extern "C" int CNC_State_SetMetricUnits() { if (!g_pipeInitialized) return -1; try { return (int)g_pipe->state->SetMetricUnits(); } catch (...) { return -1; } }
extern "C" int CNC_State_SetDroImperialUnits() { if (!g_pipeInitialized) return -1; try { return (int)g_pipe->state->SetDroImperialUnits(); } catch (...) { return -1; } }
extern "C" int CNC_State_SetDroMetricUnits() { if (!g_pipeInitialized) return -1; try { return (int)g_pipe->state->SetDroMetricUnits(); } catch (...) { return -1; } }

extern "C" int CNC_State_IsJobRunning(int* job_running)
{
    if (!g_pipeInitialized || job_running == nullptr) return -1;
    try {
        bool val; int rc = (int)g_pipe->state->IsJobRunning(val);
        *job_running = val ? 1 : 0; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetJobNameCurrent(char* job_name, int bufferSize)
{
    if (!g_pipeInitialized || job_name == nullptr) return -1;
    try {
        String^ val; int rc = (int)g_pipe->state->GetJobNameCurrent(val);
        StringToChar(val, job_name, bufferSize); return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetFullPathJobNameCurrent(char* job_name, int bufferSize)
{
    if (!g_pipeInitialized || job_name == nullptr) return -1;
    try {
        String^ val; int rc = (int)g_pipe->state->GetFullPathJobNameCurrent(val);
        StringToChar(val, job_name, bufferSize); return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetCurrentLineInfo(int* line_number, int* program_number, int* stack_level)
{
    if (!g_pipeInitialized) return -1;
    try {
        int ln, pn, sl;
        int rc = (int)g_pipe->state->GetCurrentLineInfo(ln, pn, sl);
        *line_number = ln; *program_number = pn; *stack_level = sl;
        return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetCurrentStackLevelZeroLineInfo(int* line_number, int* program_number)
{
    if (!g_pipeInitialized) return -1;
    try {
        int ln, pn;
        int rc = (int)g_pipe->state->GetCurrentStackLevelZeroLineInfo(ln, pn);
        *line_number = ln; *program_number = pn;
        return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetActiveGCodes(char* g_codes, int bufferSize)
{
    if (!g_pipeInitialized || g_codes == nullptr) return -1;
    try {
        array<String^>^ codes;
        int rc = (int)g_pipe->state->GetActiveGCodes(codes);
        StringToChar(String::Join("_", codes), g_codes, bufferSize);
        return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetGCodeDisplay(char* g_code_display, int bufferSize)
{
    if (!g_pipeInitialized || g_code_display == nullptr) return -1;
    try {
        array<String^>^ codes;
        int rc = (int)g_pipe->state->GetGCodeDisplay(codes);
        StringToChar(String::Join("_", codes), g_code_display, bufferSize);
        return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetHighRangeSpindleSpeed(int max_or_min, double* speed)
{
    if (!g_pipeInitialized || speed == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->state->GetHighRangeSpindleSpeed((CNCPipe::State::Value)max_or_min, val);
        *speed = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_SetHighRangeSpindleSpeed(int max_or_min, double value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->state->SetHighRangeSpindleSpeed((CNCPipe::State::Value)max_or_min, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetMachineHomeAtPowerUp(int* homing_type)
{
    if (!g_pipeInitialized || homing_type == nullptr) return -1;
    try {
        CNCPipe::State::HomingType val;
        int rc = (int)g_pipe->state->GetMachineHomeAtPowerUp(val);
        *homing_type = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_SetMachineHomeAtPowerUp(int homing_type)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->state->SetMachineHomeAtPowerUp((CNCPipe::State::HomingType)homing_type); }
    catch (...) { return -1; }
}

extern "C" int CNC_State_SetPlasmaInterruptPoint(double interrupt_x, double interrupt_y)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->state->SetPlasmaInterruptPoint(interrupt_x, interrupt_y); }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetUsbInputInversions(char* inversionBits, int bufferSize)
{
    if (!g_pipeInitialized || inversionBits == nullptr) return -1;
    try {
        String^ val; int rc = (int)g_pipe->state->GetUsbInputInversions(val);
        StringToChar(val, inversionBits, bufferSize); return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_SetUsbInputInversions(const char* inputInversions)
{
    if (!g_pipeInitialized || inputInversions == nullptr) return -1;
    try {
        String^ str = gcnew String(inputInversions);
        array<wchar_t>^ chars = str->ToCharArray();
        return (int)g_pipe->state->SetUsbInputInversions(chars);
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_ToggleUsbInputInversionBit(int inpNum)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->state->ToggleUsbInputInversionBit(inpNum); }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetUsbOnlineBits(int* online0, int* online1)
{
    if (!g_pipeInitialized) return -1;
    try {
        array<bool>^ bits;
        int rc = (int)g_pipe->state->GetUsbOnlineBits(bits);
        *online0 = bits[0] ? 1 : 0;
        *online1 = bits[1] ? 1 : 0;
        return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_IsPCPoweringOff(int* isPoweringOff)
{
    if (!g_pipeInitialized || isPoweringOff == nullptr) return -1;
    try {
        bool val; int rc = (int)g_pipe->state->IsPCPoweringOff(val);
        *isPoweringOff = val ? 1 : 0; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_IsAPIRestricted(int* apiRestricted)
{
    if (!g_pipeInitialized || apiRestricted == nullptr) return -1;
    try {
        bool val; int rc = (int)g_pipe->state->IsAPIRestricted(val);
        *apiRestricted = val ? 1 : 0; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetConsoleType(int* consoleType)
{
    if (!g_pipeInitialized || consoleType == nullptr) return -1;
    try {
        CNCPipe::State::ConsoleTypes val;
        int rc = (int)g_pipe->state->GetConsoleType(val);
        *consoleType = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_SetConsoleType(int consoleType)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->state->SetConsoleType((CNCPipe::State::ConsoleTypes)consoleType); }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetAcornBoardRevision(int* revision)
{
    if (!g_pipeInitialized || revision == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->state->GetAcornBoardRevision(val);
        *revision = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetScreenSize(int* width, int* height)
{
    if (!g_pipeInitialized) return -1;
    try {
        int w, h; int rc = (int)g_pipe->state->GetScreenSize(w, h);
        *width = w; *height = h; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetMonitorSize(int* width, int* height)
{
    if (!g_pipeInitialized) return -1;
    try {
        int w, h; int rc = (int)g_pipe->state->GetMonitorSize(w, h);
        *width = w; *height = h; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_State_GetScreenPosition(int* x, int* y)
{
    if (!g_pipeInitialized) return -1;
    try {
        int px, py; int rc = (int)g_pipe->state->GetScreenPosition(px, py);
        *x = px; *y = py; return rc;
    }
    catch (...) { return -1; }
}

// ============================================================
// SYSTEM
// ============================================================
extern "C" int CNC_Sys_GetSystemIdentifier(long long* identifier)
{
    if (!g_pipeInitialized || identifier == nullptr) return -1;
    try {
        long long val; int rc = (int)g_pipe->system->GetSystemIdentifier(val);
        *identifier = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Sys_GetMachineType(int* machine_type)
{
    if (!g_pipeInitialized || machine_type == nullptr) return -1;
    try {
        CNCPipe::Sys::MachineTypes val;
        int rc = (int)g_pipe->system->GetMachineType(val);
        *machine_type = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Sys_ExitSoftware()
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->system->ExitSoftware(); }
    catch (...) { return -1; }
}

extern "C" int CNC_Sys_GetPLCEXP1616NumberofDevices(int* number)
{
    if (!g_pipeInitialized || number == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->system->GetPLCEXP1616NumberofDevices(val);
        *number = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Sys_GetECAT1616NumberOfDevices(int* number)
{
    if (!g_pipeInitialized || number == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->system->GetECAT1616NumberOfDevices(val);
        *number = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Sys_IsENCEXP12Connected(int* connected)
{
    if (!g_pipeInitialized || connected == nullptr) return -1;
    try {
        bool val; int rc = (int)g_pipe->system->IsENCEXP12Connected(val);
        *connected = val ? 1 : 0; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Sys_GetSerialNumber(char* serialNumber, int bufferSize)
{
    if (!g_pipeInitialized || serialNumber == nullptr) return -1;
    try {
        String^ val; int rc = (int)g_pipe->system->GetSerialNumber(val);
        StringToChar(val, serialNumber, bufferSize); return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Sys_ImportLicense(const char* licensePath)
{
    if (!g_pipeInitialized || licensePath == nullptr) return -1;
    try { return (int)g_pipe->system->ImportLicense(gcnew String(licensePath)); }
    catch (...) { return -1; }
}

// ============================================================
// TOOL
// ============================================================
extern "C" int CNC_Tool_GetToolNumber(int* tool_number)
{
    if (!g_pipeInitialized || tool_number == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->tool->GetToolNumber(val);
        *tool_number = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetCurrentHeightOffsetNumber(int* h_number)
{
    if (!g_pipeInitialized || h_number == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->tool->GetCurrentHeightOffsetNumber(val);
        *h_number = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetToolDescription(char* description, int bufferSize)
{
    if (!g_pipeInitialized || description == nullptr) return -1;
    try {
        String^ val; int rc = (int)g_pipe->tool->GetToolDescription(val);
        StringToChar(val, description, bufferSize); return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetToolDescriptionByIndex(int i, char* description, int bufferSize)
{
    if (!g_pipeInitialized || description == nullptr) return -1;
    try {
        String^ val; int rc = (int)g_pipe->tool->GetToolDescription(i, val);
        StringToChar(val, description, bufferSize); return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetHeightOffsetAmount(double* height_offset)
{
    if (!g_pipeInitialized || height_offset == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->tool->GetHeightOffsetAmount(val);
        *height_offset = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetHeightOffsetAmountByH(int H, double* height_offset)
{
    if (!g_pipeInitialized || height_offset == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->tool->GetHeightOffsetAmount(H, val);
        *height_offset = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetDiameterOffsetAmount(double* diameter_offset)
{
    if (!g_pipeInitialized || diameter_offset == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->tool->GetDiameterOffsetAmount(val);
        *diameter_offset = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetDiameterOffsetAmountByD(int D, double* diameter_offset)
{
    if (!g_pipeInitialized || diameter_offset == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->tool->GetDiameterOffsetAmount(D, val);
        *diameter_offset = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetToolSpindleSpeed(int* speed)
{
    if (!g_pipeInitialized || speed == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->tool->GetToolSpindleSpeed(val);
        *speed = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetToolSpindleSpeedByT(int T, int* speed)
{
    if (!g_pipeInitialized || speed == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->tool->GetToolSpindleSpeed(T, val);
        *speed = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetCoolant(int* coolant)
{
    if (!g_pipeInitialized || coolant == nullptr) return -1;
    try {
        CNCPipe::Tool::Coolant val;
        int rc = (int)g_pipe->tool->GetCoolant(val);
        *coolant = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetCoolantByT(int T, int* coolant)
{
    if (!g_pipeInitialized || coolant == nullptr) return -1;
    try {
        CNCPipe::Tool::Coolant val;
        int rc = (int)g_pipe->tool->GetCoolant(T, val);
        *coolant = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetToolSpindleDirection(int* direction)
{
    if (!g_pipeInitialized || direction == nullptr) return -1;
    try {
        CNCPipe::Tool::SpindleDirection val;
        int rc = (int)g_pipe->tool->GetToolSpindleDirection(val);
        *direction = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetToolSpindleDirectionByT(int T, int* direction)
{
    if (!g_pipeInitialized || direction == nullptr) return -1;
    try {
        CNCPipe::Tool::SpindleDirection val;
        int rc = (int)g_pipe->tool->GetToolSpindleDirection(T, val);
        *direction = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetToolBin(int* bin)
{
    if (!g_pipeInitialized || bin == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->tool->GetToolBin(val);
        *bin = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetToolBinByT(int T, int* bin)
{
    if (!g_pipeInitialized || bin == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->tool->GetToolBin(T, val);
        *bin = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetToolHNumber(int* h_number)
{
    if (!g_pipeInitialized || h_number == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->tool->GetToolHNumber(val);
        *h_number = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetToolHNumberByT(int T, int* h_number)
{
    if (!g_pipeInitialized || h_number == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->tool->GetToolHNumber(T, val);
        *h_number = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetToolDNumber(int* d_number)
{
    if (!g_pipeInitialized || d_number == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->tool->GetToolDNumber(val);
        *d_number = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetToolDNumberByT(int T, int* d_number)
{
    if (!g_pipeInitialized || d_number == nullptr) return -1;
    try {
        int val; int rc = (int)g_pipe->tool->GetToolDNumber(T, val);
        *d_number = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_SetBinNumber(int T, int value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->tool->SetBinNumber(T, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_SetCoolant(int T, int type)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->tool->SetCoolant(T, (CNCPipe::Tool::Coolant)type); }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_SetToolHeightOffsetAmount(int T, double value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->tool->SetToolHeightOffsetAmount(T, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_SetToolDiameterOffsetAmount(int T, double value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->tool->SetToolDiameterOffsetAmount(T, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_SetSpindleDirection(int T, int dir)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->tool->SetSpindleDirection(T, (CNCPipe::Tool::SpindleDirection)dir); }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_SetSpindleSpeed(int T, long long speed)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->tool->SetSpindleSpeed(T, speed); }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_SetToolDescription(int T, const char* description)
{
    if (!g_pipeInitialized || description == nullptr) return -1;
    try { return (int)g_pipe->tool->SetToolDescription(T, gcnew String(description)); }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_SetToolDNumber(int T, int dia)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->tool->SetToolDNumber(T, dia); }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_SetToolHNumber(int T, int height)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->tool->SetToolHNumber(T, height); }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_GetWearAdjustment(int T, int type, double* value)
{
    if (!g_pipeInitialized || value == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->tool->GetWearAdjustment(T, (CNCPipe::Tool::ToolWearAdjustmentType)type, val);
        *value = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_SetWearAdjustment(int T, int type, double value)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->tool->SetWearAdjustment(T, (CNCPipe::Tool::ToolWearAdjustmentType)type, value); }
    catch (...) { return -1; }
}

extern "C" int CNC_Tool_Save()
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->tool->Save(); }
    catch (...) { return -1; }
}

// ============================================================
// WCS
// ============================================================
extern "C" int CNC_Wcs_GetActiveWcs(int* wcs_number)
{
    if (!g_pipeInitialized || wcs_number == nullptr) return -1;
    try {
        CNCPipe::Wcs::WCS val;
        int rc = (int)g_pipe->wcs->GetActiveWcs(val);
        *wcs_number = (int)val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Wcs_GetWorkpieceOrigin(int axis, double* origin)
{
    if (!g_pipeInitialized || origin == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->wcs->GetWorkpieceOrigin(axis, val);
        *origin = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Wcs_GetWorkpieceOriginByWcs(int wcs, int axis, double* origin)
{
    if (!g_pipeInitialized || origin == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->wcs->GetWorkpieceOrigin(wcs, axis, val);
        *origin = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Wcs_GetWorkpieceReference(int ret, int axis, double* reference)
{
    if (!g_pipeInitialized || reference == nullptr) return -1;
    try {
        double val; int rc = (int)g_pipe->wcs->GetWorkpieceReference(ret, axis, val);
        *reference = val; return rc;
    }
    catch (...) { return -1; }
}

extern "C" int CNC_Wcs_SelectNextWcs()
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->wcs->SelectNextWcs(); }
    catch (...) { return -1; }
}

extern "C" int CNC_Wcs_SelectPrevWcs()
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->wcs->SelectPrevWcs(); }
    catch (...) { return -1; }
}

extern "C" int CNC_Wcs_SetWorkpieceReference(int ret, int axis, double point)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->wcs->SetWorkpieceReference(ret, axis, point); }
    catch (...) { return -1; }
}

extern "C" int CNC_Wcs_SetWorkpieceOrigin()
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->wcs->SetWorkpieceOrigin(); }
    catch (...) { return -1; }
}

extern "C" int CNC_Wcs_SetWorkpieceOriginByAxis(int axis)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->wcs->SetWorkpieceOrigin(axis); }
    catch (...) { return -1; }
}

extern "C" int CNC_Wcs_SetWorkpieceOriginByWcsAxis(int wcs, int axis)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->wcs->SetWorkpieceOrigin(wcs, axis); }
    catch (...) { return -1; }
}

extern "C" int CNC_Wcs_SetWorkpieceLocation(int axis, double location)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->wcs->SetWorkpieceLocation(axis, location); }
    catch (...) { return -1; }
}

extern "C" int CNC_Wcs_SetWorkpieceLocationByWcs(int wcs, int axis, double location)
{
    if (!g_pipeInitialized) return -1;
    try { return (int)g_pipe->wcs->SetWorkpieceLocation(wcs, axis, location); }
    catch (...) { return -1; }
}