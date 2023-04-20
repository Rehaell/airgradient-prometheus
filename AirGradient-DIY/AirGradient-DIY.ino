/**
 * This sketch connects an AirGradient DIY sensor to a WiFi network, and runs a
 * tiny HTTP server to serve air quality metrics to Prometheus.
 */

#include <AirGradient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>

#include <Wire.h>
#include "SSD1306Wire.h"

#include <NTPClient.h>

#include <ArduinoJson.h>

#include "network_info.h"

AirGradient ag = AirGradient();

// Config ----------------------------------------------------------------------

// set to 'F' to switch display from Celcius to Fahrenheit
char temp_display = 'C';

// Hardware options for AirGradient DIY sensor.
const bool hasPM = true;
const bool hasCO2 = true;
const bool hasSHT = true;

// The frequency of measurement updates in milliseconds.
const int updateFrequency = 5000; //60000 = one minute
//updates the weather every 30 minutes
const int updateWeatherFrequency = 1800000 

// For housekeeping.
long lastUpdate;
long lastWeatherUpdate;
int counter = 0;

// To store the weather information
String weatherInfo = "";

// Config End ------------------------------------------------------------------


SSD1306Wire display(0x3c, SDA, SCL);


ESP8266WebServer server(port);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer); 

void setup() {
  Serial.begin(115200);

  // Init Display.
  display.init();
  display.flipScreenVertically();
  showTextRectangle("Init", String(ESP.getChipId(),HEX),true);

  // Enable enabled sensors.
  if (hasPM) ag.PMS_Init();
  if (hasCO2) ag.CO2_Init();
  if (hasSHT) ag.TMP_RH_Init(0x44);

  // Set static IP address if configured.
  #ifdef staticip
  WiFi.config(static_ip,gateway,subnet);
  #endif

  // Set WiFi mode to client (without this it may try to act as an AP).
  WiFi.mode(WIFI_STA);
  
  // Configure Hostname
  if ((deviceId != NULL) && (deviceId[0] == '\0')) {
    Serial.printf("No Device ID is Defined, Defaulting to board defaults");
  }
  else {
    wifi_station_set_hostname(deviceId);
    WiFi.setHostname(deviceId);
  }
  
  // Setup and wait for WiFi.
  WiFi.begin(ssid, password);
  Serial.println("");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    showTextRectangle("Trying to", "connect...", true);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Hostname: ");
  Serial.println(WiFi.hostname());
  server.on("/", HandleRoot);
  server.on("/metrics", HandleRoot);
  server.onNotFound(HandleNotFound);

  server.begin();
  Serial.println("HTTP server started at ip " + WiFi.localIP().toString() + ":" + String(port));
  showTextRectangle("Listening To", WiFi.localIP().toString() + ":" + String(port),true);
  
  timeClient.begin();
}

void loop() {
  long t = millis();

  server.handleClient();

  timeClient.update();

  updateWeather(t);

  updateScreen(t);
}

String GenerateMetrics() {
  String message = "";
  String idString = "{id=\"" + String(deviceId) + "\",mac=\"" + WiFi.macAddress().c_str() + "\"}";

  if (hasPM) {
    int stat = ag.getPM2_Raw();

    message += "# HELP pm02 Particulate Matter PM2.5 value\n";
    message += "# TYPE pm02 gauge\n";
    message += "pm02";
    message += idString;
    message += String(stat);
    message += "\n";
  }

  if (hasCO2) {
    int stat = ag.getCO2_Raw();

    message += "# HELP rco2 CO2 value, in ppm\n";
    message += "# TYPE rco2 gauge\n";
    message += "rco2";
    message += idString;
    message += String(stat);
    message += "\n";
  }

  if (hasSHT) {
    TMP_RH stat = ag.periodicFetchData();

    message += "# HELP atmp Temperature, in degrees Celsius\n";
    message += "# TYPE atmp gauge\n";
    message += "atmp";
    message += idString;
    message += String(stat.t);
    message += "\n";

    message += "# HELP rhum Relative humidity, in percent\n";
    message += "# TYPE rhum gauge\n";
    message += "rhum";
    message += idString;
    message += String(stat.rh);
    message += "\n";
  }

  return message;
}

void HandleRoot() {
  server.send(200, "text/plain", GenerateMetrics() );
}

void HandleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/html", message);
}


