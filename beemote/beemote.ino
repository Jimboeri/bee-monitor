
// Sketch to send regular temp & humidity readings to a gateway
// System is set up at the top of a bee hive
// Copyright Jim West (2016)

#include <RFM69.h>    //get it here: https://www.github.com/lowpowerlab/rfm69
#include <RFM69_ATC.h>//get it here: https://www.github.com/lowpowerlab/rfm69
#include <DHT.h>      // used to access the DHT22 temp & humidity sensor
#include <EEPROM.h>   // used to store parameters, in specific the time interval between reading transmissions
#include <LowPower.h> // used to power down/sleep the unit to save power
#include <radio_struct.h> // library to hold the radio packet structure
#include "HX711.h"    // library for the HX711 ADC that connects to the scale
#include <OneWire.h>  // library to connect to DS18B20 thermometer

//*********************************************************************************************
//************ MOTEINO specific settings
//*********************************************************************************************
#define NODEID      7    //must be unique for each node on same network (range up to 254, 255 is used for broadcast)
#define NETWORKID   100   //the same on all nodes that talk to each other (range up to 255)
#define GATEWAYID   1
//Match frequency to the hardware version of the radio on your Moteino (uncomment one):
#define FREQUENCY   RF69_433MHZ
//#define FREQUENCY   RF69_868MHZ
//#define FREQUENCY   RF69_915MHZ
#define ENCRYPTKEY  "TheWildWestHouse" //exactly the same 16 characters/bytes on all nodes!
#define IS_RFM69HW  //uncomment only for RFM69HW! Leave out if you have RFM69W!

#define ISDS18B20   // comment out if DS18B20 is not connected

//#define INITIAL_SETUP // uncomment this for an initial setup of a moteino

#define DHT_DEVICE    101
#define LED_DEVICE    3
#define SCALE_DEVICE  11
#define DS_TEMP_DEVICE 21
#define LED           9// Moteinos have LEDs on D9
#define BATTERY_CHECK A3

//*********************************************************************************************
// EEPROM Parameter offsets
//*********************************************************************************************
#define PARAM_DHT_PERIOD 1

#define SCALE_OFFSET 20
#define SCALE_FACTOR 24
#define SCALE_TEMP_FACTOR 28

#define RADIO_NETWORK 101
#define RADIO_NODE 102
#define RADIO_GATEWAY 103
#define RADIO_ENCRYPT 105


//**********************************************************************************************
// Radio transmission specific settings
// ******************************************************************************************
radioPayload receiveData, sendData;

#ifdef ENABLE_ATC
RFM69_ATC radio;
#else
RFM69 radio;
#endif

byte radio_network, radio_node;
byte radio_gateway;
char buff[20];
byte sendSize = 0;
boolean requestACK = false;
char radio_encrypt[16];

//*********************************************************************************************
// Serial channel settings
//*********************************************************************************************
#define SERIAL_BAUD   115200
#define BATTERY_FACTOR  150.0

//**********************************************************************************************
//*** Definitions for Sensors
//**********************************************************************************************

//***** DHT22 settings
#define DHTPIN 7     // what digital pin we're connected to

DHT dht(DHTPIN, DHT22);

// device 101 is temperature stored as DHT_DEVICE
unsigned long dht_period = 45000;   //send data every X milliseconds
unsigned long dht_period_time;      //seconds since last period
unsigned long t1 = 0L;
float h, t;

//************************************************************
// LED is device 3
// ***********************************************************
int ledStatus = 0;    // initially off

//************************************************************
// Scale is device 11
// ***********************************************************
#define HX711_DOUT    A1
#define HX711_PD_SCK  A0
HX711 scale(HX711_DOUT, HX711_PD_SCK);    // parameter "gain" is omited; the default value 128 is used by the library
long scale_offset = -357871;              // the offset is the '0' setting for the scale
float scale_factor = 21.6166f;            // the factor is a multiplier to convert HX711 output to grams
float temperature_factor = 0.00051f;       // this factor is a multiplier to attempt to correct for temperature variations

