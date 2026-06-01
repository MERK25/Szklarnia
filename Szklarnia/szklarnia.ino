#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "driver/rtc_io.h"
#include <Update.h>
#include <sys/time.h>
#include <LittleFS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <esp_task_wdt.h>

#define WDT_TIMEOUT 15 // Watchdog na 15 sekund
#define BUTTON_PIN 13
#define MANUAL_BTN_PIN 27
#define SOIL_PIN 34
#define LIGHT_PIN 33  
#define SENSOR_VCC_PIN 26
#define RELAY_PIN 25
#define FLOAT_PIN 32
#define ONE_WIRE_BUS 4
#define TRIG_PIN 5
#define ECHO_PIN 18

const float TEMP_LIMIT = 30.0;
const float WATER_MIN_TEMP = 15.0; 
const float SURVIVAL_VOLTAGE = 3.60; 

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature waterSensor(&oneWire);
Preferences preferences;
Adafruit_BME280 bme;
bool bmeStatus = false;

RTC_DATA_ATTR int logMoisture[7], logAction[7], logVoltage[7], logTemp[7], logHum[7], logCount = 0;
RTC_DATA_ATTR int sunSecondsToday = 0, lastDayOfYear = -1, sunriseMin = 360, sunsetMin = 1200;
RTC_DATA_ATTR int lastMoisturePct = -1;
RTC_DATA_ATTR float voltageAtSunset = 0.0, voltageAtSunrise = 0.0;
RTC_DATA_ATTR int battWarning = 0;
RTC_DATA_ATTR bool wasNight = false;

int dryThresholdPct, wateringTimeSec, aggrMode, airValue, waterValue, usMax, usMin, maxLight;
float tempOffset;

unsigned long configStartTime;
bool inConfigMode = false;
bool shouldGoToSleep = false;
bool isUpdating = false;
unsigned long sleepDelayTimer = 0;

const char* ssid = "Szklarnia-Net";
const char* password = "pomidory123";

AsyncWebServer server(80);

float getInternalVoltage() {
  int vCode = analogRead(35);
  float voltage = (vCode / 4095.0) * 3.3 * 2.0;
  return (voltage < 1.0) ? 5.01 : voltage;
}

void saveLog(int moisture, int action, float voltage, float temp, float hum) {
  if(logCount >= 7) {
    for(int i = 1; i < 7; i++) {
      logMoisture[i-1] = logMoisture[i]; logAction[i-1] = logAction[i];
      logVoltage[i-1] = logVoltage[i]; logTemp[i-1] = logTemp[i]; logHum[i-1] = logHum[i];
    }
    logCount = 6;
  }
  logMoisture[logCount] = moisture; logAction[logCount] = action;
  logVoltage[logCount] = (int)(voltage * 100); 
  logTemp[logCount] = (int)(temp * 10); logHum[logCount] = (int)hum;
  logCount++;
}

void addPumpSeconds(int sec) {
  preferences.begin("sys", false);
  preferences.putInt("pumpSec", preferences.getInt("pumpSec", 0) + sec);
  preferences.end();
}

int getUltrasonicDistance() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10); digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); 
  return (duration == 0) ? 0 : duration * 0.034 / 2;
}

