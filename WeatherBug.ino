/**The MIT License (MIT)

Copyright (c) 2015 by Daniel Eichhorn

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See more at http://blog.squix.ch
*/

#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <JsonListener.h>
#include "SSD1306.h"
#include "SSD1306Ui.h"
#include "Wire.h"
#include "WundergroundClient.h"
#include "WeatherStationFonts.h";
#include "WeatherStationImages.h";
#include "NTPClient.h"
#include "DHT.h"
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <PubSubClient.h>
// #include "AdyFonts.h"

// Function prototypes
bool drawFrame1(SSD1306 *, SSD1306UiState*, int, int); 
bool drawFrame2(SSD1306 *, SSD1306UiState*, int, int); 
bool drawFrame3(SSD1306 *, SSD1306UiState*, int, int); 
bool drawFrame4(SSD1306 *, SSD1306UiState*, int, int); 
bool drawFrame5(SSD1306 *, SSD1306UiState*, int, int); 
bool msOverlay(SSD1306 *, SSD1306UiState*);

/***************************
 * Begin Settings
 **************************/
// WIFI
const char* WIFI_SSID = "...."; 
const char* WIFI_PWD = "....";

// Setup
const int UPDATE_INTERVAL_SECS = 10 * 60; // Update every 10 minutes

// Display Settings
const int I2C_DISPLAY_ADDRESS = 0x3c;
// const int SDA_PIN = D3; // NodeMCU
// const int SDC_PIN = D4; // NodeMCU
const int SDA_PIN = D2; // Wemos D1 Mini
const int SDC_PIN = D3; // Wemos D1 Mini
SSD1306   display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);
SSD1306Ui ui     ( &display );

// DHT Settings
// #define DHTPIN D2 // NodeMCU
#define DHTPIN D4 // Wemos D1 Mini
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
char FormattedTemperature[10];
char FormattedHumidity[10];


// Wunderground Settings
const boolean IS_METRIC = true;
const String WUNDERGRROUND_API_KEY = "....";
const String WUNDERGROUND_COUNTRY = "CA";
const String WUNDERGROUND_CITY = "Santa_Clarita";

// NTP settings
#define UTC_OFFSET -28800 // UTC offset for Los Angeles is -8h

// IFTTT
String IFTTT_HotRequest,IFTTT_ColdRequest; 
String IFTTT_API_KEY = "bkKjyq8X5wVzCVvUgfoeM8";
String IFTTT_URL = "http://maker.ifttt.com/trigger/";
String IFTTT_HOT_EVENT = "Nursery_Temperature_High";
String IFTTT_COLD_EVENT = "Nursery_Temperature_Low";
int DidISendHotNotification = 0;
int DidISendColdNotification = 0;
char NurseryTemperature[10];
char NurseryHumidity[10];
// Set these two parameters as per your needs
#define MAX_TEMPERATURE 22.2 
#define MIN_TEMPERATURE 20
#define TEMPERATURE_HYSTERESIS 0.5


//MQTT / HomeKit
// Update these with values suitable for your network.
IPAddress MQTTserver(192, 168, 1, 155);
WiFiClient wclient;
PubSubClient client(wclient, MQTTserver);

/***************************
 * End Settings
 **************************/

NTPClient timeClient(UTC_OFFSET); 

// Set to false, if you prefere imperial/inches, Fahrenheit
WundergroundClient wunderground(IS_METRIC);

// Initialize the temperature/ humidity sensor
DHT dht(DHTPIN, DHTTYPE);
float humidity = 0.;
float temperature = 0.;

// this array keeps function pointers to all frames
// frames are the single views that slide from right to left
bool (*frames[])(SSD1306 *display, SSD1306UiState* state, int x, int y) = { drawFrame1, drawFrame2, drawFrame3, drawFrame4, drawFrame4};
int numberOfFrames = 5;

bool (*overlays[])(SSD1306 *display, SSD1306UiState* state)             = { msOverlay };
int overlaysCount = 1;

// flag changed in the ticker function every 10 minutes
bool readyForWeatherUpdate = false;

String lastUpdate = "--";

Ticker ticker;

// Date, time, temperature and humidity
bool msOverlay(SSD1306 *display, SSD1306UiState* state) {
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->setFont(ArialMT_Plain_10);
  
  // day and date
  String date = wunderground.getDate();
  int textWidth = display->getStringWidth(date);
  display->drawString(0, 0, date);
  // time
  display->setFont(ArialMT_Plain_16);
  String time = timeClient.getFormattedTime();
  display->drawString(0, 10, time);
  
  // temperature
  display->setFont(ArialMT_Plain_24);
  dtostrf(temperature,4, 1, FormattedTemperature);
  dtostrf(humidity,4, 1, FormattedHumidity);
  display->drawString(0, 24, String(FormattedTemperature) + "°C");
  
  // humidity
  display->setFont(ArialMT_Plain_16);
  display->drawString(0, 48, String(FormattedHumidity) + "%");
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  return true;
}

