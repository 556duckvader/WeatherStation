#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_SI1145.h>
#include <Adafruit_ADS1015.h>
#include <EEPROM.h>
#include <math.h>

extern "C" {
#include "user_interface.h"

}

//--------------------Wifi Variables-------------------//
const char* ssid     = "ASUS";
const char* password = "5Im0n2804!";

//--------------------WU PWS ID Info-------------------//
const char* WUID    = "KKYMOUNT30";
const char* WUPASS   = "yjil5ue7";
const char* host = "weatherstation.wunderground.com";

#define SEALEVELPRESSURE_HPA (1013.25)

//--------------------NTP Variables-------------------//
unsigned int localPort = 2390;
IPAddress timeServerIP;
const char* ntpServerName = "time.nist.gov";
const int NTP_PACKET_SIZE = 48;
byte packetBuffer[ NTP_PACKET_SIZE];
WiFiUDP udp;
unsigned long epoch;

//--------------------Weather Variables-------------------//
int temp_offset = 0;         // Temp. Offset
int humi_offset = 0;          // Humidity Offset
const int vaneOffset = 45;    // True North Vane Offset
int vaneDirection;   // translated 0 - 360 direction
int calDirection;    // converted value with offset applied

//Raw Weather Floats
float windspeed_raw;
float windgust_raw;
float winddir_raw;
float tempout_raw;
float dewpout_raw;
float humidity_raw;
float baro_raw;
float uvIndex_raw;
float rain1h_raw;
float rain24h_raw;

//Weather US Strings
String windspeed;
String windgust;
String winddir;
String tempout;
String dewpout;
String humidity;
String baro;
String uvIndex;
String rain1h;
String rain24h;

float tempwindgust;
float raincount = 0.00;
float raincount1h = 0.00;
const float pi = 3.14159265;  // pi number
float calcgustspeed;
float calcwindspeed;
float sensor_count = 0.00f;
float winddir_sum = 0.00f;
float tempout_sum = 0.00f;
float humidity_sum = 0.00f;
float baro_sum = 0.00f;
float uvIndex_sum = 0.00f;
unsigned long rain_last = 0;
unsigned long wind_last = 0;
int addr = 0;
float pressure;
String eepromstring = "0.00";

//--------------------GPIO Pin Map-------------------//
//int rainils = 10;              //Rain REED-ILS sensor
int windils = 14;              //WIND REED-ILS sensor

//--------------------Program Variables-------------------//
unsigned long count5sec;
unsigned long count60sec;
unsigned long count1h;
unsigned int pulseswind;
unsigned int pulsesgust;
bool debug = 0;               //debug = 1 -> enable debug

//--------------------ADS1115 Setup-------------------//
Adafruit_ADS1115 ads;

//--------------------BME280 Setup-------------------//
Adafruit_BME280 bme; // I2C

//--------------------SI11145 Setup-------------------//
Adafruit_SI1145 uv = Adafruit_SI1145();

//--------------------Start of Setup-------------------//
void setup() {
  //pinMode(rainils, INPUT);
  pinMode(windils, INPUT);
  //wifi_set_sleep_type(NONE_SLEEP_T);
  //wifi_set_sleep_type(MODEM_SLEEP_T);
  wifi_set_sleep_type(LIGHT_SLEEP_T);
  Serial.begin(115200);
  Serial.println("");
  Serial.print("Start Weather Station ");
  Serial.println(WUID);

  attachInterrupt(digitalPinToInterrupt (windils), rpm, FALLING);
  //attachInterrupt(rainils, rain, FALLING);
  startwifi();
  Wire.begin();
  //--------------------Check If BME280 Is Connected-------------------//
  if (!bme.begin()) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    ESP.restart();
  }
  //--------------------Check If UV Is Connected-------------------//
  if (!uv.begin()) {
    Serial.println("Could not find a valid Si1145 sensor, check wiring!");
    ESP.restart();
  }
  //--------------------Start ADC-------------------//
  ads.begin();

  //--------------------Start EEPROM-------------------//
  EEPROM.begin(512);

  //RESET EEPROM CONTENT - ONLY EXECUTE ONE TIME - AFTER COMMENT
  /*Serial.println("CLEAR ");
    eepromClear();
    Serial.println("SET ");
    eepromSet("raincount", "9.00");
    eepromSet("raincount1h", "0.00");
    Serial.println("LIST ");
    Serial.println(eepromList());*/
  //END - RESET EEPROM CONTENT - ONLY EXECUTE ONE TIME - AFTER COMMENT

  //GET STORED RAINCOUNT IN EEPROM
  Serial.println("GET EEPROM");
  eepromstring = eepromGet("raincount");
  raincount = eepromstring.toFloat();
  Serial.print("RAINCOUNT VALUE FROM EEPROM: ");
  Serial.println(raincount);
  eepromstring = eepromGet("raincount1h");
  raincount1h = eepromstring.toFloat();
  Serial.print("RAINCOUNT1H VALUE FROM EEPROM: ");
  Serial.println(raincount1h);
  //END - GET STORED RAINCOUNT IN EEPROM

  if (raincount1h == 0)
  {
    count1h = millis();
  }

  count5sec = millis();
  count60sec = millis();

  //start interupt
  sei();
  pulseswind = 0;
  pulsesgust = 0;

}
//--------------------End of Setup-------------------//


