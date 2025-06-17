#include <Arduino.h>
#include <LoRa.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <EEPROM.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>

String loraData;
unsigned long lastSendTime = 0;
unsigned long updateRate;
int counter = 0;
int loraRSSI;
int connectedDevices;
float humidity;
int humidityAdc;
float temperature;

// Definisi Pin
// Pin LoRA
#define ss 5
#define rst 14
#define dio0 2

// Pin sensor suhu DS18B20
const int ds18b20Pin = 4;
OneWire oneWire(ds18b20Pin);
DallasTemperature temperatureSensor(&oneWire); // Inisialisasi sensor suhu DS18B20

// Pin sensor humidity
const int humiditySensorPin = 34;

// Pin Push Button
const int pbKiri = 25;
const int pbTengah = 33;
const int pbKanan = 32;

// Pin Buzzer
const int buzzerPin = 15;

// Pin LED
const int ledKanan = 27;
const int ledKiri = 26;

// Sensor pH
const int DMSpin = 13;
const int DMSIndicator = 2;
const int DMSAdcPin = 35;

int phADC;
float lastPHRead;
float PH;

bool transmitMode = true;

// Definisi Lcd I2C di address 0x27 dengan ukuran 16x2
LiquidCrystal_I2C Lcd(0x27, 16, 2);

byte pauseChar[] = {
    B00000,
    B00000,
    B01010,
    B01010,
    B01010,
    B01010,
    B00000,
    B00000};

// Definisi Lora Parameter
struct LoraParameter
{
  byte loraLocalAddress;
  byte loraDestination;
  String outgoingMessage;
  byte msgCount;
};

LoraParameter loraParameter;

// Definisi fungsi
void onLoraReceiveCallback(int packetSize);
void sendLoraMessage(String message);
void centerText(const char *text, int row);

// definisi rtos
TaskHandle_t taskUpdateSensorHandler;
TaskHandle_t taskParameterUpdateHandler;
TaskHandle_t tasklcdUpdateHandler;
TaskHandle_t taskUpdateHumidityHandler;
TaskHandle_t taskUpdatePhSensor;
TaskHandle_t taskUpdate;

SemaphoreHandle_t loraSendSemaphore;
SemaphoreHandle_t lcdUpdateSemaphore;

// definisi fungsi rtos
void updateParameterTask(void *pvParameter);
void updateLcdTask(void *pvParameter);
void updateSensorTask(void *pvParameter);
void readHumiditySensor(void *pvParameter);
void readPhSensor(void *pvParameter);
void update(void *pvParameter);

// data dari server
struct ServerResponse
{
  bool classification;
  bool buzzerOn;
};

ServerResponse serverResponse;

bool buzzerLastState = false;
unsigned long activateBuzzerUntil = 0;
const unsigned long buzzerActiveTime = 3000;

// task related
bool paused;
uint8_t buttonState;
uint8_t lastButtonState;

// Lcd related
int lcdMenu, lastLcdMenu;
bool lcdClicked;

enum LcdScreen
{
  Monitoring,
  LoraRSSI,
  UpdateRateSetting,
  StatusKelayakan,
  StatusBuzzer,
  LoraTxPower,
  LoraSpreadingFactor,
  LoraDenominator,
  LoraSignalBandwith
};

#define LcdScreenPage 8

enum PushButtonAction
{
  Kiri_Pressed,
  Tengah_Pressed,
  Kanan_Pressed
};

bool loraSettingClicked = 0;
int loraSettingSubMenu = 0;

struct LoraSettingParameter
{
  int txPower;
  int spreadingFactor;
  int codeDenominator;
  float signalBandwidth;
};

bool loraScreenClicked;
int loraSettingPage;

LoraSettingParameter loraSettingParameter;
const float loraBandwidth[] = {7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3, 41.7E3, 62.5E3, 125E3, 250E3, 500E3};
int bandwidthSelector = 0;

int addresses[5] = {};

#define EEPROM_SIZE 512
#define ESP_BOOT_DELAY 1500

