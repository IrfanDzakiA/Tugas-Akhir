#include <Arduino.h>           // Library standar Arduino
#include <WiFiManager.h>       // Library untuk manajemen koneksi WiFi yang mudah
#include <HTTPClient.h>        // Library untuk melakukan permintaan HTTP
#include <ArduinoJson.h>       // Library untuk parsing dan serialisasi JSON
#include <LiquidCrystal_I2C.h> // Library untuk mengontrol LCD I2C
#include <LoRa.h>              // Library untuk komunikasi LoRa
#include <constant.h>          // File header kustom (kemungkinan berisi definisi konstan)
#include <EEPROM.h>            // Library untuk membaca dan menulis ke memori EEPROM

// URL API ke server python
const String Endpoint = "http://biodrying-server.local:5000/biodrying_data"; // Alamat endpoint server untuk mengirim data
bool wiFiConnected;                                                          // Variabel penanda status koneksi WiFi

// Konfigurasi LoRA
#define ss 5   // Pin Chip Select (CS) untuk modul LoRa
#define rst 14 // Pin Reset untuk modul LoRa
#define dio0 2 // Pin DIO0 (interrupt) untuk modul LoRa

// Define Pin
// Push Button
const int pbKanan = 32;  // Pin untuk push button kanan
const int pbTengah = 33; // Pin untuk push button tengah
const int pbKiri = 25;   // Pin untuk push button kiri

// LED
const int ledKanan = 26; // Pin untuk LED kanan (indikator LoRa RX)
const int ledKiri = 27;  // Pin untuk LED kiri (indikator LoRa TX)

// Buzzer Pin
const int buzzerPin = 15;                    // Pin untuk buzzer
bool buzzerLastState;                        // Menyimpan status buzzer terakhir
unsigned long activateBuzzerUntil;           // Waktu hingga buzzer aktif
const unsigned long buzzerActiveTime = 3000; // Durasi buzzer aktif dalam milidetik

// LCD
LiquidCrystal_I2C Lcd(0x27, 16, 2); // Inisialisasi objek LCD I2C dengan alamat 0x27, 16 kolom, 2 baris
int lcdMenu, lastLcdMenu;           // Variabel untuk menyimpan menu LCD saat ini dan sebelumnya
bool lcdClicked;                    // Penanda apakah tombol tengah pada menu setting ditekan

byte pauseChar[] = { // Custom character untuk simbol pause di LCD
    B00000,
    B00000,
    B01010,
    B01010,
    B01010,
    B01010,
    B00000,
    B00000};

// Parameter tertampil
bool paused;          // Status apakah sistem dijeda
int loraRSSI;         // Menyimpan nilai RSSI (Received Signal Strength Indicator) LoRa
int connectedDevices; // (Variabel ini dideklarasikan tapi tidak digunakan secara aktif dalam kode yang diberikan)
bool classification;  // Hasil klasifikasi dari server
bool buzzerOn;        // Status buzzer dari server
float humidity;       // Nilai kelembaban
float temperature;    // Nilai suhu
float pH;             // Nilai pH

// Definisi Lora Parameter
struct LoraParameter // Struktur untuk menyimpan parameter LoRa
{
  byte loraLocalAddress;  // Alamat LoRa perangkat ini
  byte loraDestination;   // Alamat LoRa tujuan
  String outgoingMessage; // Pesan yang akan dikirim (tidak terpakai di kode ini)
  String incomingMessage; // Pesan yang diterima
  byte msgCount;          // Penghitung pesan (tidak terpakai di kode ini)
};

LoraParameter loraParameter; // Membuat instance dari LoraParameter

// Data dari server
struct ServerResponse // Struktur untuk menyimpan respons dari server
{
  bool classification; // Hasil klasifikasi
  bool buzzerOn;       // Status buzzer
};

ServerResponse serverResponse; // Membuat instance dari ServerResponse

// definisi fungsi
void sendToTransmitter(String data);                      // Deklarasi fungsi (tidak ada definisi di kode ini)
int getAllConnectedDevices();                             // Deklarasi fungsi (tidak ada definisi di kode ini)
void onLoraReceiveCallback(int packetSize);               // Deklarasi fungsi callback ketika LoRa menerima paket
void sendLoraMessage(String message);                     // Deklarasi fungsi untuk mengirim pesan LoRa (overload 1)
void centerText(const char *text, int row);               // Deklarasi fungsi untuk menampilkan teks di tengah LCD
void sendLoraMessage(const ServerResponse &responseData); // Deklarasi fungsi untuk mengirim pesan LoRa (overload 2, menggunakan struct)

// definisi rtos
TaskHandle_t taskSendDataToServerHandler; // Handle untuk task mengirim data ke server (di-comment out saat pembuatan task)
TaskHandle_t taskUpdateLcdHandler;        // Handle untuk task update LCD
TaskHandle_t taskInputHandler;            // Handle untuk task menangani input

SemaphoreHandle_t serverSemaphore;    // Semaphore untuk sinkronisasi akses ke server (dibuat tapi tidak digunakan dalam task yang aktif)
SemaphoreHandle_t lcdUpdateSemaphore; // Semaphore untuk sinkronisasi update LCD

void sendToServerTask(void *pvParameter); // Deklarasi fungsi task untuk mengirim data ke server (tidak dibuat tasknya)
void lcdUpdateTask(void *pvParameter);    // Deklarasi fungsi task untuk update LCD
void inputUpdateTask(void *pvParameter);  // Deklarasi fungsi task untuk menangani input