//--------------------Start of Loop-------------------//
void loop() {

//  //--------------------Start Get Battery Level-------------------//
//  // read the battery level from the ESP8266 analog in pin.
//  // analog read level is 16 bit 0-16384 (0V-1V).
//  // our 1M & 220K voltage divider takes the max
//  // lipo value of 4.2V and drops it to 0.758V max.
//  // this means our min analog read value should be 580 (3.14V)
//  // and the max analog read value should be 774 (4.2V).
//
//  int battLevel = analogRead(A0);
//
//  // convert battery level to percent
//  battLevel = map(battLevel, 580, 774, 0, 100);
//  //--------------------End Get Battery Level-------------------//

  if ( (millis() - count5sec) >= 5000)
  {
    //Call speedgust() to store actual wind gust
    Serial.print("Take gust values each 5sec: ");
    Serial.println(speedgust());
    //Add actual wind direction to average after 60sec
    winddir_sum = winddir_sum + DirWind();
    tempout_sum = tempout_sum +  ( bme.readTemperature() + temp_offset );
    humidity_sum = humidity_sum + ( bme.readHumidity() + humi_offset );
    baro_sum = baro_sum + ( bme.readPressure() / 100.00f );
    uvIndex_sum = uvIndex_sum + uv.readUV();
    sensor_count = sensor_count + 1.00f;
    count5sec = millis();
    if (debug) {
      Serial.print("Other Sensor each 5sec: ");
      tempout_raw = tempout_sum / sensor_count;
      humidity_raw = humidity_sum / sensor_count;
      dewpout_raw = ( tempout_raw - ((100.00f - humidity_raw) / 5.00f) );
      baro_raw = baro_sum / sensor_count;
      uvIndex_raw = uvIndex_sum / sensor_count;
      winddir_raw = winddir_sum / sensor_count;
      Serial.print("Temp: ");
      Serial.print(tempout_raw);
      Serial.print(" - Dew Point: ");
      Serial.print(dewpout_raw);
      Serial.print(" - Humidity: ");
      Serial.print(humidity_raw);
      Serial.print(" - Pressure: ");
      Serial.print(baro_raw);
      Serial.print(" - UV Index: ");
      Serial.print(uvIndex_raw);
      Serial.print(" - Wind Dir: ");
      Serial.println(winddir_raw);
    }
  }

  if ( (millis() - count60sec) >= 60000)
  {
    ntptime();
    Serial.println("");
    Serial.println("Actual Local Time:");
    Serial.print("Hour: ");
    Serial.println(localhour());
    Serial.print("Min: ");
    Serial.println(localmin());
    Serial.print("Sec: ");
    Serial.println(localsec());
    Serial.println("");
    Serial.println("Store and convert all sensor value fo WU each 60sec");

    //Reset Daily Rain each 24h
    if ((localhour() >= 23) && (localmin() >= 55))
    {
      Serial.println("Reset Daily Rain");
      raincount = 0;
      rain24h_raw = 0.00;
    }

    //Get All Values of Sensor
    winddir_raw = winddir_sum / sensor_count;
    windspeed_raw = speedwind();
    //wind gust for 60sec
    windgust_raw = tempwindgust;
    tempwindgust = 0;
    tempout_raw = tempout_sum / sensor_count;
    humidity_raw = humidity_sum / sensor_count;
    dewpout_raw = ( tempout_raw - ((100.00f - humidity_raw) / 5.00f) );
    baro_raw = baro_sum / sensor_count;
    uvIndex_raw = uvIndex_sum / sensor_count;
    rain1h_raw = 0.80f * raincount1h;
    rain24h_raw = 0.800f * raincount;
    winddir_sum = 0.00f;
    tempout_sum = 0.00f;
    humidity_sum = 0.00f;
    baro_sum = 0.00f;
    sensor_count = 0.00f;

    Serial.println(" ");
    Serial.println("Raw to US conversion for WU  ");
    Serial.println(" ");
    Serial.println("Raw:  ");
    Serial.print("Temp: ");
    Serial.println(tempout_raw);
    Serial.print("Dew Point: ");
    Serial.println(dewpout_raw);
    Serial.print("Humidity: ");
    Serial.println(humidity_raw);
    Serial.print("Pressure: ");
    Serial.println(baro_raw);
    Serial.print("UV Index: ");
    Serial.println(uvIndex_raw);
    Serial.print("Wind Speed: ");
    Serial.println(windspeed_raw);
    Serial.print("Wind Gust: ");
    Serial.println(windgust_raw);
    Serial.print("Wind Direction: ");
    Serial.println(winddir_raw);
    Serial.print("Rain 1h: ");
    Serial.println(rain1h_raw);
    Serial.print("Rain 24h: ");
    Serial.println(rain24h_raw);

    //Make conversion to US for Wunderground
    windspeed = windspeed_raw * 0.62138f;
    windgust = windgust_raw * 0.62138f;
    winddir = calDirection;
    tempout = (( tempout_raw * 1.8 ) + 32);
    dewpout =  (( dewpout_raw * 1.8 ) + 32);
    humidity = humidity_raw;
    baro = 0.02952998751 * baro_raw;
    uvIndex = uvIndex_raw / 100.00;
    rain1h = rain1h_raw / 25.40 ;
    rain24h = rain24h_raw / 25.40 ;

    Serial.println(" ");
    Serial.println("US:  ");
    Serial.print("Temp: ");
    Serial.println(tempout);
    Serial.print("Dew Point: ");
    Serial.println(dewpout);
    Serial.print("Humidity: ");
    Serial.println(humidity);
    Serial.print("Pressure: ");
    Serial.println(baro);
    Serial.print("UV Index: ");
    Serial.println(uvIndex);
    Serial.print("Wind Speed: ");
    Serial.println(windspeed);
    Serial.print("Wind Gust: ");
    Serial.println(windgust);
    Serial.print("Wind Direction: ");
    Serial.println(winddir);
    Serial.print("Rain 1h: ");
    Serial.println(rain1h);
    Serial.print("Rain 24h: ");
    Serial.println(rain24h);
    //Serial.print("Batt Level: "); Serial.print(battLevel); Serial.println("%");
    Serial.println(" ");
    Serial.println("Sending Data to WU each 60sec");

    //STORE RAINCOUNT IN EEPROM
    Serial.println("SET EEPROM");
    eepromstring = String(raincount, 2);
    eepromSet("raincount", eepromstring);
    eepromstring = String(raincount1h, 2);
    eepromSet("raincount1h", eepromstring);
    //END - STORE RAINCOUNT IN EEPROM

    //senddata();  //Uncomment this to send data to WU

    count60sec = millis();
    //ESP.restart();
  }

  if ( ((millis() - count1h) >= (60000 * 60 * 1)) && (raincount1h != 0))
  {
    Serial.println("Reset hourly rain each hour");
    raincount1h = 0;
    rain1h_raw = 0.00;
  }

  if ( millis() >= (60000 * 60 * 24 * 3))
  {
    Serial.println("task each week");

    //ESP.restart();
  }
}
//--------------------End Loop-------------------//


