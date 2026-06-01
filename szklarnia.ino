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

#define WDT_TIMEOUT 15 
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
#define VENT_PIN 19 

const float TEMP_LIMIT = 30.0;
const float TEMP_VENT_CLOSE = 25.0;
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

int hasUs, hasDs, hasBme, hasLight, hasVent;
int dryThresholdPct, wateringTimeSec, aggrMode, airValue, waterValue, usMax, usMin, maxLight;
float tempOffset;

unsigned long configStartTime;
bool inConfigMode = false;
bool isUpdating = false;

// --- KOLEJKA FLAG DLA UNIKNIĘCIA WYŚCIGÓW DANYCH ---
volatile bool flag_saveConfig = false;
volatile int tmp_th, tmp_time, tmp_aggr;

volatile bool flag_saveTank = false;
volatile int tmp_vol;
volatile float tmp_flow;

volatile bool flag_saveCalib = false;
volatile int tmp_air, tmp_water;

volatile bool flag_saveExtras = false;
volatile float tmp_tOff;
volatile int tmp_usMax, tmp_usMin, tmp_lMax;

volatile bool flag_saveLocation = false;
volatile int tmp_outdoors;

volatile bool flag_testPump = false;
volatile int tmp_testSec;
volatile bool tmp_testForce;

volatile bool flag_drainPump = false;
volatile bool flag_resetFault = false;
volatile bool flag_resetWater = false;
volatile bool flag_hibernate = false;

volatile bool flag_hwConfig = false;
volatile int tmp_hwUs, tmp_hwDs, tmp_hwBme, tmp_hwLight, tmp_hwVent;

volatile bool isServiceMode = false;
volatile unsigned long serviceStartTime = 0;
volatile unsigned long sleepDelayTimer = 0;
volatile bool shouldGoToSleep = false;

const char* ssid = "Szklarnia-Net";
const char* password = "pomidory123";

AsyncWebServer server(80);

float getInternalVoltage() {
  int vCode = analogRead(35);
  return ((vCode / 4095.0) * 3.3 * 2.0 < 1.0) ? 5.01 : (vCode / 4095.0) * 3.3 * 2.0;
}

float safeFloat(float val) {
  if (isnan(val) || isinf(val)) return 0.0;
  return val;
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
  logVoltage[logCount] = (int)(safeFloat(voltage) * 100); 
  logTemp[logCount] = (int)(safeFloat(temp) * 10); logHum[logCount] = (int)(safeFloat(hum));
  logCount++;
}

void addPumpSeconds(int sec) {
  preferences.begin("sys", false);
  preferences.putInt("pumpSec", preferences.getInt("pumpSec", 0) + sec);
  preferences.end();
}

