/*
  Tank Monitor Sensor Node

  2019-09-15 C.Collins Initial Arduino version supporrting up to 4 tanks
  2019-10-10 Ported to NodeMCU board and converted to Blynk standalone via WiFi
  2019-10-11 Added alarm functionality
  2019-10-13 Added OTA
  2019-10-17 Changed architecture to sensor controller/manager node architecture where sensor controller only deals with sensors and all other external interfaces and functions are handled on manager node. Manager node can manage multiple sensor nodes.
             This allows multiple tank senor nodes to report to one manager node making number of sensors/tanks easily expandable. This also supports sensor nodes being physically distant from each other and the manager node. Used JSON over MQTT.
             Also makes it easy to add future functionality such as pump monitor/control functions with pump related sensors being implemented as another sensor node.

  TODO:

  - fix displayTankData...to actually do so...
  - add date/timestamp to terminal logs
  - add configuration persistence via flash file/string
  - add support for irregular tank shapes by using vector for liquid levels at configurable depth intervals.

*/

#include <ESP8266WiFi.h>
#include <NewPingESP8266.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "timer.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define SSID "Nexxt_582E74-2.4G"
#define PWD  "12345678"
#define ALTSSID "COWIFI151420636/0"
#define ALTPWD "WiFi-89951645"

#define WIFIRETRYCNT 5
#define WIFIRETRYDELAY 5000
#define WIFIREBOOT 1
#define WIFITRYALT 1
#define MQTTRETRYCOUNT 5

const char* ssid = SSID;
const char* password = PWD;
const char* mqtt_server = "soldier.cloudmqtt.com";
const int   mqtt_port = 12045;
const char* mqtt_uid = "afdizojo";
const char* mqtt_pwd = "hfi3smlfeSqV";
String nodename = "SNTM-ESP8266-";

//#define DEBUG_ESP_PORT Serial
#ifdef DEBUG_ESP_PORT
#define DEBUG_MSG(...) DEBUG_ESP_PORT.printf( __VA_ARGS__ )
#else
#define DEBUG_MSG(...)
#endif

#define MAXPAYLOADSIZE 256
#define MAXJSONSIZE 256
#define TOPIC "csc/homepa/tankmon"

#define NUMTANKS 2
#define MAXPINGDISTANCE 400
#define DEBUG 0
#define IMPERIAL 0
#define CVTFACTORGALLONS 0.26417F
#define CVTFACTORINCHES  0.39370F
#define TANKPINGDELAY 5000
#define LOALARMFACTOR 0.30F
#define HIALARMFACTOR 0.90F

// Alarm related bit masks & structs

#define CLEARALARMS   0b00000000
#define HIALARM       0b00000001
#define LOALARM       0b00000010
#define MAXDEPTH      0b00000100
#define NUMALARMS 3

struct alarm {
  std::uint8_t alarmType;
  char alarmName[10];
};

alarm alarms[NUMALARMS] = {
  {HIALARM, "HI"},
  {LOALARM, "LO"},
  {MAXDEPTH, "MAXDEPTH"}
};

bool globalAlarmFlag = false;

WiFiClient net;
PubSubClient client(net);

class tank {
  public:
    float depth = 0;
    float vCM = 0;        // volume in liters per cm of ht
    float maxVolume = 0;
    float liquidDepth = 0;
    float liquidVolume = 0;
    float loAlarm = 0;
    float hiAlarm = 0;
    std::uint8_t alarmFlags = 0b00000000;

    tank()
    {

    }

    tank(float depth, float vCM)
    {
      this->depth = depth;
      this->vCM = vCM;
    }

    tank(float depth, float vCM, float loAlarm, float hiAlarm)
    {
      this->depth = depth;
      this->vCM = vCM;
      this->loAlarm = loAlarm;
      this->hiAlarm  = hiAlarm;
    }
};

tank tanks[NUMTANKS] = {
  tank(97, 10.8),
  tank(97, 10.8)
};

// JSON Tank Definition

StaticJsonDocument<MAXJSONSIZE> tankmsg;
/*
  {
    "tankID" : 0,
    "depth" : 0,
    "vCM" : 0,
    "maxVolume" : 0,
    "liquidDepth" : 0,
    "liquidVolume" : 0,
    "loAlarm" : 0,
    "hiAlarm" : 0,
    "alarmFlags" : 0;  // pass as byte value representing binary flags
  }
*/


class sonarClass : public NewPingESP8266 {
  public:
    sonarClass(int trigPin, int echoPin, int maxDistance): NewPingESP8266(trigPin, echoPin, maxDistance)
    {

    };
};