//--------------------Wifi Setup-------------------//
void startwifi()
{
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  startudp();
}


//--------------------Send Data To WU-------------------//
void senddata()
{
  Serial.println("Send to WU Sensor Values");
  Serial.print("connecting to ");
  Serial.println(host);

  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(host, httpPort)) {
    Serial.println("connection failed");
    startwifi();
    return;
  }

  // We now create a URI for the request
  String url = "/weatherstation/updateweatherstation.php?ID=";
  url += WUID;
  url += "&PASSWORD=";
  url += WUPASS;
  url += "&dateutc=now&winddir=";
  url += winddir;
  url += "&windspeedmph=";
  url += windspeed;
  url += "&windgustmph=";
  url += windgust;
  url += "&tempf=";
  url += tempout;
  url += "&dewptf=";
  url += dewpout;
  url += "&humidity=";
  url += humidity;
  url += "&baromin=";
  url += baro;
  url += "&UV=";
  url += uvIndex;
  url += "&rainin=";
  url += rain1h;
  url += "&dailyrainin=";
  url += rain24h;
  url += "&weather=&clouds=&softwaretype=Arduino-ESP8266&action=updateraw";

  Serial.print("Requesting URL: ");
  Serial.println(url);

  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");
  delay(10);

  // Read all the lines of the reply from server and print them to Serial
  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }

  Serial.println();
  Serial.println("closing connection");

  //wifi_set_sleep_type(NONE_SLEEP_T);
  //wifi_set_sleep_type(MODEM_SLEEP_T);
  wifi_set_sleep_type(LIGHT_SLEEP_T);
}

