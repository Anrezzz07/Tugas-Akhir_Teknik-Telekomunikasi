#include <WiFi.h>
#include <FirebaseESP32.h>
#include "DHT.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <time.h> // WAKTU DARI NTP

// === WiFi ===
#define WIFI_SSID "Anrezzz"
#define WIFI_PASSWORD "gelapgulita88"

// === Firebase ===
#define FIREBASE_HOST "https://temhum-64803-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "AIzaSyBQXk6THQlCZjE7tFPCqYSacd59k93Cl8Y"

// === Sensor ===
#define DHTPIN 16
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
#define WATER_SENSOR_PIN 34

// === Relay ===
#define RELAY_MINUM_PIN 26
#define RELAY_VITAMIN_PIN 25
#define RELAY_LAMPU_PIN 23

// === Kipas PWM L298N ===
#define enable1Pin 19
#define motor1Pin1 33
#define motor1Pin2 32

const int freq = 30000;
const int pwmChannel = 4;
const int resolution = 8;
int dutyCycle = 0;
String statusKipas = "";
int pwmValue = 0;

// === LCD ===
LiquidCrystal_I2C lcd(0x27, 16, 2);

// === Firebase Object ===
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// === Status Global ===
String statusLampuFirebase = "OFF";

// Fuzzy logic parameters
const int KIPAS_MATI = 0;
const int KIPAS_RENDAH = 80;
const int KIPAS_SEDANG = 160;
const int KIPAS_TINGGI = 200;

// Serial2 RX/TX ke Arduino (ubah jika perlu)
#define RXD2 18  // ESP32 RX2 (ke TX Arduino)
#define TXD2 17  // ESP32 TX2 (ke RX Arduino)

// Variabel hasil baca dari Arduino
float jarakPakan = -1.0;
String statusPakan = "OFF"; // untuk status: Penuh/Habis

