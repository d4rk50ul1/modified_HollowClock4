/*
 * @autor: nikzin
 *
 * CAD Files and original Code this is based on: https://www.instructables.com/Hollow-Clock-4/
 *
 * Based on the great work of shiura. Thank your for this great project.
 *
 * Instead of the Arduino Nano in the original Version from shiura, I used an ESP8266 D1 Mini, this also (barely) fits in the provided case.
 * By using the ESP I can get the real time from an NTP server. I also added daytime saving (since in Germany we still have that). I have not tested this function though.
 *
 * When you start up the clock please set the time to 12 o`clock and wait until the it connects to WiFi and sets itself.
 *
 * After that it updates the time every 10 seconds from the server.
 *
 * If there is no internet connection the clock will just run like the original version, until it can reconnect to WiFi. Then it will set itself again to the correct time.
 *
 * Powerconsumption is about 50mA on average (including WiFi communication and motor running every minute)
 *
 * To connect to WiFi just add your WiFi Credentials below.
 *
 *
 * Have fun with this version of the Hollow Clock 4!
 *
 */

#include <ESP8266WiFi.h>
#include <NTPClient.h>    //  https://github.com/arduino-libraries/NTPClient
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <time.h>
#include <sys/time.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>





 //save int in EEPROM
void saveInt(int address, int value) {
  byte low = (value & 0xFF);
  byte high = ((value >> 8) & 0xFF);
  EEPROM.write(address, low);
  EEPROM.write(address + 1, high);
  EEPROM.commit();
}

// read int from EEPROM
int readInt(int address) {
  byte low = EEPROM.read(address);
  byte high = EEPROM.read(address + 1);
  return (high << 8) | low;
}

// save string in EEPROM
void saveString(int address, String string) {
  char charBuf[string.length() + 1];
  string.toCharArray(charBuf, string.length() + 1);
  for (int i = 0; i < string.length(); i++) {
    EEPROM.write(address + i, charBuf[i]);
  }
  EEPROM.write(address + string.length(), '\0');
  EEPROM.commit();
}

// read string from EEPROM
String readString(int address) {
  String string;
  char charBuf[100];
  int i;
  for (i = 0; EEPROM.read(address + i) != '\0'; i++) {
    charBuf[i] = EEPROM.read(address + i);
  }
  charBuf[i] = '\0';
  string = charBuf;
  return string;
}

// save bool in EEPROM
void saveBool(int address, bool value) {
  EEPROM.write(address, value);
  EEPROM.commit();
}

// read bool from EEPROM
bool readBool(int address) {
  return EEPROM.read(address);
}

int STEPS_PER_ROTATION;
const char * MY_TZ;
const char * MY_NTP_SERVER;
int delaytime;
int port[4];

void EEPROM_init() {
   if (readString(0) == "HC4") { //  check if EPROM is From Hollow Clock 4
    Serial.println("EEPROM is from Hollow Clock 4");

  }
  else {
    Serial.println("EEPROM is not from Hollow Clock 4");

    // clear EEPROM
    for (int i = 0; i < 512; i++) {
      EEPROM.write(i, 0);
    }

    saveString(0, "HC4");

    // save default values
    saveBool(100, false); //  Flip rotation
    saveInt(101, 30720); //  STEPS_PER_ROTATION  
    saveInt(103, 2); //  delaytime
    saveString(150, "CET-1CEST,M3.5.0/02,M10.5.0/03"); //  MY_TZ  https://werner.rothschopf.net/202011_arduino_esp8266_ntp_en.htm
    saveString(200, "pool.ntp.org"); //  MY_NTP_SERVER
  }

  Serial.println(readString(0));

  // read values from EEPROM
  bool flipRotation = readBool(100);
  STEPS_PER_ROTATION = readInt(101);
  delaytime = readInt(103);
  MY_TZ = readString(150).c_str();
  MY_NTP_SERVER = readString(200).c_str();


  if (flipRotation == true){
    port[0] = D5;
    port[1] = D6;
    port[2] = D7;
    port[3] = D8;

  }
  else {
    port[0] = D8;
    port[1] = D7;
    port[2] = D6;
    port[3] = D5;
  }

}



time_t now;                         // this is the epoch
tm tm;                              // the structure tm holds time information in a more convient way

// sequence of stepper motor control
int seq[8][4] = {
  {  LOW, HIGH, HIGH,  LOW},
  {  LOW,  LOW, HIGH,  LOW},
  {  LOW,  LOW, HIGH, HIGH},
  {  LOW,  LOW,  LOW, HIGH},
  { HIGH,  LOW,  LOW, HIGH},
  { HIGH,  LOW,  LOW,  LOW},
  { HIGH, HIGH,  LOW,  LOW},
  {  LOW, HIGH,  LOW,  LOW}
};

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);