//--------------------Interrupt Wind and Rain-------------------//
void rpm() {
  long thisTime = micros() - wind_last;
  wind_last = micros();
  if (thisTime > 500)
  {

    pulseswind++;
    pulsesgust++;
    if (debug) {
      Serial.print("Nb wind turn:  ");
      Serial.println(pulseswind);
    }
  }
}

// Interrupt routine
void rain() {
  long thisTime = micros() - rain_last;
  rain_last = micros();
  if (thisTime > 1000)
  {
    if (raincount1h == 0)
    {
      count1h = millis();
    }
    raincount1h = raincount1h + 1.00f;
    raincount = raincount + 1.00f;


    if (debug) {
      Serial.print("Nb rain drop:  ");
      Serial.println(raincount);
    }
  }
}

//--------------------Get Wind Direction-------------------//
float DirWind() {

  int vaneValue = ads.readADC_SingleEnded(0);

  vaneDirection = map(vaneValue, 0, 17430, 0, 360);
  calDirection = vaneDirection + vaneOffset;

  if (calDirection > 360)
    calDirection = calDirection - 360;

  if (calDirection < 0)
    calDirection = calDirection + 360;

  if (debug) {
    Serial.print("Wind Dir: ");
    Serial.print(calDirection);
    Serial.print("   Pin Status: ");
    Serial.println(vaneValue);
  }
  return winddir_raw;
}

//--------------------Get Wind Speed-------------------//
float speedwind()
{
  // cli();
  float pulseswindrpm = ( pulseswind / 60.00f );
  calcwindspeed = ( pulseswindrpm * 2.50f );
  if (calcwindspeed > tempwindgust)
  {
    tempwindgust = calcwindspeed;
  }
  if (debug) {
    Serial.print("Total pulseswindrpm:  ");
    Serial.print(pulseswindrpm);
    Serial.print("   Wind Speed:  ");
    Serial.println(calcwindspeed);
  }
  pulseswind = 0;
  //sei();
  return calcwindspeed;

}

//--------------------Get Wind Gust-------------------//
float speedgust()
{
  //cli();
  float pulsesgustrmp = ( pulsesgust / 5.00f );
  calcgustspeed = ( pulsesgustrmp * 2.50f );
  if (calcgustspeed > tempwindgust)
  {
    tempwindgust = calcgustspeed;
  }
  if (debug) {
    Serial.print("Total pulsesgustrmp:  ");
    Serial.print(pulsesgustrmp);
    Serial.print("    Gust Speed:  ");
    Serial.println(calcgustspeed);
  }
  pulsesgust = 0;
  //sei();
  return calcgustspeed;
}

//--------------------UDP NTP Setup-------------------//
void startudp()
{
  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
}

//--------------------NTP Request-------------------//
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

