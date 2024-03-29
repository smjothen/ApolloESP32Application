// this file is based on https://github.com/ZaptecCharger/ZapChargerProMCU/blob/a202f6e862b1c9914f0e06d1aae4960ef60af998/smart/smart/src/zapProtocol.h
#ifndef ZAPTEC_PROTOCOL_SERIALISATION_H
#define ZAPTEC_PROTOCOL_SERIALISATION_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Hack for not overflowing protocol buffers
#define ZAP_PROTOCOL_BUFFER_SIZE             128*5
#define ZAP_PROTOCOL_BUFFER_SIZE_ENCODED     (ZAP_PROTOCOL_BUFFER_SIZE + 1 /* overhead byte */ + 1 /* delimiter byte */ + 0 /* ~one byte overhead per 256 bytes */ + 5 /*extra*/)
#define ZAP_PROTOCOL_MAX_DATA_LENGTH         (ZAP_PROTOCOL_BUFFER_SIZE - 7 /* worst-case header */ - 2 /* checksum */ - 5 /*extra*/)

// TODO: Separate more cloud-only enums from MCU enums
typedef enum {
	IsOcppConnected = -3,
	IsOnline = -2,
	Pulse = -1,

	OfflineMode = 1,
	Capabilities = 100,

	SourceVersion = 916,
	DiagnosticsMode = 805,

	MIDPublicKey = 981,
} CloudNo;