void setTimezone(String timezone) {
  setenv("TZ", timezone.c_str(), 1);  //  Now adjust the time zone
  tzset();
}



// Variables to save date and time and other needed parameters
int Minute, Hour, currHour, currMinute, hourDiff, minuteDiff, stepsToGo;

bool skip = true;


void rotate(int step) { // original function from shiura
  static int phase = 0;
  int i, j;
  int delta = (step > 0) ? 1 : 7;
  int dt = 20;

  step = (step > 0) ? step : -step;
  for (j = 0; j < step; j++) {
    phase = (phase + delta) % 8;
    for (i = 0; i < 4; i++) {
      digitalWrite(port[i], seq[phase][i]);
    }
    delay(dt);
    if (dt > delaytime) dt--;
  }
  // power cut
  for (i = 0; i < 4; i++) {
    digitalWrite(port[i], LOW);
  }
}

void rotateFast(int step) { // this is just to rotate to the current time faster, when clock is started
  static int phase = 0;
  int i, j;
  int delta = (step > 0) ? 1 : 7;
  int dt = 1;

  step = (step > 0) ? step : -step;
  for (j = 0; j < step; j++) {
    phase = (phase + delta) % 8;
    for (i = 0; i < 4; i++) {
      digitalWrite(port[i], seq[phase][i]);
    }
    delay(dt);
    if (dt > delaytime) dt--;
  }
  // power cut
  for (i = 0; i < 4; i++) {
    digitalWrite(port[i], LOW);
  }
}


void updateTime() {

  time(&now);                       // read the current time
  localtime_r(&now, &tm);           // update the structure tm with the current time

  Hour = tm.tm_hour;
  Minute = tm.tm_min;

}

void getTimeDiff() {
  updateTime();

  if (currHour != Hour) {
    if (Hour == 12 || Hour == 24) {
      currHour = Hour;
      hourDiff = 0;
    }
    else {
      if (Hour > 12) {
        hourDiff = Hour - 12;
        currHour = Hour;
      }
      else {
        currHour = Hour;
        hourDiff = Hour;
      }

    }

  }

  if (currMinute != Minute) {
    minuteDiff = Minute;
    currMinute = Minute;
  }

}

//Ticker update;


void initWifi() {

  WiFi.begin("SSID", "PASSWORD"); //  add your WiFi credentials here


  configTime(MY_TZ, MY_NTP_SERVER);

}


void setup() {
  Serial.begin(9600);

  EEPROM.begin(512);

  EEPROM_init();

  //at startup the clock expects it´s set to 12 o`clock
  currHour = 0;
  currMinute = 0;

  //initWifi();


  pinMode(port[0], OUTPUT);
  pinMode(port[1], OUTPUT);
  pinMode(port[2], OUTPUT);
  pinMode(port[3], OUTPUT);


  getTimeDiff();  //  when first starting up the clock expects that it is set to 12 o`clock and will set itself to the current time from there

  rotate(-20); // for approach run
  rotate(20); // approach run without heavy load
  rotateFast((STEPS_PER_ROTATION * hourDiff));
  rotateFast(((minuteDiff * STEPS_PER_ROTATION) / 60));

  hourDiff = 0;
  minuteDiff = 0;


}



void loop() {

  // the clock will check if there is a time difference
  updateTime();
  skip = true;

  if (currMinute != 59 && currHour != Hour) { //  some conversion of the time to fit the 12h clock
    int newCurrHour;
    if (Hour > 12) {
      Hour = Hour - 12;
    }
    newCurrHour = currHour;
    if (currHour > 12) {
      newCurrHour = currHour - 12;
    }
    if (Hour > newCurrHour) {
      hourDiff == Hour - newCurrHour;
    }
    else if (newCurrHour > Hour) {
      hourDiff = 12 - (newCurrHour - Hour);
    }
  }

  if (currMinute != Minute) {
    if (Minute < currMinute) {
      minuteDiff = 60 - currMinute + Minute;
    }
    else {
      minuteDiff = Minute - currMinute;
      // currMinute = Minute;
    }

    currMinute = Minute;
    currHour = Hour;


    if (minuteDiff >= 0) {  //  this is used to make sure the clock does not go backwards, since its not really working backwards
      rotate(-20); // for approach run
      rotate(20); // approach run without heavy load
      rotate(((minuteDiff * STEPS_PER_ROTATION) / 60));
    }
    if (hourDiff >= 0) {
      rotate((STEPS_PER_ROTATION * hourDiff));
    }

    minuteDiff = 0;
    hourDiff = 0;

  }

  delay(10000);

}
