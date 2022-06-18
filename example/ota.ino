#include <ArduinoOTA.h>

volatile bool updateActive = false;

void setupArduinoOTA() {
  ArduinoOTA.onStart([]() {
    digitalWrite(PIN_LED_BUILTIN, LOW); // LED is active low
    updateActive = true;
  });

  ArduinoOTA.onEnd([]() {
    digitalWrite(PIN_LED_BUILTIN, HIGH);
    updateActive = false;
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    progress = total;
    progress = progress; // This and above avoid unused parameter warnings
    digitalWrite(PIN_LED_BUILTIN, LOW); // LED is active low
    delay(1);
    digitalWrite(PIN_LED_BUILTIN, HIGH);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    switch (error) {
      case OTA_AUTH_ERROR:
        blinkCode(blinkOTAFailed, 1, "OTA Auth");
        break;
      case OTA_BEGIN_ERROR:
        blinkCode(blinkOTAFailed, 2, "OTA Begin");
        break;
      case OTA_CONNECT_ERROR:
        blinkCode(blinkOTAFailed, 3, "OTA Connect");
        break;
      case OTA_RECEIVE_ERROR:
        blinkCode(blinkOTAFailed, 4, "OTA Receive");
        break;
      case OTA_END_ERROR:
        blinkCode(blinkOTAFailed, 5, "OTA End");
        break;
      default:
        blinkCode(blinkOTAFailed, 6, "OTA Unknown");
    }
  });
  ArduinoOTA.begin();
}

void handleArduinoOTA() {
  ArduinoOTA.handle();
  while (updateActive) {
    yield();
    ArduinoOTA.handle();
  }
}
