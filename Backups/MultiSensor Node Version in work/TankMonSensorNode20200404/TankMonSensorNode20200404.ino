
/*
  Tank Monitor Sensor Node

  2019-09-15 C.Collins Initial Arduino version supporrting up to 4 tanks
  2019-10-10 Ported to NodeMCU board and converted to Blynk standalone via WiFi
  2019-10-11 Added alarm functionality
  2019-10-13 Added OTA
  2019-10-17 Changed architecture to sensor controller/manager node architecture where sensor controller only deals with sensors and all other external interfaces and functions are handled on manager node. Manager node can manage multiple sensor nodes.
             This allows multiple tank senor nodes to report to one manager node making number of sensors/tanks easily expandable. This also supports sensor nodes being physically distant from each other and the manager node. Used JSON over MQTT.
             Also makes it easy to add future functionality such as pump monitor/control functions with pump related sensors being implemented as another sensor node.

  2020-03-28 Begin clean up code including elimnation of redundant code and moving shared code to libraries.

  TODO:

  - discard outliers pings
  - review alarm logic across sensor & manager nodes and clean up
  - add configuration persistence via flash file/string
  - add support for irregular tank shapes by using vector for liquid levels at configurable depth intervals.
  - add com output to browser

*/

#include <Tanksmon.h>
#include <NewPingESP8266.h>
#include "timer.h"

#define DEBUG true
#define LEDFLASHTIME 3000
bool ledOn = false;

class sonarClass : public NewPingESP8266 {
  public:
    sonarClass(int trigPin, int echoPin, int maxDistance): NewPingESP8266(trigPin, echoPin, maxDistance)
    {

    };
};

sonarClass sonars[NUMTANKS] = {
  sonarClass(14, 12, MAXPINGDISTANCE),
  sonarClass(13, 15, MAXPINGDISTANCE)
};

auto timer = timer_create_default();

void initTanks()
{

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
  float d = 0;

  digitalWrite(LED_BUILTIN, LOW);

  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    d = sonars[t].convert_cm(sonars[t].ping_median());
    Serial.print("\npinged d=");
    Serial.print(d);
    tanks[t].pingCount++;

    if (d > tanks[t].depth)
    {
      d = tanks[t].depth;
      handleAlarm(MAXDEPTH, t);
    }
    else if ((tanks[t].alarmFlags & MAXDEPTH) && (d != tanks[t].depth)) clearAlarm(MAXDEPTH, t);

    tanks[t].liquidDepth = tanks[t].depth - (d - tanks[t].sensorOffset);
    tanks[t].liquidDepthSum += tanks[t].liquidDepth;

    if (DEBUG)
    {
      Serial.print("\ntank ");
      Serial.print(t);
      Serial.print(" liquidDepth set to =");
      Serial.print(tanks[t].liquidDepth);
    };

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
  digitalWrite(LED_BUILTIN, LOW);   // Turn the LED on (Note that LOW is the voltage level

  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    tanks[t].liquidDepthAvg = tanks[t].liquidDepthSum/tanks[t].pingCount;
    tanks[t].liquidVolumeAvg = tanks[t].liquidDepthAvg * tanks[t].vCM;

    if (DEBUG)
    {
      Serial.print("\nActual liquid depth ");
      Serial.println(tanks[t].liquidDepth);
      Serial.print("liquid depth average ");
      Serial.println(tanks[t].liquidDepthAvg);
      Serial.print("\nActual liquid volume ");
      Serial.println(tanks[t].liquidVolume);
      Serial.print("liquid volume average ");
      Serial.println(tanks[t].liquidVolumeAvg);
      Serial.print("ping count =  ");
      Serial.println(tanks[t].pingCount);
    }
    
    tankmsg["t"] = t;
    tankmsg["m"] = "d";
    tankmsg["d"] = tanks[t].depth;
    tankmsg["vCM"] = tanks[t].vCM;
    tankmsg["mV"] = tanks[t].maxVolume;
    tankmsg["lD"] = tanks[t].liquidDepth;
    tankmsg["lDAvg"] = tanks[t].liquidDepthAvg;
    tankmsg["lV"] = tanks[t].liquidVolume;
    tankmsg["lVAvg"] = tanks[t].liquidVolumeAvg;
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
    tanks[t].liquidDepthSum = 0;
    tanks[t].pingCount = 0;
    
    client.publish(TOPIC, payload);
    digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH
  }
}

