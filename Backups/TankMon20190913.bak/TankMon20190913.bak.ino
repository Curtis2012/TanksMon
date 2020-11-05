 /*
   Tank_Mon

   2019-02-09 C.Collins Initial simple version to monitor 2 tanks. Plan to add more features later.

to start blynk server: from blynk scripts folder: 

cd ~/Arduino/libraries/Blynk/scripts
sudo ./blynk-ser.sh -c /dev/ttyACM0 -b 9600 -s blynk-cloud.com


TODO:

- modify to use timer events rather than delay() for better blynk/network reliability.
- change from tank dims to liters/cm of depth...this makes for simple support of all regular geomtric tanks (cylinder, square, rectangle...)
- add support for irregular tank shapes by using vector for liquid levels at configurable depth intervals.

*/
 

#include <BlynkSimpleStream.h>
#include <NewPing.h>
#define DEBUG 0

#define TANKPINGDELAY 5000L

char auth[] = "989b37ebf16b4d7584dffbb8b8e9acfb";  // Blynk auto token
WidgetLED blynkUpdateLED(V8);
WidgetLED blynkErrorLED(V9);
BlynkTimer timer;
WidgetTerminal blynkTerminal(V10);

const int numTanks = 2;
const int maxPingDistance = 400;

int sensorOffsets[numTanks] = {20,20};   // offset of sensor from max fill in cm

class tank {
  public:
    float radius = 0;
    float depth = 0;
    float maxVolume = 0;
    float waterDepth = 0;
    float waterVolume = 0;
    
    tank()
    {
      
    }

    tank(float radius, float depth)
    {
      this->radius=radius;
      this->depth=depth;
    }
};

tank tanks[numTanks]={tank(25,105), tank(25,105)};

class sonarClass : public NewPing {
  public:
    sonarClass(int trigPin, int echoPin, int maxDistance): NewPing(trigPin, echoPin, maxDistance)
    {

    };
    
    int offset = 0;
};

sonarClass sonars[numTanks] = {
  sonarClass(2,3, maxPingDistance),
  sonarClass(4,5, maxPingDistance)
};


void initTanks()
{
  if (DEBUG)
  {
    Serial.println();
    Serial.println("In initTanks");
    Serial.flush();
  }

  for (int t = 0; t <= numTanks-1; t++)
  {
    tanks[t].maxVolume = PI * ((tanks[t].radius * tanks[t].radius) * tanks[t].depth)/1000;
  }
  if (DEBUG)
  {
    printTankData();
  }
}

void initSonars()
{
  if (DEBUG)
  {
    Serial.println();
    Serial.println("In initSensors");
    Serial.flush();
  }
  
  for (int t = 0; t <= numTanks-1; t++)
  {
    sonars[t].offset = sensorOffsets[t];
    
    if (DEBUG)
    {
      Serial.println();
      Serial.println("Sonar offset");
      Serial.print(t);
      Serial.print(sonars[t].offset);
      Serial.flush();
  
    }
  }
}

void pingTanks()
{
  
  if (DEBUG)
  {
    Serial.println();
    Serial.println("In pingTanks");
    Serial.flush();
  }
  for (int t = 0; t <= numTanks-1; t++)
  {    
    int d = sonars[t].convert_cm(sonars[t].ping_median());
    
    if (DEBUG) 
    {
      Serial.println();
      Serial.print("t=");
      Serial.print(t);
      Serial.print(" ");
      Serial.print("d=");
      Serial.print(d);
    }
   
    if (d > tanks[t].depth) 
    {
      d = tanks[t].depth;
    }
     
    tanks[t].waterDepth = d;
    tanks[t].waterVolume = (PI * (tanks[t].radius * tanks[t].radius) * (tanks[t].waterDepth)) / 1000;   // Compute volume in liters

  }
  if (DEBUG) printTankData();
}

void blynkTankData()
{
    Blynk.virtualWrite(V0, tanks[0].waterDepth);
    Blynk.virtualWrite(V1, tanks[1].waterDepth);
    Blynk.virtualWrite(V2, 0);
    Blynk.virtualWrite(V3, 0);
    Blynk.virtualWrite(V4, tanks[0].waterVolume);
    Blynk.virtualWrite(V5, tanks[1].waterVolume);
    Blynk.virtualWrite(V6, 0);
    Blynk.virtualWrite(V7, 0); 
    blynkUpdateLED.on();
    blynkUpdateLED.off(); 
}

void printTankData()
{
  
  for (int t = 0; t <= numTanks-1; t++)
  {
    blynkTerminal.println();
    blynkTerminal.print("Tank ");
    blynkTerminal.print(t+1);
    blynkTerminal.println();
    blynkTerminal.print("Tank Radius ");
    blynkTerminal.println(tanks[t].radius);
    blynkTerminal.print("Tank Depth ");
    blynkTerminal.println(tanks[t].depth);
    blynkTerminal.print("Max Water Volume ");
    blynkTerminal.println(tanks[t].maxVolume);
    blynkTerminal.print("Actual Water Depth ");
    blynkTerminal.println(tanks[t].waterDepth);
    blynkTerminal.print("Actual Water Volume ");
    blynkTerminal.println(tanks[t].waterVolume);
    blynkTerminal.flush();
  }
}

void handleTimer()
{
  pingTanks();
  blynkTankData();
}

void setup()
{
  delay(3000);                                     // Initial delay to avoid Arduino upload problems
  
  Serial.begin(9600);
  Blynk.begin(Serial, auth);
  
  timer.setInterval(TANKPINGDELAY,handleTimer);   // setup timer interrupt handler

  blynkTerminal.clear();
  blynkTerminal.println("Tanks Mon V0.5");
  blynkTerminal.println();
  blynkTerminal.flush();
  blynkUpdateLED.off();
  blynkErrorLED.off();
 
  initTanks();
  initSonars();
  pingTanks();                                   // initial ping juset to get data for display
  printTankData();
}

void loop() {

  Blynk.run();
  timer.run();

}
