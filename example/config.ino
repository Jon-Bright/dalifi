#include <ESP8266WebServer.h>
#include <DNSServer.h> 
#include <EEPROM.h>

#define DNS_PORT 53

const char *apModeSSID = "DaliFi";
const IPAddress apModeIP(192,168,74,66);
const IPAddress apModeGateway(192,168,74,0);
const IPAddress apModeSubnet(255,255,255,0);

DNSServer dnsServer;
ESP8266WebServer configServer(80);
unsigned long reboot;

uint32_t calculateCRC32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xffffffff;
  while (length--) {
    uint8_t c = *data++;
    for (uint32_t i = 0x80; i > 0; i >>= 1) {
      bool bit = crc & 0x80000000;
      if (c & i) { bit = !bit; }
      crc <<= 1;
      if (bit) { crc ^= 0x04c11db7; }
    }
  }
  return crc;
}

bool readAndVerifyConfig() {
  EEPROM.begin(sizeof(daliFiConfig));
  EEPROM.get(0, daliFiConfig);
  if (!EEPROM.end())
    return false;  // Not actually a failure, but we might as well keep it in mind
  uint32_t crc = calculateCRC32((uint8_t*)&daliFiConfig, sizeof(daliFiConfig)-sizeof(uint32_t));
  return crc == daliFiConfig.crc; // If our calculated CRC matches the CRC we read, the saved info is valid
}

void resetConfig() {
  memset(&daliFiConfig, 0, sizeof(daliFiConfig));
  EEPROM.begin(sizeof(daliFiConfig));
  EEPROM.put(0, daliFiConfig);
  EEPROM.end();
}

void handleRoot() {
  configServer.send(200, "text/html", 
    F("<html>"
      " <head>"
      "  <title>DaliFi</title>"
      " </head>"
      " <body>"
      "  <h1>DaliFi</h1>"
      "  <form method='post' action='setconfig'>"
      "   <table>"
      "    <tr>"
      "     <td>SSID"
      "     <td><input name='ssid' length='20'>"
      "    <tr>"
      "     <td>Password"
      "     <td><input name='pass' length='20'>"
      "    <tr>"
      "     <td>Number of lamps"
      "     <td><input name='lamps' length='5'>"
      "    <tr>"
      "     <td>Power-on level"
      "     <td><input name='pol' length='5'>"
      "    <tr>"
      "     <td colspan=2><input type='submit' value='Save and restart'>"
      ));
}

void handleSetConfig() {
  if (!configServer.hasArg("ssid")) {
    configServer.send(500, "text/plain", "Missing ssid");
    return;
  }
  if (!configServer.hasArg("pass")) {
    configServer.send(500, "text/plain", "Missing pass");
    return;
  }
  if (!configServer.hasArg("lamps")) {
    configServer.send(500, "text/plain", "Missing lamps");
    return;
  }
  if (!configServer.hasArg("pol")) {
    configServer.send(500, "text/plain", "Missing pol");
    return;
  }
  configServer.setContentLength(CONTENT_LENGTH_UNKNOWN); // This prevents .send() sending a Content-Length header that stops subsequent messages from getting through.
  configServer.send(200, "text/plain", "Setting config...\n");
  configServer.arg("ssid").toCharArray(daliFiConfig.ssid, sizeof(daliFiConfig.ssid));
  configServer.arg("pass").toCharArray(daliFiConfig.password, sizeof(daliFiConfig.password));
  daliFiConfig.nLamps = configServer.arg("lamps").toInt();
  daliFiConfig.powerOnLvl = configServer.arg("pol").toInt();
  configServer.sendContent(String("ssid: ")+String(daliFiConfig.ssid)+String("\npass: ")+String(daliFiConfig.password)+String("\nlamps: ")+String(daliFiConfig.nLamps)+String("\npower-on level: ")+String(daliFiConfig.powerOnLvl)+String("\n"));
  daliFiConfig.crc = calculateCRC32((uint8_t*)&daliFiConfig, sizeof(daliFiConfig)-sizeof(uint32_t));
  EEPROM.begin(sizeof(daliFiConfig));
  EEPROM.put(0, daliFiConfig);
  EEPROM.end();
  configServer.sendContent("Wrote config\n");
  memset(&daliFiConfig, 0, sizeof(daliFiConfig));
  if (readAndVerifyConfig()) {
    configServer.sendContent("Config verified! Rebooting in 5s...\n");
    reboot = millis()+5000; // This doesn't work if millis is about to wrap around. Don't care.
  } else {
    configServer.sendContent("Config read-back failed!\n");
  }
}

void wiFiAPMode() {
  WiFi.softAPConfig(apModeIP, apModeGateway, apModeSubnet);
  WiFi.softAP(apModeSSID);
  setupArduinoOTA();
  dnsServer.start(DNS_PORT, "*", apModeIP);
  configServer.on("/", handleRoot);
  configServer.on("/setconfig", handleSetConfig);
  configServer.begin();

  while (true) {
    delay(100);
    if (reboot!=0 && millis()>reboot) {
      ESP.restart();
    }
    handleArduinoOTA();
    dnsServer.processNextRequest();
    configServer.handleClient();
  }
}
