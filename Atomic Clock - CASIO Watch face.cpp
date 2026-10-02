#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include "time.h"
#include "esp_sntp.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Your Network Credentials
const char* ssid     = "HarshaBlaze_2.4G";
const char* password = "helloworld!";

// NTP Server and India Standard Time Configuration (+5:30 offset)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800;      
const int   daylightOffset_sec = 0;     

// Quick string lookups for the clean text boxes
const char* daysOfWeek[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
const char* months[]     = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// Helper function to get text width for precise centering
int getTextWidth(const char* text, int textSize) {
  return strlen(text) * 6 * textSize;
}

// Custom helper function to adjust display panel brightness
void setDisplayBrightness(uint8_t brightness) {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(brightness);
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

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ==========================================
  // TOP ROW: PERFECTLY CENTERED DATE & DAY
  // ==========================================
  
  // 1. Center the Day of the Week in the left region (X: 0 to 52)
  const char* dayText = daysOfWeek[timeinfo.tm_wday];
  int dayWidth = getTextWidth(dayText, 1);
  int dayX = (52 - dayWidth) / 2;
  display.setTextSize(1);
  display.setCursor(dayX, 2);
  display.print(dayText);

  // 2. Center the Year in the left region (X: 0 to 52)
  char yearStr[8];
  sprintf(yearStr, "%d", timeinfo.tm_year + 1900);
  int yearWidth = getTextWidth(yearStr, 1);
  int yearX = (52 - yearWidth) / 2;
  display.setCursor(yearX, 13);
  display.print(yearStr);

  // 3. Maximized Right Date Frame Box (From X:54 to X:127, Height: 24)
  int boxX = 54;
  int boxY = 0;
  int boxWidth = 73; 
  int boxHeight = 24; 
  display.drawRoundRect(boxX, boxY, boxWidth, boxHeight, 2, SSD1306_WHITE);

  // 4. Grouped and centered Date text inside the frame
  char numStr[4];
  sprintf(numStr, "%02d", timeinfo.tm_mday);
  const char* monthStr = months[timeinfo.tm_mon];

  int numWidth = getTextWidth(numStr, 2) - 2;       // Size 2 text width minus trailing space
  int monthWidth = getTextWidth(monthStr, 2) - 2;   // Size 2 text width minus trailing space
  int midGap = 6;                                   // Clean fixed spacing between 02 and OCT
  
  // Total horizontal pixels occupied by the complete text group
  int totalTextWidth = numWidth + midGap + monthWidth;

  // Calculate the starting coordinate to distribute empty space evenly on left and right sides
  int startX = boxX + ((boxWidth - totalTextWidth) / 2);
  int textInsideY = boxY + 5; 

  display.setTextSize(2);
  
  // Draw the numeric digits
  display.setCursor(startX, textInsideY);
  display.print(numStr);

  // Draw the month characters right after the fixed gap spacer
  display.setCursor(startX + numWidth + midGap, textInsideY);
  display.print(monthStr);

  // ==========================================
  // BOTTOM ROW: ZERO-GAP TIME LAYOUT
  // ==========================================
  
  int hour12 = timeinfo.tm_hour % 12;
  if (hour12 == 0) hour12 = 12; 
  
  const char* ampm = (timeinfo.tm_hour >= 12) ? "PM" : "AM";

  display.setTextSize(3);
  
  // 1. Draw Hour Digits
  display.setCursor(0, 37);
  display.printf("%02d", hour12);

  // 2. Draw Colon Separator tightly tucked inward
  display.setCursor(33, 37); 
  display.print(":");

  // 3. Draw Minute Digits tightly tucked inward
  display.setCursor(47, 37);
  display.printf("%02d", timeinfo.tm_min);

  // 4. Ticking Seconds 
  display.setTextSize(2);
  display.setCursor(87, 43);
  display.printf("%02d", timeinfo.tm_sec);

  // 5. Minimized AM/PM Indicator
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
  // ==========================================
  // NEW ROTATION CONFIGURATION
  // ==========================================
  // 0: Default configuration (0 degrees rotation)
  // 1: 90 degrees clockwise
  // 2: 180 degrees flip
  // 3: 270 degrees clockwise
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
  // ==========================================================
  // CONFIGURING NEW NTP REFRESH INTERVAL
  // ==========================================================
  // 15 minutes = 15 * 60 * 1000 = 900,000 milliseconds
  // (Change 15 to 30 if you prefer a 30-minute sync window)
  sntp_set_sync_interval(15 * 60 * 1000UL); 
  // ==========================================
  // NEW BRIGHTNESS ADJUSTMENT CALL PLACE
  // ==========================================
  // 0 = dimmest setting (great for night), 255 = maximum bright glow.
  // Set it to a medium-low value like 30 to prevent burn-in over long usage.
  setDisplayBrightness(10); 
}

void loop() {
  printLocalTime();
  delay(200); 
}
