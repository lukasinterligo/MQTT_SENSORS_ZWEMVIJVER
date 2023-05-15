#include <WiFiEspAT.h>
#include <PubSubClient.h> 
IPAddress server(192, 168, 0, 223); 
WiFiClient espClient;
PubSubClient client(espClient); 
int status = WL_IDLE_STATUS;

#include "arduino_secrets.h"
///////please enter your sensitive data in the Secret tab/arduino_secrets.h
const char ssid[] = SECRET_SSID;    // your network SSID (name)
const char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)


// Emulate Serial1 on pins 6/7 if not present
#if defined(ARDUINO_ARCH_AVR) && !defined(HAVE_HWSERIAL1)
#include "SoftwareSerial.h"
SoftwareSerial Serial1(6, 7); // RX, TX
#define AT_BAUD_RATE 9600
#else
#define AT_BAUD_RATE 115200
#endif

//Setup TEMP
#include <OneWire.h>
#include <DallasTemperature.h>

// Data wire is conntec to the Arduino digital pin 4
#define ONE_WIRE_BUS 4

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);

//Setup PH
float calibration = 27; //change this value to calibrate
const int analogInPin = A2;
int sensorValue = 0;
unsigned long int avgValue;
float b;
int buf[10], temp;

//Setup TDS
#include <EEPROM.h>
#include "GravityTDS.h"
#define TdsSensorPin A1
GravityTDS gravityTds;
float tdsValue = 0;

//Setup Turbidity NTU
#include <Wire.h>
int Turbidity_Sensor_Pin = A0;
float Turbidity_Sensor_Voltage;
int samples = 600;
float ntu; // Nephelometric Turbidity Units

const char* tds_topic = "home/livingroom/tds";
const char* PH_topic = "home/livingroom/ph";
const char* Ntu_topic = "home/livingroom/ntu";
const char* temp_topic = "home/livingroom/temp";
const char* mqtt_username = "admin"; // MQTT username
const char* mqtt_password = "admin.123"; // MQTT password

void setup() {
  delay(5000);
Serial.begin(9600);
Serial1.begin(115200);
  while (!Serial);

  Serial1.begin(AT_BAUD_RATE);
WiFi.init(Serial1);

 if (WiFi.status() == WL_NO_MODULE) {
    Serial.println();
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }

  WiFi.disconnect(); // to clear the way. not persistent

  WiFi.setPersistent(); // set the following WiFi connection as persistent

  WiFi.endAP(); // to disable default automatic start of persistent AP at startup

  Serial.println();
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);

  int status = WiFi.begin(ssid, pass);

  if (status == WL_CONNECTED) {
    Serial.println();
    Serial.println("Connected to WiFi network.");
  } else {
    WiFi.disconnect(); // remove the WiFi connection
    Serial.println();
    Serial.println("Connection to WiFi network failed.");
  }
  //connect to MQTT server 
  client.setServer(server, 1883); 
  client.setCallback(callback); 

  gravityTds.setPin(TdsSensorPin);
  gravityTds.setAref(5.0);  //reference voltage on ADC, default 5.0V on Arduino UNO
  gravityTds.setAdcRange(1024);  //1024 for 10bit ADC;4096 for 12bit ADC
  gravityTds.begin();  //initialization
  pinMode(Turbidity_Sensor_Pin, INPUT);
  // Start up the library
  sensors.begin();
}

//print any message received for subscribed topic
void callback(char* topic, byte* payload, unsigned int length) { 
Serial.print("Message arrived ["); 
Serial.print(topic); 
Serial.print("] ");
 for (int i=0;i<length;i++) { 
Serial.print((char)payload[i]); 
} 
Serial.println(); 
} 

