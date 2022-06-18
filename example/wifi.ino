#include <ESP8266WiFi.h>

WiFiServer server(24601);

void setupWiFi() {
  if (!readAndVerifyConfig()) {
    wiFiAPMode();
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(daliFiConfig.ssid, daliFiConfig.password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  server.begin();

  setupArduinoOTA();
}

void serveWiFi(const char* log) {
  handleArduinoOTA();
  WiFiClient client = server.available();
  if (!client) {
    return;
  }
  while (client.connected())
  {
    yield();
    if (client.available())
    {
      char cmdbuf[101];
      int l = client.readBytesUntil('\n', cmdbuf, 100);
      if (l == 0) {
        continue;
      }
      if (cmdbuf[l - 1] == '\r') {
        cmdbuf[l - 1] = '\0';
      } else {
        cmdbuf[l] = '\0';
      }
      if (!strcmp(cmdbuf, "RESET")) {
        ESP.restart();
        delayMicroseconds(10000000);
      } else if (!strcmp(cmdbuf, "LOG")) {
        client.write(log, strlen(log));
      } else if (!strcmp(cmdbuf, "STARTINFO")) {
        rst_info *rst;
        rst = ESP.getResetInfoPtr();
        l = sprintf(cmdbuf, "rea %X, exc %X, pc1 %X, pc2 %X, pc3 %X, vad %X, dep %X\n", rst->reason, rst->exccause, rst->epc1, rst->epc2, rst->epc3, rst->excvaddr, rst->depc);
        client.write(cmdbuf, l);
      } else if (!strcmp(cmdbuf, "WIFIRESET")) {
        resetConfig();
      } else if (!strcmp(cmdbuf, "LED")) {
        // Just to test the ESP8266's built-in LED
        digitalWrite(PIN_LED_BUILTIN, LED_ACTIVE);
        client.write("ON\n", 3);
        unsigned long end = millis() + 2000;
        while (millis() < end) {
          yield();
        }
        client.write("OFF\n", 4);
        digitalWrite(PIN_LED_BUILTIN, LED_INACTIVE);
      } else if (!strcmp(cmdbuf, "UPTIME")) {
        unsigned long ms = millis();
        unsigned long days = ms / 86400000UL;
        ms %= 86400000UL;
        unsigned long hours = ms / 3600000UL;
        ms %= 3600000UL;
        unsigned long mins = ms / 60000UL;
        ms %= 60000UL;
        unsigned long secs = ms / 1000UL;
        ms %= 1000UL;
        l = sprintf(cmdbuf, "%lu ms, %lu:%lu:%lu:%lu.%lu D:H:M:S.ms\n", millis(), days, hours, mins, secs, ms);
        client.write(cmdbuf, l);
      } else if (!strcmp(cmdbuf, "STEP_ON_UP")) {
        const char* err = stepOnUp(true);
        if (!err) {
          client.write("OK\n", 3);
        } else {
          l = sprintf(cmdbuf, "ERR:%d/%s\n", dali->getError(), err);
          client.write(cmdbuf, l);
        }
      } else if (!strncmp(cmdbuf, "SET ", 4)) {
        byte lvl = atoi(cmdbuf + 4);
        const char* err = setLevel(true, lvl);
        if (!err) {
          client.write("OK\n", 3);
        } else {
          l = sprintf(cmdbuf, "ERR:%d/%s\n", dali->getError(), err);
          client.write(cmdbuf, l);
        }
      } else if (!strcmp(cmdbuf, "QUERY")) {
        int *lvl = (int*)malloc(sizeof(int) * getNumLamps());
        const char* err = query(true, lvl);
        if (!err) {
          l = 0;
          for (int i = 0; i < getNumLamps(); i++) {
            l += sprintf(cmdbuf + l, "%s%d", l ? "," : "", lvl[i]);
          }
          cmdbuf[l++] = '\n';
          client.write(cmdbuf, l);
        } else {
          l = sprintf(cmdbuf, "ERR:%d/%s\n", dali->getError(), err);
          client.write(cmdbuf, l);
        }
        free(lvl);
      } else if (!strcmp(cmdbuf, "QUERY_MIN")) {
        int *lvl = (int*)malloc(sizeof(int) * getNumLamps());
        const char* err = queryMin(true, lvl);
        if (!err) {
          l = 0;
          for (int i = 0; i < getNumLamps(); i++) {
            l += sprintf(cmdbuf + l, "%s%d", l ? "," : "", lvl[i]);
          }
          cmdbuf[l++] = '\n';
          client.write(cmdbuf, l);
        } else {
          l = sprintf(cmdbuf, "ERR:%d/%s\n", dali->getError(), err);
          client.write(cmdbuf, l);
        }
        free(lvl);
      } else if (!strcmp(cmdbuf, "QUERY_MAX")) {
        int *lvl = (int*)malloc(sizeof(int) * getNumLamps());
        const char* err = queryMax(true, lvl);
        if (!err) {
          l = 0;
          for (int i = 0; i < getNumLamps(); i++) {
            l += sprintf(cmdbuf + l, "%s%d", l ? "," : "", lvl[i]);
          }
          cmdbuf[l++] = '\n';
          client.write(cmdbuf, l);
        } else {
          l = sprintf(cmdbuf, "ERR:%d/%s\n", dali->getError(), err);
          client.write(cmdbuf, l);
        }
        free(lvl);
      } else if (!strcmp(cmdbuf, "STEP_DOWN_OFF")) {
        const char* err = stepDownOff(true);
        if (!err) {
          client.write("OK\n", 3);
        } else {
          l = sprintf(cmdbuf, "ERR:%d/%s\n", dali->getError(), err);
          client.write(cmdbuf, l);
        }
      } else if (!strcmp(cmdbuf, "QUIT")) {
        client.stop();
        break;
      }
    }
  }
  delay(100); // give some time for any final reads
}
