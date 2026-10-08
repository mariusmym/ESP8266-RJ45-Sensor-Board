/*
V3.3
- Implemented 6 menu Items
- New startup sound 
V3.2 
- Horizontal lines
- bigger font for menu item names
- implemeng of displayTemp() / displayError()
- !!! NTP Auto-Update feature !!!!
*/

#include <U8g2lib.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <avr/pgmspace.h>
#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFiUdp.h>
#include <Timezone.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#include "Adafruit_SHT31.h"
// include graphic.c

#define ONE_WIRE_BUS 0

Adafruit_BME280 bme;

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

Adafruit_SHT31 sht31 = Adafruit_SHT31();

const char* ssid = "YOUR_SSID";    //your SSID
const char* password = "YOUR_PASSWORD";  //your password

int status = WL_IDLE_STATUS;
WiFiClient client;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ro.pool.ntp.org", 0 /*10800 seconds for summer time, 7200 winter time*/, 60000);  //NTPClient(UDP& udp, const char* poolServerName, int timeOffset, int updateInterval);
uint32_t time1, time2;
time_t local, utc;
String h, m;

const int buttonAPin = 12;
const int buttonBPin = 14;
const int buzzerPin = 13;

const int numMenuItems = 6;
const char* menuItems[numMenuItems] = { "Display T1", "Display T2", "Display T3", "Display T4", "Display T5", "Set Alarm T." };
int currentMenuItem = 0;
bool showSelectedOption = false;

float dummyTemperature = 25.50;
float dummyTemperature2 = 30.52;

unsigned long lastButtonATime = 0;
unsigned long lastButtonBTime = 0;
const unsigned long debounceDelay = 150;

typedef void (*MenuAction)();
void performActionOption1();
void performActionOption2();
void performActionOption3();
void performActionOption4();
void performActionOption5();
void performActionOption6();

MenuAction menuActions[numMenuItems] = {
  performActionOption1,
  performActionOption2,
  performActionOption3,
  performActionOption4,
  performActionOption5,
  performActionOption6
};

void setup() {
  u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  pinMode(buttonAPin, INPUT_PULLUP);
  pinMode(buttonBPin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);

  sensors.begin();

  timeClient.begin();
  WiFi.begin(ssid, password);
  time1 = millis();

  playStartupSound();
}

void loop() {
  long rssi = WiFi.RSSI();
  u8g2.clearBuffer();
  u8g2.setCursor(1, 40);
  if (showSelectedOption) {
    // Display custom content for the current menu item
    menuActions[currentMenuItem]();
  } else {
    // Display menu item text
    u8g2.setFont(u8g2_font_10x20_me);
    u8g2.print(menuItems[currentMenuItem]);
    //Draw horizontal lines
    displayTime();
    drawHLines();
    drawSignalBars(rssi);

    // Draw triangle pointing to Button A
    u8g2.drawTriangle(0, 54, 5, 64, 10, 54);
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.setCursor(15, 64);
    u8g2.print("Scroll");
    // Draw triangle pointing to Button B
    u8g2.drawTriangle(108, 54, 113, 64, 118, 54);
    u8g2.setCursor(70, 64);
    u8g2.print("Select");
  }

  u8g2.sendBuffer();

  // Check button presses with debounce
  if (digitalRead(buttonAPin) == LOW && (millis() - lastButtonATime >= debounceDelay) && !showSelectedOption) {
    lastButtonATime = millis();
    currentMenuItem = (currentMenuItem + 1) % numMenuItems;
    beep();
    showSelectedOption = false;
  }

  if (digitalRead(buttonBPin) == LOW && (millis() - lastButtonBTime >= debounceDelay)) {
    lastButtonBTime = millis();
    if (showSelectedOption) {
      showSelectedOption = false;
      beep();
    } else {
      menuActions[currentMenuItem]();
      beep();
      showSelectedOption = true;
    }
  }
}

