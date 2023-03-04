#include "Arduino.h"
#include "PolledTimeout.h"
#include "dali.h"

// The times below are 30us more generous than the standard.  The slow zener diode usually means
// we end up at the long end for high halfbits and the short end for low halfbits.
#define DALI_HB_MIN 303 // half-bit
#define DALI_HB_MAX 530
#define DALI_2HB_MIN 636 // 2 half-bits
#define DALI_2HB_MAX 1030
#define DALI_HB_NOM 416 // Nominal
#define STOP_BIT_TICKS 750  // 750 * 3.2us = 2400us = stop bit time
#define DALI_HIGH() digitalWrite(this->pinOut, LOW)
#define DALI_LOW() digitalWrite(this->pinOut, HIGH)
#define LOG_SIZE 4096


// These are all "special" addresses. They're outside the range of normal short addresses
// and are (largely) used for sending commands with data.  Essentially, for those commands,
// the address is the opcode and the opcode byte is used for data.
const daliAddr Dali::broadcast              = (daliAddr)0xFF;

const daliAddr Dali::addrTerminate          = (daliAddr)0xa1;
const daliAddr Dali::addrDTR0               = (daliAddr)0xa3;
const daliAddr Dali::addrInitialise         = (daliAddr)0xa5;
const daliAddr Dali::addrRandomise          = (daliAddr)0xa7;
const daliAddr Dali::addrCompare            = (daliAddr)0xa9;
const daliAddr Dali::addrWithdraw           = (daliAddr)0xab;
const daliAddr Dali::addrPing               = (daliAddr)0xad;

const daliAddr Dali::addrSearchAddrH        = (daliAddr)0xb1;
const daliAddr Dali::addrSearchAddrM        = (daliAddr)0xb3;
const daliAddr Dali::addrSearchAddrL        = (daliAddr)0xb5;
const daliAddr Dali::addrProgramShortAddr   = (daliAddr)0xb7;
const daliAddr Dali::addrVerifyShortAddr    = (daliAddr)0xb9;
const daliAddr Dali::addrQueryShortAddr     = (daliAddr)0xbb;

const daliAddr Dali::addrEnableDeviceType   = (daliAddr)0xc1;
const daliAddr Dali::addrDTR1               = (daliAddr)0xc3;
const daliAddr Dali::addrDTR2               = (daliAddr)0xc5;
const daliAddr Dali::addrWriteMemLoc        = (daliAddr)0xc7;
const daliAddr Dali::addrWriteMemLocNoReply = (daliAddr)0xc7;


Dali *Dali::d;

// The input from the device is received as level-change interrupts.
// Additionally, the timeout while waiting for a stop bit is received as a timer interrupt.
// When handling interrupts, the flash may be otherwise occupied, so both the ...ISR() functions below
// as well as the (small) tree of functions they can be called by is marked with IRAM_ATTR, which
// indicates they should be kept in RAM.

void IRAM_ATTR Dali::inputISR(void) {
  if (digitalRead(Dali::d->pinIn) == LOW) {
    Dali::d->daliHigh();
  } else {
    Dali::d->daliLow();
  }
}

void IRAM_ATTR Dali::timerISR(void) {
  // When the timer interval triggers, we've finished receiving bits - a stop bit has been seen
  Dali::d->daliIdle();
}

Dali::Dali(int pinIn, int pinOut) {
  this->pinIn = pinIn;
  this->pinOut = pinOut;
  this->err = eNoError;

  this->logBuf = (char*)malloc(LOG_SIZE);
  this->logPtr = logBuf;
  this->edgeTimes = (unsigned long*)malloc(100*sizeof(unsigned long));
  this->edgeDests = (bool*)malloc(100*sizeof(bool));
  this->edgeStates = (daliState*)malloc(100*sizeof(daliState));
  this->nEdges = 0;
  
  Dali::d = this;
}

void IRAM_ATTR Dali::log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int l = vsnprintf(this->logPtr, LOG_SIZE - (this->logPtr - this->logBuf), fmt, ap);
  this->logPtr += l;
  if (this->logPtr - this->logBuf > (LOG_SIZE - 100)) {
    this->logPtr = this->logBuf;
  }
  va_end(ap);
}

const char* Dali::getLogBuf(void) {
  return this->logBuf;
}

void Dali::resetEdgeLog(void) {
  nEdges = 0;
}

