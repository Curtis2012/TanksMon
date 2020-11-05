/*
  Tanks_Mon

  2019-09-15 C.Collins Initial Arduino version supporrting up to 4 tanks
  2019-10-10 Ported to NodeMCU board and converted to Blynk standalone via WiFi
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
  String notification = "";

  globalAlarmFlag = true;

  if (tanks[tank].alarmFlags & HIALARM)
  {
    blynkTimestamp();
    notification = "HI ALARM! on Tank " + String(tank + 1);
    blynkTerminal.println();
    blynkTerminal.println(notification);
    blynkTerminal.flush();
    blynkAlarmLED.on();
    Blynk.notify(notification);
  }

  if (tanks[tank].alarmFlags & LOALARM)
  {
    blynkTimestamp();
    notification = "LO ALARM! on Tank " + String(tank + 1);
    blynkTerminal.println();
    blynkTerminal.println(notification);
    blynkTerminal.flush();
    blynkAlarmLED.on();
    Blynk.notify(notification);
  }

  if (tanks[tank].alarmFlags & MAXDEPTH)
  {
    blynkTimestamp();
    notification = "MAX DEPTH ALARM! on Tank " + String(tank + 1) + ", depth = " + String(tanks[tank].depth);
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

void copyJSONmsgtoStruct()
{
  int t = tankmsg["t"];

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

void handleMQTTmsg(char* topic, byte* payload, unsigned int length) {
  int t = -1;

  if (DEBUG)
  {
    printTimestamp();
    Serial.print("\nMessage arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println();
  }

  deserializeJson(tankmsg, payload, MAXJSONSIZE);
  copyJSONmsgtoStruct();

  t = tankmsg["t"];
  if (tanks[t].alarmFlags != 0)
  {
    handleAlarm(t);
  }
  else if (tanks[t].alarmFlags_prev != 0)
  {
    clearAlarm(CLEARALARMS, t);
  }

  if (DEBUG)
  {
    printTimestamp();
    Serial.print("\nDump tankmsg: ");
    serializeJson(tankmsg, Serial);
  }
}

void splashScreen()
{
  String splashMsg = "\n\nTank Monitor Manager Node \nBuild: Beta Test ";

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

  Serial.println(splashMsg);
  blynkTerminal.clear();
  blynkTerminal.println(splashMsg.c_str());
  blynkTerminal.flush();
}

void setup()
{
  delay(3000);                                     // Initial delay to avoid problems
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(IOLED, OUTPUT);
  Serial.begin(115200);
  nodename = "MNTM-ESP8266-" + String(ESP.getChipId(), HEX);
  splashScreen();
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
