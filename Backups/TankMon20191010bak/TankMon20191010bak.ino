/*
  Tank_Mon

  2019-09-15 C.Collins Initial version supporrting up to 4 tanks
  2019-10-10 Ported to NodeMCU board and converted to Blynk standalone via WiFi
  
  TODO:

  - set unused tank V pins to 0 and set tank widget color to blackW
  - add hi/low tank alarms
  - add support for irregular tank shapes by using vector for liquid levels at configurable depth intervals.

*/

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <NewPingESP8266.h>

//#define DEBUG_ESP_PORT Serial

#ifdef DEBUG_ESP_PORT
#define DEBUG_MSG(...) DEBUG_ESP_PORT.printf( __VA_ARGS__ )
#else
#define DEBUG_MSG(...)
#endif

#define BLYNK_PRINT Serial
#define NUMTANKS 2
#define MAXPINGDISTANCE 400
#define DEBUG 0
#define IMPERIAL 0
#define CVTFACTORGALLONS 0.26417F
#define CVTFACTORINCHES  0.39370F
#define TANKPINGDELAY 15000L

char auth[] = "989b37ebf16b4d7584dffbb8b8e9acfb";  // Blynk auth token
char ssid[] = "Nexxt_582E74-2.4G";
char pass[] = "12345678";

WidgetLED blynkUpdateLED(V8);
WidgetLED blynkErrorLED(V9);
BlynkTimer timer;
WidgetTerminal blynkTerminal(V10);

class tank {
  public:
    float depth = 0;
    float vCM = 0;        // volume in liters per cm
    float maxVolume = 0;
    float liquidDepth = 0;
    float liquidVolume = 0;
    float lowAlarm = 0;
    float hiAlarm = 0;

    tank()
    {

    }

    tank(float depth, float vCM)
    {
      this->depth = depth;
      this->vCM = vCM;
    }

    tank(float depth, float vCM, float lowAlarm, float hiAlarm)
    {
      this->depth = depth;
      this->vCM = vCM;
      this->lowAlarm = lowAlarm;
      this->hiAlarm  = hiAlarm;
    }
};

tank tanks[NUMTANKS] = {
  tank(105, 0.5),
  tank(105, 0.5)
  
};

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

void initTanks()
{
  DEBUG_MSG("In initTanks \n");
  for (int t = 0; t <= NUMTANKS - 1; t++)
  {
    tanks[t].maxVolume = tanks[t].depth * tanks[t].vCM;
  }

 // clear tank widgets, hardcoded for now...
 
//  Blynk.virtualWrite(V0,0);
//  Blynk.virtualWrite(V1,0);
    Blynk.virtualWrite(V2,0);
    Blynk.virtualWrite(V3,0);
  
//  Blynk.setProperty(V1, "color", "#000000");
//  Blynk.setProperty(V0, "color", "#000000");
    Blynk.setProperty(V2, "color", "#000000");
    Blynk.setProperty(V3, "color", "#000000");  
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
    }

    tanks[t].liquidDepth = d;
    tanks[t].liquidVolume = tanks[t].liquidDepth * tanks[t].vCM;
   

  }
  
  if (DEBUG) displayTankData();

  digitalWrite(LED_BUILTIN, HIGH);
}


void blynkTankData()
{
  float cvtG = 1.0, cvtI = 1.0;
  
  DEBUG_MSG("In blynkTankData \n");

  blynkUpdateLED.on();
  
  if (IMPERIAL) {
    cvtG = CVTFACTORGALLONS;
    cvtI = CVTFACTORINCHES;
  }
  
  Blynk.virtualWrite(V0, (tanks[0].liquidDepth*cvtI));
  Blynk.virtualWrite(V1, (tanks[1].liquidDepth*cvtI));
  Blynk.virtualWrite(V4, (tanks[0].liquidVolume*cvtG));
  Blynk.virtualWrite(V5, (tanks[1].liquidVolume*cvtG));
 
  blynkUpdateLED.off();
}

void displayTankData()
{
  DEBUG_MSG("In displayTankData \n");
  
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
    blynkTerminal.print("Actual liquid volume ");
    blynkTerminal.println(tanks[t].liquidVolume);
    blynkTerminal.flush();
  }
}

void handleTimer()
{
  DEBUG_MSG("In handleTimer \n");
  pingTanks();
  blynkTankData();
}

void setup()
{
  delay(3000);                                     // Initial delay to avoid Arduino upload problems
  
  Serial.begin(9600);
  DEBUG_MSG("In handleTimer \n");
  
  Blynk.begin(auth, ssid, pass);

  timer.setInterval(TANKPINGDELAY, handleTimer);  // setup timer interrupt handler

  pinMode(LED_BUILTIN, OUTPUT);

  initTanks();
  pingTanks();                                    // initial ping just to get data for display

  blynkTerminal.clear();
  blynkTerminal.print(__DATE__);
  blynkTerminal.print(" ");
  blynkTerminal.print(__TIME__);
  blynkTerminal.println(" Tanks Mon V0.5");
  blynkTerminal.println();
  blynkTerminal.print("DEBUG = ");
  blynkTerminal.println(DEBUG);
  blynkTerminal.print("IMPERIAL = ");
  blynkTerminal.println(IMPERIAL);
  blynkTerminal.print("NUMTANKS = ");
  blynkTerminal.println(NUMTANKS);
  blynkTerminal.print("MAXPINGDISTANCE = ");
  blynkTerminal.println(MAXPINGDISTANCE);  
  blynkTerminal.print("TANKPINGDELAY = ");
  blynkTerminal.println(TANKPINGDELAY);
  blynkTerminal.println();
  blynkTerminal.flush();
                                
  displayTankData();
  blynkUpdateLED.off();
  blynkErrorLED.off(); 
  
}

void loop() {

  Blynk.run();
  timer.run();

}