void performActionOption1() {
  // Add code for Option 1 action here
  u8g2.clearBuffer();
  u8g2.setCursor(0, 30);
  sensors.requestTemperatures();                    // Send the command to get temperatures
  float temperatureC = sensors.getTempCByIndex(0);  // Get the temperature in Celsius from the first sensor found
  //float t = sht31.readTemperature();
  //float temperatureBME = bme.readTemperature();

  if (temperatureC != DEVICE_DISCONNECTED_C) {  // If the temperature is valid (not disconnected)
    displayTemperature(temperatureC);
    //displayTemperature(temperatureC);
  } else {
    displayError("Sensor Error");
  }
  //u8g2.clearBuffer();
  //u8g2.setCursor(0, 30);
  //u8g2.print("Content for Option 1");
  drawBackButton();
  u8g2.sendBuffer();
}

void performActionOption2() {
  // Add code for Option 2 action here
  u8g2.clearBuffer();
  u8g2.setCursor(0, 30);
  sensors.requestTemperatures();                    // Send the command to get temperatures
  float temperatureC = sensors.getTempCByIndex(1);  // Get the temperature in Celsius from the second sensor found
  //float t = sht31.readTemperature();
  //float temperatureBME = bme.readTemperature();

  if (temperatureC != DEVICE_DISCONNECTED_C) {  // If the temperature is valid (not disconnected)
    displayTemperature(temperatureC);
    //displayTemperature(temperatureC);
  } else {
    displayError("Sensor Error");
  }
  //u8g2.clearBuffer();
  //u8g2.setCursor(0, 30);
  //u8g2.print("Content for Option 1");
  drawBackButton();
  u8g2.sendBuffer();
}

void performActionOption3() {
   // Add code for Option 3 action here
  // Add code for Option 4 action here
  u8g2.clearBuffer();
  u8g2.setCursor(0, 30);
  sensors.requestTemperatures();                    // Send the command to get temperatures
  float temperatureC = sensors.getTempCByIndex(2);  // Get the temperature in Celsius from the second sensor found
  //float t = sht31.readTemperature();
  //float temperatureBME = bme.readTemperature();

  if (temperatureC != DEVICE_DISCONNECTED_C) {  // If the temperature is valid (not disconnected)
    displayTemperature(temperatureC);
    //displayTemperature(temperatureC);
  } else {
    displayError("Sensor Error");
  }
  //u8g2.clearBuffer();
  //u8g2.setCursor(0, 30);
  //u8g2.print("Content for Option 1");
  drawBackButton();
  u8g2.sendBuffer();
}

void performActionOption4() {
    // Add code for Option 4 action here
  u8g2.clearBuffer();
  u8g2.setCursor(0, 30);
  sensors.requestTemperatures();                    // Send the command to get temperatures
  float temperatureC = sensors.getTempCByIndex(1);  // Get the temperature in Celsius from the second sensor found
  //float t = sht31.readTemperature();
  //float temperatureBME = bme.readTemperature();

  if (temperatureC != DEVICE_DISCONNECTED_C) {  // If the temperature is valid (not disconnected)
    displayTemperature(temperatureC);
    //displayTemperature(temperatureC);
  } else {
    displayError("Sensor Error");
  }
  //u8g2.clearBuffer();
  //u8g2.setCursor(0, 30);
  //u8g2.print("Content for Option 1");
  drawBackButton();
  u8g2.sendBuffer();
}

void performActionOption5() {
    // Add code for Option 5 action here
  u8g2.clearBuffer();
  u8g2.setCursor(0, 30);
  sensors.requestTemperatures();                    // Send the command to get temperatures
  float temperatureC = sensors.getTempCByIndex(1);  // Get the temperature in Celsius from the second sensor found
  //float t = sht31.readTemperature();
  //float temperatureBME = bme.readTemperature();

  if (temperatureC != DEVICE_DISCONNECTED_C) {  // If the temperature is valid (not disconnected)
    displayTemperature(temperatureC);
    //displayTemperature(temperatureC);
  } else {
    displayError("Sensor Error");
  }
  //u8g2.clearBuffer();
  //u8g2.setCursor(0, 30);
  //u8g2.print("Content for Option 1");
  drawBackButton();
  u8g2.sendBuffer();
}