void updateWeather(long now) {

  if ((now - lastWeatherUpdate) > updateWeatherFrequency) { 
    //updates the weather at a fix interval
    if (client.connect("api.openweathermap.org", 80)) {
      Serial.println("Connecting to OpenWeatherMap server...");
      // send the HTTP PUT request:
      client.println("GET /data/2.5/weather?q=" + NameOfCity + "&units=metric&APPID=" + APIKEY + "HTTP/1.1");
      client.println("Host: api.openweathermap.org");
      client.println("Connection: close");
      client.println();
      // Check HTTP status
      char status[32] = {0};
      client.readBytesUntil('\r', status, sizeof(status));
      // It should be "HTTP/1.0 200 OK" or "HTTP/1.1 200 OK"
      if (strcmp(status + 9, "200 OK") != 0) {
        Serial.print(F("Unexpected response: "));
        Serial.println(status);
        weatherInfo = "No connection!";
      }
      // Skip HTTP headers
      char endOfHeaders[] = "\r\n\r\n";
      if (!client.find(endOfHeaders)) {
        Serial.println(F("Invalid response"));
        weatherInfo = "No connection!";
      }
      // Allocate the JSON document
      // Use arduinojson.org/v6/assistant to compute the capacity.
      const size_t capacity = JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(1) + 2*JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(4) + 2*JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(13) + 270;
      DynamicJsonDocument doc(capacity);
      
      // Parse JSON object
      DeserializationError error = deserializeJson(doc, client);
      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.c_str());
        weatherInfo = "Deserialization failed!";
      }
          
      int weatherId = doc["weather"][0]["id"].as<int>();
      float weatherTemperature = doc["main"]["temp"].as<float>();
      int weatherHumidity = doc["main"]["humidity"].as<int>();
      Serial.println(F("Response:"));
      Serial.print("Weather: ");
      Serial.println(weatherId);
      Serial.print("Temperature: ");
      Serial.println(weatherTemperature);
      Serial.print("Humidity: ");
      Serial.println(weatherHumidity);
      Serial.println();

      weatherInfo = String(weatherTemperature) + "C, " + String(weatherHumidity) + "%";
      lastWeatherUpdate = millis();
    } 
    else {
      // if you couldn't make a connection:
      Serial.println("connection failed");
      weatherInfo = "No data!";
    }
  else 
    Serial.println("Weather update skipped");
}

// DISPLAY
void showTextRectangle(String ln1, String ln2, boolean small) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  if (small) {
    display.setFont(ArialMT_Plain_16);
  } else {
    display.setFont(ArialMT_Plain_24);
  }
  display.drawString(32, 16, ln1);
  display.drawString(32, 36, ln2);
  display.display();
}

void updateScreen(long now) {
  if ((now - lastUpdate) > updateFrequency) {
    // Take a measurement at a fixed interval.
    switch (counter) {
      case 0:
        if (hasPM) {
          int stat = ag.getPM2_Raw();
          showTextRectangle("PM2",String(stat),true);
          Serial.println("PM2 " + String(stat));
        }
        break;
      case 1:
        if (hasCO2) {
          int stat = ag.getCO2_Raw();
          showTextRectangle("CO2", String(stat), true);
          Serial.println("CO2 " + String(stat));
        }
        break;
      case 2:
        if (hasSHT) {
          TMP_RH stat = ag.periodicFetchData();
          if (temp_display == 'F' || temp_display == 'f') {
            showTextRectangle("TMP", String((stat.t * 9 / 5) + 32, 1) + "F", true);
            Serial.println("TMP " + String((stat.t * 9 / 5) + 32, 1) + "F");

          } else {
            showTextRectangle("TMP", String(stat.t, 1) + "C", true);
            Serial.println("TMP " + String(stat.t, 1) + "C");
          }
        }
        break;
      case 3:
        if (hasSHT) {
          TMP_RH stat = ag.periodicFetchData();
          showTextRectangle("HUM", String(stat.rh) + "%", true);
          Serial.println("HUM " + String(stat.rh) + "%");
        }
        break;
      case 4:
        showTextRectangle("Outside", weatherInfo, true);
        break;
      case 5:
        //display clock
        showTextRectangle("CLOCK", String(timeClient.getFormattedTime()), true);
        Serial.println("CLOCK " + String(timeClient.getFormattedTime()));
        delay(2000); //waits 2 seconds before going back to displaying the measurements
        break;
    }
    counter++;
    if (counter > 5) counter = 0;
    lastUpdate = millis();
  }

  //if time between 19:00 and 9:00 turn off display backlight
  if (timeClient.getHours() >= 19 || timeClient.getHours() <= 9) {
    display.setContrast(10, 5, 0);
    Serial.println("Turning the lights off");
  } else { //turns it back on
    display.setContrast(100, 241, 64);
    Serial.println("Turning the lights on");
  }
}
