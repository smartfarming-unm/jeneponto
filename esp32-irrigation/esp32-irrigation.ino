#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"

//-------------------------------------------
// Konfigurasi WiFi
//-------------------------------------------
#define WIFI_SSID "tselhome-2D72"
#define WIFI_PASSWORD "70191478"

//-------------------------------------------
// Konfigurasi Firebase
//-------------------------------------------
#define API_KEY "AIzaSyC_VCFAE8_HnP0BBziH_V616Lr-adexrfA"
#define DATABASE_URL "https://okesmartfarming-default-rtdb.firebaseio.com/"

//-------------------------------------------
// Konfigurasi NTP
//-------------------------------------------
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600;
const int daylightOffset_sec = 0;

//-------------------------------------------
// Pin Pompa (PWM) & Switch
//-------------------------------------------
int pwmPin = 25;
int switchPin = 26;
int ledBuiltin = 2;
int pwmValue = 0;
bool pumpRunning = false;

//-------------------------------------------
// Threshold
//-------------------------------------------
int thresholdKelembaban = 60;

//---------------------------------------------------------
// Function: GET data from Firebase
//---------------------------------------------------------
String firebaseGET(String path) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi terputus, tidak dapat mengambil data Firebase.");
    return "";
  }

  HTTPClient http;
  
  String url = String(DATABASE_URL) + path + ".json";
  http.begin(url);

  int httpCode = http.GET();
  String payload = "";

  if (httpCode == 200) {
    payload = http.getString();
  } else {
    Serial.printf("HTTP GET Error: %d\n", httpCode);
  }

  http.end();
  return payload;
}

//-------------------------------------------
// SETUP
//-------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(pwmPin, OUTPUT);
  pinMode(switchPin, OUTPUT);
  pinMode(ledBuiltin, OUTPUT);

  analogWrite(pwmPin, 255); // default OFF
  digitalWrite(switchPin, LOW);
  digitalWrite(ledBuiltin, LOW);

  //-----------------------------------------
  // Koneksi WiFi
  //-----------------------------------------
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Menghubungkan WiFi");

  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    
    if (millis() - startAttemptTime > 30000) {
      Serial.println("\n\n[ERROR] Koneksi WiFi Timeout");
      Serial.println("Gagal terhubung ke jaringan. Merestart ESP32...");
      ESP.restart();
    }
  }
  Serial.println("\nWiFi terhubung!");

  //-----------------------------------------
  // Sinkronisasi waktu via NTP
  //-----------------------------------------
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Sinkronisasi waktu NTP...");

  // Ambil nilai threshold kelembaban tanah dari Firebase
  String kelembabanStr = firebaseGET("/sensor/threshold_kelembaban");

  if (kelembabanStr.length() == 0) {
    Serial.println("Error: Gagal mengambil data threshold dari Firebase");
    Serial.printf("Threshold kelembaban default", thresholdKelembaban);
  } else {
    thresholdKelembaban = kelembabanStr.toInt();
    Serial.printf("Threshold kelembaban: %d %%\n", thresholdKelembaban);
  }

  digitalWrite(ledBuiltin, HIGH);
  Serial.println("-------------------------------------------------------------------");

  delay(3000);
}

//-------------------------------------------
// LOOP
//-------------------------------------------
void loop() {
  // Cek status WiFi, jika putus, coba reconnect
  if(WiFi.status() != WL_CONNECTED) {
    digitalWrite(ledBuiltin, LOW);
    Serial.println("WiFi terputus! Mencoba reconnect...");
    WiFi.disconnect();
    WiFi.reconnect();
    delay(5000); 

    // Jika masih gagal terhubung setelah percobaan ulang, lakukan restart
    if(WiFi.status() != WL_CONNECTED) {
      Serial.println("Gagal reconnect dalam batas waktu. Merestart ESP32...");
      ESP.restart();
    }

    return;
  } else{
    digitalWrite(ledBuiltin, HIGH); 
  }

  struct tm timeInfo;
  bool timeSuccess = getLocalTime(&timeInfo);

  //-------------------------------------------------------------
  // Baca data kelembaban tanah dari Firebase
  //-------------------------------------------------------------
  String dataSensor = firebaseGET("/sensor/tanah/soil2/kelembapan_tanah_persen");
  int kelembaban = 0;
  
  if(dataSensor != "" && dataSensor != "null") {
    kelembaban = dataSensor.toInt();
    Serial.printf("Kelembaban Tanah: %d %%\n", kelembaban);
  } else {
    Serial.println("Gagal baca sensor dari Firebase");
  }

  // Tampilkan status Pompa saat ini
  if (!pumpRunning){
    Serial.println("Status Pompa: OFF");
  } else {
    Serial.println("Status Pompa: ON");
  }

  bool kondisiLembab = (kelembaban >= thresholdKelembaban);
  bool nyalakanPompa = false;

  //-------------------------------------------------------------
  // LOGIKA KONTROL
  //-------------------------------------------------------------
  if (!timeSuccess) {
    // --- SKENARIO 1: Waktu NTP Gagal ---
    Serial.println("[Mode Tanpa Jadwal] NTP Error");
    if (!kondisiLembab) {
      nyalakanPompa = true;
    } else {
      nyalakanPompa = false;
    }

  } else {
    // --- SKENARIO 2: Waktu NTP Berhasil ---
    int jam = timeInfo.tm_hour;
    int menit = timeInfo.tm_min;
    int detik = timeInfo.tm_sec;
    Serial.printf("Waktu: %02d:%02d:%02d\n", jam, menit, detik);

    // Rentang waktu penyiraman: 06:00 – 09:00
    bool waktuPenyiraman = (jam >= 6 && jam < 9); 

    if (waktuPenyiraman) {
      Serial.println("-> Dalam Jam Penyiraman (06:00-09:00)");
      if (!kondisiLembab) {
        nyalakanPompa = true;
      } else {
        nyalakanPompa = false; // Tanah sudah basah
        Serial.println("Tanah cukup lembab.");
      }
    } else {
      Serial.println("-> Di Luar Jam Penyiraman (Pompa OFF)");
      nyalakanPompa = false;
    }
  }

  //-------------------------------------------------------------
  // EKSEKUSI POMPA
  //-------------------------------------------------------------
  if (nyalakanPompa) {
    if (!pumpRunning) {
      Serial.println("MEMULAI PENYIRAMAN...");
      digitalWrite(switchPin, HIGH);

      for (pwmValue = 255; pwmValue >= 0; pwmValue--) {
        analogWrite(pwmPin, pwmValue);
        delay(10);
      }
      pumpRunning = true;
    } else {
      analogWrite(pwmPin, 0);
      digitalWrite(switchPin, HIGH);
    }

  } else {
    if (pumpRunning) {
      Serial.println("MENGHENTIKAN PENYIRAMAN.");
      analogWrite(pwmPin, 255);
      digitalWrite(switchPin, LOW);
      pumpRunning = false;
    } else {
      analogWrite(pwmPin, 255);
      digitalWrite(switchPin, LOW);
    }
  }

  Serial.println("-------------------------------------------------------------------");
  delay(2000);
}
