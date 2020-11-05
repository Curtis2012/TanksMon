/*
  Tanks_Mon

  2019-09-15 C.Collins Initial Arduino based version
  2019-10-10 Ported to NodeMCU board and converted to Blynk
  2019-10-11 Added alarm functionality
  2019-10-13 Added OTA
  2019-10-22 Changed architecture to sensor node/manager node architecture where sensor node only deals with sensors and all other external interfaces and functions are handled on manager node.
             Manager node can manage multiple sensor nodes. This allows multiple senor nodes to report to one manager node making number of sensors/tanks easily expandable.
             This also supports sensor nodes being physically distant from each other and the manager node. Used MQTT protocol with JSON payload.
  2020-03-30 Begain process of restructuring code to use common library (Tankmon.h), fixing bugs, improving implementation, resolving OTA issues...


  TODO:

  - enchance code to handle multiple sensor nodes
    - use node name to differentiate, display tanks for each node on separage tab in app
  - debug/enhance clearAlarm logic
  - add date/timestamp to terminal logs
  - clean up displayTankData and blynkTankData to be better structured/integrated
  - add command support to terminal
  - add support for irregular tank shapes by using vector for liquid levels at configurable depth intervals.
  - modify blynkTankData to remove hard code tank numbers
  - standardize timer library use across sensor node and manager node, now there are two.
  - resolve compiler warning msgs
  - splash screen not displaying on blynk console
  - add com output to browser
  - remove use of Ardui

*/


#include <Tanksmon.h>
#include <BlynkSimpleEsp8266.h>

#define IOLED 2
#define BLYNK_PRINT Serial
#define DEBUG 1
#define IMPERIAL 0

#define DISPLAYUPDATETIME 15000L
#define USEAVERAGE true
#define LEDFLASHTIME 3000
#define IOLEDONTIME 500

char auth[] = "WlBesaF6zbb7UILuGZai66-sxEc4LM4l";  // Blynk auth token

WidgetLED blynkUpdateLED(V8);
WidgetLED blynkErrorLED(V9);
WidgetLED blynkAlarmLED(V11);
BlynkTimer timer;
WidgetTerminal blynkTerminal(V10);

bool ledOn = false;

void initTanks()
{
  for (int t = 0; t <= NUMTANKS - 1; t++)      // set max volume and alarm level on all tanks
  {
    tanks[t].maxVolume = tanks[t].depth * tanks[t].vCM;
    tanks[t].loAlarm = tanks[t].depth * LOALARMFACTOR;
    tanks[t].hiAlarm = tanks[t].depth * HIALARMFACTOR;
  }

  // clear unused tank widgets, hard coded for now...

  //  Blynk.virtualWrite(V0,0);
  //  Blynk.virtualWrite(V1,0);
  Blynk.virtualWrite(V2, 0);
  Blynk.virtualWrite(V3, 0);

  //  Blynk.setProperty(V1, "color", "#000000");
  //  Blynk.setProperty(V0, "color", "#000000");
  Blynk.setProperty(V2, "color", "#000000");
  Blynk.setProperty(V3, "color", "#000000");

  //  Blynk.setProperty(V1, "label", " ");
  //  Blynk.setProperty(V0, "label", " ");
  Blynk.setProperty(V2, "label", " ");
  Blynk.setProperty(V3, "label", " ");
}

void handleAlarm(int tank)
{
  #define MAXNOTIFYSIZE 40
  char notification[MAXNOTIFYSIZE];
  int n = 0;
  
  globalAlarmFlag = true;

  if (tanks[tank].alarmFlags & HIALARM)
  {
    blynkTimestamp();
    n = snprintf(notification,MAXNOTIFYSIZE, "HI ALARM! on tank %i", (tank + 1));
    blynkTerminal.println();
    blynkTerminal.println(notification);
    blynkTerminal.flush();
    blynkAlarmLED.on();
    Blynk.notify(notification);
  }

  if (tanks[tank].alarmFlags & LOALARM)
  {
    blynkTimestamp();
    n = snprintf(notification,MAXNOTIFYSIZE, "LO ALARM! on tank %i", (tank + 1));
    blynkTerminal.println();
    blynkTerminal.println(notification);
    blynkTerminal.flush();
    blynkAlarmLED.on();
    Blynk.notify(notification);
  }

  if (tanks[tank].alarmFlags & MAXDEPTH)
  {
    blynkTimestamp();
    n = snprintf(notification,MAXNOTIFYSIZE, "MAX DEPTH ALARM! on tank  %i , depth = %i", (tank + 1), tanks[tank].depth);
    blynkTerminal.println();
    blynkTerminal.println(notification);
    blynkTerminal.flush();
    blynkAlarmLED.on();
    Blynk.notify(notification);
  }
}

void IOLEDOff()
{
  digitalWrite(IOLED, HIGH);
}