void setup()
{
  Serial.begin(115200);

  // Setting DMS Sensor PH
  analogReadResolution(10);
  pinMode(DMSpin, OUTPUT);
  pinMode(DMSIndicator, OUTPUT);
  digitalWrite(DMSpin, HIGH);

  while (!Serial)
    ;

  EEPROM.begin(EEPROM_SIZE);

  int addr = 0;

  addresses[0] = addr;
  addr += sizeof(updateRate);

  addresses[1] = addr;
  addr += sizeof(loraSettingParameter.txPower);

  addresses[2] = addr;
  addr += sizeof(loraSettingParameter.spreadingFactor);

  addresses[3] = addr;
  addr += sizeof(loraSettingParameter.codeDenominator);

  addresses[4] = addr;

  // Debugging: Print stored addresses
  Serial.println("EEPROM Address Mapping:");
  Serial.print("updateRate: ");
  Serial.println(addresses[0]);
  Serial.print("txPower: ");
  Serial.println(addresses[1]);
  Serial.print("spreadingFactor: ");
  Serial.println(addresses[2]);
  Serial.print("codeDenominator: ");
  Serial.println(addresses[3]);
  Serial.print("signalBandwith: ");
  Serial.println(addresses[4]);

  // Load saved values from EEPROM
  EEPROM.get(addresses[0], updateRate);
  EEPROM.get(addresses[1], loraSettingParameter.txPower);
  EEPROM.get(addresses[2], loraSettingParameter.spreadingFactor);
  EEPROM.get(addresses[3], loraSettingParameter.codeDenominator);
  EEPROM.get(addresses[4], loraSettingParameter.signalBandwidth);

  Serial.println("\nLoaded EEPROM Values:");
  Serial.print("updateRate: ");
  Serial.println(updateRate);
  Serial.print("txPower: ");
  Serial.println(loraSettingParameter.txPower);
  Serial.print("spreadingFactor: ");
  Serial.println(loraSettingParameter.spreadingFactor);
  Serial.print("codeDenominator: ");
  Serial.println(loraSettingParameter.codeDenominator);
  Serial.print("signalBandwith: ");
  Serial.println(loraSettingParameter.signalBandwidth);

  temperatureSensor.begin();

  Lcd.init();      // Inisialisasi LCD
  Lcd.backlight(); // Menyalakan Backlight LCD
  Lcd.createChar(0, pauseChar);

  // tampilan teks awal booting
  centerText("ESP32", 0);
  centerText("Transmitter", 1);

  delay(ESP_BOOT_DELAY);

  Lcd.clear(); // Bersihkan LCD

  Serial.println("LoRa Sender");

  LoRa.setPins(ss, rst, dio0); // setup LoRa transceiver module
  LoRa.setTxPower(loraSettingParameter.txPower);
  LoRa.setSpreadingFactor(loraSettingParameter.spreadingFactor);
  LoRa.setCodingRate4(loraSettingParameter.codeDenominator);
  LoRa.setSignalBandwidth(loraSettingParameter.signalBandwidth);

  Lcd.clear();
  centerText("LoRA", 0);
  centerText("Inisialisasi", 1);
  delay(500);

  // Inisialisasi dan konfigurasi LoRa
  while (!LoRa.begin(433E6)) // 433E6 - Asia, 866E6 - Europe, 915E6 - North America
  {
    Serial.println(".");
    delay(500);
  }

  Lcd.clear();
  centerText("LoRA", 0);
  centerText("Terinisialisasi", 1);
  delay(ESP_BOOT_DELAY);

  // Konfigurasi Address Lokal dan Destinasi
  loraParameter.loraLocalAddress = 0x01;
  loraParameter.loraDestination = 0x02;

  Serial.println("LoRa Initializing OK!");

  // Konfigurasi Pin
  pinMode(pbKiri, INPUT_PULLUP);
  pinMode(pbTengah, INPUT_PULLUP);
  pinMode(pbKanan, INPUT_PULLUP);
  pinMode(ledKanan, OUTPUT);
  pinMode(ledKiri, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(humiditySensorPin, INPUT);

  // Ketika memulai perangkat bunyikan buzzer sekali
  //  digitalWrite(buzzerPin, HIGH);
  //  delay(350);
  //  digitalWrite(buzzerPin, LOW);

  Lcd.clear();
  delay(500);

  // konfigurasi RTOS
  loraSendSemaphore = xSemaphoreCreateBinary();
  lcdUpdateSemaphore = xSemaphoreCreateBinary();

  xSemaphoreGive(loraSendSemaphore);
  xSemaphoreGive(lcdUpdateSemaphore);

  xTaskCreate(
      updateParameterTask,
      "Parameter Update",
      2048,
      NULL,
      1,
      &taskParameterUpdateHandler);

  xTaskCreate(
      update,
      "Update Task",
      2048,
      NULL,
      1,
      &taskUpdate);

  xTaskCreate(
      updateSensorTask,
      "Sensor Update",
      2048,
      NULL,
      1,
      &taskUpdateSensorHandler);

  xTaskCreate(
      updateLcdTask,
      "Update LCD Task",
      2048,
      NULL,
      1,
      &tasklcdUpdateHandler);

  xTaskCreate(
      readHumiditySensor,
      "Read Humidity Task",
      2048,
      NULL,
      1,
      &taskUpdateHumidityHandler);

  xTaskCreate(
      readPhSensor,
      "Read PH Sensor Task",
      2048,
      NULL,
      1,
      &taskUpdatePhSensor);
}

void update(void *pv)
{
  while (1)
  {
    float hMinMax = map(humidityAdc, 930, 214, 3, 40);
    Serial.printf("hAdc: %d hum: %2.2f htest: %2.2f phAdc: %d ph: %2.2f\n", humidityAdc, humidity, hMinMax, phADC, PH);
    vTaskDelay(1000);
  }
}

void readPhSensor(void *pvParameter)
{
  while (1)
  {
    // Membaca data ph
    digitalWrite(DMSpin, LOW);        // aktifkan DMS
    digitalWrite(DMSIndicator, HIGH); // led indikator built-in ESP32 menyala
    vTaskDelay(pdMS_TO_TICKS(1000));  // wait DMS capture data

    phADC = analogRead(DMSAdcPin);
    //  -0.0255x + 12.89 
    PH = (-0.0255 * phADC) + 12.89 ;

    if (PH < 0.0f || PH > 14.0)
    {
      PH = lastPHRead;
    }
    else if (PH != lastPHRead)
    {
      lastPHRead = PH;
    }

    digitalWrite(DMSpin, HIGH);
    digitalWrite(DMSIndicator, LOW);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void readHumiditySensor(void *pvParameter)
{
  while (1)
  {
    // Membaca data kelembapan
    // humidity = map(analogRead(humiditySensorPin), 1023, 0, 0, 100);
    humidityAdc = analogRead(humiditySensorPin);

    // unsigned int dist = abs(humidityAdc - 1023);

    // if (dist < 5)
    // {
    //   humidity = 0.0f;
    // }
    // else
    // {
    humidityAdc = (humidityAdc <= 100) ? 100 : humidityAdc;
    humidity = -0.0998 * (humidityAdc) + 101.68;
    humidity = min(max(humidity, 0.0f), 100.0f);

    //   // Data > 50%
    //   if (humidityAdc <= 300)
    //   {
    //     humidity = map(humidityAdc, 300, 250, 50, 90);
    //     if (humidity >= 100)
    //     {
    //       humidity = 100;
    //     }
    //   }
    // }

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// fungsi untuk update sensor
void updateSensorTask(void *pvParameter)
{
  while (1)
  {
    // meminta data suhu dari sensor DS18B20
    temperatureSensor.requestTemperatures();

    // membaca data suhu
    temperature = temperatureSensor.getTempCByIndex(0);

    vTaskDelay(pdMS_TO_TICKS(750));
  }
}

// fungsi untuk update parameter kontrol
void updateParameterTask(void *pvParameter)
{
  while (1)
  {
    buttonState = !digitalRead(pbKiri) | !digitalRead(pbTengah) << 1 | !digitalRead(pbKanan) << 2;

    bool pbKiriDitekan = buttonState != lastButtonState && buttonState == 1 << 0;
    bool pbTengahDitekan = buttonState != lastButtonState && buttonState == 1 << 1;
    bool pbKananDitekan = buttonState != lastButtonState && buttonState == 1 << 2;

    // PB Kiri ditekan
    if (pbKiriDitekan)
    {
      if (lcdClicked)
      {
        xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY);
        Lcd.setCursor(0, 1);
        Lcd.print("                        ");
        xSemaphoreGive(lcdUpdateSemaphore);

        switch (lcdMenu)
        {
        case LcdScreen::UpdateRateSetting:
        {
          Lcd.clear();
          updateRate -= 100;
          break;
        }
        case LcdScreen::LoraTxPower:
        {
          loraSettingParameter.txPower--;
          loraSettingParameter.txPower = loraSettingParameter.txPower < 2 ? 2 : loraSettingParameter.txPower > 20 ? 20
                                                                                                                  : loraSettingParameter.txPower;
          break;
        }
        // Spreading factor
        case LcdScreen::LoraSpreadingFactor:
        {
          loraSettingParameter.spreadingFactor--;
          loraSettingParameter.spreadingFactor = (loraSettingParameter.spreadingFactor < 7) ? 7 : loraSettingParameter.spreadingFactor > 12 ? 12
                                                                                                                                            : loraSettingParameter.spreadingFactor;
          break;
        }
        // Code denominatorfactor
        case LcdScreen::LoraDenominator:
        {
          loraSettingParameter.codeDenominator--;
          loraSettingParameter.codeDenominator = loraSettingParameter.codeDenominator < 5 ? 5 : loraSettingParameter.codeDenominator > 8 ? 8
                                                                                                                                         : loraSettingParameter.codeDenominator;
          break;
        }
        // Signal Bandwith
        case LcdScreen::LoraSignalBandwith:
        {
          bandwidthSelector--;
          bandwidthSelector = bandwidthSelector < 0 ? 0 : bandwidthSelector > 9 ? 9
                                                                                : bandwidthSelector;

          loraSettingParameter.signalBandwidth = loraBandwidth[bandwidthSelector];
          break;
        }
        }
      }
      else
      {
        lcdMenu--;
      }
    }

    // PB Tengah ditekan
    if (pbTengahDitekan)
    {
      if (lcdMenu == LcdScreen::UpdateRateSetting || lcdMenu == LcdScreen::LoraTxPower || lcdMenu == LcdScreen::LoraSpreadingFactor || lcdMenu == LcdScreen::LoraDenominator || lcdMenu == LcdScreen::LoraSignalBandwith)
      {
        lcdClicked ^= true;
      }

      if (lcdClicked)
      {
        Lcd.setCursor(0, 1);
        Lcd.print("<");
        Lcd.setCursor(15, 1);
        Lcd.print(">");
      }
      else
      {
        Lcd.setCursor(0, 1);
        Lcd.print(" ");
        Lcd.setCursor(15, 1);
        Lcd.print(" ");
      }

      switch (lcdMenu)
      {
      case LcdScreen::Monitoring:
      {
        paused ^= true;
        break;
      }

      case LcdScreen::UpdateRateSetting:
      {
        if (!lcdClicked)
        {
          xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY);

          EEPROM.writeInt(0, updateRate);
          EEPROM.commit();
          Lcd.clear();

          centerText("Menyimpan Data", 0);
          delay(1000);

          Lcd.clear();

          xSemaphoreGive(lcdUpdateSemaphore);
        }
        break;
      }
      case LcdScreen::LoraTxPower:
      {
        if (!lcdClicked)
        {
          xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY);

          EEPROM.put(addresses[1], loraSettingParameter.txPower);
          EEPROM.commit();
          Lcd.clear();

          LoRa.setTxPower(loraSettingParameter.txPower);

          centerText("Menyimpan Data", 0);
          delay(1000);

          Lcd.clear();

          xSemaphoreGive(lcdUpdateSemaphore);
        }
        break;
      }
      // Spreading Factor
      case LcdScreen::LoraSpreadingFactor:
      {
        if (!lcdClicked)
        {
          xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY);

          EEPROM.put(addresses[2], loraSettingParameter.spreadingFactor);
          EEPROM.commit();
          Lcd.clear();

          LoRa.setSpreadingFactor(loraSettingParameter.spreadingFactor);

          centerText("Menyimpan Data", 0);
          delay(1000);

          Lcd.clear();

          xSemaphoreGive(lcdUpdateSemaphore);
        }
        break;
      }
      // Code Denominator
      case LcdScreen::LoraDenominator:
      {
        if (!lcdClicked)
        {
          xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY);

          EEPROM.put(addresses[3], loraSettingParameter.codeDenominator);
          EEPROM.commit();
          Lcd.clear();

          LoRa.setCodingRate4(loraSettingParameter.codeDenominator);

          centerText("Menyimpan Data", 0);
          delay(1000);

          Lcd.clear();

          xSemaphoreGive(lcdUpdateSemaphore);
        }
        break;
      }
      // Signal Bandwidth (Pastikan Float Tidak Overlap)
      case LcdScreen::LoraSignalBandwith:
      {
        if (!lcdClicked)
        {
          xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY);

          EEPROM.put(addresses[3] + sizeof(loraSettingParameter.codeDenominator), loraSettingParameter.signalBandwidth);
          EEPROM.commit();
          Lcd.clear();

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

      if (lcdClicked)
      {
        xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY);
        Lcd.setCursor(0, 1);
        Lcd.print("                        ");
        xSemaphoreGive(lcdUpdateSemaphore);

        switch (lcdMenu)
        {
        case LcdScreen::UpdateRateSetting:
        {
          Lcd.clear();
          updateRate += 100;
          break;
        }
        case LcdScreen::LoraTxPower:
        {
          loraSettingParameter.txPower++;
          loraSettingParameter.txPower = loraSettingParameter.txPower < 2 ? 2 : loraSettingParameter.txPower > 20 ? 20
                                                                                                                  : loraSettingParameter.txPower;
          break;
        }
        // Spreading factor
        case LcdScreen::LoraSpreadingFactor:
        {
          loraSettingParameter.spreadingFactor++;
          loraSettingParameter.spreadingFactor = (loraSettingParameter.spreadingFactor < 7) ? 7 : loraSettingParameter.spreadingFactor > 12 ? 12
                                                                                                                                            : loraSettingParameter.spreadingFactor;
          break;
        }
        // Code denominatorfactor
        case LcdScreen::LoraDenominator:
        {
          loraSettingParameter.codeDenominator++;
          loraSettingParameter.codeDenominator = loraSettingParameter.codeDenominator < 5 ? 5 : loraSettingParameter.codeDenominator > 8 ? 8
                                                                                                                                         : loraSettingParameter.codeDenominator;
          break;
        }
        // Signal Bandwith
        case LcdScreen::LoraSignalBandwith:
        {
          bandwidthSelector++;
          bandwidthSelector = bandwidthSelector < 0 ? 0 : bandwidthSelector > 9 ? 9
                                                                                : bandwidthSelector;

          loraSettingParameter.signalBandwidth = loraBandwidth[bandwidthSelector];
          break;
        }
        }
      }
      else
      {
        lcdMenu++;
      }
    }

    if (lcdMenu != lastLcdMenu)
    {
      xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY);

      Lcd.clear();
      loraSettingSubMenu = 0;

      xSemaphoreGive(lcdUpdateSemaphore);
    }

    // constrain nilai lcd menu ke 0 - 2 menu
    loraSettingPage = (loraSettingPage < 0) ? 0 : (loraSettingPage > 1) ? 1
                                                                        : loraSettingPage;
    lcdMenu = (lcdMenu > LcdScreenPage) ? LcdScreenPage : (lcdMenu < 0) ? 0
                                                                        : lcdMenu;

    if (serverResponse.buzzerOn && buzzerLastState != serverResponse.buzzerOn)
    {
      activateBuzzerUntil = millis() + buzzerActiveTime;
    }

    if (serverResponse.buzzerOn && millis() < activateBuzzerUntil)
    {
      digitalWrite(buzzerPin, HIGH);
    }
    else
    {
      digitalWrite(buzzerPin, LOW);
    }

    lastLcdMenu = lcdMenu;
    lastButtonState = buttonState;
    buzzerLastState = serverResponse.buzzerOn;
    vTaskDelay(33);
  }
}