typedef enum {
	ParamMode = 0,

	ParamRunTest = 101,
	ParamTestResult = 102,
	ParamTestReason = 103,
	ParamTestReady = 104,
	ParamTestDataPilotA = 105,
	ParamTestDataPilotC = 106,
	ParamTestDataPilotF = 107,

	AuthenticationRequired = 120,

	ParamLinuxStatus = 125,

	ParamStartCalibrations = 130,
	ParamRunCalibration = 131,
	ParamCalibrationSamples = 132,
	ParamCalibrationAveragePhase1 = 133,
	ParamCalibrationAveragePhase2 = 134,
	ParamCalibrationAveragePhase3 = 135,
	ParamCalibrationSetParameter = 136,
	ParamMidStoredCalibrationId = 137,
	ParamContinueCalibration = 138,
	ParamCalibrationReadParameter = 139,
	ParamMidEventLog = 140,

	// TODO: On Go, 140 is MidStatus
	// ParamMidStatus = 140

	// Local settings

	TransmitInterval = 145,
	TransmitChangeLevel = 146,
	PulseInterval = 147,
	CommunicationMode  = 150,
	PermanentCableLock = 151,
	ProductCode = 152,
	HmiBrightness = 153,
	LockCableWhenConnected = 154,
	ParamMIDBlinkEnabled = 155,

	ParamInternalTemperature = 201,
	ParamInternalTemperatureEmeter = 202,
	ParamInternalTemperatureEmeter2 = 204,
	ParamInternalTemperatureEmeter3 = 205,
	ParamInternalTemperatureT = 206,
	ParamInternalTemperatureT2 = 207,

	ParamInternalTemperatureLimit = 241,
	ParamHumidity = 270,
	ParamTamperCover = 280,

	ParamLedOverride = 290,
	ParamLedOverrideClear = 291,

	RCDButtonTestState = 402,
	RCDErrorCount = 403,
	RCDTestState = 404,

	ParamVoltagePhase1 = 501,
	ParamVoltagePhase2 = 502,
	ParamVoltagePhase3 = 503,

	ParamCurrentPhase1 = 507,
	ParamCurrentPhase2 = 508,
	ParamCurrentPhase3 = 509,

	ParamCurrentInMaximum = 510,
	ParamCurrentInMinimum = 511,
	ParamActivePhases = 512,

	ParamTotalChargePower = 513,
	ParamEmeterMinMax = 514,

	Param12VCurrent = 517,
	ParamPowerFactor = 518,
	ParamSetPhases = 519,

	MaxPhases = 520,
	ChargerOfflinePhase = 522,
	ChargerOfflineCurrent = 523,
	/*ParamRcdCurrentMean = 520,
	ParamRcdCurrentPeak = 521,
	ParamRcdCurrentRms = 522,
	ParamRcdCurrentRaw = 523,
	ParamRcdCalibration = 524,
	ParamRcdCalibrationNoise = 525,*/
	ParamOPENVoltage = 526,
	MaxCurrentConfigurationSource = 544,
	SwitchPosition = 545,
	ChargeCurrentInstallationMaxLimit = 546,
	StandAloneCurrent = 547,
	PhaseRotation = 548,

	ParamTotalChargePowerSession = 553,
	SignedMeterValue = 554,
	SignedMeterValueInterval = 555,
	ParamSessionEnergyCountActive = 560,
	ParamSessionEnergyCountReactive = 561,
	ParamSessionEnergyCountImportActive = 562,
	ParamSessionEnergyCountImportReactive = 563,

	ParamCumulativeEnergyUsage = 564,

	ParamSessionEnergyCountImportActiveInterpolated = 565,

	ParamInstantPilotCurrent = 700,
	ParamChargeDuration = 701,
	ParamChargeMode = 702,
	ParamChargePilotLevelInstant = 703,
	ParamChargePilotLevelAverage = 704,
	ParamProximityAnalogValue = 705,
	ParamPilotVsProximityTime = 706,
	//ChargeCurrentInstallationMaxLimit = 707,
	ParamChargeCurrentUserMax = 708,
	ParamSimplifiedModeMaxCurrent = 709,
	//...
	ParamChargeOperationMode = 710,
	ParamIsEnabled = 711,
	ParamIsStandalone = 712,
	ParamCableType = 714,
	ParamNetworkType = 715,
	DetectedCar = 716,
	GridTestResult = 717,
	FinalStopActive = 718,

	SessionIdentifier = 721,

	ChargerCurrentUserUuid = 722,
	CompletedSession = 723,

	ParamIsCalibrationHandle = 724,
	ParamAuthState = 726,//This is numbered 723 on Pro(conflicts with CompletedSession here when using same file for Cloud and MCU numbering)

	DebugCounter = 731,
	ParamWarningValue = 805,
	ParamGridTypeOverride = 733,
	ParamIT3OptimizationEnabled = 734,
	ParamUseZaptecFinishedTimeout = 735,

	NrOfChargeCards = 749,
	NewChargeCard = 750,
	AuthenticationListVersion = 751,
	EnabledNfcTechnologies = 752,
	LteRoamingDisabled = 753,

	/// TimeSchedule observations
	Location = 760,
	TimeZone = 761,
	TimeSchedule = 762,
	NextScheduleEvent = 763,
	MaxStartDelay = 764,

	InstallationId = 800, // String / Guid
	RoutingId = 801, // Int
	ChargePointName = 802, // String

	//...
	Notifications = 803,
	ParamWarnings = 804,

	ParamClearWarning = 806,
	InternalDiagnosticsLog = 807,
	//..
	ParamDiagnosticsString = 808,
	CommunicationSignalStrength = 809,
	CloudConnectionStatus = 810,
	//..
	MCUResetSource = 811,
	MCURxErrors = 812,
	McuToESPPacketErrors  = 813,
	ESPToMcuPacketErrors = 814,
	ESPResetSource = 815,
	MCUFaultPins	= 816,
	RelayStates		= 817,
	ParamAutoClearState = 818,

	UptimeVariscite = 820,
	UptimeMCU = 821,
	DataUsage = 822,
	CertificateVersion = 823,
	//..
	SecurityLog = 830,
	//..
	CarSessionLog = 850,
	CommunicationModeConfigurationInconsistency = 851,
	RawPilotMonitor = 852,
	IT3PhaseDiagnosticsLog = 853,
	PilotTestResults = 854,

        SessionController = 860,
        OcppNativeURL = 861,
        OcppNativeCBID = 862,
        OcppNativeAuthorizationKey = 863, // The key itself should be WRITE only from Zaptec portal and CS(MS)
        OcppNativeAuthorizationKeyFromZaptec = 864,
        OcppNativeSecurityProfile = 865,
        OcppNativeConnected = 866,

	ServoCheckStartPosition = 870,
	ServoCheckStartCurrent  = 871,
	ServoCheckStopPosition 	= 872,
	ServoCheckStopCurrent 	= 873,
	ServoState              = 874,
	ServoMovement   		= 875,

	HWCurrentActiveLimit	= 880,
	HWCurrentMaxLimit    	= 881,

	ProductionTestResults = 900,
	PostProductionTestResults = 901,

	ParamSmartMainboardAppSwVersion = 908,
	ParamSmartMainboardBootSwVersion = 909,
	ParamSmartMainboardHwVersion = 910,

	ParamSmartComputerAppVersion = 911,
	ParamSmartComputerFwLoaderVersion = 912,

	ParamFpgaType = 913,
	ParamFpgaVersion = 914,
	ParamFpgaVersionAndHash = 915,
	ParamFpgaNeedsConfiguration = 916,

	HwIdMCUSpeed = 917,
	HwIdMCUPower = 918,

	MacMain = 950,
	MacWiFi = 952,

	LteImsi = 960,
	LteMsisdn = 961,
	LteIccid = 962,
	LteImei = 963,

	FactoryTestStage = 970,
	FactoryHWTrigResult = 971,

	MIDCalibration = 980,
} ParamNo;