//--------------------NTP Time-------------------//
unsigned long ntptime()
{
  WiFi.hostByName(ntpServerName, timeServerIP);
  sendNTPpacket(timeServerIP);
  delay(1000);

  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no NTP packet yet");
  }
  else {
    Serial.print("NTP packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    // now convert NTP time into everyday time:
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    epoch = secsSince1900 - seventyYears;

    if (debug) {

      Serial.print("Seconds since Jan 1 1900 = " );
      Serial.println(secsSince1900);

      // print Unix time:
      Serial.print("Unix time = ");
      Serial.println(epoch);


      // print the hour, minute and second:
      Serial.print("The local time (UTC-4) is ");       // UTC-4 by (epoch-(3600*4))
      Serial.print(((epoch - (3600 * 4))  % 86400L) / 3600); // print the hour (86400 equals secs per day)
      Serial.print(':');
      if ( (((epoch - (3600 * 4)) % 3600) / 60) < 10 ) {
        // In the first 10 minutes of each hour, we'll want a leading '0'
        Serial.print('0');
      }
      Serial.print(((epoch - (3600 * 4))  % 3600) / 60); // print the minute (3600 equals secs per hour)
      Serial.print(':');
      if ( ((epoch - (3600 * 4)) % 60) < 10 ) {
        // In the first 10 seconds of each minute, we'll want a leading '0'
        Serial.print('0');
      }
      Serial.println((epoch - (3600 * 4)) % 60); // print the second
    }
  }
}

int localhour()
{
  return (((epoch - (3600 * 4))  % 86400L) / 3600);
}

int localmin()
{
  return (((epoch - (3600 * 4))  % 3600) / 60);
}

int localsec()
{
  return ((epoch - (3600 * 4)) % 60);
}

//--------------------EEPROM-------------------//
void eepromSet(String name, String value) {
  Serial.println("eepromSet");

  String list = eepromDelete(name);
  String nameValue = "&" + name + "=" + value;
  //Serial.println(list);
  //Serial.println(nameValue);
  list += nameValue;
  for (int i = 0; i < list.length(); ++i) {
    EEPROM.write(i, list.charAt(i));
  }
  EEPROM.commit();
  Serial.print(name);
  Serial.print(":");
  Serial.println(value);
}


String eepromDelete(String name) {
  Serial.println("eepromDelete");

  int nameOfValue;
  String currentName = "";
  String currentValue = "";
  int foundIt = 0;
  char letter;
  String newList = "";
  for (int i = 0; i < 512; ++i) {
    letter = char(EEPROM.read(i));
    if (letter == '\n') {
      if (foundIt == 1) {
      } else if (newList.length() > 0) {
        newList += "=" + currentValue;
      }
      break;
    } else if (letter == '&') {
      nameOfValue = 0;
      currentName = "";
      if (foundIt == 1) {
        foundIt = 0;
      } else if (newList.length() > 0) {
        newList += "=" + currentValue;
      }


    } else if (letter == '=') {
      if (currentName == name) {
        foundIt = 1;
      } else {
        foundIt = 0;
        newList += "&" + currentName;
      }
      nameOfValue = 1;
      currentValue = "";
    }
    else {
      if (nameOfValue == 0) {
        currentName += letter;
      } else {
        currentValue += letter;
      }
    }
  }
  for (int i = 0; i < 512; ++i) {
    EEPROM.write(i, '\n');
  }
  EEPROM.commit();
  for (int i = 0; i < newList.length(); ++i) {
    EEPROM.write(i, newList.charAt(i));
  }
  EEPROM.commit();
  Serial.println(name);
  Serial.println(newList);
  return newList;
}
void eepromClear() {
  Serial.println("eepromClear");
  for (int i = 0; i < 512; ++i) {
    EEPROM.write(i, '\n');
  }
}
String eepromList() {
  Serial.println("eepromList");
  char letter;
  String list = "";
  for (int i = 0; i < 512; ++i) {
    letter = char(EEPROM.read(i));
    if (letter == '\n') {
      break;
    } else {
      list += letter;
    }
  }
  Serial.println(list);
  return list;
}
String eepromGet(String name) {
  Serial.println("eepromGet");

  int nameOfValue;
  String currentName = "";
  String currentValue = "";
  int foundIt = 0;
  String value = "";
  char letter;
  for (int i = 0; i < 512; ++i) {
    letter = char(EEPROM.read(i));
    if (letter == '\n') {
      if (foundIt == 1) {
        value = currentValue;
      }
      break;
    } else if (letter == '&') {
      nameOfValue = 0;
      currentName = "";
      if (foundIt == 1) {
        value = currentValue;
        break;
      }
    } else if (letter == '=') {
      if (currentName == name) {
        foundIt = 1;
      } else {
      }
      nameOfValue = 1;
      currentValue = "";
    }
    else {
      if (nameOfValue == 0) {
        currentName += letter;
      } else {
        currentValue += letter;
      }
    }
  }
  Serial.print(name);
  Serial.print(":");
  Serial.println(value);
  return value;
}