enum LcdScreen // Enumerasi untuk layar-layar menu pada LCD
{
  Monitoring,
  LoraRSSI,
  UpdateRateSetting,
  StatusKelayakan,
  StatusBuzzer,
  WiFiStatus,
  WiFiReset,
  LoraTxPower,
  LoraSpreadingFactor,
  LoraDenominator,
  LoraSignalBandwith
};

#define EEPROM_SIZE 512     // Ukuran memori EEPROM yang digunakan
#define DITEKAN LOW         // Mendefinisikan kondisi tombol ditekan (aktif LOW karena PULLUP)
#define TIDAK_DITEKAN HIGH  // Mendefinisikan kondisi tombol tidak ditekan
#define ESP_BOOT_DELAY 1500 // Waktu tunda saat boot ESP32 dalam milidetik
#define LCD_PAGES_COUNT 10  // Jumlah halaman menu pada LCD

bool loraSettingClicked = 0; // Penanda apakah menu setting LoRa sedang dipilih (sepertinya variabel ini bisa digabung atau digantikan lcdClicked)
int loraSettingSubMenu = 0;  // Variabel untuk submenu setting LoRa (tidak terpakai)

struct LoraSettingParameter // Struktur untuk menyimpan parameter setting LoRa
{
  int txPower;           // Transmit Power
  int spreadingFactor;   // Spreading Factor
  int codeDenominator;   // Coding Rate Denominator
  float signalBandwidth; // Signal Bandwidth
};

bool loraScreenClicked; // Penanda untuk layar LoRa (sepertinya variabel ini bisa digabung atau digantikan lcdClicked)
int loraSettingPage;    // Variabel untuk halaman setting LoRa (tidak terpakai)

LoraSettingParameter loraSettingParameter;                                                                   // Instance dari LoraSettingParameter
const float loraBandwidth[] = {7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3, 41.7E3, 62.5E3, 125E3, 250E3, 500E3}; // Array nilai bandwidth LoRa yang valid
int bandwidthSelector = 0;                                                                                   // Indeks untuk memilih bandwidth dari array loraBandwidth

int addresses[5] = {}; // Array untuk menyimpan alamat EEPROM untuk setiap parameter

unsigned long updateRate = 500; // Interval update dalam milidetik (default 500ms)

