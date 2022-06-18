#ifndef __DALI_H
#define __DALI_H 1

typedef byte daliAddr;

typedef enum {
  priTxn = 1, // "used for all forward frames within a transaction [...] except for the first"
  priUser,    // "used to execute user instigated actions" (also "might also be" addressing)
  priConfig,  // "used for configuration of a bus unit" (also events that aren't User or Auto)
  priAuto,    // "used to execute automatic actions"
  priQuery,   // "used for periodic query commands"
} daliPri;

typedef enum {
  msgOff = 0x00, 
  msgUp,
  msgDown,
  msgStepUp,
  msgStepDown,
  msgRecallMax,
  msgRecallMin,
  msgStepDownOff,
  msgOnStepUp,
  msgEnableDapcSeq,
  msgGoToLastActiveLevel, // v2

  msgGoToScene = 0x10, // ...and up to 0x1f, for different scenes.

  msgReset = 0x20,
  msgStoreActualLevelDtr0,
  msgSavePersistentVars, // v2
  msgSetOperatingMode, // v2
  msgResetMemoryBank, // v2
  msgIdentifyDevice, // v2

  msgSetMaxLevel = 0x2a,
  msgSetMinLevel,
  msgSetSystemFailureLevel,
  msgSetPowerOnLevel,
  msgSetFadeTime,
  msgSetFadeRate,
  msgSetExtendedFadeTime, // v2

  msgSetScene = 0x40, // ...and up to 0x4f, for different scenes.

  msgRemoveFromScene = 0x50, // ...and up to 0x5f, for different scenes.

  msgAddToGroup = 0x60, // ...and up to 0x6f, for different groups.

  msgRemoveFromGroup = 0x70, // ...and up to 0x7f, for different groups.

  msgSetShortAddr = 0x80,
  msgEnableWriteMemory,

  msgQueryStatus = 0x90,
  msgQueryControlGearPresent,
  msgQueryLampFailure,
  msgQueryLampPowerOn,
  msgQueryLimitError,
  msgQueryResetState,
  msgQueryMissingShortAddr,
  msgQueryVersionNo,
  msgQueryContentDtr0,
  msgQueryDeviceType,
  msgQueryPhysicalMin,
  msgQueryPowerFailure,
  msgQueryContentDtr1,
  msgQueryContentDtr2,
  msgQueryOperatingMode, // v2
  msgQueryLightSourceType, // v2

  msgQueryActualLevel,
  msgQueryMaxLevel,
  msgQueryMinLevel,
  msgQueryPowerOnLevel,
  msgQuerySystemFailureLevel,
  msgQueryFadeTimeRate,
  msgQueryMfrSpecificMode, // v2
  msgQueryNextDeviceType, // v2
  msgQueryExtendedFadeTime, // v2
  msgQueryControlGearFailure = 0xaa, // v2

  msgQuerySceneLevel = 0xb0, // ...and up to 0xbf, for different scenes.

  msgQueryGroup0_7 = 0xc0,
  msgQueryGroup8_15,
  msgQueryRandomAddrH,
  msgQueryRandomAddrM,
  msgQueryRandomAddrL,
  msgReadMemoryLoc,

  msgAppExtCmdBase = 0xe0,
} daliMsg;

typedef enum {
  stIdle,
  stSending,
  stStartBitH1,
  stStartBitH2,
  stFirstHalf,
  stSecondHalf,
  stFrameReady,
} daliState;

typedef enum {
  tiTooShort,
  tiHalfBit,
  tiInvalid,
  ti2HalfBits,
  tiTooLong,
} daliTime;

typedef enum {
  eNoError,
  eWaitPri,
  eSendStartBit,
  eSendAddr,
  eSendMsg,
  eSendStop,
  eNoDevices,
  eBadBackFrame,
  eNoVerifyAns,
  eBadVerifyAns,
} daliError;

typedef enum {
  rNoFrame,
  rBadFrame,
  rGoodFrame,
} daliRcvStatus;

class Dali {
public:
  Dali(int pinIn, int pinOut);
  void init();
  void log(const char* fmt, ...);
  bool sendReset(daliAddr addr);
  bool sendLampOff(daliAddr addr, bool fromUser);
  bool sendStepDownOff(daliAddr addr, bool fromUser);
  bool sendOnStepUp(daliAddr addr, bool fromUser);
  bool sendDapc(daliAddr addr, bool fromUser, byte level);
  bool sendSetPowerOnLevel(daliAddr addr, bool fromUser, byte level);
  int queryMinLevel(daliAddr addr, bool fromUser);
  int queryMaxLevel(daliAddr addr, bool fromUser);
  int queryActualLevel(daliAddr addr, bool fromUser);
  int queryPowerOnLevel(daliAddr addr, bool fromUser);
  daliError getError(void);
  daliAddr *reAddressLamps(byte *num);
  const char* getLogBuf(void);

  static const daliAddr broadcast;
private:
  static void inputISR(void);
  static void timerISR(void);
  static Dali *d;

  static const daliAddr addrTerminate;
  static const daliAddr addrDTR0;
  static const daliAddr addrInitialise;
  static const daliAddr addrRandomise;
  static const daliAddr addrCompare;
  static const daliAddr addrWithdraw;
  static const daliAddr addrPing;

  static const daliAddr addrSearchAddrH;
  static const daliAddr addrSearchAddrM;
  static const daliAddr addrSearchAddrL;
  static const daliAddr addrProgramShortAddr;
  static const daliAddr addrVerifyShortAddr;
  static const daliAddr addrQueryShortAddr;

  static const daliAddr addrEnableDeviceType;
  static const daliAddr addrDTR1;
  static const daliAddr addrDTR2;
  static const daliAddr addrWriteMemLoc;
  static const daliAddr addrWriteMemLocNoReply;

  void setError(daliError e);
  daliTime getBitTime(void);
  void addBit(bool bit);
  void daliIdle(void);
  void daliHigh(void);
  void daliLow(void);
  bool sendBit(bool b);
  bool sendStopBit(void);
  bool sendByte(byte b);
  void delaySinceLow(unsigned long wait);
  bool waitPriority(daliPri priority);
  bool sendForwardMessage(daliPri priority, daliAddr addr, daliMsg data);
  bool sendCommand(daliPri priority, daliAddr addr, daliMsg cmd);
  daliRcvStatus receiveFrame(byte bits, byte timeoutMs);
  daliRcvStatus receiveBackwardFrame(void);
  bool findDevice(uint32 min, uint32 max, byte shortAddr);
  int queryLevel(daliAddr addr, bool fromUser, daliMsg query);

  char* logBuf;
  char* logPtr;
  void resetEdgeLog(void);
  void logEdge(unsigned long t, bool v, daliState s);
  void dumpEdgeLog(const char *tag);
  unsigned long* edgeTimes;
  bool* edgeDests;
  daliState* edgeStates;
  int nEdges;

  byte lastLevel;
  int pinIn;
  int pinOut;
  byte rcvdBits;
  unsigned long rcvdVal;
  daliError err;
  volatile unsigned long lastDaliHigh;
  volatile unsigned long lastDaliLow;
  volatile daliState state;
};

#endif