//************************************************************
// DS18B20 thermometer is device 21
// ***********************************************************
//#define DS18B20_DIN    6
//OneWire  ds(DS18B20_DIN);    // parameter "gain" is omited; the default value 128 is used by the library
//float ext_temp;

//**********************************
// General declarations
//**********************************
String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete
unsigned long requestID;
unsigned long sleepTimer = 0L;
int downTimer = 0;
int counter = 0;
int radioSent = 0;              // this is a flag to determine if a update has been sent in this radio opportunity

//*************************************************************
//***  SETUP Section
//*************************************************************
void setup() {
  //************************************************************
  // enable the serial channel
  //************************************************************
  Serial.begin(SERIAL_BAUD);

  //************************************************************
  // set up the radio
  //************************************************************

#ifdef INITIAL_SETUP
  EEPROM.put(RADIO_NETWORK, NETWORKID);        // temp to set up network
  EEPROM.put(RADIO_NODE, NODEID);              // temp to set up node
  EEPROM.put(RADIO_GATEWAY, GATEWAYID);        // temp to set up gateway
  EEPROM.put(RADIO_ENCRYPT, ENCRYPTKEY);       // temp to set up encryption key
#endif
  EEPROM.get(RADIO_NETWORK, radio_network);      // get network
  EEPROM.get(RADIO_NODE, radio_node);            // get node
  EEPROM.get(RADIO_GATEWAY, radio_gateway);      // get gateway
  EEPROM.get(RADIO_ENCRYPT, radio_encrypt);      // get encryption key

  Serial.print("Radio network = ");
  Serial.println(radio_network);
  Serial.print("Radio node = ");
  Serial.println(radio_node);
  Serial.print("Radio gateway = ");
  Serial.println(radio_gateway);
  Serial.print("Radio password = ");
  char y;
  for (int x = 0; x < 16; x++) {
    y = EEPROM.read(RADIO_ENCRYPT + x);
    Serial.print(y);
    radio_encrypt[x] = y;
  }

  radio.initialize(FREQUENCY, radio_node, radio_network);
#ifdef IS_RFM69HW
  radio.setHighPower(); //uncomment only for RFM69HW!
#endif
  radio.encrypt(radio_encrypt);

  //Auto Transmission Control - dials down transmit power to save battery (-100 is the noise floor, -90 is still pretty good)
  //For indoor nodes that are pretty static and at pretty stable temperatures (like a MotionMote) -90dBm is quite safe
  //For more variable nodes that can expect to move or experience larger temp drifts a lower margin like -70 to -80 would probably be better
  //Always test your ATC mote in the edge cases in your own environment to ensure ATC will perform as you expect
  //#ifdef ENABLE_ATC
  //radio.enableAutoPower(-70);
  //Serial.println("RFM69_ATC Enabled (Auto Transmission Control)\n");
  //#endif

  char buff[50];
  sprintf(buff, "\nTransmitting at %d Mhz...", FREQUENCY == RF69_433MHZ ? 433 : FREQUENCY == RF69_868MHZ ? 868 : 915);
  Serial.println(buff);

  sendData.nodeID = radio_node;    // This node id should be the same for all devices

  //************************************************************
  //**Setup for DHT
  //************************************************************
  dht.begin();      // Initialise the DHT module
  dht_period_time = millis();  //seconds since last period

#ifdef INITIAL_SETUP
  EEPROM.put(PARAM_DHT_PERIOD, dht_period); // temp to set up interval
#endif
  EEPROM.get(PARAM_DHT_PERIOD, t1);      // start delay parameter
  if (t1 < 4000000000)                  // a realistic delay interval
    dht_period = t1;
  else {
    EEPROM.put(PARAM_DHT_PERIOD, dht_period); // should only happen on initial setup
    Serial.print("Initial setup of ");
  }
  Serial.print("Delay between DHT reports is ");
  Serial.print(dht_period);
  Serial.println(" millseconds");

  //************************************************************
  //set up of LED
  //************************************************************
  pinMode(LED, OUTPUT);

  //************************************************************
  //**Setup for HX711 based scale
  //************************************************************

  Serial.println("Before setting up the scale:");
  Serial.print("read average raw output: \t\t");
  Serial.println(scale.read_average(20));       // print the average of 20 readings from the ADC

#ifdef INITIAL_SETUP
  EEPROM.put(SCALE_OFFSET, scale_offset);     // temp to set up initial offset not needed when in operation
  EEPROM.put(SCALE_FACTOR, scale_factor);     // temp to set up initial factor not needed when in operation
  EEPROM.put(SCALE_TEMP_FACTOR, temperature_factor);     // temp to set up initial factor not needed when in operation
#endif
  EEPROM.get(SCALE_OFFSET, scale_offset);       // scale offset

  // temp - tare scale on start up
  scale_offset = scale.read_average(20);
  Serial.print("Initial scale offset: \t\t");
  Serial.println(scale_offset);

  EEPROM.get(SCALE_FACTOR, scale_factor);       // scale factor parameter
  Serial.print("Initial scale factor: \t\t");
  Serial.println(scale_factor);

  EEPROM.get(SCALE_TEMP_FACTOR, temperature_factor);       // scale factor parameter
  Serial.print("Initial temperature factor: \t");
  Serial.println(temperature_factor * 1000);

  scale.set_scale(scale_factor);                // this value is obtained by calibrating the scale with known weights
  scale.set_offset(scale_offset);               // Initialise the scale

}
//************************************************
// End of Setup
//************************************************