void blynkTimestamp()
{
  digitalWrite(IOLED, LOW);
  blynkTerminal.println();
  blynkTerminal.print(timestampString());
  blynkTerminal.println();
  timer.setInterval(IOLEDONTIME, IOLEDOff);
}

void printTimestamp()
{
  digitalWrite(IOLED, LOW);
  Serial.println();
  Serial.print(timestampString());
  Serial.println();
  timer.setInterval(IOLEDONTIME, IOLEDOff);
}

void clearAlarm(std::uint8_t alarmType, int tank)
{
  int a = -1;
  std::uint8_t alarmCheck = 0b00000000;

  blynkTimestamp();
  a = mapAlarm(alarmType);
  if (a >= 0)
  {
    tanks[tank].alarmFlags = tanks[tank].alarmFlags & (~alarmType); // clear specific alarm flag
    blynkTerminal.println();
    blynkTerminal.print("Alarm cleared: ");
    blynkTerminal.print(alarms[a].alarmName);
    blynkTerminal.print(", tank ");
    blynkTerminal.print(tank + 1);
    blynkTerminal.println();
    blynkTerminal.flush();
  }
  else
  {
    blynkTerminal.println();
    blynkTerminal.print("Error mapping alarm type to error name in clearAlarm, alarmtype = ");
    blynkTerminal.println(alarmType);
    blynkErrorLED.on();
  }

  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    alarmCheck = alarmCheck | tanks[t].alarmFlags;
  }

  if (alarmCheck == 0)
  {
    globalAlarmFlag = false;
    blynkAlarmLED.off();
    blynkTerminal.print("All alarms cleared");
    blynkTerminal.flush();
  }
}

void blynkTankData()
{
  float cvtG = 1.0, cvtI = 1.0;

  if (DEBUG)
  {
    printTimestamp();
    Serial.println("\nIn blynkTankData");
    displayTankData();
  }

  blynkUpdateLED.on();

  if (IMPERIAL)
  {
    cvtG = CVTFACTORGALLONS;
    cvtI = CVTFACTORINCHES;
  }

  if (USEAVERAGE)
  {
    Blynk.virtualWrite(V0, (tanks[0].liquidDepthAvg * cvtI));
    Blynk.virtualWrite(V1, (tanks[1].liquidDepthAvg * cvtI));
    Blynk.virtualWrite(V4, (tanks[0].liquidVolumeAvg * cvtG));
    Blynk.virtualWrite(V5, (tanks[1].liquidVolumeAvg * cvtG));
  }
  else
  {
    Blynk.virtualWrite(V0, (tanks[0].liquidDepth * cvtI));
    Blynk.virtualWrite(V1, (tanks[1].liquidDepth * cvtI));
    Blynk.virtualWrite(V4, (tanks[0].liquidVolume * cvtG));
    Blynk.virtualWrite(V5, (tanks[1].liquidVolume * cvtG));
  }

  blynkUpdateLED.off();
  digitalWrite(IOLED, HIGH);
}

void displayTankData()
{
  blynkTimestamp();
  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    blynkTerminal.println();
    blynkTerminal.print("Tank ");
    blynkTerminal.print(t + 1);
    blynkTerminal.println();
    blynkTerminal.print("Tank Depth ");
    blynkTerminal.println(tanks[t].depth);
    blynkTerminal.print("Tank liters/cm depth ");
    blynkTerminal.println(tanks[t].vCM);
    blynkTerminal.print("Max Volume ");
    blynkTerminal.println(tanks[t].maxVolume);
    blynkTerminal.print("Actual liquid depth ");
    blynkTerminal.println(tanks[t].liquidDepth);
    blynkTerminal.print("Average liquid depth");
    blynkTerminal.println(tanks[t].liquidDepthAvg);
    blynkTerminal.print("Actual liquid volume ");
    blynkTerminal.println(tanks[t].liquidVolume);
    blynkTerminal.print("Average liquid volume ");
    blynkTerminal.println(tanks[t].liquidVolumeAvg);
    blynkTerminal.print("Lo alarm level ");
    blynkTerminal.println(tanks[t].loAlarm);
    blynkTerminal.print("Hi alarm level ");
    blynkTerminal.println(tanks[t].hiAlarm);
    blynkTerminal.println(timestampString());
    blynkTerminal.flush();
  }
}

void handleDisplayTimer()
{
  blynkTankData();
}

void handleLEDTimer()
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
}

