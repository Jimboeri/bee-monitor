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

defaults_fname = 'defaults.cfg'
defaults = configparser.ConfigParser()
defaults.read(defaults_fname)

headers = {"Content-type": "application/x-www-form-urlencoded", "Accept": "text/plain", "THINGSPEAKAPIKEY": "N6BP4CYM039RFCNY"}

updateList = []

#*******************************************************************
class gatewayMessage:
  """Defines a class to hold message objects"""

  def __init__(self, config):
    """This instantiates the message """
   
    # Prefefine instance variables
    self.title = ''
    self.base_topic = ''
    self.field1_topic = ''
    self.field2_topic = ''
    self.field3_topic = ''
    self.field4_topic = ''
    self.field5_topic = ''
    self.field6_topic = ''
    self.field7_topic = ''
    self.field8_topic = ''
    self.subtopic = ''
    self.api_key = ''

  def DecodeTopic(self, inpTopic):
    """This decodes an input topic """
        
    self.config_found = 0
    
    myList = inpTopic.split("/")
    
    for sec in config.sections():
      if sec == 'mqtt' or sec == 'thingspeak':
        continue

      if config['mqtt'].get('top_topic') + '/' + myList[1] == config[sec].get('base_topic',""):
        self.config_found = 1
        self.title = sec
        self.base_topic = config[sec].get('base_topic', '')
        self.field1_topic = config[sec].get('field1_topic', '')
        self.field2_topic = config[sec].get('field2_topic', '')
        self.field3_topic = config[sec].get('field3_topic', '')
        self.field4_topic = config[sec].get('field4_topic', '')
        self.field5_topic = config[sec].get('field5_topic', '')
        self.field6_topic = config[sec].get('field6_topic', '')
        self.field7_topic = config[sec].get('field7_topic', '')
        self.field8_topic = config[sec].get('field8_topic', '')
        self.action = myList[3]
        self.subtopic = myList[4]
        self.api_key = config[sec].get('write_key', '')
               
    if not self.config_found:
      logging.warning("Config not found, topic : {}".format(inpTopic))
    
        
  def DecodeMsg(self, inpMsg):
    """Decodes an input msg"""
    self.value = 0
    logging.info(inpMsg)
    if inpMsg.startswith("{"):    # the payload is a JSON message
      self.jmsg = json.loads(inpMsg)
    else:                         # not JSON
      if self.config_found == 1:
        self.value = float(inpMsg)
        if self.subtopic == self.mqtt_val1:
          self.float1 = self.value
        if self.subtopic == self.mqtt_val2:
          self.float2 = self.value
     
    return self.value    
      
  #def DecodeMqtt(self, mqttMsg):
  #  """Populates structure for mqtt message input"""
  #  self.DecodeTopic(mqttMsg.topic)
  #  self.DecodeMsg(mqttMsg.payload)
      
  #def SerialOutput(self):
  #  outStr = "P,{0:d},{1:d},{2:d},{3},{4:d},{5:d},{6},{7}\n".format(self.node, self.device, 
  #      self.instance, self.action, self.result, self.req_ID, self.float1, self.float2)
  #  return outStr
 
#*******************************************************************
class thingspeakUpdate:
  """Class to build up a whole TS update from a number of mqtt messages """
  def __init__(self, inp_Base_Topic, inp_API_Key):
    self.init_dt = datetime.datetime.now()
    self.field1 = ''
    self.field2 = ''
    self.field3 = ''
    self.field4 = ''
    self.field5 = ''
    self.field6 = ''
    self.field7 = ''
    self.field8 = ''
    self.base_topic = inp_Base_Topic
    self.api_key = inp_API_Key
    self.complete = 0
  
 
  

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
       
      sTopic = config[sec].get('base_topic') + '/I/#'
      client.subscribe(sTopic)
      logging.info("Subscribed to " + sTopic)
      

