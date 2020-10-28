// this file is based on  https://github.com/ZaptecCharger/ZapChargerProMCU/blob/a202f6e862b1c9914f0e06d1aae4960ef60af998/smart/smart/src/zapProtocol.c
#include "zaptec_protocol_serialisation.h"
#include "string.h"

static uint32_t packetChecksumErrors = 0;
static uint32_t packetFramingErr = 0;
static uint32_t packetInvalidLength = 0;
static uint32_t completedPackets = 0;

const uint16_t crcTable[] =
{
    0X0000, 0XC0C1, 0XC181, 0X0140, 0XC301, 0X03C0, 0X0280, 0XC241,
    0XC601, 0X06C0, 0X0780, 0XC741, 0X0500, 0XC5C1, 0XC481, 0X0440,
    0XCC01, 0X0CC0, 0X0D80, 0XCD41, 0X0F00, 0XCFC1, 0XCE81, 0X0E40,
    0X0A00, 0XCAC1, 0XCB81, 0X0B40, 0XC901, 0X09C0, 0X0880, 0XC841,
    0XD801, 0X18C0, 0X1980, 0XD941, 0X1B00, 0XDBC1, 0XDA81, 0X1A40,
    0X1E00, 0XDEC1, 0XDF81, 0X1F40, 0XDD01, 0X1DC0, 0X1C80, 0XDC41,
    0X1400, 0XD4C1, 0XD581, 0X1540, 0XD701, 0X17C0, 0X1680, 0XD641,
    0XD201, 0X12C0, 0X1380, 0XD341, 0X1100, 0XD1C1, 0XD081, 0X1040,
    0XF001, 0X30C0, 0X3180, 0XF141, 0X3300, 0XF3C1, 0XF281, 0X3240,
    0X3600, 0XF6C1, 0XF781, 0X3740, 0XF501, 0X35C0, 0X3480, 0XF441,
    0X3C00, 0XFCC1, 0XFD81, 0X3D40, 0XFF01, 0X3FC0, 0X3E80, 0XFE41,
    0XFA01, 0X3AC0, 0X3B80, 0XFB41, 0X3900, 0XF9C1, 0XF881, 0X3840,
    0X2800, 0XE8C1, 0XE981, 0X2940, 0XEB01, 0X2BC0, 0X2A80, 0XEA41,
    0XEE01, 0X2EC0, 0X2F80, 0XEF41, 0X2D00, 0XEDC1, 0XEC81, 0X2C40,
    0XE401, 0X24C0, 0X2580, 0XE541, 0X2700, 0XE7C1, 0XE681, 0X2640,
    0X2200, 0XE2C1, 0XE381, 0X2340, 0XE101, 0X21C0, 0X2080, 0XE041,
    0XA001, 0X60C0, 0X6180, 0XA141, 0X6300, 0XA3C1, 0XA281, 0X6240,
    0X6600, 0XA6C1, 0XA781, 0X6740, 0XA501, 0X65C0, 0X6480, 0XA441,
    0X6C00, 0XACC1, 0XAD81, 0X6D40, 0XAF01, 0X6FC0, 0X6E80, 0XAE41,
    0XAA01, 0X6AC0, 0X6B80, 0XAB41, 0X6900, 0XA9C1, 0XA881, 0X6840,
    0X7800, 0XB8C1, 0XB981, 0X7940, 0XBB01, 0X7BC0, 0X7A80, 0XBA41,
    0XBE01, 0X7EC0, 0X7F80, 0XBF41, 0X7D00, 0XBDC1, 0XBC81, 0X7C40,
    0XB401, 0X74C0, 0X7580, 0XB541, 0X7700, 0XB7C1, 0XB681, 0X7640,
    0X7200, 0XB2C1, 0XB381, 0X7340, 0XB101, 0X71C0, 0X7080, 0XB041,
    0X5000, 0X90C1, 0X9181, 0X5140, 0X9301, 0X53C0, 0X5280, 0X9241,
    0X9601, 0X56C0, 0X5780, 0X9741, 0X5500, 0X95C1, 0X9481, 0X5440,
    0X9C01, 0X5CC0, 0X5D80, 0X9D41, 0X5F00, 0X9FC1, 0X9E81, 0X5E40,
    0X5A00, 0X9AC1, 0X9B81, 0X5B40, 0X9901, 0X59C0, 0X5880, 0X9841,
    0X8801, 0X48C0, 0X4980, 0X8941, 0X4B00, 0X8BC1, 0X8A81, 0X4A40,
    0X4E00, 0X8EC1, 0X8F81, 0X4F40, 0X8D01, 0X4DC0, 0X4C80, 0X8C41,
    0X4400, 0X84C1, 0X8581, 0X4540, 0X8701, 0X47C0, 0X4680, 0X8641,
    0X8201, 0X42C0, 0X4380, 0X8341, 0X4100, 0X81C1, 0X8081, 0X4040
};