void dumpTankmsg()
{
  char buffer[40] = "";
  char tnodename[30] = "";
  int n = 0;
  
  
  Serial.println();
  Serial.println("Dump of tankmsg by field: ");

  tnodename[30] = tankmsg["n"];
  n=sprintf(buffer, "tankmsg[n]= %s",tnodename);
  Serial.println(buffer);
  n = sprintf(buffer, "tankmsg[m]= %c", (char)tankmsg["m"]);
  Serial.println(buffer);
  n = sprintf(buffer, "tankmsg[t]= %i", (int)tankmsg["t"]);
  Serial.println(buffer);
  n = sprintf(buffer, "tankmsg[d]= %f", (float)tankmsg["d"]);
  Serial.println(buffer);
  n = sprintf(buffer, "tankmsg[vCM]= %f", (float)tankmsg["vCM"]);
  Serial.println(buffer);
  n = sprintf(buffer, "tankmsg[mV]= %f", (float)tankmsg["mV"]);
  Serial.println(buffer);
  n = sprintf(buffer, "tankmsg[lD]= %f", (float)tankmsg["lD"]);
  Serial.println(buffer);
  n = sprintf(buffer, "tankmsg[lDAvg]= %f", (float)tankmsg["lDAvg"]);
  Serial.println(buffer);
  n = sprintf(buffer, "tankmsg[lV]= %f", (float)tankmsg["lV"]);
  Serial.println(buffer);
  n = sprintf(buffer, "tankmsg[lVAvg]= %f", (float)tankmsg["lVAvg"]);
  Serial.println(buffer);
   n = sprintf(buffer, "tankmsg[loA]= %f", (float)tankmsg["loA"]);
  Serial.println(buffer);
  n = sprintf(buffer, "tankmsg[hiA]= %f", (float)tankmsg["hiA"]);
  Serial.println(buffer);

}
void copyJSONmsgtoStruct(int t)
{
  // int t = tankmsg["t"];

  tanks[t].depth = tankmsg["d"];
  tanks[t].vCM =   tankmsg["vCM"];
  tanks[t].maxVolume = tankmsg["mV"];
  tanks[t].liquidDepth = tankmsg["lD"];
  tanks[t].liquidDepthAvg = tankmsg["lDAvg"];
  tanks[t].liquidVolume = tankmsg["lV"];
  tanks[t].liquidVolumeAvg = tankmsg["lVAvg"];
  tanks[t].loAlarm = tankmsg["loA"];
  tanks[t].hiAlarm = tankmsg["hiA"];
  tanks[t].alarmFlags_prev = tanks[t].alarmFlags;
  tanks[t].alarmFlags = tankmsg["aF"];

  if (DEBUG)
  {
    printTimestamp();
    Serial.println();
    Serial.print("in copyJSONmsgtoStruct()");
    Serial.println();
    displayTankData();
  }
}

int setTankSlot()
{
  int slot = -1;
  bool noSlot = true;

  if (MULTISENSORNODES)
  {
    for (int t = 0; t <= NUMTANKS - 1; t++)       // make this a more approriate conditional loop
    {
      if (tanks[t].nodename == "")
      {
        slot = t;
        
        tanks[slot].nodename = (tankmsg["n"]);  
        tanks[slot].MAC = tankmsg["MAC"];   
        tanks[slot].MACTankNum = tankmsg["t"];
        noSlot = false;
      }
    }

    if (noSlot)
    {
      blynkTimestamp();
      Serial.print("\nNo tank slot available for registration message from ");
      Serial.print(nodename);
      return (-1);
    }
  }
  else
  {
    slot = tankmsg["t"];
    if ((slot < 0) || (slot > NUMTANKS))
    {
      blynkTimestamp();
      Serial.print("\nInvalid tank number received ");
      Serial.print(slot);
      Serial.print("from ");
      Serial.println(nodename);
      return (-1);
    }
  }
  return (slot);
}

int mapToTank()
{

  // modify to make make a fixed len char array
  
  const char* MAC = "";
  int MACTankNum = 0;
  int slot = -1;
  
  MAC = tankmsg["MAC"];
  MACTankNum = tankmsg["t"];

  if (MULTISENSORNODES)
  {
    for (int t = 0; t <= NUMTANKS - 1; t++)
    {
      if ((tanks[t].MAC == MAC) && (tanks[t].MACTankNum == MACTankNum))
      {
        slot = t;
        return (slot);
      }
    }
  }
  else
  {
    slot = tankmsg["t"];
    if (DEBUG)
    {
          blynkTimestamp();
          Serial.print("\nmapToTank, slot = ");
          Serial.println(slot);
    }
    if ((slot < 0) || (slot > NUMTANKS))
    {
      blynkTimestamp();
      Serial.print("\nInvalid tank number received ");
      Serial.print(slot);
      Serial.print("from ");
      Serial.println(nodename);
      return (-1);
    }
  }
  return (slot);
}