void setup() // Fungsi setup, dijalankan sekali saat startup
{
  WiFiManager wm; // Inisialisasi objek WiFiManager

  EEPROM.begin(EEPROM_SIZE); // Menginisialisasi EEPROM dengan ukuran yang ditentukan

  int addr = 0; // Variabel untuk melacak alamat EEPROM saat ini

  // Menentukan alamat EEPROM untuk setiap parameter yang disimpan
  addresses[0] = addr; // Alamat untuk updateRate
  addr += sizeof(updateRate);

  addresses[1] = addr; // Alamat untuk txPower
  addr += sizeof(loraSettingParameter.txPower);

  addresses[2] = addr; // Alamat untuk spreadingFactor
  addr += sizeof(loraSettingParameter.spreadingFactor);

  addresses[3] = addr; // Alamat untuk codeDenominator
  addr += sizeof(loraSettingParameter.codeDenominator);

  addresses[4] = addr; // Alamat untuk signalBandwidth

  // Komentar Debugging: Print stored addresses (Bisa dihapus setelah debugging selesai)
  // Serial.println("EEPROM Address Mapping:");
  // Serial.print("updateRate: ");
  // Serial.println(addresses[0]);
  // Serial.print("txPower: ");
  // Serial.println(addresses[1]);
  // Serial.print("spreadingFactor: ");
  // Serial.println(addresses[2]);
  // Serial.print("codeDenominator: ");
  // Serial.println(addresses[3]);
  // Serial.print("signalBandwith: ");
  // Serial.println(addresses[4]);

  // Memuat nilai yang tersimpan dari EEPROM
  EEPROM.get(addresses[0], updateRate);
  EEPROM.get(addresses[1], loraSettingParameter.txPower);
  EEPROM.get(addresses[2], loraSettingParameter.spreadingFactor);
  EEPROM.get(addresses[3], loraSettingParameter.codeDenominator);
  EEPROM.get(addresses[4], loraSettingParameter.signalBandwidth);

  // Komentar Debugging: Print loaded EEPROM Values (Bisa dihapus setelah debugging selesai)
  // Serial.println("\nLoaded EEPROM Values:");
  // Serial.print("updateRate: ");
  // Serial.println(updateRate);
  // Serial.print("txPower: ");
  // Serial.println(loraSettingParameter.txPower);
  // Serial.print("spreadingFactor: ");
  // Serial.println(loraSettingParameter.spreadingFactor);
  // Serial.print("codeDenominator: ");
  // Serial.println(loraSettingParameter.codeDenominator);
  // Serial.print("signalBandwith: ");
  // Serial.println(loraSettingParameter.signalBandwidth);

  Serial.begin(115200);         // Memulai komunikasi serial dengan baud rate 115200
  Lcd.init();                   // Menginisialisasi LCD
  Lcd.backlight();              // Menghidupkan backlight LCD
  Lcd.createChar(0, pauseChar); // Membuat custom character 'pause' di LCD

  // tampilan teks awal booting
  centerText("ESP32", 0);    // Menampilkan "ESP32" di baris 0 LCD
  centerText("Receiver", 1); // Menampilkan "Receiver" di baris 1 LCD

  // Konfigurasi Push Button dalam mode Input Pullup
  pinMode(pbKanan, INPUT_PULLUP);  // Mengatur pin pbKanan sebagai input dengan pull-up internal
  pinMode(pbTengah, INPUT_PULLUP); // Mengatur pin pbTengah sebagai input dengan pull-up internal
  pinMode(pbKiri, INPUT_PULLUP);   // Mengatur pin pbKiri sebagai input dengan pull-up internal

  // Konfigurasi LED dalam mode output
  pinMode(ledKanan, OUTPUT); // Mengatur pin ledKanan sebagai output
  pinMode(ledKiri, OUTPUT);  // Mengatur pin ledKiri sebagai output

  // konfigurasi pin buzzer dalam mode output
  pinMode(buzzerPin, OUTPUT); // Mengatur pin buzzerPin sebagai output

  delay(ESP_BOOT_DELAY); // Memberi jeda

  // tampilan teks awal booting
  Lcd.clear();                   // Membersihkan layar LCD
  centerText("WiFI", 0);         // Menampilkan "WiFI" di baris 0
  centerText("Hubungkan...", 1); // Menampilkan "Hubungkan..." di baris 1

  // Cek apakah tombol kiri dan kanan ditekan bersamaan untuk mereset pengaturan WiFi
  if (digitalRead(pbKiri) == DITEKAN && digitalRead(pbKanan) == DITEKAN)
  {
    Lcd.clear();
    centerText("WiFi", 0);
    centerText("Reset", 1);
    wm.resetSettings(); // Mereset pengaturan WiFi yang tersimpan
    delay(ESP_BOOT_DELAY);

    Lcd.clear();
    centerText("WiFi", 0);
    centerText("Hubungkan...", 1);
  }

  bool res;                               // Variabel untuk menyimpan status koneksi WiFiManager
  res = wm.autoConnect("ESP32 Receiver"); // Mencoba menghubungkan ke WiFi, jika gagal akan membuka Access Point "ESP32 Receiver"

  if (!res) // Jika koneksi gagal
  {
    Lcd.clear();
    centerText("WiFi", 0);
    centerText("Gagal Menghubungkan", 1);
    delay(ESP_BOOT_DELAY);
  }
  else // Jika koneksi berhasil
  {
    Lcd.clear();
    centerText("WiFi", 0);
    centerText("Terhubung", 1);
    delay(ESP_BOOT_DELAY);
  }

  // Konfigurasi pin LoRA
  LoRa.setPins(ss, rst, dio0); // Mengatur pin yang digunakan oleh modul LoRa
  Serial.println("Inisialisasi LoRA!");

  Lcd.clear();
  centerText("LoRA", 0);
  centerText("Inisialisasi", 1);
  delay(500);

  // Inisialisasi LoRA dengan frekuensi 433E6 (433 MHz)
  while (!LoRa.begin(433E6)) // Mencoba memulai LoRa, ulangi jika gagal
  {
    Serial.println(".");
    delay(500);
  }

  // LoRa.setTxPower(loraSettingParameter.txPower); // Komentar: Pengaturan LoRa dipindahkan ke menu, di-load dari EEPROM
  // LoRa.setSpreadingFactor(loraSettingParameter.spreadingFactor);
  // LoRa.setCodingRate4(loraSettingParameter.codeDenominator);
  // LoRa.setSignalBandwidth(loraSettingParameter.signalBandwidth);
  // Penting: Setelah memuat dari EEPROM, parameter LoRa perlu di-apply ke library LoRa.
  // Sebaiknya lakukan apply setting LoRa dari EEPROM di sini atau pastikan dilakukan di menu setelah loading.
  // Contoh:
  // if (loraSettingParameter.txPower != 0) LoRa.setTxPower(loraSettingParameter.txPower);
  // if (loraSettingParameter.spreadingFactor != 0) LoRa.setSpreadingFactor(loraSettingParameter.spreadingFactor);
  // if (loraSettingParameter.codeDenominator != 0) LoRa.setCodingRate4(loraSettingParameter.codeDenominator);
  // if (loraSettingParameter.signalBandwidth != 0) LoRa.setSignalBandwidth(loraSettingParameter.signalBandwidth);

  Lcd.clear();
  centerText("LoRA", 0);
  centerText("Terinisialisasi", 1);
  delay(ESP_BOOT_DELAY);

  Serial.println("LoRa Terinisialisasi OK!");

  // Konfigurasi Address Lokal dan Destinasi
  loraParameter.loraLocalAddress = 0x02; // Mengatur alamat LoRa lokal
  loraParameter.loraDestination = 0x01;  // Mengatur alamat LoRa tujuan

  Lcd.clear();
  delay(500);

  // konfigurasi rtos
  serverSemaphore = xSemaphoreCreateBinary();    // Membuat binary semaphore untuk server
  lcdUpdateSemaphore = xSemaphoreCreateBinary(); // Membuat binary semaphore untuk update LCD

  xSemaphoreGive(serverSemaphore);    // Memberikan semaphore server (agar bisa diambil pertama kali)
  xSemaphoreGive(lcdUpdateSemaphore); // Memberikan semaphore LCD update (agar bisa diambil pertama kali)

  xTaskCreate( // Membuat task untuk update LCD
      lcdUpdateTask,
      "LCD Update Task",
      2048, // Ukuran stack task
      NULL, // Parameter task
      1,    // Prioritas task
      &taskUpdateLcdHandler);

  // xTaskCreate( // Task untuk mengirim data ke server (di-comment out)
  //     sendToServerTask,
  //     "Send To Server Task",
  //     8196,
  //     NULL,
  //     1,
  //     &taskSendDataToServerHandler);

  xTaskCreate( // Membuat task untuk menangani input pengguna
      inputUpdateTask,
      "Input Update Task",
      4096,
      NULL,
      1,
      &taskInputHandler);
}

uint8_t buttonState, lastButtonState; // Variabel untuk menyimpan status tombol saat ini dan sebelumnya

