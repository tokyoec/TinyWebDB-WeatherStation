 // Sample Arduino Json Web Client
// Downloads and parse http://jsonplaceholder.typicode.com/users/1
//
// Copyright Benoit Blanchon 2014-2017
// MIT License
//
// Arduino JSON library
// https://bblanchon.github.io/ArduinoJson/
// If you like this project, please add a star!

#include <ArduinoJson.h>
#include <Arduino.h>
#include <time.h>            
#define JST     3600*9

#include <Adafruit_BMP280.h>
#define BMP_SCK 13
#define BMP_MISO 12
#define BMP_MOSI 11 
#define BMP_CS 10

Adafruit_BMP280 bmp; // I2C
//Adafruit_BMP280 bmp(BMP_CS); // hardware SPI
//Adafruit_BMP280 bmp(BMP_CS, BMP_MOSI, BMP_MISO,  BMP_SCK);

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

#define USE_SERIAL Serial
#define ledPin D6
//#define ledPin BUILTIN_LED

WiFiClient client;

const char* resource = "http://webdb.cf/api/";           // http resource
const unsigned long BAUD_RATE = 9600;      // serial connection speed
const unsigned long HTTP_TIMEOUT = 10000;  // max respone time from server
const size_t MAX_CONTENT_SIZE = 512;       // max size of the HTTP response

#include "WiFiManager.h"          //https://github.com/tzapu/WiFiManager

void configModeCallback (WiFiManager *myWiFiManager) {
  USE_SERIAL.println("Entered config mode");
  USE_SERIAL.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  USE_SERIAL.println(myWiFiManager->getConfigPortalSSID());
}

HTTPClient http;

void setup() {
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, HIGH);

    USE_SERIAL.begin(115200);
   // USE_SERIAL.setDebugOutput(true);

    USE_SERIAL.println();
    USE_SERIAL.println();
    USE_SERIAL.println();

    USE_SERIAL.println("wifiManager autoConnect...");

    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;
    //reset settings - for testing
    //wifiManager.resetSettings();
  
    //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
    wifiManager.setAPCallback(configModeCallback);
  
    //fetches ssid and pass and tries to connect
    //if it does not connect it starts an access point with the specified name
    //here  "AutoConnectAP"
    //and goes into a blocking loop awaiting configuration
    if(!wifiManager.autoConnect()) {
      Serial.println("failed to connect and hit timeout");
      //reset and try again, or maybe put it to deep sleep
      // ESP.reset();
      ESP.deepSleep(300 * 1000 * 1000 , WAKE_RF_DEFAULT);
      delay(1000);
    } 
  
    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");
    configTime( JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
    digitalWrite(ledPin, LOW);

    if (!bmp.begin(0x76)) 
    {
      USE_SERIAL.println("Could not find BMP280");
      while (1) {}
    }

    while (0 == time(nullptr)) {
      delay(10);   // waiting time settle
    };
}


void loop() {
    char  tag[32];

    USE_SERIAL.printf("ESP8266 Chip id = %08X\n", ESP.getChipId());
    sprintf(tag, "weather-%06x", ESP.getChipId());
    digitalWrite(ledPin, HIGH);
    sensor_TinyWebDB(tag);
    digitalWrite(ledPin, LOW);

    sprintf(tag, "led-%06x", ESP.getChipId());
    delay(5000);
    digitalWrite(ledPin, HIGH);
    get_TinyWebDB(tag);
    digitalWrite(ledPin, LOW);
    delay(5000);

    //DEEP SLEEPモード突入命令
    Serial.println("DEEP SLEEP START!!");
  
    //1:μ秒での復帰までのタイマー時間設定  2:復帰するきっかけの設定（モード設定）
    ESP.deepSleep(300 * 1000 * 1000 , WAKE_RF_DEFAULT);
  
    //deepsleepモード移行までのダミー命令
    delay(1000);
  
    //実際にはこの行は実行されない
    Serial.println("DEEP SLEEPing....");

}

void sensor_TinyWebDB(const char* tag) {    
    int httpCode;
    char  value[256];
    char buff[24];

    // read values from the sensor
    float pressure = bmp.readPressure() / 100;
    float temperature = bmp.readTemperature();

    const size_t bufferSize = JSON_ARRAY_SIZE(4) + JSON_OBJECT_SIZE(5);
    DynamicJsonBuffer jsonBuffer(bufferSize);
    
    JsonObject& root = jsonBuffer.createObject();
    root["sensor"] = "bmp280";
    root["localIP"] = WiFi.localIP().toString();
    root["temperature"] = String(temperature);
    root["pressure_hpa"] = String(pressure);
    root["battery_Vcc"] = String(analogRead(A0) * (4.2 / 1023.0)); 

    time_t now = time(nullptr);
    struct tm *tm;    
    tm = localtime(&now);
    sprintf(buff, "%02d/%02d/%02d %02d:%02d:%02d",
        tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
        tm->tm_hour, tm->tm_min, tm->tm_sec);            
    root["localTime"] = String(buff);

    root.printTo(value);
    USE_SERIAL.printf("[TinyWebDB] %s\n", value);
    httpCode = TinyWebDBStoreValue(tag, value);

    // httpCode will be negative on error
    if(httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        USE_SERIAL.printf("[HTTP] POST... code: %d\n", httpCode);

        if(httpCode == HTTP_CODE_OK) {
            TinyWebDBValueStored();
        }
    } else {
        USE_SERIAL.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
        TinyWebDBWebServiceError(http.errorToString(httpCode).c_str());
    }

    http.end();

    delay(1000);
}

