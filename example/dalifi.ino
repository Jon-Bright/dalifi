#include <ESP8266WiFi.h>
#include <Client.h>
#include <dali.h>

#define PIN_LED_BUILTIN 2 // D4
#define PIN_DALI_O 5 // D1
#define PIN_DALI_I 4 // D2

#define LED_ACTIVE LOW
#define LED_INACTIVE HIGH

typedef enum {
  blinkResetFailed = 1,
  blinkLampOffFailed,
  blinkNotAllLampsFound,
  blinkQueryPowerOnLevelFailed,
  blinkPowerOnLevelSetFailed,
  blinkOTAFailed,
} blinkLongCode;

Dali *dali;
daliAddr *addrs;
byte nLamps;

// RTC memory gives us 512 bytes, so these 33+1+1+1+64+4=104 will fit fine
struct __attribute__((packed, aligned(4))) DaliFiConfig {
  char ssid[33];      // SSIDs are allowed to be at most 32 chars, plus terminator
  byte nLamps;        // The number of lamps we expect to find
  byte powerOnLvl;    // The level at which the lamps should be when powered on, 0-254 (will be restricted by min/max)
  byte pad;           // Padding to make config struct a multiple of 4 bytes
  char password[64];  // Passwords can be 63 chars, plus terminator
  uint32_t crc;       // CRC to ensure the data we read is valid
} daliFiConfig;

void blinkCode(blinkLongCode longFlash, byte shortFlash, const char* logBuf) {
  for (byte x=0; x != 4; x++) {
    for (byte i = 0; i != (byte)longFlash; i++) {
      digitalWrite(PIN_LED_BUILTIN, LED_ACTIVE);
      delay(600);
      serveWiFi(logBuf);
      digitalWrite(PIN_LED_BUILTIN, LED_INACTIVE);
      delay(600);
      serveWiFi(logBuf);
    }
    delay(1000);
    for (byte i = 0; i != shortFlash; i++) {
      digitalWrite(PIN_LED_BUILTIN, LED_ACTIVE);
      delay(200);
      serveWiFi(logBuf);
      digitalWrite(PIN_LED_BUILTIN, LED_INACTIVE);
      delay(300);
      serveWiFi(logBuf);
    }
    delay(2000);
    serveWiFi(logBuf);
  }
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  Serial.println("setup");
  Serial.flush();
  pinMode(PIN_LED_BUILTIN, OUTPUT);
  digitalWrite(PIN_LED_BUILTIN, LED_INACTIVE);
  setupWiFi();

  dali = new Dali(PIN_DALI_I, PIN_DALI_O);
  dali->init();
  dali->log("init\n");
  delay(2000);
  if (!dali->sendReset(Dali::broadcast)) {
    blinkCode(blinkResetFailed, dali->getError(), dali->getLogBuf());
  }
  dali->log("reset sent\n");
  delay(1000);
  if (!dali->sendLampOff(Dali::broadcast, false)) {
    blinkCode(blinkLampOffFailed, dali->getError(), dali->getLogBuf());
  }
  dali->log("lamp-off sent\n");
  digitalWrite(PIN_LED_BUILTIN, LED_ACTIVE);
  delay(200);
  digitalWrite(PIN_LED_BUILTIN, LED_INACTIVE);
  delay(2000);
  addrs = dali->reAddressLamps(&nLamps);
  dali->log("lamps addressed, nLamps %d\n", nLamps);
  if (addrs == NULL || nLamps != daliFiConfig.nLamps) {
    blinkCode(blinkNotAllLampsFound, dali->getError(), dali->getLogBuf());
  }
  for (int i = 0; i < nLamps; i++) {
    int pol = dali->queryPowerOnLevel(addrs[i], false);
    if (pol < 0) {
      blinkCode(blinkQueryPowerOnLevelFailed, dali->getError(), dali->getLogBuf());
    }
    dali->log("lamp %d, got pol %d, want %d\n", i, pol, daliFiConfig.powerOnLvl);
    if (pol == daliFiConfig.powerOnLvl) {
      // Power-on level already set to what we want, next lamp
      continue;
    }
    if (!dali->sendSetPowerOnLevel(addrs[i], false, daliFiConfig.powerOnLvl)) {
      blinkCode(blinkPowerOnLevelSetFailed, dali->getError(), dali->getLogBuf());
    }
  }
  dali->log("boot complete, %d lamps\n", nLamps);
}

byte getNumLamps(void) {
  return nLamps;
}

const char *stepOnUp(bool fromUser) {
  for (int i = 0; i < nLamps; i++) {
    if (!dali->sendOnStepUp(addrs[i], fromUser)) {
      return "Failed OSU";
    }
  }
  return NULL;
}

const char *stepDownOff(bool fromUser) {
  for (int i = 0; i < nLamps; i++) {
    if (!dali->sendStepDownOff(addrs[i], fromUser)) {
      return "Failed SDO";
    }
  }
  return NULL;
}

const char *setLevel(bool fromUser, byte level) {
  for (int i = 0; i < nLamps; i++) {
    if (!dali->sendDapc(addrs[i], fromUser, level)) {
      return "Failed DAPC";
    }
  }
  return NULL;
}

const char *query(bool fromUser, int *lvl) {
  for (int i = 0; i < nLamps; i++) {
    lvl[i] = dali->queryActualLevel(addrs[i], fromUser);
    if (lvl[i] < 0) {
      return "Failed QAL";
    }
  }
  return NULL;
}

const char *queryMin(bool fromUser, int *lvl) {
  for (int i = 0; i < nLamps; i++) {
    lvl[i] = dali->queryMinLevel(addrs[i], fromUser);
    if (lvl[i] < 0) {
      return "Failed QMinL";
    }
  }
  return NULL;
}

const char *queryMax(bool fromUser, int *lvl) {
  for (int i = 0; i < nLamps; i++) {
    lvl[i] = dali->queryMaxLevel(addrs[i], fromUser);
    if (lvl[i] < 0) {
      return "Failed QMaxL";
    }
  }
  return NULL;
}

void loop() {
  serveWiFi(dali->getLogBuf());
}