void loop() {
  
   // put your main code here, to run repeatedly:
 if (!client.connected()) { 
reconnect();
 }
 client.loop(); 
 
  // Call sensors.requestTemperatures() to issue a global temperature and Requests to all devices on the bus
  sensors.requestTemperatures(); 
  
  Serial.print("Celsius temperature: ");
  // Why "byIndex"? You can have more than one IC on the same bus. 0 refers to the first IC on the wire
  sensors.getTempCByIndex(0);
  Serial.println(sensors.getTempCByIndex(0)); 

    if (client.publish(temp_topic, String(sensors.getTempCByIndex(0)).c_str())){
        Serial.println("temperature sent");
      }

  //Loop PH
  for (int i = 0; i < 10; i++)
  {
    buf[i] = analogRead(analogInPin);
    delay(30);
  }
  for (int i = 0; i < 9; i++)
  {
    for (int j = i + 1; j < 10; j++)
    {
      if (buf[i] > buf[j])
      {
        temp = buf[i];
        buf[i] = buf[j];
        buf[j] = temp;
      }
    }
  }
  avgValue = 0;
  for (int i = 2; i < 8; i++)
    avgValue += buf[i];
  float pHVol = (float)avgValue * 5.0 / 1024 / 6;
  float phValue = -5.70 * pHVol + calibration;
  Serial.print("ph = ");
  Serial.println(phValue);
        if (client.publish(PH_topic, String(phValue).c_str())){
        Serial.println("ph sent");
        }

  //Loop TDS
  //temperature = readTemperature();  //add your temperature sensor and read it
  gravityTds.setTemperature(sensors.getTempCByIndex(0));  // set the temperature and execute temperature compensation
  gravityTds.update();  //sample and calculate
  tdsValue = gravityTds.getTdsValue();  // then get the value
  Serial.print("TDS = ");
  Serial.print(tdsValue, 0);
  Serial.println("ppm");
        if (client.publish(tds_topic, String(tdsValue).c_str())){
        Serial.println("tds sent");
        }

  //Loop turbidity NTU
  Turbidity_Sensor_Voltage = 0;
  /* its good to take some samples and take the average value. This can be quite handy
      in situations when the values fluctuates a bit. This way you can take the average value
      i am going to take 600 samples
  */
  for (int i = 0; i < samples; i++)
  {
    Turbidity_Sensor_Voltage += ((float)analogRead(Turbidity_Sensor_Pin) / 1023) * 5;
  }

  Turbidity_Sensor_Voltage = Turbidity_Sensor_Voltage / samples;
  // uncomment the following two statments to check the voltage.
  // if you see any variations, take necessary steps to correct them
  // once you are satisfied with the voltage value then again you can comment these lines

  //Serial.println("Voltage:");
  //Serial.println(Turbidity_Sensor_Voltage);

  Turbidity_Sensor_Voltage = round_to_dp(Turbidity_Sensor_Voltage, 2);
  if (Turbidity_Sensor_Voltage < 2.5) {
    ntu = 3000;
  } else {
    ntu = -1120.4 * square(Turbidity_Sensor_Voltage) + 5742.3 * Turbidity_Sensor_Voltage - 4352.9;
  }

  //Serial.print(Turbidity_Sensor_Voltage);
  //Serial.println(" V");

  Serial.print("Turbidity = ");
  Serial.print(ntu);
  Serial.println(" NTU");
        if (client.publish(Ntu_topic, String(ntu).c_str())){
        Serial.println("ntu sent");
        }
  delay(5000); 
}

void reconnect() { 
// Loop until we're reconnected
 while (!client.connected()) {
 Serial.print("Attempting MQTT connection..."); 
 Serial.print(server);
// Attempt to connect, just a name to identify the client
 if (client.connect("arduinoClient",mqtt_username, mqtt_password)) {
 Serial.println(" connected"); // Once connected, publish an announcement... 
 } else {
 Serial.print("failed, rc=");
 Serial.print(client.state());
 Serial.println(" try again in 5 seconds");
 // Wait 5 seconds before retrying
 delay(5000);
    }
  }
 }
 float round_to_dp( float in_value, int decimal_place ){
  float multiplier = powf( 10.0f, decimal_place );
  in_value = roundf( in_value * multiplier ) / multiplier;
  return in_value;

  delay(5000);
}