void loop() {
  //process any serial input
  serialEvent();            // call the function
  if (stringComplete)       // keep all serial processing in one place
    process_serial();

  //check for any received packets
  if (radio.receiveDone())
  {
    Serial.println("Radio packet received");
    process_radio();
  }

  if (radioSent == 0)
  {
    h = dht.readHumidity();
    t = dht.readTemperature();
    Serial.print(t);
    Serial.println(" degrees");
    send_temp(t, h);

    //ext_temp = ds_temp();
    //send_dstemp(ext_temp);

    scale.power_up();
    send_mass(20);
    scale.power_down();             // put the ADC in sleep mode

    radioSent = 1;
    sleepTimer = millis() + 5000;
  }

  if (sleepTimer < millis())
  {
    downTimer = (dht_period / 1000) - 5;
    Serial.print("Going to sleep for ");
    Serial.print(downTimer);
    Serial.println(" seconds");
    delay(100);
    radio.sleep();

    counter = 0;
    while (counter < downTimer)
    {
      LowPower.powerDown(SLEEP_1S, ADC_OFF, BOD_OFF);
      counter += 1;
    }
    //delay(2000);

    Serial.println("Wake up");
    radioSent = 0;
  }

}

// *************************************************************************************************
void serialEvent() {
  while (Serial.available()) { // keep on reading while info is available
    // get the new byte:
    char inByte = Serial.read();

    // add it to the inputString:
    if ((inByte >= 65 && inByte <= 90) || (inByte >= 97 && inByte <= 122) || (inByte >= 48 && inByte <= 57) || inByte == 43 || inByte == 44 || inByte == 46 || inByte == 61 || inByte == 63) {
      inputString.concat(inByte);
    }
    if (inByte == 10 || inByte == 13) {
      // user hit enter or such like
      Serial.println(inputString);
      stringComplete = true;
    }
  }
}

// ************************************************************************************************
void process_serial()
{

  if (inputString == "r") //d=dump register values
    radio.readAllRegs();
  if (inputString == "E") //E=enable encryption
    radio.encrypt(radio_encrypt);
  if (inputString == "e") //e=disable encryption
    radio.encrypt(null);

  if (inputString == "T") //T = set Tare on scale
    scale_tare();
  if (inputString == "R") //e=disable encryption
  {
    Serial.print("Query Response - base load cell offset is ");
    Serial.println(scale_offset);
    Serial.print("Query Response - temperature conversion factor is ");
    Serial.println(temperature_factor, 5);
    Serial.print("Query Response - mass conversion factor is ");
    Serial.println(scale_factor);
  }

  // clear the string:
  inputString = "";
  stringComplete = false;
}