sonarClass sonars[NUMTANKS] = {
  sonarClass(D5, D6, MAXPINGDISTANCE),
  sonarClass(D7, D8, MAXPINGDISTANCE)
};

auto timer = timer_create_default();

char payload[MAXPAYLOADSIZE] = "";

void initTanks()
{
  DEBUG_MSG("In initTanks \n");

  for (int t = 0; t <= NUMTANKS - 1; t++)      // set max volume and alarm level on all tanks
  {
    tanks[t].maxVolume = tanks[t].depth * tanks[t].vCM;
    tanks[t].loAlarm = tanks[t].depth * LOALARMFACTOR;
    tanks[t].hiAlarm = tanks[t].depth * HIALARMFACTOR;
  }
  tankmsg["d"] = tanks[0].depth;
  tankmsg["mV"] = tanks[0].maxVolume;
}

void handleAlarm(std::uint8_t alarmType, int tank)
{

  globalAlarmFlag = true;

  switch (alarmType) {
    case HIALARM :
      tanks[tank].alarmFlags = tanks[tank].alarmFlags | HIALARM;
      break;

    case LOALARM :
      tanks[tank].alarmFlags = tanks[tank].alarmFlags | LOALARM;
      break;

    case MAXDEPTH :
      tanks[tank].alarmFlags = tanks[tank].alarmFlags | MAXDEPTH;
      break;
  }
}

int mapAlarm(std::uint8_t alarmType)
{
  switch (alarmType) {
    case HIALARM :
      return (0);

    case LOALARM :
      return (1);

    case MAXDEPTH :
      return (2);

    default: return (-1);

  }
}

void clearAlarm(std::uint8_t alarmType, int tank)
{
  int a = -1;
  std::uint8_t alarmCheck = CLEARALARMS;

  a = mapAlarm(alarmType);
  if (a >= 0)
  {
    tanks[tank].alarmFlags = tanks[tank].alarmFlags & (~alarmType); // clear specific alarm flag
  }
  else
  {
    Serial.print("\nError mapping alarm type to error name in clearAlarm");
  }
  
  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    alarmCheck = alarmCheck | tanks[t].alarmFlags;
  }

  if (!alarmCheck)
  {
    globalAlarmFlag = false;
  }
}


void pingTanks()
{
  int d = 0;

  DEBUG_MSG("In blynkTankData \n");

  digitalWrite(LED_BUILTIN, LOW);

  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    d = sonars[t].convert_cm(sonars[t].ping_median());

    if (d > tanks[t].depth)
    {
      d = tanks[t].depth;
      handleAlarm(MAXDEPTH, t);
    }
    else if ((tanks[t].alarmFlags & MAXDEPTH) && (d != tanks[t].depth)) clearAlarm(MAXDEPTH, t);

    tanks[t].liquidDepth = tanks[t].depth - d;
    tanks[t].liquidVolume = tanks[t].liquidDepth * tanks[t].vCM;

    if (tanks[t].liquidDepth > tanks[t].hiAlarm)
    {
      handleAlarm(HIALARM, t);
    }
    else if (tanks[t].alarmFlags & HIALARM) clearAlarm(HIALARM, t);

    if (tanks[t].liquidDepth < tanks[t].loAlarm)
    {
      handleAlarm(LOALARM, t);
    }
    else if (tanks[t].alarmFlags & LOALARM) clearAlarm(LOALARM, t);

  }

  if (DEBUG) displayTankData();

  digitalWrite(LED_BUILTIN, HIGH);
}


void sendTankData()
{
  DEBUG_MSG("In sendTankData \n");
  digitalWrite(LED_BUILTIN, LOW);   // Turn the LED on (Note that LOW is the voltage level

  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    tankmsg["t"] = t;
    tankmsg["d"] = tanks[t].depth;
    tankmsg["vCM"] = tanks[t].vCM;
    tankmsg["mV"] = tanks[t].maxVolume;
    tankmsg["lD"] = tanks[t].liquidDepth;
    tankmsg["lV"] = tanks[t].liquidVolume;
    tankmsg["loA"] = tanks[t].loAlarm;
    tankmsg["hiA"] = tanks[t].hiAlarm;
    tankmsg["aF"] = tanks[t].alarmFlags;

    serializeJson(tankmsg, payload);
    if (DEBUG)
    {
      Serial.println("\nIn sendTankData");
      Serial.print("\npayload=(");
      Serial.print(payload);
      Serial.println(")");
      serializeJson(tankmsg, Serial);
      displayTankData();
    }

    // char testPayLoad[] = "test from tankmon";
    client.publish(TOPIC, payload);
    digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH
  }
}