int getUltrasonicDistance() {
  if(!hasUs) return 0;
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

  preferences.begin("hw", true);
  hasUs = preferences.getInt("hasUs", 1); hasDs = preferences.getInt("hasDs", 1);
  hasBme = preferences.getInt("hasBme", 1); hasLight = preferences.getInt("hasLight", 1);
  hasVent = preferences.getInt("hasVent", 0);
  preferences.end();

  if(hasVent) { pinMode(VENT_PIN, OUTPUT); }
  if(hasBme) bmeStatus = bme.begin(0x76);
  if(hasDs) waterSensor.begin();
  
  preferences.begin("garden", true);
  dryThresholdPct = preferences.getInt("th", 40); wateringTimeSec = preferences.getInt("time", 120);
  aggrMode = preferences.getInt("aggr", 1); airValue = preferences.getInt("airVal", 3200);
  waterValue = preferences.getInt("waterVal", 1500); tempOffset = preferences.getFloat("tOffset", 0.0);
  usMax = preferences.getInt("usMax", 90); usMin = preferences.getInt("usMin", 15);
  maxLight = preferences.getInt("maxLight", 4095);
  preferences.end();
  
  preferences.begin("sys", true);
  int isHibernated = preferences.getInt("hib", 0); int failCount = preferences.getInt("fail", 0);
  int checkSoak = preferences.getInt("soak", 0); int isOutdoors = preferences.getInt("outdoors", 0);
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
    
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
      if(!isServiceMode) configStartTime = millis(); 
      digitalWrite(SENSOR_VCC_PIN, HIGH); delay(1000); esp_task_wdt_reset();
      
      float v = safeFloat(getInternalVoltage());
      float temp = (hasBme && bmeStatus) ? safeFloat(bme.readTemperature()) : 0.0;
      float hum = (hasBme && bmeStatus) ? safeFloat(bme.readHumidity()) : 0.0;
      float pressure = (hasBme && bmeStatus) ? safeFloat(bme.readPressure() / 100.0F) : 0.0;
      int rawMoisture = analogRead(SOIL_PIN);
      int rawLight = hasLight ? analogRead(LIGHT_PIN) : 0; 
      
      float waterTemp = 0.0;
      if(hasDs) {
          waterSensor.requestTemperatures();
          waterTemp = safeFloat(waterSensor.getTempCByIndex(0)) + safeFloat(tempOffset);
          if(waterTemp == DEVICE_DISCONNECTED_C || waterTemp < -50.0) waterTemp = 0.0;
      }
      
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
      
      doc["service_mode"] = isServiceMode ? 1 : 0;
      doc["free_heap"] = ESP.getFreeHeap();
      doc["vent_state"] = hasVent ? digitalRead(VENT_PIN) : 0;

      JsonObject hw = doc["hw"].to<JsonObject>();
      hw["us"] = hasUs; hw["ds"] = hasDs; hw["bme"] = hasBme; hw["light"] = hasLight; hw["vent"] = hasVent;

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
      String jsonResponse; serializeJson(doc, jsonResponse);
      request->send(200, "application/json", jsonResponse);
    });

    server.on("/hwConfig", HTTP_GET, [](AsyncWebServerRequest *request){
      if(!isServiceMode) configStartTime = millis();
      if(request->hasParam("us")) tmp_hwUs = request->getParam("us")->value().toInt(); else tmp_hwUs = hasUs;
      if(request->hasParam("ds")) tmp_hwDs = request->getParam("ds")->value().toInt(); else tmp_hwDs = hasDs;
      if(request->hasParam("bme")) tmp_hwBme = request->getParam("bme")->value().toInt(); else tmp_hwBme = hasBme;
      if(request->hasParam("light")) tmp_hwLight = request->getParam("light")->value().toInt(); else tmp_hwLight = hasLight;
      if(request->hasParam("vent")) tmp_hwVent = request->getParam("vent")->value().toInt(); else tmp_hwVent = hasVent;
      flag_hwConfig = true;
      request->send(200, "text/plain", "OK");
    });

    server.on("/serviceMode", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasParam("state")) {
        isServiceMode = (request->getParam("state")->value().toInt() == 1);
        if(isServiceMode) serviceStartTime = millis(); 
      }
      request->send(200, "text/plain", "OK");
    });

    server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request){
      if(!isServiceMode) configStartTime = millis();
      if(request->hasParam("thPct") && request->hasParam("timeSec") && request->hasParam("aggr")) {
        tmp_th = request->getParam("thPct")->value().toInt();
        tmp_time = request->getParam("timeSec")->value().toInt();
        tmp_aggr = request->getParam("aggr")->value().toInt();
        flag_saveConfig = true;
        request->send(200, "text/plain", "OK");
      } else request->send(400);
    });

    server.on("/tank", HTTP_GET, [](AsyncWebServerRequest *request){
      if(!isServiceMode) configStartTime = millis();
      if(request->hasParam("vol") && request->hasParam("flow")) {
        tmp_vol = request->getParam("vol")->value().toInt();
        tmp_flow = request->getParam("flow")->value().toFloat();
        flag_saveTank = true;
        request->send(200, "text/plain", "OK");
      } else request->send(400);
    });

    server.on("/calibrate", HTTP_GET, [](AsyncWebServerRequest *request){
      if(!isServiceMode) configStartTime = millis();
      if(request->hasParam("air")) tmp_air = request->getParam("air")->value().toInt(); else tmp_air = airValue;
      if(request->hasParam("water")) tmp_water = request->getParam("water")->value().toInt(); else tmp_water = waterValue;
      flag_saveCalib = true;
      request->send(200, "text/plain", "OK");
    });

    server.on("/calibrateExtras", HTTP_GET, [](AsyncWebServerRequest *request){
      if(!isServiceMode) configStartTime = millis();
      if(request->hasParam("tOff")) tmp_tOff = request->getParam("tOff")->value().toFloat(); else tmp_tOff = tempOffset;
      if(request->hasParam("usMax")) tmp_usMax = request->getParam("usMax")->value().toInt(); else tmp_usMax = usMax;
      if(request->hasParam("usMin")) tmp_usMin = request->getParam("usMin")->value().toInt(); else tmp_usMin = usMin;
      if(request->hasParam("lMax")) tmp_lMax = request->getParam("lMax")->value().toInt(); else tmp_lMax = maxLight;
      flag_saveExtras = true;
      request->send(200, "text/plain", "OK");
    });

    server.on("/location", HTTP_GET, [](AsyncWebServerRequest *request){
      if(!isServiceMode) configStartTime = millis();
      if(request->hasParam("outdoors")) {
        tmp_outdoors = request->getParam("outdoors")->value().toInt();
        flag_saveLocation = true;
        request->send(200, "text/plain", "OK");
      } else request->send(400);
    });

    server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request){
      if(!isServiceMode) configStartTime = millis();
      bool force = false;
      if(request->hasParam("force") && request->getParam("force")->value().toInt() == 1) force = true;
      if(digitalRead(FLOAT_PIN) == HIGH && !force) request->send(403, "text/plain", "Brak Wody!");
      else if(request->hasParam("sec")) {
        tmp_testSec = request->getParam("sec")->value().toInt();
        tmp_testForce = force;
        flag_testPump = true;
        request->send(200, "text/plain", "OK");
      }
    });

    server.on("/drain", HTTP_GET, [](AsyncWebServerRequest *request){
      if(!isServiceMode) configStartTime = millis();
      if(digitalRead(FLOAT_PIN) == HIGH) request->send(200, "text/plain", "ALREADY_EMPTY");
      else { flag_drainPump = true; request->send(200, "text/plain", "DRAINED"); }
    });

    server.on("/sync", HTTP_GET, [](AsyncWebServerRequest *request){
      if(!isServiceMode) configStartTime = millis();
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
      if(!isServiceMode) configStartTime = millis();
      flag_resetFault = true;
      request->send(200, "text/plain", "OK");
    });
    
    server.on("/resetWater", HTTP_GET, [](AsyncWebServerRequest *request){
      if(!isServiceMode) configStartTime = millis();
      flag_resetWater = true;
      request->send(200, "text/plain", "OK");
    });

    server.on("/hibernate", HTTP_GET, [](AsyncWebServerRequest *request){
      flag_hibernate = true;
      request->send(200, "text/plain", "HIBERNATING");
    });

    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
      shouldGoToSleep = true; sleepDelayTimer = millis(); 
      request->send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
      delay(1000); ESP.restart();
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
      configStartTime = millis(); serviceStartTime = millis();
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
      float t = (hasBme && bmeStatus) ? safeFloat(bme.readTemperature()) : 0;
      float h = (hasBme && bmeStatus) ? safeFloat(bme.readHumidity()) : 0;
      saveLog(100, 4, currentV, t, h);
    }
    SystemGoToSleep(14400); 
  } 
  else {
    float currentPressure = (hasBme && bmeStatus) ? safeFloat(bme.readPressure() / 100.0F) : 0.0;
    float currentTemp = (hasBme && bmeStatus) ? safeFloat(bme.readTemperature()) : 0.0;
    float currentHum = (hasBme && bmeStatus) ? safeFloat(bme.readHumidity()) : 0.0;
    if (hasBme && currentPressure > 500) { preferences.begin("sys", false); preferences.putFloat("lastP", currentPressure); preferences.end(); }

    if(hasVent) {
      if(currentTemp > TEMP_LIMIT) digitalWrite(VENT_PIN, HIGH);
      else if (currentTemp < TEMP_VENT_CLOSE) digitalWrite(VENT_PIN, LOW);
    }

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
    int rawLight = hasLight ? analogRead(LIGHT_PIN) : 0; 
    
    float waterTemp = 0.0;
    if(hasDs) {
       waterSensor.requestTemperatures(); waterTemp = safeFloat(waterSensor.getTempCByIndex(0)) + safeFloat(tempOffset);
    }
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
    if (hasLight && rawLight > 2500 && timeIsSet) sunSecondsToday += calculatedSleepSec;
    
    float svp = safeFloat(0.61078 * exp((17.27 * currentTemp) / (currentTemp + 237.3)));
    float avp = safeFloat(svp * (currentHum / 100.0));
    float currentVPD = svp - avp;
    bool isSauna = (hasBme && currentVPD < 0.4 && currentVPD > 0.0);

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
        bool stormApproaching = (hasBme && isOutdoors == 1 && lastPressure > 500 && currentPressure > 500 && (lastPressure - currentPressure) >= 2.0);
        
        if (failCount >= 3) actionTaken = 6; 
        else if (stormApproaching) actionTaken = 7; 
        else if (hasLight && rawLight > 3000 && isOutdoors == 1) actionTaken = 5;  
        else if (digitalRead(FLOAT_PIN) == HIGH) actionTaken = 2;  
        else if (hasBme && currentTemp >= TEMP_LIMIT && isOutdoors == 0) actionTaken = 3; 
        else if (hasDs && waterTemp > 0.0 && waterTemp < WATER_MIN_TEMP) actionTaken = 10;  
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
    // --- BEZPIECZNE WYKONYWANIE ZADAŃ SEKWENCYJNYCH W JEDNYM WĄTKU ---
    if (flag_saveConfig) {
      preferences.begin("garden", false);
      preferences.putInt("th", tmp_th); preferences.putInt("time", tmp_time); preferences.putInt("aggr", tmp_aggr);
      preferences.end();
      dryThresholdPct = tmp_th; wateringTimeSec = tmp_time; aggrMode = tmp_aggr;
      flag_saveConfig = false;
    }
    if (flag_saveTank) {
      preferences.begin("garden", false);
      preferences.putInt("tVol", tmp_vol); preferences.putFloat("pFlow", tmp_flow);
      preferences.end();
      flag_saveTank = false;
    }
    if (flag_saveCalib) {
      preferences.begin("garden", false);
      preferences.putInt("airVal", tmp_air); preferences.putInt("waterVal", tmp_water);
      preferences.end();
      airValue = tmp_air; waterValue = tmp_water;
      flag_saveCalib = false;
    }
    if (flag_saveExtras) {
      preferences.begin("garden", false);
      preferences.putFloat("tOffset", tmp_tOff); preferences.putInt("usMax", tmp_usMax);
      preferences.putInt("usMin", tmp_usMin); preferences.putInt("maxLight", tmp_lMax);
      preferences.end();
      tempOffset = tmp_tOff; usMax = tmp_usMax; usMin = tmp_usMin; maxLight = tmp_lMax;
      flag_saveExtras = false;
    }
    if (flag_saveLocation) {
      preferences.begin("sys", false); preferences.putInt("outdoors", tmp_outdoors); preferences.end();
      flag_saveLocation = false;
    }
    if (flag_hwConfig) {
      preferences.begin("hw", false);
      preferences.putInt("hasUs", tmp_hwUs); preferences.putInt("hasDs", tmp_hwDs);
      preferences.putInt("hasBme", tmp_hwBme); preferences.putInt("hasLight", tmp_hwLight);
      preferences.putInt("hasVent", tmp_hwVent);
      preferences.end();
      hasUs = tmp_hwUs; hasDs = tmp_hwDs; hasBme = tmp_hwBme; hasLight = tmp_hwLight; hasVent = tmp_hwVent;
      if(hasVent) pinMode(VENT_PIN, OUTPUT); else digitalWrite(VENT_PIN, LOW);
      flag_hwConfig = false;
    }
    if (flag_testPump) {
      digitalWrite(RELAY_PIN, HIGH);
      for(int i=0; i<tmp_testSec; i++) { delay(1000); esp_task_wdt_reset(); }
      digitalWrite(RELAY_PIN, LOW);
      addPumpSeconds(tmp_testSec);
      flag_testPump = false;
    }
    if (flag_drainPump) {
      int drainedSec = 0; digitalWrite(RELAY_PIN, HIGH);
      while (digitalRead(FLOAT_PIN) == LOW && drainedSec < 120) { delay(1000); drainedSec++; esp_task_wdt_reset(); }
      digitalWrite(RELAY_PIN, LOW); addPumpSeconds(drainedSec);
      flag_drainPump = false;
    }
    if (flag_resetFault) {
      preferences.begin("sys", false); preferences.putInt("fail", 0); preferences.end();
      flag_resetFault = false;
    }
    if (flag_resetWater) {
      preferences.begin("sys", false); preferences.putInt("pumpSec", 0); preferences.end();
      flag_resetWater = false;
    }
    if (flag_hibernate) {
      preferences.begin("sys", false); preferences.putInt("hib", 1); preferences.end();
      flag_hibernate = false;
      shouldGoToSleep = true; sleepDelayTimer = millis(); 
    }

    // --- TIMEOUTY USYPIANIA ---
    if (isServiceMode) {
       if (millis() - serviceStartTime > 3600000) { 
           isServiceMode = false;
           SystemGoToSleep(14400); 
       }
    } else {
       if (shouldGoToSleep && !isUpdating && (millis() - sleepDelayTimer > 1500)) SystemGoToSleep(14400);
       if (!isUpdating && (millis() - configStartTime > 300000)) SystemGoToSleep(14400); 
    }
  }
}