void updateLcdTask(void *pvParameter)
{
  while (1)
  {
    if (xSemaphoreTake(lcdUpdateSemaphore, portMAX_DELAY) == pdTRUE)
    {

      switch (lcdMenu)
      {
      case LcdScreen::Monitoring:
      {
        Lcd.setCursor(0, 0);
        Lcd.printf("T %.2f C", temperature);
        Lcd.setCursor(0, 1);

        if (humidity > 10.0)
        {
          Lcd.printf("H %.2f %%", humidity);
        }
        else
        {
          Lcd.printf("H %.2f %% ", humidity);
        }

        // tampilkan icon pause
        Lcd.setCursor(15, 0);

        if (paused)
        {
          Lcd.write(0);
        }
        else
        {
          Lcd.print(" ");
        }

        Lcd.setCursor(9, 1);
        Lcd.printf("pH %.1f ", PH);

        Lcd.setCursor(14, 0);
        Lcd.printf((serverResponse.classification) ? "L" : "T");
        break;
      }
      case LcdScreen::LoraRSSI:
      {
        centerText("Lora RSSI", 0);
        if (paused)
        {
          centerText("Terjeda", 1);
        }
        else if (loraRSSI == 0)
        {
          centerText("Tidak Terhubung", 1);
        }
        else
        {
          centerText(String(loraRSSI).c_str(), 1);
        }
        break;
      }
      case LcdScreen::UpdateRateSetting:
      {
        centerText("Update Rate", 0);
        centerText(String(updateRate).c_str(), 1);

        if (lcdClicked)
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
        centerText(serverResponse.classification ? "     Layak      " : "Tidak Layak", 1);
        break;
      }
      case LcdScreen::StatusBuzzer:
      {
        centerText("Status Buzzer", 0);
        centerText(serverResponse.buzzerOn ? "Hidup" : " Mati ", 1);
        break;
      }
      case LcdScreen::LoraTxPower:
      {
        centerText("LoRA Tx Power", 0);
        centerText(String(loraSettingParameter.txPower).c_str(), 1);

        if (lcdClicked)
        {
          Lcd.setCursor(0, 1);
          Lcd.print("<");
          Lcd.setCursor(15, 1);
          Lcd.print(">");
        }
        break;
      }
      // Spreading factor
      case LcdScreen::LoraSpreadingFactor:
      {
        centerText("LoRA SP Factor", 0);
        centerText(String(loraSettingParameter.spreadingFactor).c_str(), 1);
        if (lcdClicked)
        {
          Lcd.setCursor(0, 1);
          Lcd.print("<");
          Lcd.setCursor(15, 1);
          Lcd.print(">");
        }
        break;
      }
      // Code denominatorfactor
      case LcdScreen::LoraDenominator:
      {
        centerText("LoRA Denominator", 0);
        centerText(String(loraSettingParameter.codeDenominator).c_str(), 1);
        if (lcdClicked)
        {
          Lcd.setCursor(0, 1);
          Lcd.print("<");
          Lcd.setCursor(15, 1);
          Lcd.print(">");
        }
        break;
      }
      // Signal Bandwith
      case LcdScreen::LoraSignalBandwith:
      {
        centerText("LoRA Signal Bandwith", 0);
        centerText(String(loraSettingParameter.signalBandwidth).c_str(), 1);
        if (lcdClicked)
        {
          Lcd.setCursor(0, 1);
          Lcd.print("<");
          Lcd.setCursor(15, 1);
          Lcd.print(">");
        }
        break;
      }
      }

      xSemaphoreGive(lcdUpdateSemaphore);
    }
    vTaskDelay(150);
  }
}