// **********************************************************************************************
void process_radio()
{
  Serial.print('['); Serial.print(radio.SENDERID, DEC); Serial.print("] ");
  for (byte i = 0; i < radio.DATALEN; i++)
    Serial.print((char)radio.DATA[i]);
  Serial.print("   [RX_RSSI:"); Serial.print(radio.RSSI); Serial.print("]");

  if (radio.ACKRequested())
  {
    radio.sendACK();
    Serial.print(" - ACK sent");
  }

  Serial.println();
  Serial.println("Data received");
  receiveData = *(radioPayload*)radio.DATA;
  printTheData(receiveData);
  requestID = receiveData.req_ID;

  sendData.nodeID = 1;    // always send to the gateway node

  if (receiveData.nodeID = radio_node)  // only if the message is for this node
  {
    switch (receiveData.action) {
      case 'P':   // parameter update
        sendData.req_ID = requestID;
        sendData.action = 'C';
        sendData.result = 0;
        if (receiveData.deviceID == DHT_DEVICE)  // DHT22
        {
          Serial.print("Parameter update request ");
          Serial.print(receiveData.float1);
          Serial.println(" secs");
          sendData.deviceID = DHT_DEVICE;
          sendData.float1 = receiveData.float1;
          sendData.float2 = 0;
          //sendData.result = 1;
          dht_period = receiveData.float1 * 1000;
          radio.sendWithRetry(radio_gateway, (const void*)(&sendData), sizeof(sendData));
          Serial.print("Temp regular update changed to ");
          Serial.print(receiveData.float1);
          Serial.println(" seconds");
          EEPROM.put(PARAM_DHT_PERIOD, dht_period);
        }
        if (receiveData.deviceID == SCALE_DEVICE) // there are a couple of parameters on the scale that can be adjusted
        {
          if (receiveData.float1 == 1.0)          // This command reset the tare on the scale
          {
            Serial.print("Tare update request ");
            scale_tare();
            sendData.deviceID = SCALE_DEVICE;
            sendData.float1 = scale_offset;
            sendData.float2 = 0;
            radio.sendWithRetry(radio_gateway, (const void*)(&sendData), sizeof(sendData));
          }
          if (receiveData.float1 == 2.0)          // This command updates the temperature coefficient
          {
            Serial.println("Update of temperature coefficient ");
            temperature_factor = receiveData.float2;
            EEPROM.put(SCALE_TEMP_FACTOR, temperature_factor);
            sendData.deviceID = SCALE_DEVICE;
            sendData.float1 = temperature_factor;
            sendData.float2 = 0;
            radio.sendWithRetry(radio_gateway, (const void*)(&sendData), sizeof(sendData));
            Serial.print("New value (x1000)");
            Serial.println(temperature_factor * 1000);
          }
          if (receiveData.float1 == 3.0)          // This command updates the mass coefficient
          {
            Serial.println("Update of mass coefficient ");
            scale_factor = receiveData.float2;
            EEPROM.put(SCALE_FACTOR, scale_factor);
            sendData.deviceID = SCALE_DEVICE;
            sendData.float1 = scale_factor;
            sendData.float2 = 0;
            radio.sendWithRetry(radio_gateway, (const void*)(&sendData), sizeof(sendData));
            Serial.print("New value ");
            Serial.println(scale_factor);
            scale.set_scale(scale_factor);
          }
        }
        break;
      case 'Q':   // parameter query
        sendData.req_ID = requestID;
        sendData.action = 'R';
        sendData.result = 0;
        if (receiveData.deviceID == DHT_DEVICE)    // DHT22
        {
          sendData.deviceID = DHT_DEVICE;
          sendData.float1 = dht_period / 1000;
          radio.sendWithRetry(radio_gateway, (const void*)(&sendData), sizeof(sendData));
          Serial.print("Query - DHT period is ");
          Serial.print(sendData.float1);
          Serial.println(" seconds");
        }
        if (receiveData.deviceID == SCALE_DEVICE) // there are a couple of parameters on the scale that need to be reported on
        {
          sendData.deviceID = SCALE_DEVICE;
          sendData.float1 = 1;
          sendData.float2 = scale_offset;
          radio.sendWithRetry(radio_gateway, (const void*)(&sendData), sizeof(sendData));
          Serial.print("Query Response - base load cell offset is ");
          Serial.println(sendData.float2);
          sendData.float1 = 2;
          sendData.float2 = temperature_factor;
          radio.sendWithRetry(radio_gateway, (const void*)(&sendData), sizeof(sendData));
          Serial.print("Query Response - temperature conversion factor is ");
          Serial.println(sendData.float2, 5);
          sendData.float1 = 3;
          sendData.float2 = scale_factor;
          radio.sendWithRetry(radio_gateway, (const void*)(&sendData), sizeof(sendData));
          Serial.print("Query Response - mass conversion factor is ");
          Serial.println(sendData.float2);
        }
        break;
      case 'R':    // information request
        h = dht.readHumidity();
        t = dht.readTemperature();
        send_temp(t, h);

        //ext_temp = ds_temp();
        //send_dstemp(ext_temp);

        //scale.power_up();
        //send_mass(20);
        //scale.power_down();
        break;
      case 'A':
        if (receiveData.deviceID == LED_DEVICE)
        {
          Serial.print("LED updated to ");
          if (receiveData.float1 == 1.0)
          {
            ledStatus = 1;
            digitalWrite(LED, HIGH);
            Serial.println("ON");
          }
          else
          {
            ledStatus = 0;
            digitalWrite(LED, LOW);
            Serial.println("OFF");
          }
          sendData.req_ID = requestID;
          sendData.deviceID = LED_DEVICE;
          sendData.instance = receiveData.instance;
          sendData.action = 'C';
          sendData.result = 0;
          sendData.float1 = ledStatus;
          radio.sendWithRetry(radio_gateway, (const void*)(&sendData), sizeof(sendData));

        }
        break;
      case 'S':   // Status request
        Serial.println(receiveData.nodeID);
        sendData.req_ID = requestID;
        sendData.action = 'C';
        sendData.result = 0;
        sendData.float1 = battCheck();
        sendData.float2 = radio.RSSI;
        radio.sendWithRetry(radio_gateway, (const void*)(&sendData), sizeof(sendData));
        Serial.print("Vcc battery level is ");
        Serial.print(sendData.float1 / 1000);
        Serial.println(" volts");
        Serial.print("Radio signal is ");
        Serial.println(sendData.float2);

        break;
    }   // end swicth case for action
  }   // end if when checking for NODEID
}