void IRAM_ATTR Dali::logEdge(unsigned long t, bool v, daliState s) {
  edgeTimes[nEdges] = t;
  edgeDests[nEdges] = v;
  edgeStates[nEdges] = s;
  nEdges++;
}

void Dali::dumpEdgeLog(const char *tag) {
  log("%s: b %d v %02X\n", tag, rcvdBits, rcvdVal);
  unsigned long lastT = edgeTimes[0];
  for (int i = 0; i < nEdges; i++) {
    log("%ld %c %d\n", edgeTimes[i] - lastT, edgeDests[i]?'H':'L', edgeStates[i]);
    lastT = edgeTimes[i];
  }
}

void Dali::init(void) {
  pinMode(this->pinIn, INPUT);
  pinMode(this->pinOut, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(this->pinIn), Dali::inputISR, CHANGE);
  timer1_attachInterrupt(Dali::timerISR);
  DALI_HIGH();
}

daliError Dali::getError(void) {
  return this->err;
}

void Dali::setError(daliError e) {
  this->err = e;
}

daliTime IRAM_ATTR Dali::getBitTime(void) {
  unsigned long diff;
  if (lastDaliHigh > lastDaliLow) {
    diff = lastDaliHigh - lastDaliLow;
  } else {
    diff = lastDaliLow - lastDaliHigh;
  }
  if (diff < DALI_HB_MIN) {
    return tiTooShort;
  }
  if (diff < DALI_HB_MAX) {
    return tiHalfBit;
  }
  if (diff < DALI_2HB_MIN) {
    return tiInvalid;
  }
  if (diff < DALI_2HB_MAX) {
    return ti2HalfBits;
  }
  return tiTooLong;
}

void IRAM_ATTR Dali::addBit(bool bit) {
  this->rcvdBits++;
  this->rcvdVal <<= 1;
  if (bit) {
    this->rcvdVal |= 1;
  }
}

void IRAM_ATTR Dali::daliIdle(void) {
  if (this->state == stSecondHalf) {
    this->addBit(true);
    this->state = stFrameReady;
    Dali::d->log("fR SH\n");
  } else if (this->state == stFirstHalf) {
    // We saw the line go high after a zero and assumed the first half of another zero, but
    // it turned out to be a stop
    this->state = stFrameReady;
    Dali::d->log("fR FH\n");
  } else {    
    // Incorrect bit timing
    this->state = stIdle;
    Dali::d->log("idle\n");
  }
}

void IRAM_ATTR Dali::daliHigh(void) {
  this->lastDaliHigh = micros();
  if (this->state == stSending) {
    return;
  }
  logEdge(this->lastDaliHigh, true, this->state);
  daliTime bitTime = getBitTime();
  if (this->state == stStartBitH1) {
    if (bitTime == tiHalfBit) {
      this->state = stStartBitH2;
    } else {
      // Incorrect bit timing
      log("h-sbh1e\n");
      this->state = stIdle;
    }
  } else if (this->state == stFirstHalf) {
    if (bitTime == tiHalfBit) {
      // The first half of a one.  Now second half.  Stop bit might follow.
      this->state = stSecondHalf;
      timer1_enable(TIM_DIV256, TIM_EDGE, TIM_SINGLE);
      timer1_write(STOP_BIT_TICKS);
    } else {
      // Incorrect bit timing
      log("h-fhe\n");
      this->state = stIdle;
    }
  } else if (this->state == stSecondHalf) {
    if (bitTime == tiHalfBit) {
      // The second half of a zero.  Back in first half of a zero, or stop bit.
      this->addBit(false);
      this->state = stFirstHalf;
      timer1_enable(TIM_DIV256, TIM_EDGE, TIM_SINGLE);
      timer1_write(STOP_BIT_TICKS);
    } else if (bitTime == ti2HalfBits) {
      // The second half of a zero and the first half of a one.  Remain in second half.
      // Stop bit might follow.
      this->addBit(false);
      timer1_enable(TIM_DIV256, TIM_EDGE, TIM_SINGLE);
      timer1_write(STOP_BIT_TICKS);
    } else {
      // Incorrect bit timing
      log("h-she\n");
      this->state = stIdle;
    }
  }
}