uint32_t GetPacketFramingErrors()
{
    return packetFramingErr;
}

uint32_t GetPacketLengthErrors()
{
    return packetInvalidLength;
}

uint32_t GetPacketChecksumErrors()
{
    return packetChecksumErrors;
}

uint32_t GetCompletedPackets()
{
    return completedPackets;
}

uint16_t checksum(uint8_t* data, uint16_t length)
{
    uint16_t crc = 0xffff;

    int i;
    for (i = 0; i < length; i++)
    {
        uint8_t tableIndex = (uint8_t)(crc ^ data[i]);
        crc >>= 8;
        crc ^= crcTable[tableIndex];
    }

 //   static unsigned int simerrctr = 0;
//    if (++simerrctr % 10 == 0)
//        crc++; // TODO !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! REMOVE

    return crc;
}

uint8_t frameBuffer[128]; // TODO sun - check with JH, enough?
ZapMessage msg = { 0, 0, 0, 0, 0 };

/*
* StuffData byte stuffs "length" bytes of
* data at the location pointed to by "ptr",
* writing the output to the location pointed
* to by "dst".
*/

#define FinishBlock(X) (*code_ptr = (X), code_ptr = dst++, code = 0x01)

int StuffData(const unsigned char *ptr,
    unsigned long length, unsigned char *dst)
{
    const unsigned char* dstBegin = dst;
    const unsigned char *end = ptr + length;
    unsigned char *code_ptr = dst++;
    unsigned char code = 0x01;

    while (ptr < end)
    {
        if (*ptr == 0)
            FinishBlock(code);
        else
        {
            *dst++ = *ptr;
            if (++code == 0xFF)
                FinishBlock(code);
        }
        ptr++;
    }

    FinishBlock(code);

    return dst - dstBegin - 1;
}

uint16_t ZAppendChecksumAndStuffBytes(uint8_t* startOfMsg, uint16_t lengthOfMsg, uint8_t* outByteStuffedMsg)
{
    // Append checksum
    lengthOfMsg += ZEncodeUint16(checksum(startOfMsg, lengthOfMsg), startOfMsg + lengthOfMsg);
    
    // Perform COBS-encoding
int lengthOfEncodedMsg = StuffData(startOfMsg, lengthOfMsg, outByteStuffedMsg);
    outByteStuffedMsg[lengthOfEncodedMsg++] = 0; // Delimiter byte
    
    return lengthOfEncodedMsg;
}

uint16_t ZEncodeMessageHeader(const ZapMessage* msg, uint8_t* begin)
{
    uint8_t* ptr = begin;
    *(ptr++) = msg->type;

    switch (msg->type)
    {
    case MsgRead:
    case MsgReadGroup:
    case MsgFirmwareAck:
        ZEncodeUint16(msg->timeId, ptr);
        ptr += 2;
        ZEncodeUint16(msg->identifier, ptr);
        ptr += 2;
    break;

    case MsgReadAck:
    case MsgCommand:
    case MsgWrite:
    case MsgWriteAck:
    case MsgCommandAck:
    case MsgFirmware:
        ZEncodeUint16(msg->timeId, ptr);
        ptr += 2;
        ZEncodeUint16(msg->identifier, ptr);
        ptr += 2;
        ZEncodeUint16(msg->length, ptr);
        ptr += 2;
    break;

    default:
        break;
    }

    return (ptr - begin);
}