void inputUpdateTask(void *pvParameter) // Task untuk menangani input tombol
{
  while (1) // Loop tak terbatas untuk task
  {
    // Membaca status ketiga tombol dan menyimpannya dalam satu byte
    // !digitalRead karena INPUT_PULLUP (LOW = ditekan)
    // pbKiri -> bit 0, pbTengah -> bit 1, pbKanan -> bit 2
    buttonState = !digitalRead(pbKiri) | !digitalRead(pbTengah) << 1 | !digitalRead(pbKanan) << 2;

    // Mendeteksi penekanan tombol (transisi dari tidak ditekan ke ditekan)
    bool pbKiriDitekan = buttonState != lastButtonState && buttonState == 1 << 0;
    bool pbTengahDitekan = buttonState != lastButtonState && buttonState == 1 << 1;
    bool pbKananDitekan = buttonState != lastButtonState && buttonState == 1 << 2;

    // PB Kiri ditekan
    if (pbKiriDitekan)
    {
      if (lcdClicked) // Jika sedang dalam mode edit nilai di LCD
      {
        xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY); // Mengambil semaphore LCD
        Lcd.setCursor(0, 1);
        Lcd.print("                ");      // Membersihkan baris kedua LCD (tempat nilai)
        xSemaphoreGive(lcdUpdateSemaphore); // Memberikan kembali semaphore LCD

        switch (lcdMenu) // Aksi berdasarkan menu yang aktif
        {
        case LcdScreen::UpdateRateSetting:
        {
          Lcd.clear();       // Membersihkan LCD sebelum update nilai
          updateRate -= 100; // Mengurangi updateRate
          break;
        }
        case LcdScreen::LoraTxPower:
        {
          loraSettingParameter.txPower--; // Mengurangi Tx Power
          // Membatasi nilai Tx Power antara 2 dan 20
          loraSettingParameter.txPower = loraSettingParameter.txPower < 2 ? 2 : loraSettingParameter.txPower > 20 ? 20
                                                                                                                  : loraSettingParameter.txPower;
          break;
        }
        case LcdScreen::LoraSpreadingFactor:
        {
          loraSettingParameter.spreadingFactor--; // Mengurangi Spreading Factor
          // Membatasi nilai Spreading Factor antara 7 dan 12
          loraSettingParameter.spreadingFactor = (loraSettingParameter.spreadingFactor < 7) ? 7 : loraSettingParameter.spreadingFactor > 12 ? 12
                                                                                                                                            : loraSettingParameter.spreadingFactor;
          break;
        }
        case LcdScreen::LoraDenominator:
        {
          loraSettingParameter.codeDenominator--; // Mengurangi Code Denominator
          // Membatasi nilai Code Denominator antara 5 dan 8
          loraSettingParameter.codeDenominator = loraSettingParameter.codeDenominator < 5 ? 5 : loraSettingParameter.codeDenominator > 8 ? 8
                                                                                                                                         : loraSettingParameter.codeDenominator;
          break;
        }
        case LcdScreen::LoraSignalBandwith:
        {
          bandwidthSelector--; // Mengurangi indeks selector bandwidth
          // Membatasi indeks selector antara 0 dan 9
          bandwidthSelector = bandwidthSelector < 0 ? 0 : bandwidthSelector > 9 ? 9
                                                                                : bandwidthSelector;
          loraSettingParameter.signalBandwidth = loraBandwidth[bandwidthSelector]; // Mengupdate nilai signalBandwidth dari array
          break;
        }
        }
      }
      else // Jika tidak dalam mode edit, pindah ke menu sebelumnya
      {
        lcdMenu--;
      }
    }

    // PB Tengah ditekan
    if (pbTengahDitekan)
    {
      if (lcdMenu == LcdScreen::WiFiReset) // Jika di menu WiFi Reset
      {
        Lcd.clear();
        centerText("Reset WiFi", 0);
        delay(1000);
        Lcd.clear();
        Lcd.noBacklight(); // Matikan backlight LCD
        WiFiManager wm;
        wm.resetSettings(); // Reset pengaturan WiFi
        esp_restart();      // Restart ESP32
      }

      // Jika menu adalah salah satu dari menu setting
      if (lcdMenu == LcdScreen::UpdateRateSetting || lcdMenu == LcdScreen::LoraTxPower || lcdMenu == LcdScreen::LoraSpreadingFactor || lcdMenu == LcdScreen::LoraDenominator || lcdMenu == LcdScreen::LoraSignalBandwith)
      {
        lcdClicked ^= true; // Toggle status lcdClicked (masuk/keluar mode edit)
      }

      if (lcdClicked) // Jika masuk mode edit
      {
        // Menampilkan kursor < > di LCD
        Lcd.setCursor(0, 1);
        Lcd.print("<");
        Lcd.setCursor(15, 1);
        Lcd.print(">");
      }
      else // Jika keluar mode edit
      {
        // Menghapus kursor < > di LCD
        Lcd.setCursor(0, 1);
        Lcd.print(" ");
        Lcd.setCursor(15, 1);
        Lcd.print(" ");
      }

      switch (lcdMenu) // Aksi berdasarkan menu saat ini
      {
      case LcdScreen::Monitoring:
        paused ^= true; // Toggle status pause pada menu monitoring
        break;

      case LcdScreen::UpdateRateSetting:
      {
        if (!lcdClicked) // Jika baru saja keluar dari mode edit
        {
          xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY); // Ambil semaphore LCD
          EEPROM.writeInt(0, updateRate);                    // Simpan updateRate ke EEPROM di alamat 0 (hati-hati jika alamat berubah)
                                                             // Sebaiknya gunakan addresses[0]
          EEPROM.commit();                                   // Menyimpan perubahan ke EEPROM
          Lcd.clear();
          centerText("Menyimpan Data", 0);
          delay(1000);
          Lcd.clear();
          xSemaphoreGive(lcdUpdateSemaphore); // Lepas semaphore LCD
        }
        break;
      }
      case LcdScreen::LoraTxPower:
      {
        if (!lcdClicked) // Jika baru saja keluar dari mode edit
        {
          xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY);
          EEPROM.put(addresses[1], loraSettingParameter.txPower); // Simpan txPower ke EEPROM
          EEPROM.commit();
          Lcd.clear();
          LoRa.setTxPower(loraSettingParameter.txPower); // Langsung terapkan perubahan Tx Power ke modul LoRa
          centerText("Menyimpan Data", 0);
          delay(1000);
          Lcd.clear();
          xSemaphoreGive(lcdUpdateSemaphore);
        }
        break;
      }
      case LcdScreen::LoraSpreadingFactor:
      {
        if (!lcdClicked) // Jika baru saja keluar dari mode edit
        {
          xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY);
          EEPROM.put(addresses[2], loraSettingParameter.spreadingFactor); // Simpan spreadingFactor ke EEPROM
          EEPROM.commit();
          Lcd.clear();
          LoRa.setSpreadingFactor(loraSettingParameter.spreadingFactor); // Langsung terapkan perubahan Spreading Factor
          centerText("Menyimpan Data", 0);
          delay(1000);
          Lcd.clear();
          xSemaphoreGive(lcdUpdateSemaphore);
        }
        break;
      }
      case LcdScreen::LoraDenominator:
      {
        if (!lcdClicked) // Jika baru saja keluar dari mode edit
        {
          xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY);
          EEPROM.put(addresses[3], loraSettingParameter.codeDenominator); // Simpan codeDenominator ke EEPROM
          EEPROM.commit();
          Lcd.clear();
          LoRa.setCodingRate4(loraSettingParameter.codeDenominator); // Langsung terapkan perubahan Coding Rate
          centerText("Menyimpan Data", 0);
          delay(1000);
          Lcd.clear();
          xSemaphoreGive(lcdUpdateSemaphore);
        }
        break;
      }
      // Signal Bandwidth (Pastikan Float Tidak Overlap) -> Komentar ini penting
      case LcdScreen::LoraSignalBandwith:
      {
        if (!lcdClicked) // Jika baru saja keluar dari mode edit
        {
          xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY);
          // EEPROM.put(addresses[3] + sizeof(loraSettingParameter.codeDenominator), loraSettingParameter.signalBandwidth);
          // Koreksi: Seharusnya menggunakan addresses[4] yang sudah dialokasikan untuk signalBandwidth
          EEPROM.put(addresses[4], loraSettingParameter.signalBandwidth); // Simpan signalBandwidth ke EEPROM
          EEPROM.commit();
          Lcd.clear();
          LoRa.setSignalBandwidth(loraSettingParameter.signalBandwidth); // Langsung terapkan perubahan Signal Bandwidth
          centerText("Menyimpan Data", 0);
          delay(1000);
          Lcd.clear();
          xSemaphoreGive(lcdUpdateSemaphore);
        }
        break;
      }
      }
    }

    // PB Kanan ditekan
    if (pbKananDitekan)
    {
      if (lcdClicked) // Jika sedang dalam mode edit nilai
      {
        xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY);
        Lcd.setCursor(0, 1);
        Lcd.print("                "); // Membersihkan baris kedua LCD
        xSemaphoreGive(lcdUpdateSemaphore);

        switch (lcdMenu) // Aksi berdasarkan menu yang aktif
        {
        case LcdScreen::UpdateRateSetting:
        {
          Lcd.clear();
          updateRate += 100; // Menambah updateRate
          break;
        }
        case LcdScreen::LoraTxPower:
        {
          loraSettingParameter.txPower++; // Menambah Tx Power
          loraSettingParameter.txPower = loraSettingParameter.txPower < 2 ? 2 : loraSettingParameter.txPower > 20 ? 20
                                                                                                                  : loraSettingParameter.txPower;
          break;
        }
        case LcdScreen::LoraSpreadingFactor:
        {
          loraSettingParameter.spreadingFactor++; // Menambah Spreading Factor
          loraSettingParameter.spreadingFactor = (loraSettingParameter.spreadingFactor < 7) ? 7 : loraSettingParameter.spreadingFactor > 12 ? 12
                                                                                                                                            : loraSettingParameter.spreadingFactor;
          break;
        }
        case LcdScreen::LoraDenominator:
        {
          loraSettingParameter.codeDenominator++; // Menambah Code Denominator
          loraSettingParameter.codeDenominator = loraSettingParameter.codeDenominator < 5 ? 5 : loraSettingParameter.codeDenominator > 8 ? 8
                                                                                                                                         : loraSettingParameter.codeDenominator;
          break;
        }
        case LcdScreen::LoraSignalBandwith:
        {
          bandwidthSelector++; // Menambah indeks selector bandwidth
          bandwidthSelector = bandwidthSelector < 0 ? 0 : bandwidthSelector > 9 ? 9
                                                                                : bandwidthSelector;
          loraSettingParameter.signalBandwidth = loraBandwidth[bandwidthSelector]; // Mengupdate nilai signalBandwidth
          break;
        }
        }
      }
      else // Jika tidak dalam mode edit, pindah ke menu berikutnya
      {
        lcdMenu++;
      }
    }

    if (lcdMenu != lastLcdMenu) // Jika ada perubahan menu
    {
      xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY);
      Lcd.clear(); // Bersihkan LCD
      xSemaphoreGive(lcdUpdateSemaphore);
    }

    // constrain nilai lcd menu ke 0 - LCD_PAGES_COUNT menu
    // Memastikan nilai lcdMenu selalu dalam rentang yang valid
    lcdMenu = (lcdMenu > LCD_PAGES_COUNT) ? LCD_PAGES_COUNT : (lcdMenu < 0) ? 0
                                                                            : lcdMenu;

    // Logika untuk mengaktifkan buzzer berdasarkan respons server
    if (serverResponse.buzzerOn && buzzerLastState != serverResponse.buzzerOn) // Jika buzzerOn dari server true dan status sebelumnya false
    {
      activateBuzzerUntil = millis() + buzzerActiveTime; // Set waktu buzzer aktif
    }

    if (serverResponse.buzzerOn && millis() < activateBuzzerUntil) // Jika buzzerOn true dan masih dalam durasi aktif
    {
      digitalWrite(buzzerPin, HIGH); // Nyalakan buzzer
    }
    else
    {
      digitalWrite(buzzerPin, LOW); // Matikan buzzer
    }

    lastLcdMenu = lcdMenu;                     // Simpan menu saat ini sebagai menu terakhir
    lastButtonState = buttonState;             // Simpan status tombol saat ini sebagai status terakhir
    buzzerLastState = serverResponse.buzzerOn; // Simpan status buzzer saat ini sebagai status terakhir
    vTaskDelay(33);                            // Memberi jeda task sekitar 30 FPS (1000ms / 30 = ~33ms)
  }
}

