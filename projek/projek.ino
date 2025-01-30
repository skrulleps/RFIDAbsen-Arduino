#include <SPI.h>
#include <MFRC522.h>
#include <RtcDS1302.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Definisi pin
#define RST_PIN 9       // Pin RST RFID
#define SS_PIN 10       // Pin SDA RFID
#define BUZZER 3        // Pin Buzzer
#define BUTTON 4        // Pin Tombol
#define LED_HIJAU 2     // Pin LED Hijau
#define LED_MERAH 5     // Pin LED Merah

// Inisialisasi modul
MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);
ThreeWire myWire(7, 8, 6); // IO, SCLK, CE == DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

// Daftar UID yang terdaftar dan pemiliknya
String validUID[] = {"9AB9392", "8CD8463", "479433A175E80", "8473F2"};
String namaPemilik[] = {"John Doe", "Jane Smith", "Bonge", "Bob White"};
int totalUID = 4; // Jumlah UID awal

// Variabel untuk waktu absensi
bool isAbsenMasuk = true; // Status absensi, true = Masuk, false = Pulang
unsigned long previousMillis = 0;  // Menyimpan waktu terakhir diperbarui
const long interval = 1000;        // Interval 1 detik (1000 ms)

// Variabel untuk PLX-DAQ
unsigned int entryNumber = 1; // Nomor urut data
bool userHasCheckedIn[10] = {false}; // Status apakah user sudah absen masuk
int lastEntryIndex[10] = {-1}; // Menyimpan nomor entri terakhir untuk setiap pengguna (maks. 10 pengguna)


void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();

  lcd.init();
  lcd.backlight();

  pinMode(BUZZER, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(LED_HIJAU, OUTPUT);
  pinMode(LED_MERAH, OUTPUT);

  Rtc.Begin();

  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  if (!Rtc.IsDateTimeValid()) {
    Rtc.SetDateTime(compiled);
  }
  if (Rtc.GetIsWriteProtected()) {
    Rtc.SetIsWriteProtected(false);
  }
  if (!Rtc.GetIsRunning()) {
    Rtc.SetIsRunning(true);
  }

  lcd.print("Sistem Absen");
  delay(2000);
  lcd.clear();

  // Kirim header ke PLX-DAQ
  Serial.println("CLEARSHEET");
  Serial.println("LABEL,NO,DATE,TIME,UID,NAME,ABSEN MASUK,ABSEN KELUAR");
}

void loop() {
  static bool lastButtonState = HIGH;
  bool currentButtonState = digitalRead(BUTTON);

  // Deteksi perubahan tombol untuk toggle absen masuk/keluar
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    isAbsenMasuk = !isAbsenMasuk;
    lcd.clear();
    if (isAbsenMasuk) {
      lcd.setCursor(5, 0);
      lcd.print("Mode:");
      lcd.setCursor(2, 1);
      lcd.print("Absen Masuk");
    } else {
      lcd.setCursor(5, 0);
      lcd.print("Mode:");
      lcd.setCursor(2, 1);
      lcd.print("Absen Pulang");
    }
    delay(3000); // Debounce delay
  }
  lastButtonState = currentButtonState;

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    RtcDateTime now = Rtc.GetDateTime();
    printDateTime(now, true);
  }

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Baca UID kartu
  String UID = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    UID += String(rfid.uid.uidByte[i], HEX);
  }
  UID.toUpperCase();

  // Cek apakah UID terdaftar
  bool aksesDiizinkan = false;
  String pemilik = "Tidak Dikenal";
  int userIndex = -1;
  for (int i = 0; i < totalUID; i++) {
    if (UID == validUID[i]) {
      aksesDiizinkan = true;
      pemilik = namaPemilik[i];
      userIndex = i;
      break;
    }
  }

  lcd.clear();
  if (aksesDiizinkan) {
    RtcDateTime now = Rtc.GetDateTime();

    if (isAbsenMasuk) {
  if (!userHasCheckedIn[userIndex]) {
    // Absen masuk pertama kali
    lcd.print("Selamat Datang");
    lcd.setCursor(0, 1);
    lcd.print(pemilik);

    sendToPLXDAQ(now, UID, pemilik, true, false, userIndex);
    userHasCheckedIn[userIndex] = true; // Tandai sudah absen masuk
  } else {
    lcd.print("Anda sudah absen");
    lcd.setCursor(0, 1);
    lcd.print("masuk!");
  }
} else {
  if (!userHasCheckedIn[userIndex]) {
    lcd.print("Anda belum absen");
    lcd.setCursor(0, 1);
    lcd.print("masuk!");
  } else {
    lcd.print("Selamat Jalan");
    lcd.setCursor(0, 1);
    lcd.print(pemilik);

    sendToPLXDAQ(now, UID, pemilik, false, true, userIndex);
    userHasCheckedIn[userIndex] = false; // Tandai sudah absen keluar
  }
}



    // Nyalakan LED Hijau dan beri bunyi buzzer
    digitalWrite(LED_HIJAU, HIGH);
    digitalWrite(BUZZER, HIGH);
    delay(500);
    digitalWrite(LED_HIJAU, LOW);
    digitalWrite(BUZZER, LOW);

  } else {
    lcd.print("Belum Terdaftar!");
    digitalWrite(LED_MERAH, HIGH);
    digitalWrite(BUZZER, HIGH);
    delay(300);
    digitalWrite(LED_MERAH, LOW);
    digitalWrite(BUZZER, LOW);
    delay(200);
    digitalWrite(LED_MERAH, HIGH);
    digitalWrite(BUZZER, HIGH);
    delay(300);
    digitalWrite(LED_MERAH, LOW);
    digitalWrite(BUZZER, LOW);
    Serial.println(UID);
  }

  rfid.PICC_HaltA();
}

