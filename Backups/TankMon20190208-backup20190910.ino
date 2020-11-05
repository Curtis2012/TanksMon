 /*
   Tank_Mon

   2019-02-09 C.Collins Initial simple version to monitor 2 tanks. Plan to add more features later.

to start blynk server: from blynk scripts folder: 

cd ~/Arduino/libraries/Blynk/scripts
sudo ./blynk-ser.sh -c /dev/ttyACM0 -b 9600 -s blynk-cloud.com

*/
 

#include <BlynkSimpleStream.h>
#include <NewPing.h>
#define DEBUG 0
#define BLYNK 1
#define TANKPINGDELAY 5000

char auth[] = "989b37ebf16b4d7584dffbb8b8e9acfb";
WidgetLED blynkLED(V8);

const int numTanks = 2;
const int maxPingDistance = 300;
const int pingPeriod = 60000;

//int sonarPins[numTanks][2]= {{2, 3},{4,5}};  // Ping/echo pin pairs. Ping pins in row 1. Echo pins row 2
int sensorOffsets[numTanks] = {20,20}; // offset of sensor from max fill in cm

class tank {
  public:
    float radius = 0;
    float depth = 0;
    float maxVolume = 0;
    float waterDepth = 0;
    float waterVolume = 0;
    float delta = 0;

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
  //  int trigPin = 0;
  //  int echoPin = 0;
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
  //tanks = new tank[numTanks];
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
  int prevVolume = 0;
  
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
    prevVolume = tanks[t].waterVolume;
    tanks[t].waterVolume = (PI * (tanks[t].radius * tanks[t].radius) * (tanks[t].waterDepth)) / 1000;   // Compute volume in liters
    tanks[t].delta = tanks[t].waterVolume - prevVolume;
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
    blynkLED.on();
    delay(2000);
    blynkLED.off();   
}
void printTankData()
{
  
  for (int t = 0; t <= numTanks-1; t++)
  {
    Serial.println();
    Serial.print("Tank ");
    Serial.print(t);
    Serial.println();
    Serial.print("Tank Radius ");
    Serial.println(tanks[t].radius);
    Serial.print("Tank Depth ");
    Serial.println(tanks[t].depth);
    Serial.print("Max Water Volume ");
    Serial.println(tanks[t].maxVolume);
    Serial.print("Actual Water Depth ");
    Serial.println(tanks[t].waterDepth);
    Serial.print("Actual Water Volume ");
    Serial.println(tanks[t].waterVolume);
    Serial.print("Volume delta  ");
    Serial.println(tanks[t].delta);
    Serial.flush();
  }
}

void setup()
{
  delay(5000);         // Always do initial delay to avoid Arduino upload problems
  Serial.begin(9600);
  
  if (BLYNK)
  {
    Blynk.begin(Serial, auth);
  }
  
  if (DEBUG)
  {
    Serial.println("Tanks Mon V0.5");
    Serial.println();
    Serial.flush();
  }
  initTanks();

  initSonars();
  pingTanks();         // do initial ping
  // sonars[0].timer_ms(pingPeriod, pingTanks); // not working...debug...delay for now

}

void loop() {

  if (BLYNK) Blynk.run();
  delay(TANKPINGDELAY);
  pingTanks();
  if (BLYNK) blynkTankData();
  

}