void lcdUpdateTask(void *pvParameter) // Task untuk memperbarui tampilan LCD
{
  while (1) // Loop tak terbatas
  {
    if (xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY) == pdTRUE) // Mencoba mengambil semaphore LCD
    {
      switch (lcdMenu) // Menampilkan konten berdasarkan menu yang aktif
      {
      case LcdScreen::Monitoring:
      {
        Lcd.setCursor(0, 0);
        Lcd.printf("T %.2f C", temperature); // Menampilkan suhu
        Lcd.setCursor(0, 1);

        if (humidity > 10.0) // Format tampilan kelembaban agar rapi
        {
          Lcd.printf("H %.2f %%", humidity);
        }
        else
        {
          Lcd.printf("H %.2f %% ", humidity); // Tambah spasi jika angka satuan
        }

        // tampilkan icon pause
        Lcd.setCursor(15, 0); // Posisi ikon pause
        if (paused)
        {
          Lcd.write(0); // Menampilkan custom character pause
        }
        else
        {
          Lcd.print(" "); // Kosongkan jika tidak pause
        }

        Lcd.setCursor(9, 1);
        Lcd.printf("pH %.1f ", pH); // Menampilkan pH

        Lcd.setCursor(14, 0);
        Lcd.printf((serverResponse.classification) ? "L" : "T"); // Menampilkan status klasifikasi (L/T)
        break;
      }
      case LcdScreen::LoraRSSI:
      {
        centerText("Lora RSSI", 0);
        if (paused)
        {
          centerText("Terjeda", 1);
        }
        else if (loraRSSI == 0) // Jika RSSI 0 (belum ada koneksi/data)
        {
          centerText("Tidak Terhubung", 1);
        }
        else
        {
          centerText(String(loraRSSI).c_str(), 1); // Menampilkan nilai RSSI
        }
        break;
      }
      case LcdScreen::UpdateRateSetting:
      {
        centerText("Update Rate", 0);
        centerText(String(updateRate).c_str(), 1); // Menampilkan nilai updateRate

        if (lcdClicked) // Jika dalam mode edit, tampilkan kursor
        {
          Lcd.setCursor(0, 1);
          Lcd.print("<");
          Lcd.setCursor(15, 1);
          Lcd.print(">");
        }
        break;
      }
      case LcdScreen::StatusKelayakan:
      {
        centerText("Kelayakan", 0);
        centerText(classification ? "      Layak      " : "Tidak Layak", 1); // Menampilkan status kelayakan
        break;
      }
      case LcdScreen::StatusBuzzer:
      {
        centerText("Status Buzzer", 0);
        centerText(serverResponse.buzzerOn ? "Hidup" : " Mati ", 1); // Menampilkan status buzzer
        break;
      }
      case LcdScreen::WiFiStatus:
      {
        centerText("Status HTTP", 0);                                  // Seharusnya "Status WiFi" atau "Status Server"
        centerText(wiFiConnected ? "Terhubung" : "   Terputus   ", 1); // Menampilkan status koneksi WiFi/server
        break;
      }
      case LcdScreen::WiFiReset:
      {
        centerText("WiFi Reset", 0);
        centerText("PB Tengah Reset", 1); // Instruksi untuk reset WiFi
        break;
      }
      case LcdScreen::LoraTxPower:
      {
        centerText("LoRA Tx Power", 0);
        centerText(String(loraSettingParameter.txPower).c_str(), 1); // Menampilkan nilai Tx Power

        if (lcdClicked) // Tampilkan kursor jika mode edit
        {
          Lcd.setCursor(0, 1);
          Lcd.print("<");
          Lcd.setCursor(15, 1);
          Lcd.print(">");
        }
        break;
      }
      case LcdScreen::LoraSpreadingFactor:
      {
        centerText("LoRA SP Factor", 0);
        centerText(String(loraSettingParameter.spreadingFactor).c_str(), 1); // Menampilkan Spreading Factor

        if (lcdClicked) // Tampilkan kursor jika mode edit
        {
          Lcd.setCursor(0, 1);
          Lcd.print("<");
          Lcd.setCursor(15, 1);
          Lcd.print(">");
        }
        break;
      }
      case LcdScreen::LoraDenominator:
      {
        centerText("LoRA Denominator", 0);
        centerText(String(loraSettingParameter.codeDenominator).c_str(), 1); // Menampilkan Code Denominator

        if (lcdClicked) // Tampilkan kursor jika mode edit
        {
          Lcd.setCursor(0, 1);
          Lcd.print("<");
          Lcd.setCursor(15, 1);
          Lcd.print(">");
        }
        break;
      }
      case LcdScreen::LoraSignalBandwith:
      {
        centerText("LoRA Signal BW", 0);                                     // Disingkat agar muat
        centerText(String(loraSettingParameter.signalBandwidth).c_str(), 1); // Menampilkan Signal Bandwidth

        if (lcdClicked) // Tampilkan kursor jika mode edit
        {
          Lcd.setCursor(0, 1);
          Lcd.print("<");
          Lcd.setCursor(15, 1);
          Lcd.print(">");
        }
        break;
      }
      }
      xSemaphoreGive(lcdUpdateSemaphore); // Memberikan kembali semaphore LCD
    }
    vTaskDelay(150); // Jeda task update LCD (sekitar 6-7 FPS)
  }
}