int handleMQTTmsg(char* topic, byte * payload, unsigned int length)
{
  int t = -1;
  char msgType;
  const char* tmpmsg = "";
  DeserializationError error;

  error = deserializeJson(tankmsg, payload, MAXJSONSIZE);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
   // return(-1);
  }
  else
  {
    dumpTankmsg();
    t = mapToTank();
    Serial.print("t = ");
    Serial.println(t);
    if (t != -1){
      copyJSONmsgtoStruct(t);
      displayTankData();
    }

  }
  
  if (DEBUG)
  {
    printTimestamp();
    Serial.print("\nMessage arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i = 0; i < length; i++)
    {
      Serial.print((char)payload[i]);
    }
    Serial.println();
  }

        blynkTimestamp();
        Serial.print("\nmessage type received in tankmsg[m] [");
        Serial.print((char)tankmsg["m"]);
        
  msgType = tankmsg["m"];
        blynkTimestamp();
        Serial.print("\nmessage type [");
        Serial.print(msgType);
  nodename = tankmsg["n"];

  switch (msgType)
  {
    case ALARM:
      {
        blynkTimestamp();
        Serial.print("\nAlarm message received from ");
        Serial.println(nodename);
        Serial.print("\nAlarm message: ");
        tmpmsg = tankmsg["msg"];
        Serial.println(tmpmsg);
      }

    case INFO:
      {
        blynkTimestamp();
        Serial.print("\nInfo message received from ");
        Serial.println(nodename);
        Serial.print("\nInfo message: ");
        tmpmsg = tankmsg["msg"];
        Serial.println(tmpmsg);
      }

    case ERR:
      {
        blynkTimestamp();
        Serial.print("\nError message received from ");
        Serial.println(nodename);
        Serial.print("\nError message: ");
        tmpmsg=tankmsg["msg"];
        Serial.println(tmpmsg);
      }

    case REGISTER:
      {
        blynkTimestamp();
        Serial.print("\nRegistration message received from ");
        Serial.println(nodename);

        t = setTankSlot();
        if (t >= 0)
        {
          blynkTimestamp();
          Serial.print("\nRegistration successful from ");
          Serial.println(nodename);
        }
        else
        {
          blynkTimestamp();
          Serial.print("\nRegistration successful from ");
          Serial.println(nodename);
        }
      }

    case SENSORDATA:
      {
        t = mapToTank();
        if (t >= 0)
        {
          copyJSONmsgtoStruct(t);
          if (tanks[t].alarmFlags != 0)
          {
            handleAlarm(t);
          }
          else if (tanks[t].alarmFlags_prev != 0)
          {
            clearAlarm(CLEARALARMS, t);
          }
        }
        else
        {
          blynkTimestamp();
          Serial.print("\nInvalid sensor data message from ");
          Serial.println(nodename);
        }
      }

    default:
      {
        blynkTimestamp();
        Serial.print("\nUnsupported message type [");
        Serial.print(msgType);
        Serial.print("] received from ");
        Serial.println(nodename);
      }
  }
}

void splashScreen()
{
  std::string splashMsg = "\n\nTank Monitor Manager Node \nBuild: Beta Test ";

  splashMsg += __DATE__;
  splashMsg += " ";
  splashMsg += __TIME__;
  splashMsg += "\nSite: ";
  splashMsg += SITE;
  splashMsg += "\nNodename: ";
  splashMsg += nodename;
  splashMsg += "\nDEBUG = ";
  splashMsg += DEBUG;
  splashMsg += "\nNUMTANKS = ";
  splashMsg += NUMTANKS;

  Serial.println(splashMsg.c_str());
  blynkTerminal.clear();
  blynkTerminal.println(splashMsg.c_str());
  blynkTerminal.flush();
}

void setup()
{
  #define MAXNODENAME 30
  int i = 0;
  char tmp[MAXNODENAME];
  
  delay(3000);                                     // Initial delay to avoid problems
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(IOLED, OUTPUT);
  Serial.begin(115200);
  i=snprintf(tmp,MAXNODENAME, "MNTM-ESP8266-%X",ESP.getChipId());
  nodename = tmp;
  connectWiFi();
  setupNTP();
  printTimestamp();
  setupOTA();
  printTimestamp();
  setupMQTT(true, handleMQTTmsg);
  printTimestamp();
  Blynk.begin(auth, ssid, password);

  timer.setInterval(DISPLAYUPDATETIME, handleDisplayTimer);
  timer.setInterval(LEDFLASHTIME, handleLEDTimer);

  initTanks();
  displayTankData();

  blynkUpdateLED.off();
  blynkErrorLED.off();
  blynkAlarmLED.off();

  Serial.print("\n\n");
  Serial.print(nodename);
  Serial.print(" started on ");
  printTimestamp();
  Serial.print("\n\n");
  blynkTimestamp();
  printTimestamp();
}

void loop() {
  ArduinoOTA.handle();
  Blynk.run();
  timer.run();
  if (!client.connected()) {
    connectMQTT(true);
  }
  client.loop();
}
