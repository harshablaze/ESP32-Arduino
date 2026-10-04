#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include "time.h"
#include "esp_sntp.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==========================================================
// DYNAMIC REMOTE CONTROL REGISTER VARIABLES
// ==========================================================
volatile uint8_t dynamicBrightness = 1; 
volatile uint8_t dynamicFlipState  = 0; // 0 = Normal, 1 = Negative Display
volatile uint8_t dynamicForceSleep = 0; // 0 = Normal, 1 = Force Screen Off Override
volatile uint8_t pixelShiftTest    = 0; // 0 = Normal Slow Shift, 1 = Fast-paced Shift Test Mode
volatile uint8_t enableGlitch      = 1; // 0 = Glitch Disabled, 1 = Glitch Enabled
volatile uint8_t borderStyle       = 1; // 0 = Solid Border, 1 = Animated Dotted Border

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

// Your Local PC Web Server Endpoints
const char* settingsUrl = "http://192.168.0.219:8080/settings";
const char* streamUrl   = "http://192.168.0.219:8080/stream.bin";

// Thread-Safe Shared Buffers and State Flags
const int frameBufferSize = 1024; // 128x64 pixels / 8 bits
uint8_t sharedFrameBuffer[frameBufferSize];
volatile bool isStreamActive = false; 

// Quick string lookups for the clean text boxes
const char* daysOfWeek[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
const char* months[]     = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// Task handles for FreeRTOS dual-core separation
TaskHandle_t NetworkTaskHandle = NULL;

// ==========================================================
// SCREEN PRESERVATION & ANIMATION VARIABLES
// ==========================================================
int8_t shiftX = 0;
int8_t shiftY = 0;
uint32_t lastShiftTime = 0;
uint32_t lastMarqueeTime = 0;
uint8_t shiftPatternIndex = 0;
uint8_t dottedLineOffset = 0;

struct ShiftCoord {
  int8_t x;
  int8_t y;
};

// The predefined 3-pixel bounded shift coordinate system
const ShiftCoord shiftPattern[] = {
  {0, 0}, {1, 0}, {2, 0}, {3, 0},
  {3, 1}, {2, 1}, {1, 1}, {0, 1},
  {0, 2}, {1, 2}, {2, 2}, {3, 2},
  {3, 3}, {2, 3}, {1, 3}, {0, 3},
  {-1, 0}, {-2, 0}, {-3, 0},
  {-3, 1}, {-2, 1}, {-1, 1},
  {-1, 2}, {-2, 2}, {-3, 2},
  {-3, 3}, {-2, 3}, {-1, 3}
};
const uint8_t totalShiftPatterns = sizeof(shiftPattern) / sizeof(shiftPattern[0]);

int getTextWidth(const char* text, int textSize) {
  return strlen(text) * 6 * textSize;
}

void setDisplayBrightness(uint8_t brightness) {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(brightness);
}

// Custom function to draw the dynamic crawling marquee dotted border
void drawAnimatedDottedBox(int16_t startX, int16_t startY, int16_t w, int16_t h, uint8_t offset) {
  // Top edge
  for (int16_t x = 0; x < w; x++) {
    int16_t globalX = startX + x;
    if ((x + offset) % 6 < 2) {
      display.drawPixel(globalX, startY, SSD1306_WHITE);
    }
  }
  // Bottom edge
  for (int16_t x = 0; x < w; x++) {
    int16_t globalX = startX + x;
    if ((x + offset) % 6 < 2) {
      display.drawPixel(globalX, startY + h - 1, SSD1306_WHITE);
    }
  }
  // Left edge
  for (int16_t y = 0; y < h; y++) {
    int16_t globalY = startY + y;
    if ((y + offset) % 6 < 2) {
      display.drawPixel(startX, globalY, SSD1306_WHITE);
    }
  }
  // Right edge
  for (int16_t y = 0; y < h; y++) {
    int16_t globalY = startY + y;
    if ((y + offset) % 6 < 2) {
      display.drawPixel(startX + w - 1, globalY, SSD1306_WHITE);
    }
  }
}

// Applies horizontal slicing displacement to mimic a cyberpunk graphical error
void applyGlitchEffect(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (random(0, 100) > 92) { // 8% chance to execute per display refresh
    int16_t sliceY = y + random(2, h - 4);
    int16_t sliceH = random(1, 4);
    int16_t shiftVal = random(-3, 4);
    
    // Draw an intersecting black rectangle to strip data lines out cleanly
    display.fillRect(x, sliceY, w, sliceH, SSD1306_BLACK);
    
    // Shift elements visually using pixel drawing blocks
    if (shiftVal != 0) {
      display.drawFastHLine(x + shiftVal, sliceY, w - abs(shiftVal), SSD1306_WHITE);
    }
  }
}

// ==========================================================
// BACKGROUND CORE 0 TASK: INTELLIGENT MODE-SWITCHING LOGIC
// ==========================================================
void networkManagementTask(void * parameter) {
  HTTPClient http;
  
  for(;;) {
    if(WiFi.status() == WL_CONNECTED) {
      
      // STEP 1: RUN NORMAL CLOCK MODE POLLING SEQUENCE
      if (!isStreamActive) {
        // Poll settings URL
        http.begin(settingsUrl);
        http.setTimeout(300); // Small threshold preventing network drops from blocking core processing
        int httpCode = http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
          String payload = http.getString();
          StaticJsonDocument<256> doc;
          DeserializationError error = deserializeJson(doc, payload);
          if (!error) {
            dynamicBrightness = doc["brightness"] | 1;
            dynamicForceSleep = doc["sleep"] | 0;
            dynamicFlipState  = doc["flip"] | 0;
            pixelShiftTest    = doc["pixelShiftTest"] | 0; // Parse fast test flag from JSON payload
            enableGlitch       = doc["glitch"] | 0;       // Parse glitch toggle state
            borderStyle        = doc["borderStyle"] | 0;   // Parse border format configuration
          }
        }
        http.end(); 

        http.begin(streamUrl);
        http.setTimeout(200); 
        httpCode = http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
          WiFiClient* stream = http.getStreamPtr();
          uint8_t tempBuffer[frameBufferSize];
          int bytesRead = stream->readBytes(tempBuffer, frameBufferSize);
          
          if (bytesRead == frameBufferSize) {
            memcpy(sharedFrameBuffer, tempBuffer, frameBufferSize);
            isStreamActive = true; // Switch to Streaming Mode instantly!
            Serial.println("[Core 0] Active bitstream caught! Switching to high-speed mode.");
          }
        }
        http.end();

        // If no stream data was found, maintain your clean, low-overhead 2-second check loop
        if (!isStreamActive) {
          vTaskDelay(pdMS_TO_TICKS(2000));
          continue; 
        }
      }

      // STEP 2: STREAMING MODE ENGAGED (LOCKS CORE 0 INTO FAST 50MS MEDIA CYCLES)
      if (isStreamActive) {
        http.begin(streamUrl);
        http.setTimeout(150);
        http.setReuse(true); // Persist connection context headers to sustain maximum packet speeds
        
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
          WiFiClient* stream = http.getStreamPtr();
          uint8_t tempBuffer[frameBufferSize];
          int bytesRead = stream->readBytes(tempBuffer, frameBufferSize);
          
          if (bytesRead == frameBufferSize) {
            memcpy(sharedFrameBuffer, tempBuffer, frameBufferSize);
          } else {
            isStreamActive = false; // Packet structural drop, drop out out of loop 
          }
        } else {
          // If server stops running, cleanly exit streaming mode and trigger recovery logic
          isStreamActive = false;
          http.setReuse(false);
          Serial.println("[Core 0] Stream ended or connection lost. Falling back to Clock Mode.");
        }
        http.end();
        
        // High-speed 50ms interval loop pacing matching the media script requirements
        vTaskDelay(pdMS_TO_TICKS(50)); 
      }

    } else {
      isStreamActive = false;
      vTaskDelay(pdMS_TO_TICKS(1000)); // Sleep loop momentarily if local router link drops offline
    }
  }
}

