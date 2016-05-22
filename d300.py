#!/usr/bin/python2
import logging
import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish
import syslog
import json


# ********************************************************************
def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))
    logging.info("Connected to mqtt server on local host")
    client.subscribe("homeassistant/#")
    logging.info("Subsctibed to homeassistant/#")

#********************************************************************
def on_message(client, userdata, msg):
    print(msg.topic+" "+str(msg.payload))

#*******************************************************************
def strDict(inStr):
    myList = inStr.split(",")
    myDict = {}
    myDict['node'] = int(myList[0])
    myDict['device'] = int(myList[1])
    myDict['instance'] = int(myList[2])
    myDict['action'] = myList[3]
    myDict['result'] = int(myList[4])
    myDict['req_ID'] = int(myList[5])
    myDict['float1'] = float(myList[6])
    myDict['float2'] = float(myList[7])
    return myDict


#******************************************************************
def lo():
    """ The program turns on LED
    """

    # Set up logging
    syslog.syslog('Lo Starting')
    logging.basicConfig(filename='ha-conn.log',level=logging.DEBUG)
    logging.info('Lo starting')

    # set up the MQTT environment
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    client.connect("127.0.0.1", 1883, 60)

    topic = "homeassistant/2/101/1/P"
    msgDict = {}
    msgDict['action'] = "P"
    msgDict['result'] = 0
    msgDict['req_ID'] = 4
    msgDict['deviceID'] = 101
    msgDict['instance'] = 1
    msgDict['float1'] = 300 

    msg = json.dumps(msgDict) 
    print(topic + " " + msg)

    logging.info(topic + " " + msg)
    publish.single(topic, msg, hostname="127.0.0.1")


#********************************************************************
if __name__ == "__main__":
    lo()