void IRAM_ATTR Dali::daliLow(void) {
  this->lastDaliLow = micros();
  if (this->state == stSending) {
    return;
  }
  timer1_disable();
  logEdge(this->lastDaliLow, false, this->state);
  daliTime bitTime = getBitTime();
  if (this->state == stIdle) {
    this->state = stStartBitH1;
    this->rcvdBits = 0;
    this->rcvdVal = 0;
  } else if (this->state == stStartBitH2) {
    if (bitTime == tiHalfBit) {
      this->state = stFirstHalf;
    } else if (bitTime == ti2HalfBits) {
      this->state = stSecondHalf;
    } else {
      // Incorrect bit timing
      log("l-sbh2e\n");
      this->state = stIdle;
    }
  } else if (this->state == stFirstHalf) {
    if (bitTime == tiHalfBit) {
      // The first half of a zero.  Now second half.
      this->state = stSecondHalf;
    } else {
      // Incorrect bit timing
      log("l-fhe\n");
      this->state = stIdle;
    }
  } else if (this->state == stSecondHalf) {
    if (bitTime == tiHalfBit) {
      // The second half of a one.  Back in first half of a one.
      this->addBit(true);
      this->state = stFirstHalf;
    } else if (bitTime == ti2HalfBits) {
      // The second half of a one and the first half of a zero.  Remain in second half.
      this->addBit(true);
    } else {
      // Incorrect bit timing
      log("l-she\n");
      this->state = stIdle;
    }
  }
}

// sendBit sends the bit in b, using Manchester encoding. It returns true if the bit was successfully sent, false if a collision was detected.
bool Dali::sendBit(bool b) {
  unsigned long li;
  if (b) {
    DALI_LOW();
    delayMicroseconds(DALI_HB_NOM);
    li = this->lastDaliLow;
    DALI_HIGH();
    delayMicroseconds(DALI_HB_NOM);
    if (li != this->lastDaliLow) {
      // The last low should have been ~25us after when we shorted the DALI bus - it's not, so we collided
      return false;
    }
  } else {
    li = this->lastDaliLow;
    DALI_HIGH();
    delayMicroseconds(DALI_HB_NOM);
    if (li != this->lastDaliLow) {
      // We've not done anything, should be the same as before
      return false;
    }
    DALI_LOW();
    delayMicroseconds(DALI_HB_NOM);
  }
  return true;
}

bool Dali::sendStopBit(void) {
  DALI_HIGH();
  unsigned long li = this->lastDaliLow;
  delaySinceLow(2400);
  if (li != this->lastDaliLow) {
    // No rise should have happened, we've collided
    return false;
  }
  return true;
}

// sendByte sends the given byte. It returns true if the byte was successfully sent, false if a collision was detected.
bool Dali::sendByte(byte b) {
  for (int i = 0; i < 8; i++) {
    if (!sendBit((b&128)==128)) {
      return false;
    }
    b<<=1;
  }
  return true;
}

void Dali::delaySinceLow(unsigned long wait) {
  unsigned long li = this->lastDaliLow;
  unsigned long start = micros();
  unsigned long now = start;
  while (now - start < wait && now - li < wait) {
    yield();
    now = micros();
  }
}

// waitPriority waits until a message of the given priority can be sent.  It returns true if the wait completed without another 
bool Dali::waitPriority(daliPri priority) {
  unsigned long li = this->lastDaliLow;
  delaySinceLow(12000 + 1000 * priority);
  return this->lastDaliLow==li;
}

// sendMessage sends a message with the given priority, address and message.
// It returns true if the message was successfully sent, false if a collision was detected.
bool Dali::sendForwardMessage(daliPri priority, daliAddr addr, daliMsg msg) {
  if (!waitPriority(priority)) {
    setError(eWaitPri);
    return false;
  }
  // We don't check the state before setting stSending.  Whatever was happening before,
  // we've just waited for a bunch of ms and nothing is happening now.  We're OK to just
  // overwrite a previous state. (This will also allow us to recover a few odd states.)
  // Second interesting case: we get interrupted during send.  If this happens,
  // well, it should be a start bit.  In that case, we set state to stStartBitH1.  Only
  // if we uneventfully complete sending do we set state to stIdle.
  this->state = stSending;
  if (!sendBit(true)) { // Start bit
    this->state = stStartBitH1;
    setError(eSendStartBit);
    return false;
  }
  if (!sendByte((byte)(addr&0xFF))) {
    this->state = stStartBitH1;
    setError(eSendAddr);
    return false;
  }
  if (!sendByte((byte)(msg&0xFF))) {
    this->state = stStartBitH1;
    setError(eSendMsg);
    return false;
  }
  if (!sendStopBit()) {
    this->state = stStartBitH1;
    setError(eSendStop);
    return false;
  }
  this->state = stIdle;
  return true;
}