bool handlePingTimer(void *)
{
  pingTanks();
  return true;
}

bool handleSendDataTimer(void *)
{
  sendTankData();
  return true;
}

bool handleLEDTimer(void *)
{
  if (ledOn)
  {
    digitalWrite(LED_BUILTIN, HIGH);
  }
  else
  {
    digitalWrite(LED_BUILTIN, LOW);
  }

  ledOn = !ledOn;
  return(true);
}

void displayTankData()
{
  if (DEBUG)
  {
    Serial.println("\nIn displayTankData");
  }

  for (int t = 0; t <= NUMTANKS - 1; t++)
  {    
    Serial.println();
    Serial.print("Tank ");
    Serial.print(t + 1);
    Serial.println();
    Serial.print("Tank Depth ");
    Serial.println(tanks[t].depth);
    Serial.print("Tank liters/cm depth ");
    Serial.println(tanks[t].vCM);
    Serial.print("Max Volume ");
    Serial.println(tanks[t].maxVolume);
    Serial.print("Actual liquid depth ");
    Serial.println(tanks[t].liquidDepth);
    Serial.print("Average liquid depth ");
    Serial.println(tanks[t].liquidDepthAvg);
    Serial.print("Actual liquid volume ");
    Serial.println(tanks[t].liquidVolume);
    Serial.print("Average liquid volume ");
    Serial.println(tanks[t].liquidVolumeAvg);
    Serial.print("Lo alarm level ");
    Serial.println(tanks[t].loAlarm);
    Serial.print("Hi alarm level ");
    Serial.println(tanks[t].hiAlarm);
  }
}

void handleMQTTmsg(char* topic, byte* payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void displayStartHeader(int i)
{

  if (i == 1)
  {
    Serial.println();
    Serial.println("Tank Monitor Sensor Node");
    Serial.print("Build: ");
    Serial.print(__DATE__);
    Serial.print(" ");
    Serial.print(__TIME__);
    Serial.println();
  }

  if (i == 2)
  {
    Serial.print("\nNodename: ");
    Serial.println(nodename);
    Serial.print("MQTT topic: ");
    Serial.println(TOPIC);
    Serial.print("\nDEBUG = ");
    Serial.println(DEBUG);
    Serial.print("NUMTANKS = ");
    Serial.println(NUMTANKS);
    Serial.print("MAXPINGDISTANCE = ");
    Serial.println(MAXPINGDISTANCE);
    Serial.print("TANKPINGDELAY = ");
    Serial.println(TANKPINGDELAY);
    displayTankData();
    Serial.print("\nStarted:  ");
    Serial.println(timestampString());
    Serial.println();
    Serial.flush();
  }
}

void setup()
{

  #define MAXNODENAME 30
  int i = 0;
  char tmp[MAXNODENAME];
  
  delay(3000);                                     // Initial delay to avoid Arduino upload problems
  Serial.begin(115200);
  displayStartHeader(1);
  i=snprintf(tmp,MAXNODENAME, "SNTM-ESP8266-%X",ESP.getChipId());
  nodename = tmp;
  pinMode(LED_BUILTIN, OUTPUT);
  connectWiFi();
  setupNTP();
  setupOTA();
  setupMQTT(false, handleMQTTmsg);

  timer.every(TANKPINGDELAY, handlePingTimer);
  timer.every(SENDDATADELAY, handleSendDataTimer);
  timer.every(LEDFLASHTIME, handleLEDTimer);

  pinMode(LED_BUILTIN, OUTPUT);

  initTanks();
  pingTanks();                                    // initial ping just to get data for display
  displayStartHeader(2);
}

void loop() {

  ArduinoOTA.handle();
  timer.tick();

  if (!client.connected()) {
    connectMQTT(false);
  }
  client.loop();
}