void performActionOption6() {
  u8g2.clearBuffer();
  u8g2.setCursor(0, 30);

  setAlarmTemp();

  drawBackButton();
  u8g2.sendBuffer();
}

void beep() {
  tone(buzzerPin, 2300, 40);
}

void playStartupSound() {

  tone(buzzerPin, 750, 100);
  delay(200);
  tone(buzzerPin, 750, 100);
  //delay(5000);
}

void drawBackButton() {
  //Draw horizontal lines
  drawHLines();
  u8g2.setFont(u8g2_font_ncenB08_tr);  // make sure the font is the correct one
  u8g2.drawTriangle(108, 54, 113, 64, 118, 54);
  u8g2.setCursor(70, 64);
  u8g2.print("Back");
}

void drawHLines() {
  //Draw horizontal lines
  u8g2.drawHLine(0, 12, 128);
  u8g2.drawHLine(0, 52, 128);
}

void displayTemperature(float temperature) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont29_tf);  // Set a smaller font for the temperature value
  u8g2.setCursor(15, 43);
  u8g2.print(temperature, 1);  // Display the temperature with one decimal point
  u8g2.print(" C");
}

void displayError(const char* errorMessage) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont12_tf);  // Set the font for error message display
  u8g2.setCursor(10, 30);
  u8g2.print(errorMessage);
}

void displayTime() {
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  // convert received time stamp to time_t object
  utc = epochTime;

  // Then convert the UTC UNIX timestamp to local time
  TimeChangeRule EEST = { "EEST", Last, Sun, Mar, 3, 180 };  //Ora de vara - //Central European Time (Frankfurt, Paris) *Sun, 2*
  TimeChangeRule EET = { "EET", Last, Sun, Oct, 4, 120 };    //Ora de iarna -  Central European Time (Frankfurt, Paris)
  Timezone EE(EEST, EET);
  local = EE.toLocal(utc);

  h = "";
  // format the time to 12-hour format with AM/PM and no seconds
  if (hour(local) < 10)  // add a space if hour is under 10
    h += "0";
  h += hour(local);

  //t += ":"; // colon is handled in display
  m = "";
  if (minute(local) < 10)  // add a zero if minute is under 10
    m += "0";
  m += minute(local);
  u8g2.setCursor(1, 11);
  u8g2.setFont(u8g2_font_spleen6x12_mr);  // set a thiner font
  u8g2.print(h + ":" + m);
}

void drawSignalBars(long rssi) {
  // 5. High quality: 90% ~= -55db
  // 4. Good quality: 75% ~= -65db
  // 3. Medium quality: 50% ~= -75db
  // 2. Low quality: 30% ~= -85db
  // 1. Unusable quality: 8% ~= -96db
  // 0. No signal
  int bars;

  if (rssi > -55) {
    bars = 5;
  } else if (rssi< -55 & rssi > - 65) {
    bars = 4;
  } else if (rssi< -65 & rssi > - 75) {
    bars = 3;
  } else if (rssi< -75 & rssi > - 85) {
    bars = 2;
  } else if (rssi< -85 & rssi > - 96) {
    bars = 1;
  } else {
    bars = 0;
  }
  if (WiFi.status() != WL_CONNECTED) {
    u8g2.setFont(u8g2_font_profont11_tf);
    u8g2.setCursor(116, 8);
    u8g2.print("AP");
  } else {
    for (int b = 0; b <= bars; b++) {
      u8g2.drawBox(111 + (b * 3), 10 - (b * 2), 2, b * 2);
    }
    if (bars == 0) {
      u8g2.setFont(u8g2_font_spleen6x12_mu);
      u8g2.drawStr(118, 9, "X");
    }
  }
}

void setAlarmTemp(){

}