// sendCommand sends a command with the given priority to the given address.
// It repeats the message if the spec requires this.
// It returns true if the message was successfully sent, false if a collision was detected.
bool Dali::sendCommand(daliPri priority, daliAddr addr, daliMsg cmd) {
  if (!sendForwardMessage(priority, addr, cmd)) {
    return false;
  }
  if ((cmd >= 32 && cmd <= 129) || addr == addrInitialise || addr == addrRandomise) {
    // Message should be repeated
    if (!sendForwardMessage(priTxn, addr, cmd)) {
      return false;
    }
  }
  return true;
}

daliRcvStatus Dali::receiveFrame(byte bits, byte timeoutMs) {
  unsigned long wait = timeoutMs * 1000UL;
  unsigned long start = micros();
  resetEdgeLog();
  do {
    if (state == stFrameReady) {
      if (rcvdBits == bits) {
        dumpEdgeLog("Good");
        return rGoodFrame;
      }
      dumpEdgeLog("Bad");
      return rBadFrame;
    }
    yield();
  } while (micros() - start < wait);
  log("No frame\n");
  return rNoFrame;
}

daliRcvStatus Dali::receiveBackwardFrame(void) {
  // 20ms == 10.5ms max settle time, plus 1 start bit + 8 data bits at 1ms/bit, rounded up
  return receiveFrame(8, 20);
}

// sendReset sends a factory reset to the given address.  It returns true if the message was successfully sent, false if a collision was detected.
bool Dali::sendReset(daliAddr addr) {
  addr |= 1;
  return sendCommand(priConfig, addr, msgReset);
}

bool Dali::sendLampOff(daliAddr addr, bool fromUser) {
  addr |= 1;
  return sendCommand(fromUser ? priUser : priAuto, addr, msgOff);
}

bool Dali::sendStepDownOff(daliAddr addr, bool fromUser) {
  addr |= 1;
  return sendCommand(fromUser ? priUser : priAuto, addr, msgStepDownOff);
}

bool Dali::sendOnStepUp(daliAddr addr, bool fromUser) {
  addr |= 1;
  return sendCommand(fromUser ? priUser : priAuto, addr, msgOnStepUp);
}

bool Dali::sendDapc(daliAddr addr, bool fromUser, byte level) {
  return sendCommand(fromUser ? priUser : priAuto, addr, (daliMsg)level);
}

bool Dali::sendSetPowerOnLevel(daliAddr addr, bool fromUser, byte level) {
  // We have to set DTR0 first, then set POL to DTR0
  addr |= 1;
  if (!sendCommand(fromUser ? priUser : priAuto, addrDTR0, (daliMsg)level)) {
    return false;
  }
  return sendCommand(priTxn, addr, msgSetPowerOnLevel);
}

int Dali::queryLevel(daliAddr addr, bool fromUser, daliMsg query) {
  addr |= 1;
  if (!sendCommand(fromUser ? priUser : priAuto, addr, query)) {
    return -1;
  }
  if (receiveBackwardFrame() != rGoodFrame) {
    return -2;
  }
  return (int)rcvdVal;
}

int Dali::queryMinLevel(daliAddr addr, bool fromUser) {
  return queryLevel(addr, fromUser, msgQueryMinLevel);
}

int Dali::queryMaxLevel(daliAddr addr, bool fromUser) {
  return queryLevel(addr, fromUser, msgQueryMaxLevel);
}

int Dali::queryActualLevel(daliAddr addr, bool fromUser) {
  return queryLevel(addr, fromUser, msgQueryActualLevel);
}

int Dali::queryPowerOnLevel(daliAddr addr, bool fromUser) {
  return queryLevel(addr, fromUser, msgQueryPowerOnLevel);
}