// ***********************************************************************************************
void send_temp(float t, float h)
{
  if (isnan(t) || isnan(h)) {
    Serial.println("Failed to read from DHT");
  }
  else
  {
    Serial.print("Humidity: ");
    Serial.print(h);
    Serial.print(" %\t");
    Serial.print("Temperature: ");
    Serial.print(t);
    Serial.println(" *C");

    //send data
    sendData.deviceID = DHT_DEVICE; // DHT22
    sendData.instance = 1;
    sendData.req_ID = millis();
    sendData.action = 'I';
    sendData.result = 0;
    sendData.float1 = t;
    sendData.float2 = h;
    radio.sendWithRetry(radio_gateway, (const void*)(&sendData), sizeof(sendData));
    printTheData(sendData);
    Serial.println("Transmitted");
  }

  // Serial.println();
}
// ***********************************************************************************************
void send_dstemp(float t)
{
  if (isnan(t)) {
    Serial.println("Failed to read from DS18B20");
  }
  else
  {
    Serial.print("DS18B20 Temperature: ");
    Serial.print(t);
    Serial.println(" *C");

    //send data
    sendData.deviceID = DS_TEMP_DEVICE; // DS18B20
    sendData.instance = 1;
    sendData.req_ID = millis();
    sendData.action = 'I';
    sendData.result = 0;
    sendData.float1 = t;
    sendData.float2 = 0;
    radio.sendWithRetry(radio_gateway, (const void*)(&sendData), sizeof(sendData));
    printTheData(sendData);
  }

  Serial.println();
}