// One day's forecast from Weather Underground
void drawForecast(SSD1306 *display, int x, int y, int dayIndex) {
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  String day = wunderground.getForecastTitle(dayIndex).substring(0, 3);
  day.toUpperCase();
  display->drawString(x + 30, y, day);
  
  display->setFont(Meteocons_0_21);
  display->drawString(x + 30, y + 20, wunderground.getForecastIcon(dayIndex));

  display->setFont(ArialMT_Plain_16);
  display->drawString(x + 30, y + 44, wunderground.getForecastLowTemp(dayIndex) + "-" + wunderground.getForecastHighTemp(dayIndex));
  //display.drawString(x + 20, y + 51, );
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

// Outside temperature from Weather Underground
bool drawFrame1(SSD1306 *display, SSD1306UiState* state, int x, int y) {
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(110 + x, y, "Outside");
  // display->drawStringMaxWidth(90 + x, y+12, 24, wunderground.getWeatherText());

  display->setFont(Meteocons_0_21);
  String weatherIcon = wunderground.getTodayIcon();
  // int weatherIconWidth = display->getStringWidth(weatherIcon);
  display->drawString(110 + x, 20 + y, weatherIcon);

  display->setFont(ArialMT_Plain_16);
  String temp = wunderground.getCurrentTemp() + "°";
  display->drawString(106 + x, 44 + y,temp);
  int tempWidth = display->getStringWidth(temp);
}

// Outside Humidity, PRessure and Rain from Weather Underground
bool drawFrame2(SSD1306 *display, SSD1306UiState* state, int x, int y) {
  // display->setTextAlignment(TEXT_ALIGN_LEFT);
  // display->setFont(ArialMT_Plain_10);
  // display->drawString(90 + x, 0 + y, "Humid.");
  // display->drawString(90 + x, 20 + y, "Press.");
  // display->drawString(90 + x, 40 + y, "Rain");

  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(110 + x, 0 + y, wunderground.getHumidity());
  display->setFont(ArialMT_Plain_10);
  display->drawString(110 + x, 22 + y, wunderground.getPressure());
    display->setFont(ArialMT_Plain_16);
  display->drawString(110 + x, 44 + y, wunderground.getPrecipitationToday());
}

// Today's forecast
bool drawFrame3(SSD1306 *display, SSD1306UiState* state, int x, int y) {
  drawForecast(display, x + 80, y, 0);
}

// Tomorrow's forecast
bool drawFrame4(SSD1306 *display, SSD1306UiState* state, int x, int y) {
   drawForecast(display, x + 80, y, 2);
}

// Day after's forecast
bool drawFrame5(SSD1306 *display, SSD1306UiState* state, int x, int y) {
   drawForecast(display, x + 80, y, 6);
}

// Progress bar when updating
void drawProgress(SSD1306 *display, int percentage, String label) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawStringMaxWidth(64, 10, 128,label);
  // display->drawRect(10, 48, 108, 4);
  display->fillRect(12, 50, 104 * percentage / 100 , 2);
  display->display();
}