bool handleTimer(void *)
//void handleTimer()
{
  DEBUG_MSG("In handleTimer \n");
  pingTanks();
  //displayTankData();
  sendTankData();
}

void connectWiFi()
{
  int i = 0;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("\nWiFi connecting to ");
  Serial.print(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(WIFIRETRYDELAY);
    WiFi.begin(ssid, password);
    i++;
    if (i > WIFIRETRYCNT)
    {
      i = 0;
      if (WIFITRYALT && (ssid != ALTSSID))
      {
        ssid = ALTSSID;
        password = ALTPWD;
        Serial.print("\nWiFi connecting to alt SSID: ");
        Serial.print(ssid);
      }
      else if (WIFIREBOOT)
      {
        Serial.print("\nRebooting...");
        ESP.restart();
      }

    }

  }
  Serial.print("\nWiFi connected to ");
  Serial.print(SSID);
}


void displayTankData()
{
  if (DEBUG)
  {
    Serial.println("\nIn displayTankData");
  };

  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    Serial.println();
    // fix this dummy
    serializeJson(tankmsg, Serial);

  }

}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    digitalWrite(BUILTIN_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is active low on the ESP-01)
  } else {
    digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  }
}

void connectMQTT() {

  int i = 0;

  Serial.print("\nConnecting to MQTT Server: ");
  Serial.print(mqtt_server);
  while (!client.connected()) {

    // Attempt to connect

    if (client.connect(nodename.c_str(), mqtt_uid, mqtt_pwd)) {
      Serial.println();
      Serial.print(nodename);
      Serial.println(" connected");
      client.publish("hello", nodename.c_str());                        // Once connected, publish an announcement...
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
    i++;
    if (i > MQTTRETRYCOUNT)
    {
      Serial.println();
      Serial.println("Rebooting...");
      ESP.restart();
    }
  }
}

void setupMQTT()
{
  //
  // MQTT Setup

  client.setServer(mqtt_server, 12045);
  client.setCallback(callback);
  connectMQTT();
}

void setupJSON()
{

  //
  // JSON setup
  //
  /*
    {
      "tanknum" : 0,
      "depth" : 0,
      "vCM" : 0,
      "maxVolume" : 0,
      "liquidDepth" : 0,
      "liquidVolume" : 0,
      "loAlarm" : 0,
      "hiAlarm" : 0,
      "alarmFlags" : 0;  // pass as byte value representing binary flags
    }
  */

  // populate tankmsg

  tankmsg["n"] = nodename;
  tankmsg["t"] = 0;
  tankmsg["d"] = 0;
  tankmsg["vCM"] = 0;
  tankmsg["mV"] = 0;
  tankmsg["lD"] = 0;
  tankmsg["lV"] = 0;
  tankmsg["loA"] = 0;
  tankmsg["hiA"] = 0;
  tankmsg["aF"] = 0;
}


void setupOTA ()
{

  //
  // OTA Setup
  //
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");


  ArduinoOTA.setHostname(nodename.c_str());

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("OTA Ready");
}

void displayStartHeader()
{
  Serial.println();
  Serial.println("Tank Monitor Sensor Node");
  Serial.print("Build: ");
  Serial.print(__DATE__);
  Serial.print(" ");
  Serial.print(__TIME__);
  Serial.println();
  Serial.print("Nodename: ");
  Serial.print(nodename);
  Serial.println();
  Serial.print("DEBUG = ");
  Serial.println(DEBUG);
  Serial.print("NUMTANKS = ");
  Serial.println(NUMTANKS);
  Serial.print("MAXPINGDISTANCE = ");
  Serial.println(MAXPINGDISTANCE);
  Serial.print("TANKPINGDELAY = ");
  Serial.println(TANKPINGDELAY);
  Serial.println();
  Serial.flush();
}

void setup()
{
  delay(3000);                                     // Initial delay to avoid Arduino upload problems
  DEBUG_MSG("In setup \n");
  Serial.begin(115200);
  nodename += String(ESP.getChipId(), HEX);
  displayStartHeader();
  pinMode(LED_BUILTIN, OUTPUT);
  setupOTA();
  setupJSON();
  connectWiFi();
  setupMQTT();

  timer.every(5000, handleTimer);

  pinMode(LED_BUILTIN, OUTPUT);

  initTanks();
  pingTanks();                                    // initial ping just to get data for display
  displayTankData();
}

void loop() {

  ArduinoOTA.handle();
  timer.tick();

  if (!client.connected()) {
    connectMQTT();
  }
  client.loop();
}