void onLoraReceiveCallback(int packetSize)
{
  


  if (packetSize == 0)
    return; 

  digitalWrite(ledKanan, HIGH); // RX LED ON

  int recipient = LoRa.read();
  byte sender = LoRa.read();
  byte incomingMsgId = LoRa.read();
  byte incomingLength = LoRa.read();
  String incoming = "";
  while (LoRa.available())
  {
    incoming += (char)LoRa.read();
  }

  if (incomingLength != incoming.length())
  {
    Serial.println("[LoRa RX] Length mismatch!");
    digitalWrite(ledKanan, LOW); // RX LED OFF
    return;
  }
  if (recipient != loraParameter.loraLocalAddress)
  {
    Serial.println("[LoRa RX] Invalid recipient!");
    digitalWrite(ledKanan, LOW); // RX LED OFF
    return;
  }

  // --- Pemrosesan Respons yang Valid ---
Serial.print("[Data Respons LoRa Diterima] -> "); // Menunjukkan bahwa ini adalah data respons
Serial.println(incoming);                         // Mencetak isi data respons yang diterima ke Serial Monitor


  loraRSSI = LoRa.packetRssi(); // Menyimpan nilai RSSI dari respons


  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, incoming);

  if (error)
  {
    Serial.print(F("[LoRa RX] Response JSON deserialize failed: "));
    Serial.println(error.f_str());
    // Optional: Reset serverResponse values if parsing fails
    serverResponse.classification = false;
    serverResponse.buzzerOn = false;
  }
  else
  {
    // Perbarui status berdasarkan respons valid dari receiver/server
    serverResponse.classification = doc["classification"];
    serverResponse.buzzerOn = doc["buzzer_on"];
    Serial.printf("[LoRa RX] Response Parsed: Class=%d, Buzzer=%d\n", serverResponse.classification, serverResponse.buzzerOn);
  }

  digitalWrite(ledKanan, LOW); // RX LED OFF after processing
}