// Called every ten minutes
void updateData(SSD1306 *display) {
  display->flipScreenVertically();
  drawProgress(display, 10, "Updating time...");
  timeClient.begin();
  drawProgress(display, 20, "Updating time...");
  drawProgress(display, 30, "Updating conditions...");
  wunderground.updateConditions(WUNDERGRROUND_API_KEY, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  drawProgress(display, 40, "Updating conditions...");
  drawProgress(display, 50, "Updating forecasts...");
  wunderground.updateForecast(WUNDERGRROUND_API_KEY, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  drawProgress(display, 60, "Updating forecasts...");
  drawProgress(display, 70, "Updating local temperature and humidity");
  humidity = dht.readHumidity();
  // delay(200);
  drawProgress(display, 80, "Updating local temperature and humidity");
  temperature = dht.readTemperature(!IS_METRIC);
  // delay(200);
  drawProgress(display, 90, "Updating local temperature and humidity");
  lastUpdate = timeClient.getFormattedTime();
  Serial.println(lastUpdate);
  drawProgress(display, 100, "Done...");
  readyForWeatherUpdate = false;
  delay(500);
}

void FirmwareUpdate(SSD1306 *display,unsigned int progress, unsigned int total ) {
  drawProgress(display, (progress / (total / 100)), "OTA Firmware update");
}

void FirmwareUpdateDone(SSD1306 *display) {
  drawProgress(display, 100, "Done. Rebooting...");
}

void setReadyForWeatherUpdate() {
  Serial.println("Setting readyForUpdate to true");
  readyForWeatherUpdate = true;
}

void IFTTT(){
   // wait for WiFi connection
    if((WiFi.status() == WL_CONNECTED)) {
      HTTPClient http;
  
      // Serial.print("[HTTP] begin...\n");

      // Convert temperature & humidity  to a string
      dtostrf(temperature,4, 1, NurseryTemperature);
      dtostrf(humidity,4, 1, NurseryHumidity);
       
      // Generate HTTP request string for IFTTT
      IFTTT_HotRequest = IFTTT_URL + IFTTT_HOT_EVENT + "/with/key/" + IFTTT_API_KEY + "?value1=" + String(NurseryTemperature) + "&value2=" + String(NurseryHumidity);
      IFTTT_ColdRequest = IFTTT_URL + IFTTT_COLD_EVENT + "/with/key/" + IFTTT_API_KEY + "?value1=" + String(NurseryTemperature) + "&value2=" + String(NurseryHumidity);
  
      // Reset notification flags when temperature returns to normal
      if(temperature < (MAX_TEMPERATURE - TEMPERATURE_HYSTERESIS) && temperature > (MIN_TEMPERATURE + TEMPERATURE_HYSTERESIS)){
        DidISendHotNotification = 0;
        DidISendColdNotification = 0;
        Serial.println("Temperature is normal");
      }
  
      // Check if temperature is outside normal
      if(temperature >= MAX_TEMPERATURE && DidISendHotNotification == 0){
        http.begin(IFTTT_HotRequest);
        DidISendHotNotification = 1;
      }
      
      if(temperature <= MIN_TEMPERATURE && DidISendColdNotification == 0){
        http.begin(IFTTT_ColdRequest);
        DidISendColdNotification = 1;
      }
  
      // Check if Notification was successfull
      if(DidISendHotNotification || DidISendColdNotification){
        // Serial.print("[HTTP] GET...\n");
        // start connection and send HTTP header
        int httpCode = http.GET();
  
        // httpCode will be negative on error
        if(httpCode) {
          // HTTP header has been send and Server response header has been handled
          //Serial.printf("[HTTP] GET... code: %d\n", httpCode);
  
          // file found at server
          if(httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.println(payload);
          }
        } else {
          Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
          // retry if it fails for some reason
          DidISendHotNotification = 0;
          DidISendColdNotification = 0;
        }
  
        http.end();
      }
    }
} 

void HomeKit(){
    if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      if (client.connect("ESPNurseryTemperatureSensor")) {
        client.publish("HomeKit","Nursery Temperature Sensor Online!");
      }
    }

    if (client.connected()){
      Serial.println("publishing");
        client.publish("NurseryTemperature",String(NurseryTemperature));   
        client.loop();
    }
      
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  // initialize display
  display.init();
  display.clear();
  display.display();

  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(255);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PWD);

  // while (WiFi.waitForConnectResult() != WL_CONNECTED) {
  //   Serial.println("Connection Failed! Rebooting...");
  //   delay(5000);
  //   ESP.restart();
  // }

  int counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.clear();

    display.drawXbm(5, 5, WiFi_Logo_width, WiFi_Logo_height, WiFi_Logo_bits);
    display.drawString(95, 10, "WeatherBug");
    display.drawString(95, 30, "by @Ady");
    display.drawXbm(46, 50, 8, 8, counter % 3 == 0 ? activeSymbole : inactiveSymbole);
    display.drawXbm(60, 50, 8, 8, counter % 3 == 1 ? activeSymbole : inactiveSymbole);
    display.drawXbm(74, 50, 8, 8, counter % 3 == 2 ? activeSymbole : inactiveSymbole);
    display.display();
    
    counter++;
  }
  
  ui.setTargetFPS(30);

  ui.setActiveSymbole(activeSymbole);
  ui.setInactiveSymbole(inactiveSymbole);

  // You can change this to
  // TOP, LEFT, BOTTOM, RIGHT
  ui.setIndicatorPosition(RIGHT);

  // Defines where the first frame is located in the bar.
  ui.setIndicatorDirection(LEFT_RIGHT);

  // You can change the transition that is used
  // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN
  ui.setFrameAnimation(SLIDE_UP);

  // Add frames
  ui.setFrames(frames, numberOfFrames);

  // Add overlays
  ui.setOverlays(overlays, overlaysCount);

  // Inital UI takes care of initalising the display too.
  ui.init();

  Serial.println("");

  updateData(&display);

  ticker.attach(UPDATE_INTERVAL_SECS, setReadyForWeatherUpdate);

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);
  
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("WeatherBug");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    display.clear();
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    FirmwareUpdateDone(&display);
    Serial.println("\nEnd");
    // delay(500);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    FirmwareUpdate(&display, progress, total);   
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
     
}

void loop() {

  if (readyForWeatherUpdate && ui.getUiState().frameState == FIXED) {
    updateData(&display);
  
  // IFTTT
  IFTTT();

  // MQTT
  HomeKit();

  }

  int remainingTimeBudget = ui.update();

  if (remainingTimeBudget > 0) {
    // You can do some work here
    // Don't do stuff if you are below your
    // time budget.

    delay(remainingTimeBudget);
  }

  ArduinoOTA.handle();

}

