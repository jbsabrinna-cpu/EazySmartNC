#pragma once

#ifdef CENTROIDWRAPPER_EXPORTS
#define CENTROID_API __declspec(dllexport)
#else
#define CENTROID_API __declspec(dllimport)
#endif

extern "C" {

// ============================================================
// CONNEXION
// ============================================================
CENTROID_API int  CNC_Connect(int useVcpPipe, int timeout);
CENTROID_API void CNC_Disconnect();
CENTROID_API int  CNC_IsConstructed();

// ============================================================
// AXIS
// ============================================================
CENTROID_API int CNC_Axis_GetAccelTime(int axis, double* accel_time);
CENTROID_API int CNC_Axis_SetAccelTime(int axis, double time);
CENTROID_API int CNC_Axis_GetDeadstartVelocity(int axis, double* velocity);
CENTROID_API int CNC_Axis_SetDeadstartVelocity(int axis, double velocity);
CENTROID_API int CNC_Axis_GetDeltaVMax(int axis, double* delta_v_max);
CENTROID_API int CNC_Axis_SetDeltaVMax(int axis, double delta_v_max);
CENTROID_API int CNC_Axis_GetRate(int axis, int jog_rate, double* rate);
CENTROID_API int CNC_Axis_SetRate(int axis, int rate_type, double rate);
CENTROID_API int CNC_Axis_GetLabel(int axis, char* label);
CENTROID_API int CNC_Axis_SetLabel(int axis, char label);
CENTROID_API int CNC_Axis_GetLashComp(int axis, double* lash_comp);
CENTROID_API int CNC_Axis_SetLashComp(int axis, double value);
CENTROID_API int CNC_Axis_GetScrewPitch(int axis, double* screw_pitch);
CENTROID_API int CNC_Axis_SetScrewPitch(int axis, double value);
CENTROID_API int CNC_Axis_GetLimit(int axis, int direction, double* limit);
CENTROID_API int CNC_Axis_SetLimit(int axis, int direction, double value);
CENTROID_API int CNC_Axis_GetHomeLimit(int axis, int direction, double* home_limit);
CENTROID_API int CNC_Axis_SetHomeLimit(int axis, int direction, double value);
CENTROID_API int CNC_Axis_GetAxisReversal(int axis, int* is_reversed);
CENTROID_API int CNC_Axis_SetAxisReversal(int axis, int is_reversed);
CENTROID_API int CNC_Axis_GetCountsPerTurn(int axis, double* counts);
CENTROID_API int CNC_Axis_SetCountsPerTurn(int axis, double value);
CENTROID_API int CNC_Axis_GetTravelLimit(int axis, int direction, double* travel_limit);
CENTROID_API int CNC_Axis_SetTravelLimit(int axis, int direction, double value);
CENTROID_API int CNC_Axis_GetScalesInput(int axis, int* encoder_number);
CENTROID_API int CNC_Axis_SetScalesInput(int axis, int value);
CENTROID_API int CNC_Axis_GetScalesEnable(int axis, int* enabled);
CENTROID_API int CNC_Axis_SetScalesEnable(int axis, int enabled);
CENTROID_API int CNC_Axis_GetScalesCounts(int axis, double* counts);
CENTROID_API int CNC_Axis_SetScalesCounts(int axis, double value);
CENTROID_API int CNC_Axis_GetScalesVelocity(int axis, double* velocity);
CENTROID_API int CNC_Axis_SetScalesVelocity(int axis, double value);
CENTROID_API int CNC_Axis_GetScalesDeadband(int axis, int* deadband);
CENTROID_API int CNC_Axis_SetScalesDeadband(int axis, int value);
CENTROID_API int CNC_Axis_GetPower(int axis, int* power_state);

// ============================================================
// CSR (Coordinate System Rotation)
// ============================================================
CENTROID_API int CNC_CSR_GetAngle(double* angle);
CENTROID_API int CNC_CSR_GetAngleWcs(int wcs, double* angle);
CENTROID_API int CNC_CSR_SetAngle(double angle);
CENTROID_API int CNC_CSR_SetAngleWcs(int wcs, double angle);
CENTROID_API int CNC_CSR_Disable();
CENTROID_API int CNC_CSR_Reenable();

// ============================================================
// DRO
// ============================================================
CENTROID_API int CNC_Dro_GetDroPositions(int dro_coordinates, double* positions, int* count);
CENTROID_API int CNC_Dro_SetDroCoordinates(int dro_coordinates);

// ============================================================
// INBOUND COMM (synchronisation temps reel)
// ============================================================
CENTROID_API int  CNC_InboundComm_ChangeDroType(int dro_coordinates);
CENTROID_API int  CNC_InboundComm_ChangeJobInfoType(int type);
CENTROID_API void CNC_StartListening();
CENTROID_API void CNC_StopListening();
CENTROID_API void CNC_ClearUnhandledMessages();

// Callback pour recevoir les evenements de CNC12
typedef void(*CNC_EventCallback)(int eventType, int flag, double* positions, int lineNumber, int stackLevel, const char* message, long long ticks);
CENTROID_API void CNC_SetEventCallback(CNC_EventCallback callback);

// ============================================================
// JOB
// ============================================================
CENTROID_API int CNC_Job_Load(const char* path);
CENTROID_API int CNC_Job_LoadWithDir(const char* path, const char* working_dir);
CENTROID_API int CNC_Job_RunCommand(const char* command, int require_cycle_start);
CENTROID_API int CNC_Job_RunCommandWithDir(const char* command, const char* working_dir, int require_cycle_start);
CENTROID_API int CNC_Job_CancelExecution();
CENTROID_API int CNC_Job_ContinueExecution();
CENTROID_API int CNC_Job_GetRepeatState(int* repeat_state);
CENTROID_API int CNC_Job_SetRepeatState(int state);
CENTROID_API int CNC_Job_RefreshGraph(int refresh_rtg);
CENTROID_API int CNC_Job_GetPartCount(int* part_count);
CENTROID_API int CNC_Job_GetPartNumber(int* part_number);
CENTROID_API int CNC_Job_GetCurrentlyRunningJob(char* job_name, int bufferSize);
CENTROID_API int CNC_Job_GetUserSystemVariable(int variable_number, double* value);
CENTROID_API int CNC_Job_GetUserStringSystemVariable(int variable_number, char* value, int bufferSize);
CENTROID_API int CNC_Job_GetSystemVariable(int variable_number, double* value);
CENTROID_API int CNC_Job_GetSystemVariableStr(int variable_number, char* value, int bufferSize);
CENTROID_API int CNC_Job_SetSystemVariable(int variable_number, double value);
CENTROID_API int CNC_Job_SetSystemVariableStr(int variable_number, const char* value);

// ============================================================
// MESSAGE WINDOW
// ============================================================
CENTROID_API int CNC_MessageWindow_GetMessages(char* messages, int bufferSize);
CENTROID_API int CNC_MessageWindow_AddMessage(const char* message, int backgroundColor, int textColor);

// ============================================================
// PARAMETER
// ============================================================
CENTROID_API int CNC_Parameter_GetMachineParameterValue(int parameter_num, double* value);
CENTROID_API int CNC_Parameter_SetMachineParameter(int parameter_num, double value);

// ============================================================
// PLC
// ============================================================
CENTROID_API int  CNC_Plc_GetInputState(int bitNumber, int* state);
CENTROID_API int  CNC_Plc_GetOutputState(int bitNumber, int* state);
CENTROID_API int  CNC_Plc_GetMemoryState(int bitNumber, int* state);
CENTROID_API int  CNC_Plc_GetMemoryStateBool(int bitNumber);
CENTROID_API int  CNC_Plc_GetInputStateBool(int bitNumber);
CENTROID_API int  CNC_Plc_GetOutputStateBool(int bitNumber);
CENTROID_API int  CNC_Plc_SetIoForceState(int ioBit, int bitType, int state);
CENTROID_API int  CNC_Plc_SetInputInversionState(int inputBit, int state);
CENTROID_API int  CNC_Plc_SetInputForceState(int inputBit, int state);
CENTROID_API int  CNC_Plc_GetWordValue(int index, int* value);
CENTROID_API int  CNC_Plc_GetDoubleWordValue(int index, long long* value);
CENTROID_API int  CNC_Plc_GetFloatWordValue(int index, float* value);
CENTROID_API int  CNC_Plc_GetDoubleFloatWordValue(int index, double* value);
CENTROID_API int  CNC_Plc_SetSkinningDataWord(int index, int value, int sendImmediately);
CENTROID_API int  CNC_Plc_GetSkinningDataWord(int index, int* value);
CENTROID_API int  CNC_Plc_SetSkinningDataFloatWord(int index, float value, int sendImmediately);
CENTROID_API int  CNC_Plc_GetSkinningDataFloatWord(int index, float* value);
CENTROID_API int  CNC_Plc_SetSkinningDataDoubleFloatWord(int index, double value, int sendImmediately);
CENTROID_API int  CNC_Plc_GetSkinningDataDoubleFloatWord(int index, double* value);
CENTROID_API int  CNC_Plc_SetSkinEventState(int eventNumber, int state);
CENTROID_API int  CNC_Plc_GetSkinEventState(int skin_event, int* state);
CENTROID_API int  CNC_Plc_GetPcSystemVariableBit(int bit, int* state);
CENTROID_API int  CNC_Plc_GetPlcSystemVariableBit(int bit, int* state);
CENTROID_API unsigned long long CNC_Plc_GetVcpLedStates();

// ============================================================
// STATE
// ============================================================
CENTROID_API int CNC_State_GetJogState(int* jog_state);
CENTROID_API int CNC_State_GetFeedrate(double* feedrate);
CENTROID_API int CNC_State_GetSpindleSpeed(double* spindle_speed);
CENTROID_API int CNC_State_GetFeedrateOverride(int* feedrate_override);
CENTROID_API int CNC_State_GetCurrentMachinePosition(double* positions, int* count);
CENTROID_API int CNC_State_GetCurrentLocalPosition(double* positions, int* count);
CENTROID_API int CNC_State_GetEncoderPosition(long long* positions, int* count);
CENTROID_API int CNC_State_GetMoveMode(int* move_mode);
CENTROID_API int CNC_State_GetPositioningMode(int* positioning_mode);
CENTROID_API int CNC_State_GetFeedHoldState(int* feed_hold_state);
CENTROID_API int CNC_State_GetMdiState(int* mdi_state);
CENTROID_API int CNC_State_GetUnitsOfMeasureDefault(int* units);
CENTROID_API int CNC_State_GetUnitsOfMeasureDro(int* units);
CENTROID_API int CNC_State_SetImperialUnits();
CENTROID_API int CNC_State_SetMetricUnits();
CENTROID_API int CNC_State_SetDroImperialUnits();
CENTROID_API int CNC_State_SetDroMetricUnits();
CENTROID_API int CNC_State_IsJobRunning(int* job_running);
CENTROID_API int CNC_State_GetJobNameCurrent(char* job_name, int bufferSize);
CENTROID_API int CNC_State_GetFullPathJobNameCurrent(char* job_name, int bufferSize);
CENTROID_API int CNC_State_GetCurrentLineInfo(int* line_number, int* program_number, int* stack_level);
CENTROID_API int CNC_State_GetCurrentStackLevelZeroLineInfo(int* line_number, int* program_number);
CENTROID_API int CNC_State_GetActiveGCodes(char* g_codes, int bufferSize);
CENTROID_API int CNC_State_GetGCodeDisplay(char* g_code_display, int bufferSize);
CENTROID_API int CNC_State_GetHighRangeSpindleSpeed(int max_or_min, double* speed);
CENTROID_API int CNC_State_SetHighRangeSpindleSpeed(int max_or_min, double value);
CENTROID_API int CNC_State_GetMachineHomeAtPowerUp(int* homing_type);
CENTROID_API int CNC_State_SetMachineHomeAtPowerUp(int homing_type);
CENTROID_API int CNC_State_SetPlasmaInterruptPoint(double interrupt_x, double interrupt_y);
CENTROID_API int CNC_State_GetUsbInputInversions(char* inversionBits, int bufferSize);
CENTROID_API int CNC_State_SetUsbInputInversions(const char* inputInversions);
CENTROID_API int CNC_State_ToggleUsbInputInversionBit(int inpNum);
CENTROID_API int CNC_State_GetUsbOnlineBits(int* online0, int* online1);
CENTROID_API int CNC_State_IsPCPoweringOff(int* isPoweringOff);
CENTROID_API int CNC_State_IsAPIRestricted(int* apiRestricted);
CENTROID_API int CNC_State_GetConsoleType(int* consoleType);
CENTROID_API int CNC_State_SetConsoleType(int consoleType);
CENTROID_API int CNC_State_GetAcornBoardRevision(int* revision);
CENTROID_API int CNC_State_GetScreenSize(int* width, int* height);
CENTROID_API int CNC_State_GetMonitorSize(int* width, int* height);
CENTROID_API int CNC_State_GetScreenPosition(int* x, int* y);

// ============================================================
// SYSTEM
// ============================================================
CENTROID_API int CNC_Sys_GetUnlockVersion(int* unlocks);
CENTROID_API int CNC_Sys_GetHighestLicenseVersion(int* version);
CENTROID_API int CNC_Sys_GetPluginUnlocked(int plugin, int* unlocked);
CENTROID_API int CNC_Sys_GetSystemIdentifier(long long* identifier);
CENTROID_API int CNC_Sys_GetMachineType(int* machine_type);
CENTROID_API int CNC_Sys_ExitSoftware();
CENTROID_API int CNC_Sys_GetPLCEXP1616NumberofDevices(int* number);
CENTROID_API int CNC_Sys_GetECAT1616NumberOfDevices(int* number);
CENTROID_API int CNC_Sys_IsENCEXP12Connected(int* connected);
CENTROID_API int CNC_Sys_GetSerialNumber(char* serialNumber, int bufferSize);
CENTROID_API int CNC_Sys_ImportLicense(const char* licensePath);

// ============================================================
// TOOL
// ============================================================
CENTROID_API int CNC_Tool_GetToolNumber(int* tool_number);
CENTROID_API int CNC_Tool_GetCurrentHeightOffsetNumber(int* h_number);
CENTROID_API int CNC_Tool_GetToolDescription(char* description, int bufferSize);
CENTROID_API int CNC_Tool_GetToolDescriptionByIndex(int i, char* description, int bufferSize);
CENTROID_API int CNC_Tool_GetHeightOffsetAmount(double* height_offset);
CENTROID_API int CNC_Tool_GetHeightOffsetAmountByH(int H, double* height_offset);
CENTROID_API int CNC_Tool_GetDiameterOffsetAmount(double* diameter_offset);
CENTROID_API int CNC_Tool_GetDiameterOffsetAmountByD(int D, double* diameter_offset);
CENTROID_API int CNC_Tool_GetToolSpindleSpeed(int* speed);
CENTROID_API int CNC_Tool_GetToolSpindleSpeedByT(int T, int* speed);
CENTROID_API int CNC_Tool_GetCoolant(int* coolant);
CENTROID_API int CNC_Tool_GetCoolantByT(int T, int* coolant);
CENTROID_API int CNC_Tool_GetToolSpindleDirection(int* direction);
CENTROID_API int CNC_Tool_GetToolSpindleDirectionByT(int T, int* direction);
CENTROID_API int CNC_Tool_GetToolBin(int* bin);
CENTROID_API int CNC_Tool_GetToolBinByT(int T, int* bin);
CENTROID_API int CNC_Tool_GetToolHNumber(int* h_number);
CENTROID_API int CNC_Tool_GetToolHNumberByT(int T, int* h_number);
CENTROID_API int CNC_Tool_GetToolDNumber(int* d_number);
CENTROID_API int CNC_Tool_GetToolDNumberByT(int T, int* d_number);
CENTROID_API int CNC_Tool_SetBinNumber(int T, int value);
CENTROID_API int CNC_Tool_SetCoolant(int T, int type);
CENTROID_API int CNC_Tool_SetToolHeightOffsetAmount(int T, double value);
CENTROID_API int CNC_Tool_SetToolDiameterOffsetAmount(int T, double value);
CENTROID_API int CNC_Tool_SetSpindleDirection(int T, int dir);
CENTROID_API int CNC_Tool_SetSpindleSpeed(int T, long long speed);
CENTROID_API int CNC_Tool_SetToolDescription(int T, const char* description);
CENTROID_API int CNC_Tool_SetToolDNumber(int T, int dia);
CENTROID_API int CNC_Tool_SetToolHNumber(int T, int height);
CENTROID_API int CNC_Tool_GetWearAdjustment(int T, int type, double* value);
CENTROID_API int CNC_Tool_SetWearAdjustment(int T, int type, double value);
CENTROID_API int CNC_Tool_Save();

// ============================================================
// WCS (Work Coordinate System)
// ============================================================
CENTROID_API int CNC_Wcs_GetActiveWcs(int* wcs_number);
CENTROID_API int CNC_Wcs_GetWorkpieceOrigin(int axis, double* origin);
CENTROID_API int CNC_Wcs_GetWorkpieceOriginByWcs(int wcs, int axis, double* origin);
CENTROID_API int CNC_Wcs_GetWorkpieceReference(int ret, int axis, double* reference);
CENTROID_API int CNC_Wcs_SelectNextWcs();
CENTROID_API int CNC_Wcs_SelectPrevWcs();
CENTROID_API int CNC_Wcs_SetWorkpieceReference(int ret, int axis, double point);
CENTROID_API int CNC_Wcs_SetWorkpieceOrigin();
CENTROID_API int CNC_Wcs_SetWorkpieceOriginByAxis(int axis);
CENTROID_API int CNC_Wcs_SetWorkpieceOriginByWcsAxis(int wcs, int axis);
CENTROID_API int CNC_Wcs_SetWorkpieceLocation(int axis, double location);
CENTROID_API int CNC_Wcs_SetWorkpieceLocationByWcs(int wcs, int axis, double location);

} // extern "C"