/*
* Defensive UnStuffData, which prevents poorly
* conditioned data at *ptr from over-running
* the available buffer at *dst.
*/

int UnStuffData(const unsigned char *ptr,
    unsigned long length, unsigned char *dst)
{
    const unsigned char* dstBegin = dst;
    const unsigned char *end = ptr + length;
    while (ptr < end)
    {
        int i, code = *ptr++;
        for (i = 1; ptr < end && i < code; i++)
            *dst++ = *ptr++;

        if (ptr >= end && i < code)
            return 0;

        if (code < 0xFF)
            *dst++ = 0;
    }

    return dst - dstBegin;
}

bool ZParseFrame(uint8_t nextChar, ZapMessage* outMsg)
{
    static int encIdx = 0;
    static uint8_t encodedFrameBuffer[128];
    
    if (nextChar != 0)
    {
        if(encIdx < 128)
            encodedFrameBuffer[encIdx++] = nextChar;
        
        return false;
    }
    else if (encIdx == 0)
    {
        return false;
    }
    else
    {        
        int frameLength = UnStuffData(encodedFrameBuffer, encIdx, frameBuffer) - 1;

        if (frameLength < 0)
        {
            packetFramingErr++;
            encIdx = 0;
#ifdef DEBUG_SERIAL_PROTOCOL
            printf("\r\n[SERIAL] Framing error");
#endif
            return false;
        }
        else if (frameLength < 7)
        {
#ifdef DEBUG_SERIAL_PROTOCOL
            printf("\r\n[SERIAL] Packet length");
#endif
            packetInvalidLength++;
            encIdx = 0;
            return false;
        }

        uint8_t* ptr = frameBuffer;
        msg.type = *ptr;
        ptr += 1;
        msg.timeId = ZDecodeUint16(ptr);
        ptr += 2;
        msg.identifier = ZDecodeUint16(ptr);
        ptr += 2;

        if (msg.type == MsgReadAck || msg.type == MsgWrite || msg.type == MsgWriteAck || msg.type == MsgCommandAck || msg.type == MsgCommand || msg.type == MsgFirmware)
        {
            msg.length = ZDecodeUint16(ptr);
            ptr += 2;

            msg.data = ptr;
            ptr += msg.length;

            if (frameLength < 7 + 2 + msg.length)
            {
#ifdef DEBUG_SERIAL_PROTOCOL
            printf("\r\n[SERIAL] Packet length");
#endif
                packetInvalidLength++;
                encIdx = 0;
                return false;
            }
        }
        else if (msg.type == MsgFirmwareAck)
        {
        	msg.length = 1;
        	msg.data = ptr++;
        }
        

        uint16_t receivedChecksum = ZDecodeUint16(ptr);
        ptr += 2;
        uint16_t checkSum = checksum(frameBuffer, frameLength - 2);

        if (receivedChecksum == checkSum)
        {
            *outMsg = msg;
            encIdx = 0;
            completedPackets++;
#ifdef DEBUG_SERIAL_PROTOCOL
            printf("\r\n[SERIAL] Packet: %i", msg.identifier);
#endif
            return true;
        }
        else
        {
#ifdef DEBUG_SERIAL_PROTOCOL
            printf("\r\n[SERIAL] Checksum error");
#endif
            packetChecksumErrors++;
            encIdx = 0;
            return false;
        }
    }
}

uint16_t ZEncodeMessageHeaderOnly(ZapMessage* msg, uint8_t* txBuf, uint8_t* encodedTxBuf)
{
    uint8_t* ptr = txBuf;

    msg->length = 0;
    ptr += ZEncodeMessageHeader(msg, txBuf);
    //ptr += ZEncodeFloat(val, ptr);
    return ZAppendChecksumAndStuffBytes(txBuf, ptr - txBuf, encodedTxBuf);
}


