#!/usr/bin/python3

import paho.mqtt.client as mqtt
import http.client
import urllib
import logging
import configparser
import datetime
import time
import json

config_fname = 'thingspeak.cfg'
config = configparser.ConfigParser()
config.read(config_fname)

headers = {"Content-type": "application/x-www-form-urlencoded", "Accept": "text/plain", "THINGSPEAKAPIKEY": "N6BP4CYM039RFCNY"}

updateList = []

#*******************************************************************
class gatewayMessage:
  """Defines a class to hold message objects"""

  def __init__(self, config, msg):
    """This instantiates the message """
   
    # Prefefine instance variables
    self.title = ''
    self.base_topic = ''
    self.thingspeakFields = {}
    self.subtopic = ''
    self.api_key = ''
    self.jmsg = {}      # hold a dict created from JSON payload
    self.json = 0       # set to 1 if the payload is a JSON message
    
    self.DecodeTopic(msg.topic)
    self.DecodeMsg(msg.payload)
    
    return

  ##############################################
  def DecodeTopic(self, inTopic):
    """This decodes an input topic """
        
    self.config_found = 0
    
    myList = inTopic.split("/")
    #print("Decode topic")
    #print(myList)
    
    for sec in config.sections():
      if sec == 'mqtt' or sec == 'thingspeak' or sec == 'general':
        continue

      if config['mqtt'].get('top_topic') + '/' + myList[1] == config[sec].get('base_topic',""):
        self.config_found = 1
        self.title = sec
        self.base_topic = config[sec].get('base_topic', '')
        
        for i in range(1, 9):                         # Iterate from 1 - 8
          f = 'field' + str(i)                        # f looks like field1
          f2 = f + '_topic'                           # f2 looks like field1_topic
          if config[sec].get(f2, '') != '':          # if there is a field defined in the config file
            self.thingspeakFields[config[sec].get(f2, '')] = f    #create dict entry like {'temp': 'field1'}
                
 
        self.action = myList[3]
        self.subtopic = myList[4]
        self.api_key = config[sec].get('write_key', '')
        
        #config.set(sec,'active', '1')
        #print(config_fname)
        #with open('thingspeak.cfg', 'w') as f:
        #  config.write(f, True)
        #  print('Config file updated')
               
    if not self.config_found:
      print("Config not found, topic : {}".format(inTopic))
      logging.warning("Config not found, topic : {}".format(inTopic))

  
  ##############################################      
  def DecodeMsg(self, inpMsg):
    """Decodes an input msg"""
    self.value = 0
    logging.info(inpMsg)
    self.json = 0
    inpMsg = str(inpMsg, 'utf-8')
    
    if inpMsg.startswith( '{' ):    # the payload is a JSON message
      self.jmsg = json.loads(inpMsg)
      self.json = 1
      print ("Payload {}".format(self.jmsg))
    else:                         # not JSON
      if self.config_found == 1:
        self.value = float(inpMsg)
        self.jmsg[self.subtopic] = self.value
        if self.subtopic == self.mqtt_val1:
          self.float1 = self.value
        if self.subtopic == self.mqtt_val2:
          self.float2 = self.value
     
    return self.value    
      


# **************************************************************
# The callback for when the client receives a CONNACK response from the server.
def on_connect(client, userdata, flags, rc):
    
    logging.info("Connected to mqtt server")

    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    #client.subscribe("homeassistant/#")
    #logging.info("Subscribed to homeassistant/#")
    
    for sec in config.sections():
      if sec == 'mqtt' or sec == 'thingspeak' or sec == 'general':
        continue
       
      sTopic = config[sec].get('base_topic') + '/#'
      client.subscribe(sTopic)
      logging.info("Subscribed to " + sTopic)
      

# **************************************************************
# The callback for when a MQTT message is received from the server.
def on_message(client, userdata, msg):
  print(msg.topic+" "+str(msg.payload))

  jStr = msg.payload.decode("utf-8")
    
  # instantiate & populate the message rec
  gMsg = gatewayMessage(config, msg)
  #gMsg.DecodeTopic(msg.topic)
  #gMsg.DecodeMsg(msg.payload)
  logging.info("MQTT message received {} : {}".format(msg.topic, msg.payload))
    
  # don't bother with errors or rubbish mqtt messages
  if gMsg.config_found == 0:
    print("Config error")
    return
  
  headers = {"Content-type": "application/x-www-form-urlencoded", "Accept": "text/plain",
      "THINGSPEAKAPIKEY": gMsg.api_key}
 
  params = ""   # init the parameter field

  for k, v in gMsg.jmsg.items():                  # iterate through the field in the input message
    if k in gMsg.thingspeakFields:                # if the parameter name is defined in the config file
      if len(params) > 1:
        params = params + "&{}={}".format(gMsg.thingspeakFields[k], v)
      else:
        params = params + "{}={}".format(gMsg.thingspeakFields[k], v)
  print(params)
  
  try:
    conn = http.client.HTTPConnection(config['thingspeak'].get('url'))
    conn.request("POST", "/update", params, headers)
    response = conn.getresponse()
    logging.info("HTTP send success")
    print("HTTP Success")
    conn.close()
  except:
    logging.warning("HTTP send error {}:{}".format(headers, params)) 
     
  

# ****************************************************************
def main_program():
    """This program subscribes to the mqtt server and publishes specfic
    data items to thingspeak channels
    """
    time_cntr = time.time()

    # set up logging
    FORMAT = '%(asctime)-15s %(message)s'
    logging.basicConfig(filename='thingspeak.log',
        level=config["general"].getint("loglevel"),
            format=FORMAT)
    logging.info('Thingspeak starting')

    # Connect to the mqtt server
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(config['mqtt'].get('ip_address','127.0.0.1'),
      config['mqtt'].getint('port', 1883),
      config['mqtt'].getint('timeout',60))

    # Blocking call that processes network traffic, dispatches callbacks and
    # handles reconnecting.
    # Other loop*() functions are available that give a threaded interface and a
    # manual interface.
    client.loop_start()
    
    while True:
      time.sleep(1)
      if time_cntr + 60 < (time.time()):
        time_cntr = time.time()
        config.read(config_fname)
        
      


# Only run the program if called directly
if __name__ == "__main__":

    main_program()

