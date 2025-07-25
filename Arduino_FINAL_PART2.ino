#include <SoftwareSerial.h>
#include <Servo.h>
#define TRIG_PIN 7
#define ECHO_PIN 6
#define SERVO_PIN 9
#define BUZZER_PIN 13

// RX, TX (hubungkan ke TX, RX ESP32)
SoftwareSerial espSerial(10, 11);

Servo servo;
String dataDariESP = "";

// Variabel untuk deteksi perubahan
long lastJarak = -1;
String lastStatusPakan = "";

long readUltrasonicCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 20000); // timeout 20ms
  if (duration == 0) return -1;  // Tidak ada pantulan
  long distance = duration * 0.034 / 2;
  return distance;
}

void setup() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  servo.attach(SERVO_PIN);
  servo.write(0);
  delay(500);
  servo.detach();
  espSerial.begin(9600); // Komunikasi ke ESP32
  Serial.begin(9600);    // Debug monitor
}

void loop() {
  // === Cek Perintah Manual dari ESP32 ===
  if (espSerial.available()) {
    dataDariESP = espSerial.readStringUntil('\n');
    dataDariESP.trim();

    if (dataDariESP == "1") {
      Serial.println("PERINTAH MANUAL: Buka Servo dari ESP");

      if (!servo.attached()) servo.attach(SERVO_PIN);
      digitalWrite(BUZZER_PIN, HIGH);
      delay(1000);
      digitalWrite(BUZZER_PIN, LOW);
      servo.write(90);
      delay(3000);
      servo.write(0);
      delay(500);
      servo.detach();

      espSerial.println("Servo: BUKA_MANUAL");
    } 
    else if (dataDariESP == "0") {
      Serial.println("PERINTAH MANUAL: Tutup Servo dari ESP");

      if (!servo.attached()) servo.attach(SERVO_PIN);
      servo.write(0);
      delay(500);
      servo.detach();
      digitalWrite(BUZZER_PIN, LOW);

      espSerial.println("Servo: TUTUP_MANUAL");
    }
  }

  // === Kalibrasi Jarak ===
  long jarak = round(1.0524 * readUltrasonicCM() - 0.3245);
  String statusPakan = "";

  if (jarak >= 0 && jarak <= 9) {
    statusPakan = "Penuh";
  } else if (jarak >= 10 && jarak <= 13) {
    statusPakan = "Habis";
  } else {
    statusPakan = "Di luar rentang";
  }

  // === Servo buka otomatis jika jarak == 13 ===
  if (jarak == 13) {
    if (!servo.attached()) servo.attach(SERVO_PIN);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);
    servo.write(90);
    delay(2000);
    servo.write(0);
    delay(500);
    servo.detach();
    espSerial.println("Servo: BUKA_AUTO");
  }

  // === Kirim data hanya jika berubah ===
  if (jarak != lastJarak || statusPakan != lastStatusPakan) {
    String pesan = "🥣 Pakan – Jarak: " + String(jarak) + " cm | Status: " + statusPakan;
    Serial.println(pesan);
    espSerial.println(pesan);

    lastJarak = jarak;
    lastStatusPakan = statusPakan;
  }

  delay(500); // Lebih cepat sedikit supaya tetap responsif
}