uint16_t ZEncodeMessageHeaderAndOneFloat(ZapMessage* msg, float val, uint8_t* txBuf, uint8_t* encodedTxBuf)
{
    uint8_t* ptr = txBuf;

    msg->length = 4;
    ptr += ZEncodeMessageHeader(msg, txBuf);
    ptr += ZEncodeFloat(val, ptr);
    return ZAppendChecksumAndStuffBytes(txBuf, ptr - txBuf, encodedTxBuf);
}

uint16_t ZEncodeMessageHeaderAndOneByte(ZapMessage* msg, uint8_t val, uint8_t* txBuf, uint8_t* encodedTxBuf)
{
    uint8_t* ptr = txBuf;

    msg->length = 1;
    ptr += ZEncodeMessageHeader(msg, txBuf);
    ptr += ZEncodeUint8(val, ptr);
    return ZAppendChecksumAndStuffBytes(txBuf, ptr - txBuf, encodedTxBuf);
}

uint16_t ZEncodeMessageHeaderAndOneUInt16(ZapMessage* msg, uint16_t val, uint8_t* txBuf, uint8_t* encodedTxBuf)
{
    uint8_t* ptr = txBuf;

    msg->length = 2;
    ptr += ZEncodeMessageHeader(msg, txBuf);
    ptr += ZEncodeUint16(val, ptr);
    return ZAppendChecksumAndStuffBytes(txBuf, ptr - txBuf, encodedTxBuf);
}

uint16_t ZEncodeMessageHeaderAndOneUInt32(ZapMessage* msg, uint32_t val, uint8_t* txBuf, uint8_t* encodedTxBuf)
{
    uint8_t* ptr = txBuf;

    msg->length = 4;
    ptr += ZEncodeMessageHeader(msg, txBuf);
    ptr += ZEncodeUint32(val, ptr);
    return ZAppendChecksumAndStuffBytes(txBuf, ptr - txBuf, encodedTxBuf);
}

uint16_t ZEncodeMessageHeaderAndByteArray(ZapMessage* msg, const char* array, size_t length, uint8_t* txBuf, uint8_t* encodedTxBuf)
{
    uint8_t* ptr = txBuf;
    
    // Bad hack to prevent buffer overflows from long strings/arrays
    if(length > ZAP_PROTOCOL_MAX_DATA_LENGTH) {
        length = ZAP_PROTOCOL_MAX_DATA_LENGTH;
    }
    
    msg->length = length;
    ptr += ZEncodeMessageHeader(msg, txBuf);
    memcpy(ptr, array, msg->length);
    ptr += msg->length;
    return ZAppendChecksumAndStuffBytes(txBuf, ptr - txBuf, encodedTxBuf);
}

uint16_t ZEncodeMessageHeaderAndOneString(ZapMessage* msg, const char* str, uint8_t* txBuf, uint8_t* encodedTxBuf)
{
    size_t length = strlen(str);
    return ZEncodeMessageHeaderAndByteArray(msg, str, length, txBuf, encodedTxBuf);
}

uint16_t ZEncodeAck(const ZapMessage* request, uint8_t errorCode, uint8_t* txBuf, uint8_t* encodedTxBuf)
{
    ZapMessage reply;
    uint8_t* ptr = txBuf;

    if (request->type == MsgWrite)
        reply.type = MsgWriteAck;

    if (request->type == MsgCommand)
        reply.type = MsgCommandAck;
    
    if (request->type == MsgFirmware)
        reply.type = MsgFirmwareAck;

    reply.timeId = request->timeId;
    reply.identifier = request->identifier;

    reply.length = 1;
    ptr += ZEncodeMessageHeader(&reply, txBuf);
    ptr += ZEncodeUint8(errorCode, ptr);
    return ZAppendChecksumAndStuffBytes(txBuf, ptr - txBuf, encodedTxBuf);
}