// === Jadwal Vitamin via NTP ===
int jamVitamin = 12;
int menitVitamin = 00;
bool vitaminSudahDiberiHariIni = false;
int lastCheckedDay = -1;

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2); // Untuk komunikasi dengan Arduino

  dht.begin();
  delay(1000);

  pinMode(WATER_SENSOR_PIN, INPUT);

  pinMode(RELAY_MINUM_PIN, OUTPUT);
  pinMode(RELAY_VITAMIN_PIN, OUTPUT);
  pinMode(RELAY_LAMPU_PIN, OUTPUT);
  digitalWrite(RELAY_MINUM_PIN, HIGH);
  digitalWrite(RELAY_VITAMIN_PIN, HIGH);
  digitalWrite(RELAY_LAMPU_PIN, HIGH);

  pinMode(motor1Pin1, OUTPUT);
  pinMode(motor1Pin2, OUTPUT);
  pinMode(enable1Pin, OUTPUT);
  digitalWrite(motor1Pin1, LOW);
  digitalWrite(motor1Pin2, HIGH);
  ledcAttach(enable1Pin, freq, resolution);
  ledcWrite(enable1Pin, 0);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Connect WiFi...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ WiFi Terhubung");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected!");

  config.api_key = FIREBASE_AUTH;
  config.database_url = FIREBASE_HOST;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("✅ Firebase SignUp berhasil");
  } else {
    Serial.printf("❌ SignUp gagal: %s\n", config.signer.signupError.message.c_str());
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // ----------- NTP Setup -----------
  // Set zona waktu ke WIB (GMT+7)
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("⏳ Sinkronisasi waktu");
  while (time(nullptr) < 100000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\n✅ Waktu tersinkron");
}

int readWaterLevel() {
  int adcValue = analogRead(WATER_SENSOR_PIN);
  int tinggiAirMM = map(adcValue, 0, 4095, 0, 100);  // 40 mm tinggi maksimal
  return tinggiAirMM;
}

void aktifkanRelay(int pin, const char* nama, const char* statusPath) {
  Serial.print("⚙️ Relay ON: "); Serial.println(nama);
  digitalWrite(pin, LOW);
  if (Firebase.ready()) {
    Firebase.setString(fbdo, statusPath, "ON");
  }
  delay(2000);
  digitalWrite(pin, HIGH);
  Serial.print("⚙️ Relay OFF: "); Serial.println(nama);
  if (Firebase.ready()) {
    Firebase.setString(fbdo, statusPath, "OFF");
  }
}

void kontrolLampu(float suhu) {
  if (suhu > 28.20) {
    digitalWrite(RELAY_LAMPU_PIN, LOW);
    if (Firebase.ready()) Firebase.setString(fbdo, "/Status/Status_Lampu", "OFF");
  } else {
    digitalWrite(RELAY_LAMPU_PIN, HIGH);
    if (Firebase.ready()) Firebase.setString(fbdo, "/Status/Status_Lampu", "ON");
  }
  Serial.print("💡 Lampu: ");
  if (suhu > 28.20) Serial.println("OFF");
  else Serial.println("ON");
}
// ====== FUZZY PADA KIPAS DC =====
int fuzzyFanControl(float humidity) {
  if (humidity >= 30 && humidity < 60) return KIPAS_RENDAH;
  else if (humidity >= 60 && humidity < 75) return KIPAS_SEDANG;
  else if (humidity >= 75 && humidity <= 90) return KIPAS_TINGGI;
  else return KIPAS_MATI;
}
void kontrolKipas(float kelembapan) {
  pwmValue = fuzzyFanControl(kelembapan);
  if (pwmValue == KIPAS_MATI) statusKipas = "OFF";
  else if (pwmValue == KIPAS_RENDAH) statusKipas = "LOW";
  else if (pwmValue == KIPAS_SEDANG) statusKipas = "MID";
  else if (pwmValue == KIPAS_TINGGI) statusKipas = "HIGH";

  if (pwmValue > 0) {
    digitalWrite(motor1Pin1, LOW);
    digitalWrite(motor1Pin2, HIGH);
    ledcWrite(enable1Pin, pwmValue);
  } else {
    digitalWrite(motor1Pin1, LOW);
    digitalWrite(motor1Pin2, LOW);
    ledcWrite(enable1Pin, 0);
  }

  if (Firebase.ready()) {
    Firebase.setString(fbdo, "/Status/Status_Kipas", statusKipas);
  }

  Serial.print("🌀 Kipas: "); Serial.print(statusKipas);
  Serial.print(" (Humidity: "); Serial.print(kelembapan);
  Serial.print("%, PWM: "); Serial.print(pwmValue); Serial.println(")");
}
// ========== END KIPAS ===============

// Fungsi parsing data dari Arduino
void bacaDataDariArduino() {
  while (Serial2.available()) {
    String data = Serial2.readStringUntil('\n');
    Serial.print("[Dari Arduino] "); Serial.println(data);

    // Format data dari Arduino: "Jarak: <angka> | Status: <Penuh/Habis/Error>"
    int idxJarak = data.indexOf("Jarak:");
    int idxStatus = data.indexOf("| Status:");

    if (idxJarak != -1 && idxStatus != -1) {
      String sJarak = data.substring(idxJarak + 6, idxStatus);
      sJarak.trim();
      jarakPakan = sJarak.toFloat();

      String statusStr = data.substring(idxStatus + 9); // setelah "| Status: "
      statusStr.trim();
      statusPakan = statusStr;
    }
  }
}

// BACA SENSOR KE ESP 32
void loop() {
  // Baca data dari Arduino jika ada
  bacaDataDariArduino();

//KALIBRASI SENSOR 
  float suhu = 0.969 * dht.readTemperature() + 0.2553;
  float kelembapan = 0.9466 * dht.readHumidity() - 0.5885;
  int tinggiAirMM = 0.893 * readWaterLevel() + 2.21;
  float volumeMinum = tinggiAirMM; 
// volumeMinum sekarang menyimpan ketinggian air (mm)

  if (volumeMinum < 10) {
    aktifkanRelay(RELAY_MINUM_PIN, "Pompa Minum", "/Status/Status_Pompa_Minum");
  }

  kontrolLampu(suhu);
  kontrolKipas(kelembapan);

  Serial.println("\n📡 Mengirim data ke Firebase:");
  Serial.print("🌡 Suhu: "); Serial.print(suhu); Serial.println(" °C");
  Serial.print("💧 Kelembapan: "); Serial.print(kelembapan); Serial.println(" %");
  Serial.print("📦 Status Pakan: "); Serial.println(statusPakan);
  Serial.print("🚰 Sisa Minum (mm): "); Serial.println(tinggiAirMM);

  if (Firebase.ready()) {
    Firebase.setFloat(fbdo, "/Sensor/Suhu", suhu);
    Firebase.setFloat(fbdo, "/Sensor/Kelembapan", kelembapan);
    float pakanUntukFirebase = (jarakPakan > 0 && jarakPakan <= 10) ? jarakPakan : 0;
    Firebase.setFloat(fbdo, "/Sensor/Sisa_Pakan", pakanUntukFirebase);
    Firebase.setFloat(fbdo, "/Sensor/Sisa_Minuman", volumeMinum);
  }

// TAMPILAN KE LCD 
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("S:");
  lcd.print(suhu, 1);
  lcd.print("C H:");
  lcd.print(kelembapan, 0);
  lcd.print("%");

  lcd.setCursor(0, 1);
  lcd.print("P:");
  lcd.print(jarakPakan, 0);
  lcd.print("cm ");

  lcd.print("M:");
  lcd.print(volumeMinum, 0);
  lcd.print("mm ");

  // === KONTROL MANUAL DARI FIREBASE ===
  if (Firebase.ready()) {
    if (Firebase.getString(fbdo, "/KONTROL/Kipas")) {
      if (fbdo.stringData() == "1") {
        Serial.println("🌀 Manual: Kipas FULL");
        digitalWrite(motor1Pin1, LOW);
        digitalWrite(motor1Pin2, HIGH);
        ledcWrite(enable1Pin, 255);
        delay(15000);
        ledcWrite(enable1Pin, 0);
        Firebase.setString(fbdo, "/KONTROL/Kipas", "0");
      }
    }

    if (Firebase.getString(fbdo, "/KONTROL/Lampu")) {
      if (fbdo.stringData() == "1") {
        Serial.println("💡 Manual: Lampu ON");
        digitalWrite(RELAY_LAMPU_PIN, LOW);
        delay(15000);
        digitalWrite(RELAY_LAMPU_PIN, HIGH);
        Firebase.setString(fbdo, "/KONTROL/Lampu", "0");
      }
    }

    if (Firebase.getString(fbdo, "/KONTROL/Pompa_Minum")) {
      if (fbdo.stringData() == "1") {
        aktifkanRelay(RELAY_MINUM_PIN, "Pompa Minum", "/Status/Status_Pompa_Minum");
        Firebase.setString(fbdo, "/KONTROL/Pompa_Minum", "0");
      }
    }

    if (Firebase.getString(fbdo, "/KONTROL/Vitamin")) {
      if (fbdo.stringData() == "1") {
        aktifkanRelay(RELAY_VITAMIN_PIN, "Pompa Vitamin", "/Status/Status_Vitamin");
        Firebase.setString(fbdo, "/KONTROL/Vitamin", "0");
      }
    }
        if (Firebase.getString(fbdo, "/KONTROL/Servo")) {
      String nilai = fbdo.stringData();
      if (nilai == "1" || nilai == "0") {
        Serial.print("📨 Kirim perintah ke Arduino (Servo): ");
        Serial.println(nilai);
        Serial2.println(nilai);  // Kirim hanya "1" atau "0"
        Firebase.setString(fbdo, "/KONTROL/Servo", "0");
      }
    }
  }

  // ===== JADWAL VITAMIN OTOMATIS JAM 12:00 (NTP) ======
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  int jam = timeinfo->tm_hour;
  int menit = timeinfo->tm_min; 
  int hariIni = timeinfo->tm_mday;

  Serial.print("🕒 Waktu sekarang: ");
  Serial.print(timeinfo->tm_hour); Serial.print(":");
  Serial.print(timeinfo->tm_min); Serial.print(" ");

  if (hariIni != lastCheckedDay) {
    vitaminSudahDiberiHariIni = false;
    lastCheckedDay = hariIni;
  }

 if (jam == jamVitamin && !vitaminSudahDiberiHariIni){
    Serial.println("🧪 Jadwal Vitamin Otomatis (12:00)");
    aktifkanRelay(RELAY_VITAMIN_PIN, "Pompa Vitamin", "/Status/Status_Vitamin");
    vitaminSudahDiberiHariIni = true;
  }
  // =====================================================

  delay(1000);
 }