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
const long gmtOffset_sec = 8 * 3600;   // GMT+7
const int daylightOffset_sec = 0;

//-------------------------------------------
// Pin Pompa (PWM)
//-------------------------------------------
int pwmPin = 25;
int pwmValue = 0;
bool pumpRunning = false;

//-------------------------------------------
// Threshold
//-------------------------------------------
float thresholdHujan = 0.5;       // 1 = hujan
int thresholdKelembaban = 60; // >60% dianggap cukup lembab

//---------------------------------------------------------
// Function: GET data from Firebase
//---------------------------------------------------------
String firebaseGET(String path) {
  HTTPClient http;
  
  String url = String(DATABASE_URL) + path + ".json?auth=" + API_KEY;
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
void setup() {
  Serial.begin(115200);

  pinMode(pwmPin, OUTPUT);
  analogWrite(pwmPin, 0); // default OFF

  //-----------------------------------------
  // Koneksi WiFi
  //-----------------------------------------
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Menghubungkan WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi terhubung!");

  //-----------------------------------------
  // Sinkronisasi waktu via NTP
  //-----------------------------------------
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Sinkronisasi waktu NTP...");
}

//-------------------------------------------
void loop() {

  // Ambil waktu sekarang
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) {
    Serial.println("Gagal mendapatkan waktu dari NTP");
    delay(1000);
    return;
  }

  int jam   = timeInfo.tm_hour;
  int menit = timeInfo.tm_min;
  int detik = timeInfo.tm_sec;

  Serial.printf("Waktu Sekarang: %02d:%02d:%02d\n", jam, menit, detik);

  //-------------------------------------------------------------
  // 1. Baca data curah hujan dari Firebase
  //-------------------------------------------------------------
  float curahHujan = firebaseGET("/sensor/curah_hujan_mm").toFloat();
  Serial.printf("Curah Hujan: %.2f mm\n", curahHujan);

  //-------------------------------------------------------------
  // 2. Baca data kelembaban tanah dari Firebase
  //-------------------------------------------------------------
  int kelembaban = firebaseGET("/sensor/tanah/soil2/kelembapan_tanah_persen").toInt();
  Serial.printf("Kelembaban Tanah: %d %%\n", kelembaban);

  //-------------------------------------------------------------
  // 3. LOGIKA KONTROL POMPA (06:00 – 09:00)
  //-------------------------------------------------------------

  bool kondisiHujan = (curahHujan >= thresholdHujan);
  bool kondisiLembab = (kelembaban >= thresholdKelembaban);

  //  Rentang waktu penyiraman: 06:00 – 09:00
  bool waktuPenyiraman = (jam >= 6 && jam < 24);

  if (waktuPenyiraman) {

    Serial.println("Dalam rentang penyiraman (06:00–09:00)");

    // Hentikan penyiraman jika hujan atau tanah cukup lembab
    if (kondisiHujan) {
      Serial.println("Hujan terdeteksi. Pompa OFF.");
      analogWrite(pwmPin, 0);
      pumpRunning = false;
    }
    else if (kondisiLembab) {
      Serial.println("Kelembaban tanah sudah cukup. Pompa OFF.");
      analogWrite(pwmPin, 0);
      pumpRunning = false;
    }
    else {
      // Tidak hujan & tanah tidak lembab ⇒ nyalakan pompa
      if (!pumpRunning) {
        Serial.println("Tidak hujan & tanah kering. Pompa ON");

        for (pwmValue = 0; pwmValue <= 255; pwmValue++) {
          analogWrite(pwmPin, pwmValue);
          Serial.print("PWM: ");
          Serial.println(pwmValue);
          delay(20);   // kecepatan naik
        }

        pumpRunning = true;  // tandai pompa sudah ON
      } 
      else {
        // Pompa sudah ON → tetap pada 255
        analogWrite(pwmPin, 255);
      }
    }

  } else {
    // Di luar rentang waktu -> pompa harus OFF
    Serial.println("Di luar jam penyiraman (Pompa OFF).");
    analogWrite(pwmPin, 0);
    pumpRunning = false;
  }

  Serial.println("-------------------------------------------------------------------");

  delay(2000);
}
