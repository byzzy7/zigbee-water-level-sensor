/*
  Zigbee senzor hladiny vody - LaskaKit ESP32-C6-DEVKit V2 + JSN-SR04T
  ---------------------------------------------------------
  VERZE 2 - používá STANDARDNÍ Zigbee senzor typ (vlhkost/Humidity), ne vlastní
  AnalogInput cluster. Důvod: vlastní cluster potřeboval external converter
  v Zigbee2MQTT, který se ukázal jako nespolehlivý (hodnoty se nepublikovaly
  do MQTT, i když je zařízení odeslalo). Standardní typ senzoru Z2M/HA
  rozpozná automaticky, BEZ jakéhokoliv external converteru.

  - Měří vzdálenost k hladině ultrazvukovým senzorem JSN-SR04T
  - Přepočítá na % naplnění nádrže
  - Odešle % jako "vlhkost" (Humidity) přes Zigbee - v HA se přejmenuje na "Hladina vody"
  - Litry se počítají přímo v Home Assistant šablonou (% x kapacita nádrže / 100)
  - Mezi měřeními jde do deep sleep -> úspora baterie
  - Napájení senzoru se zapíná/vypíná přes MOSFET modul (N-kanál, low-side spínač)

  NASTAVENÍ V ARDUINO IDE (Tools menu):
    Board:            ESP32C6 Dev Module
    Zigbee mode:       Zigbee ED (end device)
    Partition Scheme:  Zigbee 4MB with spiffs
    Erase All Flash Before Sketch Upload: Enabled (jen při prvním nahrání / novém párování)

  Než začneš: uprav si konstanty v sekci "NASTAVENÍ NÁDOBY" a "NASTAVENÍ PINŮ" níže.
*/

#include "Zigbee.h"

// ---------------------- NASTAVENÍ NÁDOBY (IBC 1000 l) ----------------------
const float TANK_HEIGHT_CM   = 100.0;
const float TANK_FULL_DIST_CM = 8.0;
const float TANK_VOLUME_L    = 1000.0;

// ---------------------- NASTAVENÍ PINŮ (LaskaKit ESP32-C6-DEVKit V2) ----------------------
const uint8_t PIN_TRIG        = 12;
const uint8_t PIN_ECHO        = 13;
const uint8_t PIN_SENSOR_PWR  = 10;

// ---------------------- ZIGBEE ENDPOINT ----------------------
// Používáme "Temp Sensor" endpoint s přidaným Humidity clusterem - to je STANDARDNÍ
// Zigbee typ, který Z2M/HA umí rozpoznat automaticky. Teplotu neřešíme (necháme na 0),
// zajímá nás jen vlhkost (=naše procento hladiny).
#define EP_LEVEL 10
ZigbeeTempSensor zbLevel = ZigbeeTempSensor(EP_LEVEL);

const uint64_t SLEEP_SECONDS = 1800;

float measureDistanceCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(5);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long duration = pulseIn(PIN_ECHO, HIGH, 25000);
  if (duration == 0) return -1;
  return duration / 58.0;
}

float measureDistanceMedian(uint8_t samples = 5) {
  float readings[10];
  uint8_t valid = 0;
  for (uint8_t i = 0; i < samples && valid < 10; i++) {
    float d = measureDistanceCm();
    if (d > 0) readings[valid++] = d;
    delay(60);
  }
  if (valid == 0) return -1;
  for (uint8_t i = 0; i < valid - 1; i++) {
    for (uint8_t j = 0; j < valid - i - 1; j++) {
      if (readings[j] > readings[j + 1]) {
        float tmp = readings[j];
        readings[j] = readings[j + 1];
        readings[j + 1] = tmp;
      }
    }
  }
  return readings[valid / 2];
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_SENSOR_PWR, OUTPUT);
  digitalWrite(PIN_SENSOR_PWR, LOW);

  uint8_t batteryPercent = 100; // zatím pevná hodnota

  // ---------------------- ZIGBEE SETUP ----------------------
  zbLevel.setManufacturerAndModel("DIY", "WaterLevelSensor");
  zbLevel.addHumiditySensor(0, 100, 1);  // rozsah 0-100%, tolerance 1
  zbLevel.setMinMaxValue(0, 50);          // rozsah pro (nepoužívaný) teplotní cluster
  zbLevel.setPowerSource(ZB_POWER_SOURCE_BATTERY, batteryPercent);

  Zigbee.addEndpoint(&zbLevel);
  Zigbee.setRxOnWhenIdle(true); // spolehlivější doručování (viz historie ladění)

  Serial.println("Startuji Zigbee...");
  if (!Zigbee.begin(ZIGBEE_END_DEVICE)) {
    Serial.println("Zigbee se nepodarilo spustit, restartuji...");
    delay(1000);
    ESP.restart();
  }

  Serial.println("Pripojuji se k siti...");
  uint32_t start = millis();
  while (!Zigbee.connected()) {
    delay(100);
    if (millis() - start > 30000) {
      Serial.println("Nepodarilo se pripojit, jdu spat a zkusim pozdeji.");
      goToSleep();
    }
  }
  Serial.println("Pripojeno!");

  // ---------------------- MĚŘENÍ ----------------------
  digitalWrite(PIN_SENSOR_PWR, HIGH);
  delay(1000);

  float distance = measureDistanceMedian();

  digitalWrite(PIN_SENSOR_PWR, LOW);

  float percent = 0;

  if (distance > 0) {
    if (distance > TANK_HEIGHT_CM) distance = TANK_HEIGHT_CM;
    if (distance < TANK_FULL_DIST_CM) distance = TANK_FULL_DIST_CM;
    percent = (1.0 - (distance - TANK_FULL_DIST_CM) / (TANK_HEIGHT_CM - TANK_FULL_DIST_CM)) * 100.0;
    Serial.printf("Vzdalenost: %.1f cm -> %.1f %%\n", distance, percent);
  } else {
    Serial.println("Mereni selhalo (mimo dosah), hlasim posledni hodnoty.");
  }

  Serial.printf("Odesilam: hladina=%.1f %%\n", percent);
  zbLevel.setTemperature(0); // nepoužíváme, jen vyplnění povinné hodnoty
  zbLevel.setHumidity(percent);
  zbLevel.report();

  zbLevel.setBatteryPercentage(batteryPercent);
  zbLevel.reportBatteryPercentage();

  delay(3000);
  goToSleep();
}

void goToSleep() {
  Serial.println("Jdu spat...");
  Serial.flush();
  esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {
}
