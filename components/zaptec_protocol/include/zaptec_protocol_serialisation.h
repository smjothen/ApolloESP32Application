// this file is based on https://github.com/ZaptecCharger/ZapChargerProMCU/blob/a202f6e862b1c9914f0e06d1aae4960ef60af998/smart/smart/src/zapProtocol.h
#ifndef ZAPTEC_PROTOCOL_SERIALISATION_H
#define ZAPTEC_PROTOCOL_SERIALISATION_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Hack for not overflowing protocol buffers
#define ZAP_PROTOCOL_BUFFER_SIZE             128
#define ZAP_PROTOCOL_BUFFER_SIZE_ENCODED     (ZAP_PROTOCOL_BUFFER_SIZE + 1 /* overhead byte */ + 1 /* delimiter byte */ + 0 /* ~one byte overhead per 256 bytes */ + 5 /*extra*/)
#define ZAP_PROTOCOL_MAX_DATA_LENGTH         (ZAP_PROTOCOL_BUFFER_SIZE - 7 /* worst-case header */ - 2 /* checksum */ - 5 /*extra*/)
    
    typedef enum
    {
        ParamMode = 0,
                
        ParamProductCode = 99,
        ParamSerialNumber = 100,
                
        ParamRunTest = 101,
        ParamTestResult = 102,
        ParamTestReason = 103,
        ParamTestReady = 104,
        ParamTestDataPilotA = 105,
        ParamTestDataPilotC = 106,
        ParamTestDataPilotF = 107,

		AuthenticationRequired = 120,

        ParamLinuxStatus = 125,

		// Local settings

		TransmitInterval = 145,
		TransmitChangeLevel = 146,

		CommunicationMode  = 150,
		PermanentCableLock = 151,
		ProductCode = 152,
		HmiBrightness = 153,
		LockCableWhenConnected = 154,
        
        ParamInternalTemperature = 201,
        ParamInternalTemperatureEmeter = 202,
		ParamInternalTemperatureEmeter2 = 204,
		ParamInternalTemperatureEmeter3 = 205,
		ParamInternalTemperatureT = 206,
		ParamInternalTemperatureT2 = 207,

        ParamInternalTemperatureLimit = 241,
        ParamHumidity = 270,
                
        ParamGridDetectionResult = 400,
        ParamGridDetectionStart = 401,
                
        ParamEmeterVoltageSnapshotSlot = 499,
        ParamEmeterVoltageSnapshot = 500,

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
                
        ParamRelayState = 530,
        ParamSoftStartDuration = 531,
        ParamRelayStep = 532,

        ParamTotalChargePowerSession = 553,
        
        ParamSessionEnergyCountActive = 560,
        ParamSessionEnergyCountReactive = 561,
        ParamSessionEnergyCountImportActive = 562,
        ParamSessionEnergyCountImportReactive = 563,

        ParamChargeDuration = 701,
        ParamChargeMode = 702,
        ParamChargePilotLevelInstant = 703,
        ParamChargePilotLevelAverage = 704,
        ParamProximityAnalogValue = 705,
        ParamPilotVsProximityTime = 706,
        //...
        ParamChargeCurrentUserMax = 708,
        ParamSimplifiedModeMaxCurrent = 709,
        //...
        ParamChargeOperationMode = 710,
        ParamIsEnabled = 711,
        ParamIsStandalone = 712,
        ParamCableType = 714,
        ParamNetworkType = 715,
        ParamPermanentCableLock = 716,
        ParamLockCableWhenConnected = 717,
        ParamSoftStartDisabled = 718,
        ParamAllowedNeutralCurrent = 719,
                
        ParamSessionChargeCycle = 720,
        ParamMaxSessionRestart = 721,

        ChargerCurrentUserUuid = 722,
        CompletedSession = 723,

		SwitchPosition = 730,
		DebugCounter = 731,

        ParamHmiBrightness = 800,
        ParamAveraging = 801,
        //...
        ParamWarnings = 804,
        ParamWarningValue = 805,
        ParamClearWarning = 806,
        ParamFlags = 807,
        //..
        ParamDiagnosticsString = 808,
		CommunicationSignalStrength = 809,
        //..
        ParamResetSource = 811,
        ParamRxErrors = 812,
        ParamPacketErrors = 813,
        ESPToMcuPacketErrors = 814,
		ESPResetSource = 815,
        //..

		ParamSmartMainboardAppSwVersion = 908,
		ParamSmartMainboardBootSwVersion = 909,
		ParamSmartMainboardHwVersion = 910,

		ParamSmartComputerAppVersion = 911,
		ParamSmartComputerFwLoaderVersion = 912,
		ParamSmartComputerImageVersion = 913,

		ParamSmartFpgaVersion = 914,
        ParamSmartFpgaVersionAndHash = 915,

    } ParamNo;

    typedef enum
    {
        CommandSwReboot    = 102,
        CommandReset       = 103,
        CommandGlobalReset = 104,
                
        CommandSerialRedirect = 110,

        CommandUpgradeMcuFirmware = 204,
        CommandHostFwUpdateStart = 205,
		CommandFpgaFwUpdateStart = 206,

        CommandResetCommsErrors = 260,
        CommandResetNotifications = 261,
        CommandResetWarnings = 262,
        CommandSetVarisciteWarning = 263,

        CommandVoltageSnapshot = 300,
                
        CommandStartCharging = 501,
        CommandStopCharging = 502,
		CommandResetSession = 505,
		CommandSetFinished = 506,
        CommandRestartCarInterface = 507,
                
        CommandAuthorizationGranted = 601,
        CommandAuthorizationDenied = 602,
        CommandIndicateAppConnect = 603,
        CommandIndicateDisabled = 604,
        CommandIndicateOffline = 605,

    } CommandNo;

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
        uint8_t* data;
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
    uint16_t ZEncodeMessageHeaderAndOneString(ZapMessage* msg, const char* str, uint8_t* txBuf, uint8_t* encodedTxBuf);

    uint16_t ZEncodeAck(const ZapMessage* request, uint8_t errorCode, uint8_t* txBuf, uint8_t* encodedTxBuf);

//#ifdef __cplusplus
//}
//#endif

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
