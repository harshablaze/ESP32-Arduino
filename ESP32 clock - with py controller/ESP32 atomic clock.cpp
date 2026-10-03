#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include "time.h"
#include "esp_sntp.h"
#include <HTTPClient.h>
#include <ArduinoJson.h> // Ensure this library is installed!

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==========================================================
// DYNAMIC REMOTE CONTROL REGISTER VARIABLES
// ==========================================================
// Thread-safe variables modified by Core 0 and read by Core 1
volatile uint8_t dynamicBrightness = 1; 
volatile uint8_t dynamicFlipState  = 0; // 0 = Normal, 1 = Negative Display
volatile uint8_t dynamicForceSleep = 0; // 0 = Normal, 1 = Force Screen Off Override

// Local Tracking States
uint8_t currentAppliedFlip = 0;
bool isScreenAsleep = false;

// Automated Fallback Sleep Schedule Configuration (24-Hour Format)
const int SLEEP_HOUR = 23; // 11 PM
const int SLEEP_MIN  = 10; // :10 PM
const int WAKE_HOUR  = 7;  // 7 AM
const int WAKE_MIN   = 15; // :15 AM

// Your Network Credentials
const char* ssid     = "HarshaBlaze_2.4G";
const char* password = "helloworld!";

// NTP Server and India Standard Time Configuration (+5:30 offset)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800;      
const int   daylightOffset_sec = 0;     

// Your Local PC Web Server Control Endpoint
const char* settingsUrl = "http://192.168.0.219:8080/settings";

// Quick string lookups for the clean text boxes
const char* daysOfWeek[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
const char* months[]     = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// Task handles for FreeRTOS dual-core separation
TaskHandle_t SettingsTaskHandle = NULL;

int getTextWidth(const char* text, int textSize) {
  return strlen(text) * 6 * textSize;
}

void setDisplayBrightness(uint8_t brightness) {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(brightness);
}

// ==========================================================
// BACKGROUND CORE 0 TASK: ASYNCHRONOUS SETTINGS POLLING
// ==========================================================
void fetchSettingsTask(void * parameter) {
  for(;;) {
    if(WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(settingsUrl);
      int httpCode = http.GET();
      
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
          // Extract variables into thread-safe registers
          dynamicBrightness = doc["brightness"] | 1;
          dynamicForceSleep = doc["sleep"] | 0;
          dynamicFlipState  = doc["flip"] | 0;
        }
      }
      http.end();
    }
    // Poll your PC server every 2 seconds completely off-core to stay accurate
    vTaskDelay(pdMS_TO_TICKS(2000)); 
  }
}

// Handles automatic panel shutdown and wake transitions
void handleSleepSchedule(struct tm* timeinfo) {
  int currentMinutes = (timeinfo->tm_hour * 60) + timeinfo->tm_min;
  int sleepMinutes   = (SLEEP_HOUR * 60) + SLEEP_MIN;
  int wakeMinutes    = (WAKE_HOUR * 60) + WAKE_MIN;
  
  bool shouldSleep = false;
  
  // 1. Evaluate fallback hardware time rules
  if (sleepMinutes > wakeMinutes) {
    if (currentMinutes >= sleepMinutes || currentMinutes < wakeMinutes) {
      shouldSleep = true;
    }
  } else {
    if (currentMinutes >= sleepMinutes && currentMinutes < wakeMinutes) {
      shouldSleep = true;
    }
  }

  // 2. Inject forced over-the-air remote sleep override check
  if (dynamicForceSleep == 1) {
    shouldSleep = true;
  }

  // State transitions
  if (shouldSleep && !isScreenAsleep) {
    setDisplayBrightness(0); 
    display.ssd1306_command(SSD1306_DISPLAYOFF); 
    isScreenAsleep = true;
    Serial.println("OLED Sleep Mode triggered.");
  } 
  else if (!shouldSleep && isScreenAsleep) {
    display.ssd1306_command(SSD1306_DISPLAYON);  
    setDisplayBrightness(dynamicBrightness);     
    isScreenAsleep = false;
    Serial.println("OLED Wake Mode triggered.");
  }
}

