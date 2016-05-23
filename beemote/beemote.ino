// Sketch to send regular temp & humidity readings to a gateway
// System is set up at the top of a bee hive
// Copyright Jim West (2016)

#include <RFM69.h>    //get it here: https://www.github.com/lowpowerlab/rfm69
#include <RFM69_ATC.h>//get it here: https://www.github.com/lowpowerlab/rfm69
#include <DHT.h>      // used to access the DHT22 temp & humidity sensor
#include <EEPROM.h>   // used to store parameters, in specific the time interval between reading transmissions
#include <LowPower.h> // used to power down/sleep the unit to save power
#include <radio_struct.h> // library to hold the radio packet structure

//*********************************************************************************************
//************ MOTEINO specific settings
//*********************************************************************************************
#define NODEID        3    //must be unique for each node on same network (range up to 254, 255 is used for broadcast)
#define NETWORKID     100  //the same on all nodes that talk to each other (range up to 255)
#define GATEWAYID     1
//Match frequency to the hardware version of the radio on your Moteino (uncomment one):
#define FREQUENCY   RF69_433MHZ
//#define FREQUENCY   RF69_868MHZ
//#define FREQUENCY   RF69_915MHZ
#define ENCRYPTKEY    "TheWildWestHouse" //exactly the same 16 characters/bytes on all nodes!
//#define IS_RFM69HW    //uncomment only for RFM69HW! Leave out if you have RFM69W!
#define ENABLE_ATC    //comment out this line to disable AUTO TRANSMISSION CONTROL
#define DHT_DEVICE    101
#define LED_DEVICE    3

#define LED           9 // Moteinos have LEDs on D9
#define FLASH_SS      8 // and FLASH SS on D8

//**********************************************************************************************
// Radio transmission specific settings
// ******************************************************************************************
radioPayload theData, sendData;

#ifdef ENABLE_ATC
RFM69_ATC radio;
#else
RFM69 radio;
#endif

char buff[20];
byte sendSize = 0;
boolean requestACK = false;

//*********************************************************************************************
// Serial channel settings
//*********************************************************************************************
#define SERIAL_BAUD   115200

//**********************************************************************************************
//*** Definitions for Sensors
//**********************************************************************************************

//***** DHT22 settings
#define DHTPIN 7     // what digital pin we're connected to
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321

DHT dht(DHTPIN, DHTTYPE);

// device 101 is temperature stored as DHT_DEVICE
unsigned long dht_period = 45000;   //send data every X milliseconds
unsigned long dht_period_time;      //seconds since last period
unsigned long t1 = 0L;

// EEPROM Parameter offsets
#define PARAM_DHT_PERIOD 1

//***********************************************************

//************************************************************
// LED is device 3
// ***********************************************************
int ledStatus = 0;    // initially off


//**********************************
// General declarations
//**********************************
unsigned long timepassed = 0L;
String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete
unsigned long requestID;
long lastPeriod = 0;
unsigned long sleepTimer = 0L;
int downTimer = 0;
int counter = 0;

//*************************************************************
//***  SETUP Section
//*************************************************************
void setup() {
  Serial.begin(SERIAL_BAUD);
  radio.initialize(FREQUENCY, NODEID, NETWORKID);
#ifdef IS_RFM69HW
  radio.setHighPower(); //uncomment only for RFM69HW!
#endif
  radio.encrypt(ENCRYPTKEY);

  //Auto Transmission Control - dials down transmit power to save battery (-100 is the noise floor, -90 is still pretty good)
  //For indoor nodes that are pretty static and at pretty stable temperatures (like a MotionMote) -90dBm is quite safe
  //For more variable nodes that can expect to move or experience larger temp drifts a lower margin like -70 to -80 would probably be better
  //Always test your ATC mote in the edge cases in your own environment to ensure ATC will perform as you expect
#ifdef ENABLE_ATC
  //radio.enableAutoPower(-70);
#endif

  char buff[50];
  sprintf(buff, "\nTransmitting at %d Mhz...", FREQUENCY == RF69_433MHZ ? 433 : FREQUENCY == RF69_868MHZ ? 868 : 915);
  Serial.println(buff);

#ifdef ENABLE_ATC
  Serial.println("RFM69_ATC Enabled (Auto Transmission Control)\n");
#endif
  Serial.print("Node number is ");
  Serial.println(NODEID);

  dht.begin();      // Initialise the DHT module
  sendData.nodeID = NODEID;    // This node id should be the same for all devices

  //***************
  //**Setup for DHT
  //****************
  // dev4 is temperature_F/humidity
  dht_period_time = millis();  //seconds since last period

  //EEPROM.put(PARAM_DHT_PERIOD, dht_period); // temp to set up interval
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
  
  pinMode(LED, OUTPUT);

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

  //-------------------------------------------------------------------------------
  // deviceID = DHT_DEVICE;  //temperature humidity
  //-------------------------------------------------------------------------------
  //DHT22 --------------------------------

  //send every x seconds

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

    sleepTimer = millis() + 5000;
    Serial.println("Wake up");
    send_temp();
  }

}