void SystemGoToSleep(int secondsToSleep) {
  WiFi.softAPdisconnect(true);
  esp_sleep_enable_timer_wakeup((uint64_t)secondsToSleep * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason != ESP_SLEEP_WAKEUP_EXT0) WiFi.mode(WIFI_OFF);

  pinMode(BUTTON_PIN, INPUT_PULLUP); pinMode(MANUAL_BTN_PIN, INPUT_PULLUP);
  pinMode(FLOAT_PIN, INPUT_PULLUP); pinMode(RELAY_PIN, OUTPUT); pinMode(SENSOR_VCC_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);
  digitalWrite(RELAY_PIN, LOW);

  bmeStatus = bme.begin(0x76);
  waterSensor.begin();
  
  preferences.begin("garden", true);
  dryThresholdPct = preferences.getInt("th", 40);
  wateringTimeSec = preferences.getInt("time", 120);
  aggrMode = preferences.getInt("aggr", 1); 
  airValue = preferences.getInt("airVal", 3200);
  waterValue = preferences.getInt("waterVal", 1500);
  tempOffset = preferences.getFloat("tOffset", 0.0);
  usMax = preferences.getInt("usMax", 90);
  usMin = preferences.getInt("usMin", 15);
  maxLight = preferences.getInt("maxLight", 4095);
  preferences.end();
  
  preferences.begin("sys", true);
  int isHibernated = preferences.getInt("hib", 0);
  int failCount = preferences.getInt("fail", 0);
  int checkSoak = preferences.getInt("soak", 0); 
  int isOutdoors = preferences.getInt("outdoors", 0);
  float lastPressure = preferences.getFloat("lastP", 0.0);
  preferences.end();

  float currentV = getInternalVoltage();
  int actualWateringSec = (currentV < SURVIVAL_VOLTAGE) ? max(1, wateringTimeSec / 2) : wateringTimeSec;
  bool isSurvivalMode = (currentV < SURVIVAL_VOLTAGE);

  esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);
  if(isHibernated == 0) esp_sleep_enable_ext1_wakeup(1ULL << MANUAL_BTN_PIN, ESP_EXT1_WAKEUP_ALL_LOW);

  if (isHibernated == 1 && wakeup_reason != ESP_SLEEP_WAKEUP_EXT0) esp_deep_sleep_start();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    inConfigMode = true;
    WiFi.softAP(ssid, password);
    LittleFS.begin(true);
    
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    
    if(isHibernated == 1) { preferences.begin("sys", false); preferences.putInt("hib", 0); preferences.end(); }
    
    // --- ASYNCHRONICZNE ENDPOINTY API ---
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
      digitalWrite(SENSOR_VCC_PIN, HIGH); delay(1000); esp_task_wdt_reset();
      
      float v = getInternalVoltage();
      float temp = bmeStatus ? bme.readTemperature() : 0.0;
      float hum = bmeStatus ? bme.readHumidity() : 0.0;
      float pressure = bmeStatus ? (bme.readPressure() / 100.0F) : 0.0;
      int rawMoisture = analogRead(SOIL_PIN);
      int rawLight = analogRead(LIGHT_PIN); 
      waterSensor.requestTemperatures();
      float waterTemp = waterSensor.getTempCByIndex(0) + tempOffset;
      int usDist = getUltrasonicDistance();
      digitalWrite(SENSOR_VCC_PIN, LOW);

      preferences.begin("garden", true);
      int currentTankVol = preferences.getInt("tVol", 200);
      float currentPumpFlow = preferences.getFloat("pFlow", 2.0);
      preferences.end();

      preferences.begin("sys", true);
      int pumpSec = preferences.getInt("pumpSec", 0);
      int failCountRead = preferences.getInt("fail", 0);
      preferences.end();

      if(waterTemp == DEVICE_DISCONNECTED_C || waterTemp < -50.0) waterTemp = 0.0;

      JsonDocument doc;
      doc["cfg_th"] = dryThresholdPct; doc["cfg_time"] = wateringTimeSec / 60.0; doc["cfg_aggr"] = aggrMode;
      doc["tank_vol"] = currentTankVol; doc["pump_flow"] = currentPumpFlow; doc["voltage"] = v;
      doc["batt_warn"] = battWarning; doc["raw_adc"] = rawMoisture; doc["raw_light"] = rawLight;
      doc["light"] = constrain(map(rawLight, 0, maxLight, 0, 100), 0, 100);
      doc["water_temp"] = waterTemp; doc["t_off"] = tempOffset; doc["us_dist"] = usDist;
      doc["us_max"] = usMax; doc["us_min"] = usMin; doc["sun_hours"] = sunSecondsToday / 3600.0;
      doc["sr"] = sunriseMin; doc["ss"] = sunsetMin; doc["air_val"] = airValue; doc["water_val"] = waterValue;
      doc["fault"] = (failCountRead >= 3 ? 1 : 0); doc["hibernated"] = 0; doc["outdoors"] = isOutdoors;
      doc["survival"] = (v < SURVIVAL_VOLTAGE ? 1 : 0); doc["pump_sec"] = pumpSec;
      doc["temp"] = temp; doc["air_hum"] = hum; doc["pressure"] = pressure;
      doc["water"] = (digitalRead(FLOAT_PIN) == HIGH ? "EMPTY" : "OK");

      JsonArray logs = doc["logs"].to<JsonArray>();
      for(int i = 0; i < logCount; i++) {
        JsonObject l = logs.add<JsonObject>();
        l["moisture"] = logMoisture[i]; l["v"] = logVoltage[i] / 100.0; 
        l["t"] = logTemp[i] / 10.0; l["h"] = logHum[i];
        
        String aTxt = "OK"; int a = logAction[i];
        if(a==1) aTxt="PODLANO"; if(a==2) aTxt="BRAK WODY!"; if(a==3) aTxt="ZA GORĄCO";
        if(a==4) aTxt="RĘCZNE"; if(a==5) aTxt="BLOKADA (SŁOŃCE)"; if(a==6) aTxt="AWARIA POMPY!";
        if(a==7) aTxt="BLOKADA (BURZA)"; if(a==8) aTxt="TRYB SURVIVAL"; if(a==9) aTxt="NOCNY SEN";
        if(a==10) aTxt="ZIMNA WODA"; if(a==11) aTxt="BLOKADA (SAUNA)"; 
        l["action"] = aTxt;
      }
      String jsonResponse;
      serializeJson(doc, jsonResponse);
      request->send(200, "application/json", jsonResponse);
    });

    server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasParam("thPct") && request->hasParam("timeSec") && request->hasParam("aggr")) {
        preferences.begin("garden", false);
        preferences.putInt("th", request->getParam("thPct")->value().toInt());
        preferences.putInt("time", request->getParam("timeSec")->value().toInt());
        preferences.putInt("aggr", request->getParam("aggr")->value().toInt());
        preferences.end();
        shouldGoToSleep = true; sleepDelayTimer = millis();
        request->send(200, "text/plain", "OK");
      } else request->send(400);
    });

    server.on("/tank", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasParam("vol") && request->hasParam("flow")) {
        preferences.begin("garden", false);
        preferences.putInt("tVol", request->getParam("vol")->value().toInt());
        preferences.putFloat("pFlow", request->getParam("flow")->value().toFloat());
        preferences.end();
        request->send(200, "text/plain", "OK");
      } else request->send(400);
    });

    server.on("/calibrate", HTTP_GET, [](AsyncWebServerRequest *request){
      preferences.begin("garden", false);
      if(request->hasParam("air")) preferences.putInt("airVal", request->getParam("air")->value().toInt());
      if(request->hasParam("water")) preferences.putInt("waterVal", request->getParam("water")->value().toInt());
      preferences.end();
      request->send(200, "text/plain", "OK");
    });

    server.on("/calibrateExtras", HTTP_GET, [](AsyncWebServerRequest *request){
      preferences.begin("garden", false);
      if(request->hasParam("tOff")) preferences.putFloat("tOffset", request->getParam("tOff")->value().toFloat());
      if(request->hasParam("usMax")) preferences.putInt("usMax", request->getParam("usMax")->value().toInt());
      if(request->hasParam("usMin")) preferences.putInt("usMin", request->getParam("usMin")->value().toInt());
      if(request->hasParam("lMax")) preferences.putInt("maxLight", request->getParam("lMax")->value().toInt());
      preferences.end();
      request->send(200, "text/plain", "OK");
    });

    server.on("/location", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasParam("outdoors")) {
        preferences.begin("sys", false); preferences.putInt("outdoors", request->getParam("outdoors")->value().toInt()); preferences.end();
        request->send(200, "text/plain", "OK");
      } else request->send(400);
    });

    server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request){
      if(digitalRead(FLOAT_PIN) == HIGH) request->send(403, "text/plain", "Brak Wody!");
      else if(request->hasParam("sec")) {
        int tSec = request->getParam("sec")->value().toInt();
        digitalWrite(RELAY_PIN, HIGH); delay(tSec * 1000); digitalWrite(RELAY_PIN, LOW);
        addPumpSeconds(tSec); 
        shouldGoToSleep = true; sleepDelayTimer = millis();
        request->send(200, "text/plain", "OK");
      }
    });

    server.on("/drain", HTTP_GET, [](AsyncWebServerRequest *request){
      if(digitalRead(FLOAT_PIN) == HIGH) { request->send(200, "text/plain", "ALREADY_EMPTY"); return; }
      int drainedSec = 0; digitalWrite(RELAY_PIN, HIGH);
      while (digitalRead(FLOAT_PIN) == LOW && drainedSec < 120) { delay(1000); drainedSec++; esp_task_wdt_reset(); }
      digitalWrite(RELAY_PIN, LOW); addPumpSeconds(drainedSec);
      shouldGoToSleep = true; sleepDelayTimer = millis();
      request->send(200, "text/plain", "DRAINED");
    });

    server.on("/sync", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasParam("t")) {
        struct timeval tv; tv.tv_sec = request->getParam("t")->value().toInt(); tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        if(request->hasParam("sr") && request->hasParam("ss")) {
           sunriseMin = request->getParam("sr")->value().toInt();
           sunsetMin = request->getParam("ss")->value().toInt();
        }
        request->send(200, "text/plain", "OK");
      } else request->send(400);
    });

    server.on("/resetFault", HTTP_GET, [](AsyncWebServerRequest *request){
      preferences.begin("sys", false); preferences.putInt("fail", 0); preferences.end();
      request->send(200, "text/plain", "OK");
    });
    
    server.on("/resetWater", HTTP_GET, [](AsyncWebServerRequest *request){
      preferences.begin("sys", false); preferences.putInt("pumpSec", 0); preferences.end();
      request->send(200, "text/plain", "OK");
    });

    server.on("/hibernate", HTTP_GET, [](AsyncWebServerRequest *request){
      preferences.begin("sys", false); preferences.putInt("hib", 1); preferences.end();
      shouldGoToSleep = true; sleepDelayTimer = millis();
      request->send(200, "text/plain", "HIBERNATING");
    });

    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
      shouldGoToSleep = true; sleepDelayTimer = millis();
      request->send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
      delay(1000); ESP.restart();
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
      if(!index) { isUpdating = true; Update.begin(UPDATE_SIZE_UNKNOWN); }
      if(!Update.hasError()) Update.write(data, len);
      if(final) { if(Update.end(true)) isUpdating = false; }
    });

    server.begin(); configStartTime = millis();
  } 
  else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
    if (digitalRead(FLOAT_PIN) == LOW && failCount < 3) {
      digitalWrite(RELAY_PIN, HIGH); delay(actualWateringSec * 1000); digitalWrite(RELAY_PIN, LOW);
      addPumpSeconds(actualWateringSec);
      float t = bmeStatus ? bme.readTemperature() : 0;
      float h = bmeStatus ? bme.readHumidity() : 0;
      saveLog(100, 4, currentV, t, h);
    }
    SystemGoToSleep(14400); 
  } 
  else {
    float currentPressure = bmeStatus ? (bme.readPressure() / 100.0F) : 0.0;
    float currentTemp = bmeStatus ? bme.readTemperature() : 0.0;
    float currentHum = bmeStatus ? bme.readHumidity() : 0.0;
    if (currentPressure > 500) { preferences.begin("sys", false); preferences.putFloat("lastP", currentPressure); preferences.end(); }

    struct tm ti; getLocalTime(&ti);
    bool timeIsSet = (ti.tm_year > 120); 

    if (timeIsSet && ti.tm_yday != lastDayOfYear) { sunSecondsToday = 0; lastDayOfYear = ti.tm_yday; }

    int currentMin = ti.tm_hour * 60 + ti.tm_min;
    bool isNight = (timeIsSet && (currentMin >= sunsetMin || currentMin < sunriseMin));

    if (isNight && !wasNight) voltageAtSunset = currentV; 
    else if (!isNight && wasNight) {
        voltageAtSunrise = currentV;
        battWarning = (voltageAtSunset > 0 && (voltageAtSunset - voltageAtSunrise) > 0.4) ? 1 : 0;
    }
    wasNight = isNight;

    digitalWrite(SENSOR_VCC_PIN, HIGH); delay(1000); esp_task_wdt_reset();
    int reading = 0; for(int i=0; i<10; i++) { reading += analogRead(SOIL_PIN); delay(30); }
    int rawMoisture = reading / 10;
    int rawLight = analogRead(LIGHT_PIN); 
    
    waterSensor.requestTemperatures();
    float waterTemp = waterSensor.getTempCByIndex(0) + tempOffset;
    digitalWrite(SENSOR_VCC_PIN, LOW);

    int currentMoisturePct = constrain(map(rawMoisture, airValue, waterValue, 0, 100), 0, 100);

    if (checkSoak == 0 && lastMoisturePct != -1 && (currentMoisturePct - lastMoisturePct) > 25) currentMoisturePct = lastMoisturePct; 
    lastMoisturePct = currentMoisturePct;

    int activeAggr = (currentV < 3.75) ? 0 : aggrMode; 

    int delta = currentMoisturePct - dryThresholdPct;
    int calculatedSleepSec = 14400; 

    if (delta > 20) calculatedSleepSec = (activeAggr == 2) ? 28800 : (activeAggr == 1 ? 36000 : 43200);
    else if (delta > 0) calculatedSleepSec = (activeAggr == 2) ? map(delta, 1, 20, 3600, 14400) : (activeAggr == 1 ? map(delta, 1, 20, 7200, 21600) : map(delta, 1, 20, 10800, 28800)); 
    else calculatedSleepSec = (activeAggr == 2) ? 2700 : (activeAggr == 1 ? 3600 : 5400); 

    if (failCount > 0) calculatedSleepSec = 21600; 
    if (rawLight > 2500 && timeIsSet) sunSecondsToday += calculatedSleepSec;
    
    float svp = 0.61078 * exp((17.27 * currentTemp) / (currentTemp + 237.3));
    float avp = svp * (currentHum / 100.0);
    float currentVPD = svp - avp;
    bool isSauna = (currentVPD < 0.4 && currentVPD > 0.0);

    if (checkSoak == 1) {
      preferences.begin("sys", false); preferences.putInt("soak", 0);
      preferences.putInt("fail", (currentMoisturePct >= dryThresholdPct) ? 0 : failCount + 1);
      preferences.end();
      SystemGoToSleep(calculatedSleepSec);
    } 
    else {
      int actionTaken = 0;

      if (isNight) {
        actionTaken = 9;
        int minsToSunrise = sunriseMin - currentMin;
        if (minsToSunrise <= 0) minsToSunrise += 1440; 
        SystemGoToSleep(minsToSunrise * 60);
      }
      else if (currentMoisturePct < dryThresholdPct) {
        bool stormApproaching = (isOutdoors == 1 && lastPressure > 500 && currentPressure > 500 && (lastPressure - currentPressure) >= 2.0);
        
        if (failCount >= 3) actionTaken = 6; 
        else if (stormApproaching) actionTaken = 7; 
        else if (rawLight > 3000 && isOutdoors == 1) actionTaken = 5;  
        else if (digitalRead(FLOAT_PIN) == HIGH) actionTaken = 2;  
        else if (currentTemp >= TEMP_LIMIT && isOutdoors == 0) actionTaken = 3; 
        else if (waterTemp > 0.0 && waterTemp < WATER_MIN_TEMP) actionTaken = 10;  
        else if (isSauna && isOutdoors == 0) actionTaken = 11; 
        else {
          actionTaken = isSurvivalMode ? 8 : 1; 
          digitalWrite(RELAY_PIN, HIGH); delay(actualWateringSec * 1000); digitalWrite(RELAY_PIN, LOW);
          addPumpSeconds(actualWateringSec);
          preferences.begin("sys", false); preferences.putInt("soak", 1); preferences.end();
          SystemGoToSleep(900); 
        }
      } 
      else {
        preferences.begin("sys", false); preferences.putInt("fail", 0); preferences.end();
      }
      saveLog(currentMoisturePct, actionTaken, currentV, currentTemp, currentHum);
      SystemGoToSleep(calculatedSleepSec);
    }
  }
}

void loop() {
  esp_task_wdt_reset();
  if (inConfigMode) {
    if (shouldGoToSleep && !isUpdating && (millis() - sleepDelayTimer > 1500)) SystemGoToSleep(14400);
    if (!isUpdating && (millis() - configStartTime > 180000)) SystemGoToSleep(14400);
  }
}