void sendToServer(String data) // Fungsi untuk mengirim data ke server Python
{
  if (WiFi.status() != WL_CONNECTED) // Cek status koneksi WiFi
  {
    wiFiConnected = false; // Set status WiFi tidak terhubung
    Serial.println("Tidak terhubung ke internet, restart perangkat dan hubungkan lagi");
    return; // Keluar dari fungsi jika tidak ada koneksi
  }

  // Tinggal kirim data ke server python setelah LoRA (Komentar ini mungkin bisa diperjelas atau dihapus)
  WiFiClient client; // Membuat objek WiFiClient
  HTTPClient http;   // Membuat objek HTTPClient

  http.begin(client, Endpoint);                       // Memulai koneksi HTTP ke endpoint server
  http.addHeader("Content-Type", "application/json"); // Menambahkan header Content-Type

  int httpResponseCode = http.POST(data); // Mengirim data JSON via metode POST dan mendapatkan kode respons
  Serial.printf("[Mengirim ke %s] -> %s\n", Endpoint.c_str(), data.c_str());

  // Jika berhasil (kode respons 200 OK)
  if (httpResponseCode == 200)
  {
    String response = http.getString();                          // Mendapatkan respons dari server sebagai String
    JsonDocument doc;                                            // Membuat objek JsonDocument untuk parsing respons
    DeserializationError error = deserializeJson(doc, response); // Parsing JSON respons

    if (error) // Jika terjadi error saat parsing JSON
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.print(error.f_str());
      return; // Keluar dari fungsi
    }

    wiFiConnected = true;                   // Set status WiFi terhubung (karena server merespons)
    classification = doc["classification"]; // Mengambil nilai "classification" dari JSON respons
    buzzerOn = doc["buzzer_on"];            // Mengambil nilai "buzzer_on" dari JSON respons

    serverResponse.classification = classification; // Menyimpan hasil ke struct serverResponse
    serverResponse.buzzerOn = buzzerOn;

    Serial.printf("[%d] -> %s\n", httpResponseCode, response.c_str());

    sendLoraMessage(serverResponse); // Mengirim respons server kembali ke transmitter via LoRa
  }
  else // Jika terjadi error saat mengirim POST
  {
    classification = false; // Set default nilai jika error
    buzzerOn = false;
    wiFiConnected = false; // Set status WiFi tidak terhubung (karena error)

    serverResponse.classification = classification;
    serverResponse.buzzerOn = buzzerOn;
    Serial.println("Error on sending POST: " + String(httpResponseCode));
  }

  http.end(); // Menutup koneksi HTTP
}

