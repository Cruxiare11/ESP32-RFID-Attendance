#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <WiFi.h>
#include <time.h>

const char* ssid     = "CCIS-SD";
const char* password = "cc1ssmartdevices_";

const long  gmtOffset_sec = 8 * 3600;
const int   daylightOffset_sec = 0;

#define BUTTON_PIN 4  // GPIO pin for button input

#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK  18

#define RST_PIN  22
#define SS_PIN   21
MFRC522 mfrc522(SS_PIN, RST_PIN);

#define TFT_CS   5
#define TFT_DC   17
#define TFT_RST  16
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

char UID_Str[32] = "";
bool showingCard = false;
unsigned long lastRead = 0;
const unsigned long displayTime = 3000;

unsigned long lastTimeUpdate = 0;
const unsigned long timeRefresh = 1000;

uint16_t bgColor = ST77XX_WHITE;
uint16_t textColor = ST77XX_BLACK;

// For card highlight animation
uint8_t highlightAlpha = 0;
bool highlightIncreasing = true;

bool userTimeOut = false;    // false = Time In, true = Time Out
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 200; // ms

void connectToWiFi() {
  static unsigned long startTime = millis();
  static bool connecting = false;
  static bool wasConnected = false;

  if (WiFi.status() == WL_CONNECTED) {
    if (!wasConnected) {
      Serial.println("WiFi connected.");
      tft.fillScreen(bgColor);
      wasConnected = true;
    }
    connecting = false;
    return;
  }

  if (!connecting) {
    Serial.println("Connecting to WiFi...");
    tft.fillScreen(ST77XX_BLACK);  // Only draw once
    tft.setCursor(10, 10);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.println("Connecting to WiFi...");
    WiFi.begin(ssid, password);
    connecting = true;
    wasConnected = false;
    startTime = millis();
  }

  if (millis() - startTime > 20000) {
    Serial.println("WiFi connect timeout, retrying...");
    WiFi.disconnect(true);
    WiFi.begin(ssid, password);
    startTime = millis();
  }
}

void byteArrayToHexString(byte array[], unsigned int len, char buffer[]) {
  for (unsigned int i = 0; i < len; i++) {
    byte nib1 = (array[i] >> 4) & 0x0F;
    byte nib2 = (array[i]) & 0x0F;
    buffer[i * 2 + 0] = nib1 < 10 ? '0' + nib1 : 'A' + nib1 - 10;
    buffer[i * 2 + 1] = nib2 < 10 ? '0' + nib2 : 'A' + nib2 - 10;
  }
  buffer[len * 2] = '\0';
}

void drawTopBanner(struct tm* timeinfo) {
  tft.fillRect(0, 0, 320, 20, ST77XX_GREEN);  // Smaller banner
  tft.setTextColor(ST77XX_WHITE);
  tft.setFont();  // Use built-in font
  tft.setTextSize(1);

  char dateStr[16];
  char timeStr[20];  // increased size for AM/PM

  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", timeinfo);

  // Convert 24-hour to 12-hour format with AM/PM
  int hour12 = timeinfo->tm_hour % 12;
  if (hour12 == 0) hour12 = 12;
  const char* ampm = (timeinfo->tm_hour >= 12) ? "PM" : "AM";
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d %s", hour12, timeinfo->tm_min, timeinfo->tm_sec, ampm);

  tft.setCursor(5, 5);
  tft.print(dateStr);
  tft.setCursor(180, 5);
  tft.print(timeStr);
}

void drawWaitingMessage() {
  tft.fillRect(0, 20, 320, 220, bgColor);
  tft.setCursor(10, 120);
  tft.setTextColor(textColor);
  tft.setFont();
  tft.setTextSize(2);  // Larger built-in text
  tft.println("Waiting for card...");
}

void drawModeSelection() {
  // Clear a small area near top or bottom to show mode
  tft.fillRect(0, 200, 320, 40, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 210);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print("Mode: ");
  tft.print(userTimeOut ? "Time Out" : "Time In");
}