void get_TinyWebDB(const char* tag) {    
    int httpCode;
    char  tag2[32];
    char  value[128];

    httpCode = TinyWebDBGetValue(tag);

    // httpCode will be negative on error
    if(httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        USE_SERIAL.printf("[HTTP] GET... code: %d\n", httpCode);

        if(httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            USE_SERIAL.println(payload);
            const char * msg = payload.c_str();
            if (TinyWebDBreadReponseContent(tag2, value, msg)){
                TinyWebDBGotValue(tag2, value);
            }
        }
    } else {
        USE_SERIAL.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        TinyWebDBWebServiceError(http.errorToString(httpCode).c_str());
    }

    http.end();

    delay(1000);
}

int TinyWebDBWebServiceError(const char* message)
{
}

// ----------------------------------------------------------------------------------------
// Wp TinyWebDB API
// Action        URL                      Post Parameters  Response
// Get Value     {ServiceURL}/getvalue    tag              JSON: ["VALUE","{tag}", {value}]
// ----------------------------------------------------------------------------------------
int TinyWebDBGetValue(const char* tag)
{
    char url[64];

    sprintf(url, "%s%s?tag=%s", resource, "getvalue/", tag);

    USE_SERIAL.printf("[HTTP] %s\n", url);
    // configure targed server and url
    http.begin(url);
    
    USE_SERIAL.print("[HTTP] GET...\n");
    // start connection and send HTTP header
    int httpCode = http.GET();

    return httpCode;
}

int TinyWebDBGotValue(const char* tag, const char* value)
{
    USE_SERIAL.printf("[TinyWebDB] %s\n", tag);
    USE_SERIAL.printf("[TinyWebDB] %s\n", value);

    String s = value;
    if(s.compareTo("on")) digitalWrite(ledPin, HIGH);
    else  digitalWrite(ledPin, LOW);
    USE_SERIAL.printf("GET %s:%s\n", tag, value);
    
    return 0;   
}

// ----------------------------------------------------------------------------------------
// Wp TinyWebDB API
// Action        URL                      Post Parameters  Response
// Store A Value {ServiceURL}/storeavalue tag,value        JSON: ["STORED", "{tag}", {value}]
// ----------------------------------------------------------------------------------------
int TinyWebDBStoreValue(const char* tag, const char* value)
{
    char url[64];
  
    sprintf(url, "%s%s", resource, "storeavalue");
    USE_SERIAL.printf("[HTTP] %s\n", url);

    // POST パラメータ作る
    char params[256];
    sprintf(params, "tag=%s&value=%s", tag, value);
    USE_SERIAL.printf("[HTTP] POST %s\n", params);

    // configure targed server and url
    http.begin(url);

    // start connection and send HTTP header
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpCode = http.POST(params);
    String payload = http.getString();                  //Get the response payload
    Serial.println(payload);    //Print request response payload

    http.end();
    return httpCode;
}

int TinyWebDBValueStored()
{
  
    return 0;   
}


// Parse the JSON from the input string and extract the interesting values
// Here is the JSON we need to parse
// [
//   "VALUE",
//   "LED1",
//   "on",
// ]
bool TinyWebDBreadReponseContent(char* tag, char* value, const char* payload) {
  // Compute optimal size of the JSON buffer according to what we need to parse.
  // See https://bblanchon.github.io/ArduinoJson/assistant/
  const size_t BUFFER_SIZE =
      JSON_OBJECT_SIZE(3)    // the root object has 3 elements
      + MAX_CONTENT_SIZE;    // additional space for strings

  // Allocate a temporary memory pool
  DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);

  // JsonObject& root = jsonBuffer.parseObject(payload);
  JsonArray& root = jsonBuffer.parseArray(payload);
  JsonArray& root_ = root;

  if (!root.success()) {
    Serial.println("JSON parsing failed!");
    return false;
  }

  // Here were copy the strings we're interested in
  strcpy(tag, root_[1]);   // "led1"
  strcpy(value, root_[2]); // "on"

  return true;
}


// Pause for a 1 minute
void wait() {
  Serial.println("Wait 60 seconds");
  delay(60000);
}
