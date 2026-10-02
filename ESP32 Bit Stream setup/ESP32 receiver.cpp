#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Network Credentials
const char* ssid     = "HarshaBlaze_2.4G";
const char* password = "helloworld!";

// Your PC's Local IP Address running the Python script
const char* serverUrl = "http://192.168.0.219:8080/stream.bin";
// const char* serverUrl = "http://192.168.0.219"; // <-- CHANGE TO YOUR PC'S IP

// 128x64 pixels divided by 8 bits per byte = 1024 bytes per frame
const int frameBufferSize = 1024; 
uint8_t frameBuffer[frameBufferSize];
void setDisplayBrightness(uint8_t brightness) {
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(brightness);
}
void fetchAndDisplayImage() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(serverUrl);
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    WiFiClient* stream = http.getStreamPtr();
    
    // Read the streaming binary data straight into our buffer
    int bytesRead = stream->readBytes(frameBuffer, frameBufferSize);
    
    if (bytesRead == frameBufferSize) {
      display.clearDisplay();
      // Draw the raw bitmap buffer straight to the screen coordinate 0,0
      display.drawBitmap(0, 0, frameBuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
      display.display();
    }
  } else {
    Serial.printf("HTTP Request failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
}

void setup() {
  Serial.begin(115200);
  
  Wire.begin();
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    return;
  }
  
  display.setRotation(2); // Matches your existing physical orientation
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 25);
  display.println("Connecting to WiFi...");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  display.clearDisplay();
  display.println("Connected! Streaming...");
  display.display();
  setDisplayBrightness(10); 
}

void loop() {
  fetchAndDisplayImage();
  delay(50); // Refreshes and checks for a new image every 3 seconds
}