# **************************************************************
# The callback for when a MQTT message is received from the server.
def on_message(client, userdata, msg):
  #print(msg.topic+" "+str(msg.payload))

  jStr = msg.payload.decode("utf-8")
    
  # instantiate & populate the message rec
  gMsg = gatewayMessage(config)
  gMsg.DecodeTopic(msg.topic)
  logging.info("MQTT message received {} : {}".format(msg.topic, msg.payload))
    
  # don't bother with errors or rubbish mqtt messages
  if gMsg.config_found == 0:
    return
    
  # look for an update record
  uD_found = 0
  nCnt=0
  for uD in updateList:
    if gMsg.base_topic == uD.base_topic:
      curr_uD = uD
      uD_found = 1
      break
    else:
      nCnt += nCnt

  # add one to the list if it doesn't exist
  if not uD_found:
    updateList.append(thingspeakUpdate(gMsg.base_topic, gMsg.api_key))
    curr_uD = updateList[-1]
    nCnt = len(updateList)-1
      
  # update the relevant field in the update record
  if gMsg.subtopic == gMsg.field1_topic:
    curr_uD.field1 = jStr
  elif gMsg.subtopic == gMsg.field2_topic:
    curr_uD.field2 = jStr
  elif gMsg.subtopic == gMsg.field3_topic:
    curr_uD.field3 = jStr
  elif gMsg.subtopic == gMsg.field4_topic:
    curr_uD.field4 = jStr
  elif gMsg.subtopic == gMsg.field5_topic:
    curr_uD.field5 = jStr
  elif gMsg.subtopic == gMsg.field6_topic:
    curr_uD.field6 = jStr
  elif gMsg.subtopic == gMsg.field7_topic:
    curr_uD.field7 = jStr
  elif gMsg.subtopic == gMsg.field8_topic:
    curr_uD.field8 = jStr
  
  # work out if we have all the fields
  curr_uD.complete = 1
  if gMsg.field1_topic != '' and curr_uD.field1 == '':
    curr_uD.complete = 0
  if gMsg.field2_topic != '' and curr_uD.field2 == '':
    curr_uD.complete = 0
  if gMsg.field3_topic != '' and curr_uD.field3 == '':
    curr_uD.complete = 0
  if gMsg.field4_topic != '' and curr_uD.field4 == '':
    curr_uD.complete = 0
  if gMsg.field5_topic != '' and curr_uD.field5 == '':
    curr_uD.complete = 0
  if gMsg.field6_topic != '' and curr_uD.field6 == '':
    curr_uD.complete = 0
  if gMsg.field7_topic != '' and curr_uD.field7 == '':
    curr_uD.complete = 0
  if gMsg.field8_topic != '' and curr_uD.field8 == '':
    curr_uD.complete = 0      
     
  

# ****************************************************************
def main_program():
    """This program subscribes to the mqtt server and publishes specfic
    data items to thingspeak channels
    """

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
      nCnt=0
      for uD in updateList:
 
  
        # if it's taken longer than 5 seconds, send the update anyway  
        timeDiff = datetime.datetime.now() - uD.init_dt
        if timeDiff.seconds > 5:
          logging.info("Timeout - sending update anyway")
          uD.complete = 1
  
        if uD.complete == 1:
          headers = {"Content-type": "application/x-www-form-urlencoded", "Accept": "text/plain",
            "THINGSPEAKAPIKEY": uD.api_key}
 
          params = "field1={}".format(uD.field1)
          if uD.field2 != '':
            params = params + "&field2={}".format(uD.field2)
          if uD.field3 != '':
            params = params + "&field3={}".format(uD.field3)
          if uD.field4 != '':
            params = params + "&field4={}".format(uD.field4)
          if uD.field5 != '':
            params = params + "&field5={}".format(uD.field5)
          if uD.field6 != '':
            params = params + "&field6={}".format(uD.field6)
          if uD.field7 != '':
            params = params + "&field7={}".format(uD.field7)
          if uD.field8 != '':
            params = params + "&field8={}".format(uD.field8)
    
          try:
            conn = http.client.HTTPConnection(config['thingspeak'].get('url'))
            conn.request("POST", "/update", params, headers)
            response = conn.getresponse()
            logging.info("HTTP send success")
            conn.close()
            updateList.pop(nCnt)
          except:
 
            logging.warning("HTTP send error {}:{}".format(headers, params))
        else:
          nCnt += 1


# Only run the program if called directly
if __name__ == "__main__":

    main_program()

