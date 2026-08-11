#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------- WiFi ----------
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ---------- Supabase ----------
const char* SUPABASE_URL      = "https://YOUR_PROJECT_REF.supabase.co";
const char* SUPABASE_ANON_KEY = "YOUR_ANON_PUBLIC_KEY";
const char* DEVICE_ID          = "node1";

// ---------- Pins ----------
#define SOIL_PIN 34
#define DHT_PIN  4
#define DHT_TYPE DHT11

#define SOIL_DRY_VALUE 4095
#define SOIL_WET_VALUE 1200

DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2); // change to 0x3F if needed

int intervalMinutes = 15; // default, overwritten by fetchConfig()

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("WiFi connection FAILED");
  }
}

// Read soil + DHT, return true if DHT read succeeded
bool readSensors(int &soilPercent, float &tempC, float &humidity) {
  int soilRaw = analogRead(SOIL_PIN);
  soilPercent = map(soilRaw, SOIL_DRY_VALUE, SOIL_WET_VALUE, 0, 100);
  soilPercent = constrain(soilPercent, 0, 100);

  humidity = dht.readHumidity();
  tempC = dht.readTemperature();
  return !isnan(humidity) && !isnan(tempC);
}

// POST one reading row to Supabase
bool postReading(int soilPercent, float tempC, float humidity) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/readings";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_ANON_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);

  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["soil_moisture"] = soilPercent;
  doc["temperature"] = tempC;
  doc["humidity"] = humidity;

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  Serial.print("POST reading -> HTTP ");
  Serial.println(code);
  http.end();
  return code == 201 || code == 200;
}

// GET the farmer-set interval_minutes from device_config
void fetchConfig() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/device_config?device_id=eq." + DEVICE_ID + "&select=interval_minutes,moisture_threshold";
  http.begin(url);
  http.addHeader("apikey", SUPABASE_ANON_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);

  int code = http.GET();
  if (code == 200) {
    String response = http.getString();
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, response);
    if (!err && doc.size() > 0) {
      intervalMinutes = doc[0]["interval_minutes"] | 15;
      Serial.print("Fetched interval_minutes: ");
      Serial.println(intervalMinutes);
    }
  } else {
    Serial.print("GET config -> HTTP ");
    Serial.println(code);
  }
  http.end();
}

void showOnLCD(int soilPercent, float tempC, float humidity, bool dhtOk) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Soil: ");
  lcd.print(soilPercent);
  lcd.print("%");

  lcd.setCursor(0, 1);
  if (dhtOk) {
    lcd.print("T:");
    lcd.print(tempC, 1);
    lcd.print("C H:");
    lcd.print(humidity, 0);
    lcd.print("%");
  } else {
    lcd.print("DHT read error");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  dht.begin();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Booting...");

  connectWiFi();
  fetchConfig(); // get farmer's chosen interval before first reading
}

unsigned long lastReadingTime = 0;

void loop() {
  unsigned long intervalMs = (unsigned long)intervalMinutes * 60UL * 1000UL;

  if (millis() - lastReadingTime >= intervalMs || lastReadingTime == 0) {
    if (WiFi.status() != WL_CONNECTED) connectWiFi();

    int soilPercent;
    float tempC, humidity;
    bool dhtOk = readSensors(soilPercent, tempC, humidity);

    Serial.print("Soil: "); Serial.print(soilPercent); Serial.print("% ");
    if (dhtOk) {
      Serial.print("| Temp: "); Serial.print(tempC);
      Serial.print(" | Humidity: "); Serial.println(humidity);
    } else {
      Serial.println("| DHT read failed");
    }

    showOnLCD(soilPercent, tempC, humidity, dhtOk);

    if (dhtOk) {
      postReading(soilPercent, tempC, humidity);
    }

    fetchConfig(); // re-check in case farmer changed the interval on the dashboard

    lastReadingTime = millis();
  }

  delay(1000);
}