void printLocalTime() {
  struct tm timeinfo;
  
  if(!getLocalTime(&timeinfo)){
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 25);
    display.println("Syncing NTP Time...");
    display.display();
    return;
  }

  handleSleepSchedule(&timeinfo);

  if (isScreenAsleep) {
    return;
  }

  // Handle dynamic layout inversion changes
  if (dynamicFlipState != currentAppliedFlip) {
    currentAppliedFlip = dynamicFlipState;
    display.invertDisplay(currentAppliedFlip == 1);
  }

  // Process live slider brightness increments
  setDisplayBrightness(dynamicBrightness);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ==========================================
  // TOP ROW: PERFECTLY CENTERED DATE & DAY
  // ==========================================
  const char* dayText = daysOfWeek[timeinfo.tm_wday];
  int dayWidth = getTextWidth(dayText, 1);
  int dayX = (52 - dayWidth) / 2;
  display.setTextSize(1);
  display.setCursor(dayX, 2);
  display.print(dayText);

  char yearStr[8];
  sprintf(yearStr, "%d", timeinfo.tm_year + 1900);
  int yearWidth = getTextWidth(yearStr, 1);
  int yearX = (52 - yearWidth) / 2;
  display.setCursor(yearX, 13);
  display.print(yearStr);

  int boxX = 54;
  int boxY = 0;
  int boxWidth = 73; 
  int boxHeight = 24; 
  display.drawRoundRect(boxX, boxY, boxWidth, boxHeight, 2, SSD1306_WHITE);

  char numStr[4];
  sprintf(numStr, "%02d", timeinfo.tm_mday);
  const char* monthStr = months[timeinfo.tm_mon];

  int numWidth = getTextWidth(numStr, 2) - 2;       
  int monthWidth = getTextWidth(monthStr, 2) - 2;   
  int midGap = 6;                                   
  
  int totalTextWidth = numWidth + midGap + monthWidth;
  int startX = boxX + ((boxWidth - totalTextWidth) / 2);
  int textInsideY = boxY + 5; 

  display.setTextSize(2);
  display.setCursor(startX, textInsideY);
  display.print(numStr);
  display.setCursor(startX + numWidth + midGap, textInsideY);
  display.print(monthStr);

  // ==========================================
  // BOTTOM ROW: ZERO-GAP TIME LAYOUT
  // ==========================================
  int hour12 = timeinfo.tm_hour % 12;
  if (hour12 == 0) hour12 = 12; 
  const char* ampm = (timeinfo.tm_hour >= 12) ? "PM" : "AM";

  display.setTextSize(3);
  display.setCursor(0, 37);
  display.printf("%02d", hour12);

  display.setCursor(33, 37); 
  display.print(":");

  display.setCursor(47, 37);
  display.printf("%02d", timeinfo.tm_min);

  display.setTextSize(2);
  display.setCursor(87, 43);
  display.printf("%02d", timeinfo.tm_sec);

  display.setTextSize(1);
  display.setCursor(114, 50); 
  display.print(ampm);
  
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin();
  pinMode(22, INPUT_PULLUP); 
  pinMode(21, INPUT_PULLUP);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      return;
    }
  }

  display.setRotation(2); 
  display.fillScreen(SSD1306_WHITE); 
  display.display(); 
  delay(1000); 

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  sntp_set_sync_interval(15 * 60 * 1000UL);
  setDisplayBrightness(dynamicBrightness); 

  // ==========================================================
  // ASSIGN SETTINGS TASK INTERFACE TO RUN ON PROCESSOR CORE 0
  // ==========================================================
  xTaskCreatePinnedToCore(
    fetchSettingsTask,      // Function tracking pointer
    "SettingsTask",         // Debug name assignment
    4096,                   // Buffer memory allocation sizing
    NULL,                   // Input handles
    1,                      // Process priority ranking
    &SettingsTaskHandle,    // Core management address
    0                       // Locked core target mapping assignment
  );
}

void loop() {
  printLocalTime();
  delay(200); 
}