// Handles automatic panel shutdown and wake transitions
void handleSleepSchedule(struct tm* timeinfo) {
  int currentMinutes = (timeinfo->tm_hour * 60) + timeinfo->tm_min;
  int sleepMinutes   = (SLEEP_HOUR * 60) + SLEEP_MIN;
  int wakeMinutes    = (WAKE_HOUR * 60) + WAKE_MIN;
  
  bool shouldSleep = false;
  
  if (sleepMinutes > wakeMinutes) {
    if (currentMinutes >= sleepMinutes || currentMinutes < wakeMinutes) {
      shouldSleep = true;
    }
  } else {
    if (currentMinutes >= sleepMinutes && currentMinutes < wakeMinutes) {
      shouldSleep = true;
    }
  }

  if (dynamicForceSleep == 1) {
    shouldSleep = true;
  }

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

void displayClockFace(struct tm* timeinfo) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  uint32_t now = millis();
  
  // Update Pixel Shifting Calculations
  uint32_t shiftInterval = (pixelShiftTest == 1) ? 200 : 900000; // 200ms for fast testing vs 15 minutes normal tracking
  if (now - lastShiftTime >= shiftInterval) {
    lastShiftTime = now;
    shiftPatternIndex = (shiftPatternIndex + 1) % totalShiftPatterns;
    shiftX = shiftPattern[shiftPatternIndex].x;
    shiftY = shiftPattern[shiftPatternIndex].y;
  }

  // Update Crawling Marquee Dotted Line Phase
  uint32_t marqueeInterval = (pixelShiftTest == 1) ? 50 : 250;
  if (now - lastMarqueeTime >= marqueeInterval) {
    lastMarqueeTime = now;
    dottedLineOffset = (dottedLineOffset + 1) % 6; 
  }

  // Periodic Box Swapping Interval Tracker (Swaps placements every 2 minutes)
  bool isBoxSwapped = ((timeinfo->tm_min % 4) >= 2);

  // Set default coordinates for Top Component Sections
  int16_t dayBoxX = 0;
  int16_t dateBoxX = 54;
  
  if (isBoxSwapped) {
    dayBoxX = 76;   // Displace to the rightmost track bounds
    dateBoxX = 0;   // Drop box directly into starting coordinate arrays
  }

  // ==========================================
  // TOP LEFT SUBSECTION: CENTERED DATE & DAY (With Pixel Shifting applied inside its boundaries)
  // ==========================================
  const char* dayText = daysOfWeek[timeinfo->tm_wday];
  int dayWidth = getTextWidth(dayText, 1);
  int dayX = dayBoxX + ((52 - dayWidth) / 2) + shiftX;
  
  display.setTextSize(1);
  display.setCursor(dayX, 2 + shiftY);
  display.print(dayText);

  char yearStr[8];
  sprintf(yearStr, "%d", timeinfo->tm_year + 1900);
  int yearWidth = getTextWidth(yearStr, 1);
  int yearX = dayBoxX + ((52 - yearWidth) / 2) + shiftX;
  display.setCursor(yearX, 13 + shiftY);
  display.print(yearStr);

  // ==========================================
  // TOP RIGHT SUBSECTION: BOXED DATE MODULE
  // ==========================================
  int boxY = 0;
  int boxWidth = 73; 
  int boxHeight = 24; 
  
  if (borderStyle == 1) {
    // Dynamic crawling marquee style
    drawAnimatedDottedBox(dateBoxX, boxY, boxWidth, boxHeight, dottedLineOffset);
  } else {
    // Standard solid fallback box line style
    display.drawRoundRect(dateBoxX, boxY, boxWidth, boxHeight, 2, SSD1306_WHITE);
  }

  char numStr[4];
  sprintf(numStr, "%02d", timeinfo->tm_mday);
  const char* monthStr = months[timeinfo->tm_mon];

  int numWidth = getTextWidth(numStr, 2) - 2;       
  int monthWidth = getTextWidth(monthStr, 2) - 2;   
  int midGap = 6;                                   
  
  int totalTextWidth = numWidth + midGap + monthWidth;
  int startX = dateBoxX + ((boxWidth - totalTextWidth) / 2);
  int textInsideY = boxY + 5; 

  display.setTextSize(2);
  display.setCursor(startX, textInsideY);
  display.print(numStr);
  display.setCursor(startX + numWidth + midGap, textInsideY);
  display.print(monthStr);

  // Apply Cyberpunk glitch visual overrides if enabled remotely
  if (enableGlitch == 1) {
    applyGlitchEffect(0, 0, SCREEN_WIDTH, 24);
  }

  // ==========================================
  // BOTTOM ROW: ZERO-GAP TIME LAYOUT
  // ==========================================
  int hour12 = timeinfo->tm_hour % 12;
  if (hour12 == 0) hour12 = 12; 
  const char* ampm = (timeinfo->tm_hour >= 12) ? "PM" : "AM";

  display.setTextSize(3);
  display.setCursor(0, 37);
  display.printf("%02d", hour12);

  display.setCursor(33, 37); 
  display.print(":");

  display.setCursor(47, 37);
  display.printf("%02d", timeinfo->tm_min);

  display.setTextSize(2);
  display.setCursor(87, 43);
  display.printf("%02d", timeinfo->tm_sec);

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
  // SPIN UP INTERMEDIARY NETWORK ENGINE PIPED TO CORE 0
  // ==========================================================
  xTaskCreatePinnedToCore(
    networkManagementTask,  
    "NetworkTask",       
    8192, // Scaled stack buffer ensuring dual HTTP allocations stay safe
    NULL,                   
    1,
    &NetworkTaskHandle,
    0
    );
}


void loop() {
  // 1. Core 1 updates hardware features instantly using internal registers
  if (dynamicFlipState != currentAppliedFlip) {
    currentAppliedFlip = dynamicFlipState;
    display.invertDisplay(currentAppliedFlip == 1);
  }
  
  if (!isScreenAsleep) {
    setDisplayBrightness(dynamicBrightness);
  }
  // 2. Continuous structural system evaluation checks
  struct tm timeinfo;
  bool hasTime = getLocalTime(&timeinfo);
  if (hasTime) {
    handleSleepSchedule(&timeinfo);
  }
  // 3. Render routing branch decisions
  if (!isScreenAsleep) {
    if (isStreamActive) {
      // Stream is active, drop buffer arrays cleanly onto the screen interface
      display.clearDisplay();
      display.drawBitmap(0, 0, sharedFrameBuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
      display.display();
    } else if (hasTime) {
      // Normal fallback mode, render local time arrays fluidly
      displayClockFace(&timeinfo);
    }
  }
  // Blazing fast 50ms display execution cycle rate loops
  delay(50);
}