// Fungsi sendLoraMessage overload untuk mengirim struct ServerResponse
void sendLoraMessage(const ServerResponse &responseData)
{
  if (paused)
  { // Jangan kirim jika sistem dijeda
    Serial.println("Paused, LoRa TX skipped.");
    return;
  }

  digitalWrite(ledKiri, HIGH); // Nyalakan LED TX LoRa

  // Membuat payload JSON dari struct ServerResponse
  JsonDocument doc;
  String serializedResponse;
  doc["classification"] = responseData.classification;
  doc["buzzer_on"] = responseData.buzzerOn;
  serializeJson(doc, serializedResponse); // Serialisasi JSON ke String

  // *** ADD LoRa State Management *** (Komentar ini menandakan bagian penting)
  LoRa.idle(); // Masuk ke mode standby sebelum mengirim

  // Kirim ke Receiver (actually back to Transmitter) -> Komentar ini menjelaskan tujuan pengiriman
  if (LoRa.beginPacket())
  {                                             // Memulai paket LoRa
    LoRa.write(loraParameter.loraDestination);  // Tambahkan alamat tujuan (transmitter asal)
    LoRa.write(loraParameter.loraLocalAddress); // Tambahkan alamat pengirim (receiver ini)
    LoRa.write(0xAA);                           // Tambahkan ID pesan (gunakan ID unik untuk respons)
    LoRa.write(serializedResponse.length());    // Tambahkan panjang payload
    LoRa.print(serializedResponse);             // Tambahkan payload

    if (LoRa.endPacket())
    { // Selesaikan dan kirim paket (blocking)
      Serial.print("[LoRa TX Response Sent] -> ");
      Serial.println(serializedResponse);
    }
    else
    {
      Serial.println("[LoRa TX ERROR] Failed to send packet!");
    }
  }
  else
  {
    Serial.println("[LoRa TX ERROR] Failed to begin packet!");
  }

  // *** ADD LoRa State Management *** (Komentar ini menandakan bagian penting)
  LoRa.receive();             // PENTING: Kembali ke mode receive setelah mengirim
  digitalWrite(ledKiri, LOW); // Matikan LED TX LoRa
}