void drawCardInfo(bool userTimeOut, struct tm* timeinfo) {
  tft.fillRect(0, 20, 320, 220, bgColor);
  tft.setFont();
  tft.setTextSize(2);

  tft.setCursor(10, 60);
  tft.setTextColor(userTimeOut ? ST77XX_RED : ST77XX_BLUE, bgColor);
  tft.print(userTimeOut ? "Time Out" : "Time In");

  tft.setCursor(10, 100);
  tft.setTextColor(ST77XX_GREEN, bgColor);
  tft.println("Card Detected!");

  tft.setTextSize(1);
  tft.setCursor(10, 140);
  tft.setTextColor(textColor, bgColor);
  tft.print("UID:");
  tft.setCursor(10, 155);
  tft.print(UID_Str);

  // Show scan time in 12-hour format with AM/PM
  char scanTime[20];
  int hour12 = timeinfo->tm_hour % 12;
  if (hour12 == 0) hour12 = 12;
  const char* ampm = (timeinfo->tm_hour >= 12) ? "PM" : "AM";
  snprintf(scanTime, sizeof(scanTime), "%02d:%02d:%02d %s", hour12, timeinfo->tm_min, timeinfo->tm_sec, ampm);

  tft.setCursor(10, 175);
  tft.print("Time: ");
  tft.print(scanTime);

  // Show scan date
  char scanDate[16];
  strftime(scanDate, sizeof(scanDate), "%Y-%m-%d", timeinfo);
  tft.setCursor(10, 190);
  tft.print("Date: ");
  tft.print(scanDate);
}

void updateColors(struct tm* timeinfo) {
    bgColor = ST77XX_WHITE;
    textColor = ST77XX_BLACK;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  tft.init(240, 320); // Match screen buffer to landscape mode
  tft.setRotation(1);

  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 10);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.println("Powering ON!");
  delay(3000);

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  tft.init(240, 320);         // Re-init here just to be safe
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);  // Try a clear screen here

  delay(100); // allow SPI/TFT to stabilize

  mfrc522.PCD_Init();         // Now initialize RFID after TFT
  connectToWiFi();

  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(500);
  }

  updateColors(&timeinfo);
  drawTopBanner(&timeinfo);
  drawWaitingMessage();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  drawModeSelection();// Display initial selection
  Serial.println("Ready. Scan your card.");
}

void loop() {
   // Check button press (active low)
  if (digitalRead(BUTTON_PIN) == LOW) {
  unsigned long now = millis();
  if (now - lastButtonPress > debounceDelay) {
    userTimeOut = !userTimeOut;
    drawModeSelection();
    lastButtonPress = now;
    Serial.print("Mode changed to: ");
    Serial.println(userTimeOut ? "Time Out" : "Time In");
  }
}

  if (millis() - lastTimeUpdate > timeRefresh) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to update time.");
    return;
  }
  static int lastSec = -1;
  if (timeinfo.tm_sec != lastSec) {
    updateColors(&timeinfo);
    drawTopBanner(&timeinfo);
    lastSec = timeinfo.tm_sec;
  }
  lastTimeUpdate = millis();
}

  // Handle card display timeout
  if (showingCard && millis() - lastRead > displayTime) {
    drawWaitingMessage();
    showingCard = false;
    drawModeSelection();
  }

  // WiFi reconnect attempt non-blocking
  connectToWiFi();

  // Check for new card
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  byteArrayToHexString(mfrc522.uid.uidByte, mfrc522.uid.size, UID_Str);

  Serial.print("Card UID: ");
  Serial.println(UID_Str);

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  struct tm timeinfo;
  getLocalTime(&timeinfo);

  drawTopBanner(&timeinfo);
  drawCardInfo(userTimeOut, &timeinfo);

  showingCard = true;
  lastRead = millis();
}