//Custom - MCU Param numbers - Some are overlapping with cloud parameter numbering
typedef enum
{
	ParamEmeterAlarm = 809,
} MCUParamNo;


//Cloud command numbers
typedef enum
{
	CommandSwReboot    = 102,
	CommandReset       = 103,
	CommandGlobalReset = 104,

	CommandSerialRedirect = 110,

	CommandMidInitDefaultCalibration = 120,
	CommandMidInitCalibration = 121,
	CommandMidClearCalibration = 122,
	CommandMidMarkCalibrationVerified = 123,

	CommandUpgradeFirmware = 200,
	CommandUpgradeFirmwareForced = 201,
	CommandUpgradeMcuFirmware = 204,
	CommandHostFwUpdateStart = 205,
	CommandFpgaFwUpdateStart = 206,
	CommandHostFwUpdateEnd = 207,

	CommandResetCommsErrors = 260,
	CommandResetNotifications = 261,
	CommandResetWarnings = 262,
	CommandSetVarisciteWarning = 263,

	CommandVoltageSnapshot = 300,
	CommandCurrentSnapshot = 301,
	CommandActivePowerSnapshot = 302,

	ParamEmeterVoltageSnapshotSlot = 499,
	ParamEmeterVoltageSnapshot = 500,

	CommandStartCharging = 501,
	CommandStopCharging = 502,
	CommandResetSession = 505,
	CommandSetFinished = 506,
	CommandResumeChargingESP = 507,
	CommandStopChargingFinal = 508,
	CommandResumeChargingMCU = 509,

	CommandAuthorizationGranted = 601,
	CommandAuthorizationDenied = 602,
	CommandIndicateAppConnect = 603,
	CommandIndicateDisabled = 604,
	CommandIndicateOffline = 605,
	CommandIndicateOk = 606,
	CommandBlinkWhite = 607,
	CommandEnterProductionMode = 701,
	CommandServoClearCalibration = 702,
	CommandTestHWTrig = 705,
	CommandOpenPENRelay = 706,
	CommandClosePENRelay = 707,
	CommandServoForceUnlock = 708,

	CommandFactoryReset = 710,
	CommandRunGridTest = 804,
	CommandITDiagnosticsStart = 806,
	CommandITDiagnosticsStop = 807,
	CommandRunRCDTest = 808,
	CommandActivateWatchdog = 810,
	CommandStartServoCheck = 811,
	CommandGetOPENSamples = 812,

	CommandDisableCertificateOnce = 820,
	CommandDisableCertificateAlways = 821,

	CommandFpgaBitstreamData = 916,
} CommandNo;


//Custom - MCU Command numbers - Some are overlapping with cloud command numbering
typedef enum
{
	MCUCommandRestartCarInterface = 507,
} MCUCommandNo;


typedef enum
{
	MsgRead = 10,
	MsgReadGroup = 11,
	MsgReadAck = 12,
	MsgWrite = 20,
	MsgWriteAck = 21,
	MsgCommand = 30,
	MsgCommandAck = 31,
	MsgFirmware = 40,
	MsgFirmwareAck = 41
} MessageType;

typedef struct
{
	MessageType type;
	uint16_t timeId;
	uint16_t identifier;
	uint16_t length;
	uint8_t data[128];
} ZapMessage;


