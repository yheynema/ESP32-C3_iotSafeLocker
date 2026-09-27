const char cmdList[]={'T','L','R','W','B'};

bool checkValidCmd(char cmd) {
  bool retCode = false;

  for (int i=0; i<sizeof(cmdList);i++)
    if(cmd == cmdList[i]) retCode = true;

  return retCode;
}

bool checkWiFi(bool recycle=false) {
  bool retCode = false;  //au départ, on assume que le wifi n'est pas activé...
  if (WiFi.status() != WL_CONNECTED) {

    //si requis, pour forcer un re-cyclage du wifi, permettant de sortir d'un potentiel état "bloqué"
    if (recycle) {
      WiFi.disconnect();
      delay(1000);
      WiFi.mode(WIFI_STA);
      WiFi.setAutoReconnect(true);
      esp_wifi_set_mac(WIFI_IF_STA, myMAC);  // ne pas changer l'adresse ip...
    }

    uint32_t delayStart = millis();
    int localLoopCount = 0;

    while (wifimulti.run() != WL_CONNECTED && localLoopCount < MAXLOOPCOUNT) {
      if (localLoopCount%50)
        void(0);
      else {
        void(0);
      } 
      localLoopCount++;
      delay(1000);
    }

    if (localLoopCount < MAXLOOPCOUNT){
      retCode = true;  //ok! Wifi valide et connecté
    }
  } else retCode = true;  //ok! Wifi valide et connecté

  return retCode;
}

bool sendData2Broker(bool dryMode=false) {
  bool retCode = false;
  // à développer
  if (WiFi.status() == WL_CONNECTED) {
    JsonDocument data;

    //forge du data à envoyer à TB:
    data["version"] = String(_VERSION);
    data["doorState"] = digitalRead(lSense_pin);
    data["TMP"] = temperatureCourante;
    data["LP"] = loopCount;
    data["DOLC"] = doorOpenLoopCount;
    data["rssi"] = WiFi.RSSI(); //Suivi du niveau Wifi

    char buffer[256];

    int dataSize = serializeJson(data, buffer);

    if (clientMQTT.connect(mqttDevice,mqttUsername,mqttUserPass)) {
      clientMQTT.subscribe(MQTT_LISTEN_TOPIC);  //pas la meilleure place, mais mieux que rien

      if (clientMQTT.publish(mqttPublishTo,buffer)) {
        void(0);
        retCode = true;
      } else {
        void(0);
        mqttBrokerErrorCount++;
      }

    } else {
      clientMQTT.loop();
      clientMQTT.disconnect();
    }
  } else {
    wifiErrorCount++;
  }
  return retCode;
}

//Routine pour traiter un msg reçu via MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  
  JsonDocument doc;

  // Deserialize the JSON document
  DeserializationError errorCatcher = deserializeJson(doc, payload);

  // Test if parsing succeeds
  if (errorCatcher.code() != DeserializationError::Ok) {
    void(0);
    //ignore silencieusement le message, pas de retour.
  } else {
    if (doc["recipient"].is<String>()) {
      mqttRecvCmd.validStatement = false;   // On assume avant de tout valider
      String recipient = doc["recipient"].as<String>();
      //comparaison de mon ID et recipient: 
      if (recipient == MQTT_DEVICEID) {
        char cmd = (doc["cmd"].as<String>()).charAt(0);
        //Traite la commande
        if(checkValidCmd(cmd)) {
          mqttRecvCmd.cmd = cmd;
        } else {
          mqttRecvCmd.cmd = 0;
        }
        if (doc["arg1"].is<int>()) {
          //Traite l'argument 1
          mqttRecvCmd.arg1 = doc["arg1"].as<int>();
        } else {
          mqttRecvCmd.arg1 = 0;
        }
        if (doc["arg2"].is<unsigned short>()) {
          //Traite l'argument 2
          mqttRecvCmd.arg2 = doc["arg2"].as<int>();
        } else {
          mqttRecvCmd.arg2 = 0;
        }
        if (doc["replyTo"].is<String>()) {
          //Traite le replyto
          String replyTo = doc["replyTo"].as<String>();
          if (replyTo.length() >= 5) {
            mqttRecvCmd.replyTo = replyTo;
            if (mqttRecvCmd.cmd != 0) {
              mqttRecvCmd.validStatement = true;
              mqttReceivedCmd = true;
            }
          }
        }
      } else void(0);
    } else void(0);
  }
}

bool checkMQTT(void) {
  bool retCode = false;

  if (mqttRecvCmd.validStatement) {

    JsonDocument retDoc;

    retDoc["cmd"] = String(mqttRecvCmd.cmd);

    switch (mqttRecvCmd.cmd) {
      case 'T':
        //envoie Temperature
        retDoc["value"] = temperatureCourante;
        break;
      case 'L':
        //active le Latch
        if (digitalRead(lSense_pin)==LOW) {
          digitalWrite(latch_pin,HIGH);
          if (mqttRecvCmd.arg1>100 && mqttRecvCmd.arg1<10000)
            delay(mqttRecvCmd.arg1);
          digitalWrite(latch_pin,LOW);
          retDoc["value"] = latchDelay;
        }
        break;
      case 'R':
        //lecture état de la porte
        retDoc["value"] = digitalRead(lSense_pin);
        break;
      case 'W':
        //valeur de la couleur du WS2812B... j'ai trouvé ces 8 "couleurs"
        if (mqttRecvCmd.arg1==1)
          leds[0] = CRGB::Blue;
        if (mqttRecvCmd.arg1==2)
          leds[0] = CRGB::Red;
        if (mqttRecvCmd.arg1==3)
          leds[0] = CRGB::Yellow;
        if (mqttRecvCmd.arg1==4)
          leds[0] = CRGB::Green;
        if (mqttRecvCmd.arg1==5)
          leds[0] = CRGB::Aqua;
        if (mqttRecvCmd.arg1==6)
          leds[0] = CRGB::Orange;
        if (mqttRecvCmd.arg1==7)
          leds[0] = CRGB::White;
        if (mqttRecvCmd.arg1==8)
          leds[0] = CRGB::Black;

        FastLED.show();
        retDoc["value"] = mqttRecvCmd.arg1;
        break;
      case 'B':
        digitalWrite(buzzer_pin,HIGH);
        if (mqttRecvCmd.arg1>100 && mqttRecvCmd.arg1<10000)
          delay(mqttRecvCmd.arg1);
        else delay(750);
        digitalWrite(buzzer_pin,LOW);
        retDoc["value"] = mqttRecvCmd.arg1;
        break;
      default:
        retDoc["value"] = "error";
        break;
    }
    retDoc["version"] = _VERSION;

    //Envoie à replyTo:
    if (WiFi.status() == WL_CONNECTED) {

      char buffer[256];

      int dataSize = serializeJson(retDoc, buffer);

      //SerPrnLogLN(LVL2,">>> Replying back ...");

      if (clientMQTT.connect(mqttDevice,mqttUsername,mqttUserPass)) {
        if (clientMQTT.publish(mqttRecvCmd.replyTo.c_str(),buffer)) {
            retCode = true;
          } else {
            void(0);
          }
      } else {
        void(0);
      }
    } else {
      void(0);
    }

  } //End-if validStatement

  return retCode;
}