// ***********************************************************************************************
void send_mass(float t)
{
  Serial.print("read average raw output: \t\t");
  Serial.println(scale.read_average(50));       // print the average of 20 readings from the ADC

  float m = scale.get_units(10);
  Serial.print("mass: ");
  Serial.print(m);
  Serial.println("g\t");

  float td = t - 20;
  float wt_corr = m * t * temperature_factor;

  Serial.print("temp: ");
  Serial.print(t);
  Serial.print(" temp diff: ");
  Serial.print(td);
  Serial.print(" mass correction: ");
  Serial.print(wt_corr);
  Serial.print(" adjusted mass: ");
  Serial.println(m + wt_corr);

  //send data
  sendData.deviceID = SCALE_DEVICE; // DHT22
  sendData.instance = 1;
  sendData.req_ID = millis();
  sendData.action = 'I';
  sendData.result = 0;
  sendData.float1 = m + wt_corr;
  sendData.float2 = battCheck();
  Serial.print("Battery level ");
  Serial.print(sendData.float2);
  if (radio.sendWithRetry(radio_gateway, (const void*)(&sendData), sizeof(sendData)))
    Serial.println(" ok!");
  else Serial.println(" nothing");
  printTheData(sendData);
  Serial.println("Transmitted");
}
// ***********************************************************************************************
void scale_tare()
{
  Serial.println("Scale TARE function started");
  scale.power_up();
  scale_offset = scale.read_average(20);      // get the current load cell value
  Serial.println("Scale read complete");
  delay(100);
  EEPROM.put(SCALE_OFFSET, scale_offset);     // save the off set for future
  Serial.println("EEProm updated");
  Serial.print("Offset changed to: \t\t"); //print the value
  Serial.println(scale_offset);

  scale.set_offset(scale_offset);             // Initialise the scale
  Serial.println("Scale TARE request completed");
}

// ***************************************************************************************************
float battCheck()
{
  int bLevel = analogRead(BATTERY_CHECK);
  float v = bLevel / BATTERY_FACTOR;
  return v;
}

// *************************************************************************************************
void printTheData(radioPayload &myData)
{
  Serial.print("NodeID=");
  Serial.print(myData.nodeID);
  Serial.print(", deviceID=");
  Serial.print(myData.deviceID);
  Serial.print(", instance=");
  Serial.print(myData.instance);
  Serial.print(", action=");
  Serial.print(myData.action);
  Serial.print(", result=");
  Serial.print(myData.result);
  Serial.print(", req_ID=");
  Serial.print(myData.req_ID);
  Serial.print(", float1=");
  Serial.print(myData.float1);
  Serial.print(", float2=");
  Serial.println(myData.float2);
}

float ds_temp()
{
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];
  float celsius;

  //ds.reset_search();
  //if ( !ds.search(addr)) {
  //  Serial.println("No OneWire device found.");
  //  Serial.println();
  //  return (0);
  //}

  //if (OneWire::crc8(addr, 7) != addr[7]) {
  //  Serial.println("CRC is not valid!");
  //  return (0);
  //}

  // the first ROM byte indicates which chip
  switch (addr[0]) {
    case 0x10:
      //Serial.println("  Chip = DS18S20");  // or old DS1820
      type_s = 1;
      break;
    case 0x28:
      //Serial.println("  Chip = DS18B20");
      type_s = 0;
      break;
    case 0x22:
      //Serial.println("  Chip = DS1822");
      type_s = 0;
      break;
    default:
      //Serial.println("Device is not a DS18x20 family device.");
      return (0);
  }

  //ds.reset();
  //ds.select(addr);
  //ds.write(0x44, 1);        // start conversion, with parasite power on at the end

  //delay(1000);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.

  //present = ds.reset();
  //ds.select(addr);
  //ds.write(0xBE);         // Read Scratchpad

  //for ( i = 0; i < 9; i++) {           // we need 9 bytes
  //  data[i] = ds.read();
  //}

  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }
  celsius = (float)raw / 16.0;
  return (celsius);
}