unsigned long msgId = 0;

void sendLoraMessage(String message)
{
  digitalWrite(ledKiri, HIGH); // Turn on TX LED

  // *** Ensure LoRa is idle before starting transmission ***
  LoRa.idle();

  // Kirim ke Receiver
  if (LoRa.beginPacket())
  {                                             // start packet
    LoRa.write(loraParameter.loraDestination);  // add destination address
    LoRa.write(loraParameter.loraLocalAddress); // add sender address
    LoRa.write(msgId);                          // add message ID
    LoRa.write(message.length());               // add payload length
    LoRa.print(message);                        // add payload

    if (LoRa.endPacket())
{ // menyelesaikan paket dan mengirimkannya (secara blocking)
  // Serial.print("[Data LoRa Dikirim] -> ");
  // Serial.println(message);
}

    else
    {
      Serial.println("[LoRa TX ERROR] Failed to send packet!");
      // // Pertimbangkan bagaimana menangani kegagalan pengiriman (TX) – apakah perlu dicoba ulang? Dicatat (log)?

    }
  }
  else
  {
    Serial.println("[LoRa TX ERROR] Failed to begin packet!");
  }

  digitalWrite(ledKiri, LOW); // // Matikan LED TX segera setelah percobaan pengiriman


  msgId++; // Increment message ID

  
}
// Fungsi untuk menampilkan teks di tengah LCD
void centerText(const char *text, int row)
{
  int len = strlen(text);
  int startCol = (16 - len) / 2;

  Lcd.setCursor(startCol, row);
  Lcd.print(text);
}