// Assigns new random short addresses to all available lamps
// 
// Returns number of lamps discovered
daliAddr* Dali::reAddressLamps(byte *num) {
  if (!sendCommand(priUser, addrInitialise, (daliMsg)0)) {
    *num = 0;
    return NULL;
  }
  if (!sendCommand(priUser, addrRandomise, (daliMsg)0)) {
    *num = 0;
    sendCommand(priUser, addrTerminate, (daliMsg)0); // No error checking - already in error
    return NULL;
  }
  delay(100); // Randomised addresses are to be available 100ms after RANDOMISE
  byte shortAddr;
  setError(eNoError);
  // We loop through all 64 possible short addresses. For each, we call findDevice, which will find a
  // lamp (if there's a lamp with an unassigned short address) and assign it this short address
  for (shortAddr = 0; shortAddr < 64; shortAddr++) { // 6 bits of short addr = max 63
    if (!findDevice(0x000000, 0xFFFFFE, shortAddr)) {
      break;
    }
  }
  // Stop addressing mode
  if (!sendCommand(priUser, addrTerminate, (daliMsg)0)) {
    *num = 0;
    return NULL;
  }
  if (shortAddr == 0) {
    // Didn't find any devices
    if (getError() == eNoError) {
      setError(eNoDevices);
    }
    *num = 0;
    return NULL;
  }
  daliAddr *ret = (daliAddr*)malloc(shortAddr * sizeof(daliAddr));
  if (!ret)
  {
    *num = 0;
    return NULL;
  }
  *num = shortAddr;
  for (byte b = 0; b < shortAddr; b++) {
    ret[b] = b << 1;
  }
  return ret;
}

// findDevice is a recursive function that binary searches for the 24-bit address of lamps
// with no assigned short address. Given a min address and a max address, it chooses the
// midpoint, sets that with the device, then sends a compare message. If no reply is
// received, there is no lamp with a long address <= the selected midpoint and subsequent
// searches are restricted to the top half of the currently-searched space. If a reply is
// received there's a lamp with a long address <= the selected midpoint. If min==max, that
// means we've found a long address. Otherwise, the top half of the currently-searched
// space is searched.
bool Dali::findDevice(uint32 min, uint32 max, byte shortAddr) {
  log("findDevice(%06x, %06x, %02x)\n", min, max, shortAddr);
  if (min > max) {
    return false;
  }
  uint32 mid = (min + max) / 2;
  if (!sendForwardMessage(priUser, addrSearchAddrH, (daliMsg)((mid >> 16) & 0xFF))) {
    return false;
  }
  if (!sendForwardMessage(priUser, addrSearchAddrM, (daliMsg)((mid >> 8) & 0xFF))) {
    return false;
  }
  if (!sendForwardMessage(priUser, addrSearchAddrL, (daliMsg)(mid & 0xFF))) {
    return false;
  }
  if (!sendForwardMessage(priUser, addrCompare, (daliMsg)0)) {
    return false;
  }
  daliRcvStatus reply = receiveBackwardFrame();
  if (reply == rNoFrame) { 
    log("No\n");
    // No lamp in bottom half inc mid, search top half
    return findDevice(mid+1, max, shortAddr);
  }
  if (reply == rBadFrame || rcvdVal != 0xFF) {
    // TODO: We should actually treat this as "two lamps in the top half", unless min==max
    log("BF rB %d rV %02X\n", rcvdBits, rcvdVal);
    setError(eBadBackFrame);
    return false;
  }
  // Lamp in bottom half inc mid
  log("Yes\n");
  if (min != max) {
    // Lamp in bottom half inc mid, search bottom half
    return findDevice(min, mid, shortAddr);
  }

  log("Found %06X, setting %02X\n", min, shortAddr);
  if (!sendForwardMessage(priUser, addrProgramShortAddr, (daliMsg)((shortAddr << 1) | 1))) {
    return false;
  }
  if (!sendForwardMessage(priUser, addrVerifyShortAddr, (daliMsg)((shortAddr << 1) | 1))) {
    return false;
  }
  reply = receiveBackwardFrame();
  if (reply == rNoFrame) {
    log("V-No\n");
    setError(eNoVerifyAns);
    return false;
  }
  if (reply == rBadFrame || rcvdVal != 0xFF) {
    log("BV rB %d rV %02X\n", rcvdBits, rcvdVal);
    setError(eBadVerifyAns);
    return false;
  }
  if (!sendForwardMessage(priUser, addrWithdraw, (daliMsg)0)) {
    return false;
  }
  return true;
}