// *************************************************************************************************
void serialEvent() {
  while (Serial.available()) { // keep on reading while info is available
    // get the new byte:
    char inByte = Serial.read();

    // add it to the inputString:
    if ((inByte >= 65 && inByte <= 90) || (inByte >= 97 && inByte <= 122) || (inByte >= 48 && inByte <= 57) || inByte == 43 || inByte == 61 || inByte == 63) {
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
    radio.encrypt(ENCRYPTKEY);
  if (inputString == "e") //e=disable encryption
    radio.encrypt(null);

  // clear the string:
  inputString = "";
  stringComplete = false;
}

// **********************************************************************************************
void process_radio()
{
  Serial.print('['); Serial.print(radio.SENDERID, DEC); Serial.print("] ");
  // for (byte i = 0; i < radio.DATALEN; i++)
  //   Serial.print((char)radio.DATA[i]);
  Serial.print("   [RX_RSSI:"); Serial.print(radio.RSSI); Serial.print("]");

  if (radio.ACKRequested())
  {
    radio.sendACK();
    Serial.print(" - ACK sent");
  }

  Serial.println();
  Serial.println("Data received");
  theData = *(radioPayload*)radio.DATA;
  printThedata(theData);
  requestID = theData.req_ID;

  sendData.nodeID = 1;    // always send to the gateway node

  if (theData.nodeID = NODEID)  // only if the message is for this node
  {
    switch (theData.action) {
      case 'P':   // parameter update
        if (theData.deviceID = DHT_DEVICE)  // DHT22
        {
          Serial.print('Parameter update request ');
          Serial.print(theData.float1);
          Serial.println(' secs');
          sendData.req_ID = requestID;
          sendData.deviceID = DHT_DEVICE;
          sendData.action = 'C';
          sendData.result = 0;
          sendData.float1 = theData.float1;
          sendData.float1 = 0;
          //sendData.result = 1;
          dht_period = theData.float1 * 1000;
          radio.sendWithRetry(GATEWAYID, (const void*)(&sendData), sizeof(sendData));
          Serial.print("Temp regular update changed to ");
          Serial.print(theData.float1);
          Serial.println(" seconds");
          EEPROM.put(PARAM_DHT_PERIOD, dht_period);
        }
        break;
      case 'Q':   // parameter query
        if (theData.deviceID = DHT_DEVICE)    // DHT22
        {
          sendData.req_ID = requestID;
          sendData.deviceID = DHT_DEVICE;
          sendData.action = 'C';
          sendData.result = 0;
          sendData.float1 = dht_period / 1000;
          radio.sendWithRetry(GATEWAYID, (const void*)(&sendData), sizeof(sendData));
          Serial.print("Query - DHT period is ");
          Serial.print(sendData.float1);
          Serial.println(" seconds");
        }
        break;
      case 'R':    // information request
        send_temp();
        break;
      case 'A':
        if (theData.deviceID = LED_DEVICE)
        {
          Serial.print("LED updated to ");
          if (ledStatus) {
            ledStatus = 0;
            digitalWrite(LED, LOW);
            Serial.println("OFF");
          }
          else
          {
            ledStatus = 1;
            digitalWrite(LED, HIGH);
            Serial.println("ON");
          }
          sendData.req_ID = requestID;
          sendData.deviceID = LED_DEVICE;
          sendData.instance = theData.instance;
          sendData.action = 'C';
          sendData.result = 0;
          sendData.float1 = ledStatus;
          radio.sendWithRetry(GATEWAYID, (const void*)(&sendData), sizeof(sendData));

        }
        break;
      case 'S':   // Status request
        Serial.println(theData.nodeID);
        sendData.req_ID = requestID;
        sendData.action = 'C';
        sendData.result = 0;
        sendData.float1 = readVcc();
        sendData.float2 = radio.RSSI;
        radio.sendWithRetry(GATEWAYID, (const void*)(&sendData), sizeof(sendData));
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
void send_temp()
{
  float h = dht.readHumidity();
  float t = dht.readTemperature();
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
    radio.sendWithRetry(GATEWAYID, (const void*)(&sendData), sizeof(sendData));
    printThedata(sendData);
  }

  // Serial.println();
}

// ******************************************************************************************
long readVcc() {
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
  ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
  ADMUX = _BV(MUX5) | _BV(MUX0);
#elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
  ADMUX = _BV(MUX3) | _BV(MUX2);
#else
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#endif

  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA, ADSC)); // measuring

  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH
  uint8_t high = ADCH; // unlocks both

  long result = (high << 8) | low;

  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result; // Vcc in millivolts
}

// *************************************************************************************************
void printThedata(radioPayload &myData)
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