void loop()
{
  // // Periksa apakah saat ini waktunya untuk memulai siklus kirim & terima

  if (millis() - lastSendTime > updateRate && !paused)
  {
    // --- Phase 1: Send Sensor Data ---
    JsonDocument doc;
    String serializedJson;

    // // Pastikan nilai sensor masih cukup baru (tugas pembacaan sensor harus berjalan)

    doc["humidity"] = humidity;
    doc["temperature"] = temperature;
    doc["ph"] = PH;

    serializeJson(doc, serializedJson);

    Serial.println("------------------------------");
    Serial.printf("[%lu] Starting Send cycle...\n", millis());
    sendLoraMessage(serializedJson); // Call the send function

    // // --- Fase 2: Menunggu Respons ---

    Serial.println("[LoRa] TX Done. Switching to RX mode for response...");
    LoRa.receive(); // Explicitly enter receive mode to listen

    const unsigned long responseTimeout = 2000; // Wait up to 2000ms (2 seconds) for a response
    unsigned long listenStartTime = millis();
    bool responseReceived = false;

    while (millis() - listenStartTime < responseTimeout)
    {
      int packetSize = LoRa.parsePacket();
      if (packetSize)
      {
        Serial.printf("[%lu] Received response packet!\n", millis());
        onLoraReceiveCallback(packetSize); // Process the received packet
        responseReceived = true;
        break; // Exit the listening loop once response is received
      }
      // Briefly yield to allow other tasks/RTOS functions
      vTaskDelay(pdMS_TO_TICKS(5));
    }

    // --- Phase 3: Handle Timeout / Go Idle ---
    if (!responseReceived)
    {
      Serial.printf("[%lu] No response received within timeout.\n", millis());
      
      loraRSSI = 0; 
    }

    Serial.println("[LoRa] Listening period over. Idling LoRa module.");
    LoRa.idle(); // Put LoRa module to sleep/idle until the next send cycle

 // Perbarui lastSendTime hanya SETELAH seluruh siklus selesai

    lastSendTime = millis();
    Serial.println("------------------------------");

  } // Akhir dari pemeriksaan interval waktu


  //  Jika dijeda, tangani indikator jeda (pertahankan bagian ini)

  if (paused)
  {
    

    digitalWrite(ledKiri, HIGH);  // Indicate paused
    digitalWrite(ledKanan, HIGH); // Indicate paused
  }
  else
  {
    
  }

  
  vTaskDelay(pdMS_TO_TICKS(10));
}