uint32_t GetPacketFramingErrors();
uint32_t GetPacketLengthErrors();
uint32_t GetPacketChecksumErrors();
uint32_t GetCompletedPackets();
bool ZParseFrame(uint8_t nextRxByte, ZapMessage* outMsg);
uint16_t ZEncodeMessageHeader(const ZapMessage* msg, uint8_t* begin);
uint16_t ZAppendChecksumAndStuffBytes(uint8_t* startOfMsg, uint16_t lengthOfMsg, uint8_t* outByteStuffedMsg);
uint16_t ZEncodeMessageHeaderOnly(ZapMessage* msg, uint8_t* txBuf, uint8_t* encodedTxBuf);
uint16_t ZEncodeMessageHeaderAndOneFloat(ZapMessage* msg, float val, uint8_t* txBuf, uint8_t* encodedTxBuf);
uint16_t ZEncodeMessageHeaderAndOneByte(ZapMessage* msg, uint8_t val, uint8_t* txBuf, uint8_t* encodedTxBuf);
uint16_t ZEncodeMessageHeaderAndOneUInt16(ZapMessage* msg, uint16_t val, uint8_t* txBuf, uint8_t* encodedTxBuf);
uint16_t ZEncodeMessageHeaderAndOneUInt32(ZapMessage* msg, uint32_t val, uint8_t* txBuf, uint8_t* encodedTxBuf);
uint16_t ZEncodeMessageHeaderAndByteArray(ZapMessage* msg, const char* array, size_t length, uint8_t* txBuf, uint8_t* encodedTxBuf);
uint16_t ZEncodeMessageHeaderAndOneString(ZapMessage* msg, const char* str, uint8_t* txBuf, uint8_t* encodedTxBuf);

uint16_t ZEncodeAck(const ZapMessage* request, uint8_t errorCode, uint8_t* txBuf, uint8_t* encodedTxBuf);

uint16_t ZEncodeMessageHeaderAndByteArrayNoCheck(ZapMessage* msg, const char* array, size_t length, uint8_t* txBuf, uint8_t* encodedTxBuf);
void ZEncodeFirmwareAckHasLength(bool hasLength);

static unsigned int __attribute__ ((unused)) ZEncodeUint8(uint8_t var, uint8_t* data) { data[0] = var; return 1;}
static unsigned int __attribute__ ((unused)) ZEncodeUint16(uint16_t var, uint8_t* data) { data[0] = var >> 8; data[1] = var; return 2;}
static unsigned int __attribute__ ((unused)) ZEncodeUint32(uint32_t var, uint8_t* data) { data[0] = var >> 24; data[1] = var >> 16; data[2] = var >> 8; data[3] = var; return 4; }
static unsigned int __attribute__ ((unused)) ZEncodeInt16(int16_t var, uint8_t* data) { data[0] = var >> 8; data[1] = var; return 2;}
static unsigned int __attribute__ ((unused)) ZEncodeFloat(float var, uint8_t* data) { data[0] = (*(uint32_t*)&var) >> 24; data[1] = (*(uint32_t*)&var) >> 16; data[2] = (*(uint32_t*)&var) >> 8; data[3] = *(uint32_t*)&var; return 4;}
static uint32_t __attribute__ ((unused)) ZDecodeUint32(const uint8_t* data) { return ((uint32_t)data[0] << 24) + ((uint32_t)data[1] << 16) + (data[2] << 8) + data[3]; }
static uint16_t __attribute__ ((unused)) ZDecodeUint16(const uint8_t* data) { return ((data[0] << 8) & 0xFF00) + data[1]; }
static uint8_t __attribute__ ((unused)) ZDecodeUInt8(const uint8_t* data) { return data[0]; }
static int16_t __attribute__ ((unused)) ZDecodeInt16(const uint8_t* data) { return ((data[0] << 8) & 0xFF00) + data[1]; }
static float __attribute__ ((unused)) ZDecodeFloat(const uint8_t* data)
{
	uint32_t ret = (((uint32_t)data[0] << 24) & 0xff000000) + (((uint32_t)data[1] << 16) & 0x00ff0000) + (((uint32_t)data[2] << 8) & 0x0000ff00) + ((uint32_t)data[3] & 0x000000ff);
	return *(float*)&ret;
}

#endif /* ZAPTEC_PROTOCOL_SERIALISATION_H */