void onLoraReceiveCallback(int packetSize) // Callback yang dipanggil ketika ada paket LoRa masuk
{
  if (packetSize == 0) // Jika ukuran paket 0 (tidak ada data)
  {
    digitalWrite(ledKanan, LOW); // Matikan LED RX jika tidak ada data (sebaiknya LED dikontrol saat data valid diterima/selesai diproses)
    return;                      // Keluar
  }

  // nyalakan indikator led jika ada data masuk
  digitalWrite(ledKanan, HIGH); // Nyalakan LED RX LoRa

  int recipient = LoRa.read();       // Baca alamat penerima dari paket
  byte sender = LoRa.read();         // Baca alamat pengirim dari paket
  byte incomingMsgId = LoRa.read();  // Baca ID pesan dari paket
  byte incomingLength = LoRa.read(); // Baca panjang pesan dari paket

  String incoming = ""; // String untuk menyimpan data yang diterima

  while (LoRa.available()) // Selama masih ada data yang bisa dibaca dari buffer LoRa
  {
    incoming += (char)LoRa.read(); // Baca per karakter dan tambahkan ke string
  }

  // Cek jika panjang pesan tidak sesuai
  if (incomingLength != incoming.length())
  {
    Serial.println("Panjang pesan tidak sesuai");
    digitalWrite(ledKanan, LOW); // Matikan LED jika error
    return;                      // Keluar
  }

  // Cek jika alamat penerima tidak valid (bukan alamat lokal atau broadcast 0xFF)
  if (recipient != loraParameter.loraLocalAddress && recipient != 0xFF)
  {
    Serial.println("Alamat Recipient tidak valid");
    digitalWrite(ledKanan, LOW); // Matikan LED jika error
    return;                      // Keluar
  }

  Serial.print("[Received LoRA Packet] -> ");
  Serial.println(incoming);

  loraParameter.incomingMessage = incoming; // Simpan pesan masuk
  loraRSSI = LoRa.packetRssi();             // Dapatkan nilai RSSI dari paket terakhir

  JsonDocument doc;                                            // Objek untuk parsing JSON
  DeserializationError error = deserializeJson(doc, incoming); // Parse JSON dari string masuk

  if (error) // Jika error parsing JSON
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.print(error.f_str());
    digitalWrite(ledKanan, LOW); // Matikan LED jika error
    return;                      // Keluar
  }

  // Dapatkan semua parameter dari JSON
  humidity = doc["humidity"];
  temperature = doc["temperature"];
  pH = doc["ph"];

  // Send directly (Komentar ini menandakan data langsung dikirim ke server)
  sendToServer(incoming);      // Kirim data yang diterima dari LoRa ke server
  digitalWrite(ledKanan, LOW); // Matikan LED RX setelah selesai memproses
}

// Fungsi sendLoraMessage overload untuk mengirim String (tidak dipakai dalam alur utama saat ini, tapi ada untuk kemungkinan lain)
void sendLoraMessage(String message)
{
  digitalWrite(ledKiri, HIGH);   // Nyalakan LED TX
  vTaskDelay(pdMS_TO_TICKS(10)); // Jeda singkat

  // Kirim ke Receiver (Sebenarnya ke Transmitter, karena ini Receiver Node)
  LoRa.beginPacket();                         // Mulai paket
  LoRa.write(loraParameter.loraDestination);  // Tambah alamat tujuan
  LoRa.write(loraParameter.loraLocalAddress); // Tambah alamat pengirim
  LoRa.write(1);                              // Tambah ID pesan (contoh: 1)
  LoRa.write(message.length());               // Tambah panjang payload
  LoRa.print(message);                        // Tambah payload
  LoRa.endPacket();                           // Selesaikan dan kirim paket

  Serial.print("[Data sent] -> ");
  Serial.println(message);

  vTaskDelay(pdMS_TO_TICKS(10)); // Jeda singkat
  digitalWrite(ledKiri, LOW);    // Matikan LED TX
}

// Fungsi untuk menampilkan teks di tengah LCD
void centerText(const char *text, int row)
{
  int len = strlen(text);        // Dapatkan panjang teks
  int startCol = (16 - len) / 2; // Hitung kolom awal agar teks di tengah (untuk LCD 16 kolom)

  Lcd.setCursor(startCol, row); // Posisikan kursor
  Lcd.print(text);              // Cetak teks
}

unsigned long lastSendTime = 0; // Variabel untuk melacak waktu pengiriman terakhir (tidak terpakai)
unsigned long interval;         // Variabel untuk interval (tidak terpakai)

void loop() // Fungsi loop utama, akan dipanggil berulang kali
{
  if (!paused) // Jika sistem tidak dijeda
  {
    onLoraReceiveCallback(LoRa.parsePacket()); // Cek dan proses paket LoRa yang masuk
  }
  if (paused) // Jika sistem dijeda
  {
    // Menyalakan kedua LED sebagai indikasi pause
    digitalWrite(ledKiri, HIGH);
    digitalWrite(ledKanan, HIGH);
  }
  // Tidak ada delay di loop utama karena sebagian besar pekerjaan dilakukan oleh task RTOS
  // dan onLoraReceiveCallback bersifat non-blocking jika tidak ada paket.
}