void sendToPLXDAQ(const RtcDateTime& dt, const String& UID, const String& name, bool isMasuk, bool isKeluar, int userIndex) {
  // Format tanggal
  char date[11]; // Format: DD/MM/YYYY
  snprintf_P(date, sizeof(date),
             PSTR("%02u/%02u/%04u"),
             dt.Month(), dt.Day(), dt.Year());

  // Format waktu
  char time[9]; // Format: HH:MM:SS
  snprintf_P(time, sizeof(time),
             PSTR("%02u:%02u:%02u"),
             dt.Hour(), dt.Minute(), dt.Second());

  if (isMasuk) {
    // Kirim data baru untuk absensi masuk
    Serial.print("DATA,");
    Serial.print(entryNumber);
    Serial.print(",");
    Serial.print(date);
    Serial.print(",");
    Serial.print(time);
    Serial.print(",");
    Serial.print(UID);
    Serial.print(",");
    Serial.print(name);
    Serial.print(",");
    Serial.print(time); // Waktu absen masuk
    Serial.println(","); // Kolom waktu absen keluar kosong

    lastEntryIndex[userIndex] = entryNumber; // Simpan nomor entri terakhir
    entryNumber++; // Tingkatkan nomor urut data
  } else if (isKeluar && lastEntryIndex[userIndex] != -1) {
    // Perbarui data untuk absensi keluar
    Serial.print("DATA,");
    Serial.print(lastEntryIndex[userIndex]); // Gunakan nomor entri terakhir
    Serial.print(",");
    Serial.print(date);
    Serial.print(",");
    Serial.print(time);
    Serial.print(",");
    Serial.print(UID);
    Serial.print(",");
    Serial.print(name);
    Serial.print(",");
    Serial.print(","); // Kolom waktu absen masuk tetap kosong
    Serial.println(time); // Isi waktu absen keluar
  }
}



void printDateTime(const RtcDateTime& dt, bool displayOnLCD) {
  char datestring[26];
  snprintf_P(datestring, sizeof(datestring),
             PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
             dt.Month(), dt.Day(), dt.Year(), dt.Hour(), dt.Minute(), dt.Second());
  Serial.println(datestring);

  if (displayOnLCD) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Date: ");
    lcd.print(dt.Month());
    lcd.print('/');
    lcd.print(dt.Day());
    lcd.print('/');
    lcd.print(dt.Year());

    lcd.setCursor(0, 1);
    lcd.print("Time: ");
    lcd.print(dt.Hour());
    lcd.print(':');
    lcd.print(dt.Minute());
    lcd.print(':');
    lcd.print(dt.Second());
  }
}
