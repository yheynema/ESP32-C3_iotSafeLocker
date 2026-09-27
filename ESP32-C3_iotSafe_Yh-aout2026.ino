/*  --- Entête principale -- information sur le programme
 *   
 *  Programme:        Safe IoT Locker
 *  Date:             aout 2026
 *  Auteur:           Y. Heynemand
 *  Pltform matériel: ESP32C3 - M5 Stack C3U v3.3.8
 *  Pltform develop : Arduino IDE 2.3.10
 *  Description:      Module de contrôle pour le latch électronique de la boîte.
                      project connecté.
 *  Fonctionnalités:  Latch électronique, DS18B20, WS2850 RGB LED chip, buzzer,  status LEDs
 *  Notes:  NE PAS Activer le CDC (pourle Serial) car entre en conflit avec le USB et DEL et WS2812B.
 *          Lors de test, ne fat pas brancher avec le USB seulement car étrange comportement de la DEL et WS2812B,
 *          car en conflit...  eh! El cheapo!
 *     
 */
 
/* --- Materiel et composants -------------------------------------------
 * ESP32C3U de M5 Stack
 * Capteur de T DS18B20, broche G5
 * WS2812B data: G18   (shared USB)
 * Buzzer: G10
 * Latch key: G3
 * Latch sense: G4
 * LEDs: 
    - on-board: G19   (shared USB)
    - remote 1: G6
    - remote 2: G7
 * SCL: G9
 * SDA: G8

*/


/* --- HISTORIQUE de développement --------------------------------------
 * v0.1.x: version initiale de test 
 * v0.2.x: de base avec echange de message via MQTT. Supporte lecture DS18B20, ctrl WS2812B, Latch, latchSense.
 * v0.3.x: ajout de loopCount dans la réponse (pour avoir une idée du temps "ON"). Déf utilité des DELs, door data dans le data (loopcount et etat).
      - DEL verte: système en activité lorsque clignote, en cas de perte de comm, reste éteint.
      - DEL rouge: fixe = comande en cours de traitement; clignote 120ms = porte ouverte; clignote 50ms: perte de communication
 * v0.4.x: (Todo): 
      - OTA update autorisé via une cmd MQTT;
      - état de l'heure (pour des ouvertures autorisées ou non);
      - logging des ouvertures et demandes, msg;
      - interface Web (avec méthode de token autorisés, etc);
      - configuration des paramètres (sauvegarde à l'aide de params);
      - configurer l'envoi ou non de msg, délais entre les msg...;
      - UI et méthode pour config du Wifi;
*/
//-----------------------------------------------------------------------



//--- Déclaration des librairies (en ordre alpha) -----------------------

#include <Arduino.h>
#include <Wire.h>
#include <WiFiMulti.h>
#include <esp_wifi.h>
#include <PubSubClient.h>           // https://github.com/knolleary/pubsubclient (v2.8)
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <FastLED.h>
#include "blinker.h"
#include "secret.h"

//-----------------------------------------------------------------------

#define _VERSION "0.3.7"

//--- Definitions -------------------------------------------------------
#define DEBUG      false
#define MAXLOOPCOUNT 15
#define I2CData       8
#define I2CClk        9

#define ds18b20_data 5
#define ws2812b_data 18
#define NUM_LEDS 1      // Change if using a strip/matrix
#define buzzer_pin   10
#define latch_pin     3
#define lSense_pin    4
#define onBboardLed  19
#define remLedRed      6   //Rouge
#define remLedGreen     7   //verte

typedef struct {
  char cmd;
  uint16_t arg1;
  uint16_t arg2;
  bool validStatement;
  String replyTo;
} mqttCMD;

//-----------------------------------------------------------------------


//--- Declaration des objets --------------------------------------------
WiFiMulti wifimulti;
WiFiClient MQTTClient;
PubSubClient clientMQTT(MQTTClient);
TwoWire myI2C(0);

// GPIO where the DS18B20 is connected to
const int oneWireBus = ds18b20_data;  
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

// Objet pour la Status LED:
Blinker statusLED(onBboardLed,500);

// Objet pour la remote LED01:
Blinker remLED01(remLedRed,500);

// Objet pour la remote LED02:
Blinker remLED02(remLedGreen,500);

// Objet WS2812B:
CRGB leds[NUM_LEDS];

//-----------------------------------------------------------------------


//--- Constantes --------------------------------------------------------

bool dryMode = false;   //dryMode ensure not sending data to TB (no publish)

//MQTT server:
const char* mqttBroker    = MQTT_BROKER;
const char* mqttUsername  = MQTT_USERNAME;  //usager du service MQTT
const char* mqttDevice    = MQTT_DEVICEID;
const char* mqttUserPass  = MQTT_TOKEN; //token du device (associé au device "etudiantXX")
const char* mqttPublishTo = "iotSafeLock/data";  //Nom du canal (topic) de publication

const char* ntpServer = "ca.pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 0;

const byte myMAC[6] = MY_MAC_ADDR; //Cette séquence sera fournie par l'enseignant

//-----------------------------------------------------------------------


//--- Variables globales ------------------------------------------------
 
unsigned long msgTimer = 0;

bool MQTTActivated = true;

uint16_t latchDelay = 750;  //par default

unsigned long previousMillis = 0;
const unsigned long interval = 2000;

float temperatureCourante = 0.0;

uint32_t loopCount=0;
uint32_t doorOpenLoopCount=0;
uint32_t mqttTBErrorCount = 0;
uint32_t mqttBrokerErrorCount = 0;
uint32_t wifiErrorCount = 0;
bool mqttReceivedCmd = false;

mqttCMD mqttRecvCmd;

//-----------------------------------------------------------------------


//--- Prototypes --------------------------------------------------------
bool  sendData2Broker(bool);
bool  checkWiFi(bool);
//-----------------------------------------------------------------------

//--- Section des routines specifiques ----------------------------------
void setup() {

//  Serial.begin(115200);   //yh retiré pcq en conflit avec on-board LED sur la G19 ainsi que la G18
//  while (!Serial) {yield();}
//  delay(2500);

//  Serial.println("IoT safe v"+String(_VERSION));
  pinMode(buzzer_pin,OUTPUT);
  digitalWrite(buzzer_pin,LOW);
  pinMode(latch_pin,OUTPUT);
  digitalWrite(latch_pin,LOW);
  
  pinMode(lSense_pin,INPUT);

  statusLED.begin();
  remLED01.begin();
  remLED02.begin();

  FastLED.addLeds<SK6812, ws2812b_data, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50); // Set brightness (0-255)
  leds[0] = CRGB::Red;
  FastLED.show();

  sensors.begin();

  wifimulti.addAP(MY_SSID_1, MY_PASSWORD_1);
  wifimulti.addAP(MY_SSID_2, MY_PASSWORD_2);
  // wifimulti.addAP(MY_SSID_4, MY_PASSWORD_4);
  //wifimulti.addAP(MY_SSID_5, MY_PASSWORD_5);
          // Connecting to WiFi ..> CONNECTED to MQTT
          // >	IP is:10.180.98.3
          // > initialisation terminée

  if (!checkWiFi(true)) {
    void(0);
  } else { leds[0] = CRGB::Blue; FastLED.show();}

  clientMQTT.setServer(mqttBroker, 1883);
  clientMQTT.setBufferSize(512);
  clientMQTT.setCallback(mqttCallback);

  sendData2Broker(false);
}

void loop() {

  statusLED.update();
  remLED01.update();
  remLED02.update();

  if (MQTTActivated) clientMQTT.loop();

  if (mqttReceivedCmd) {
    remLED02.stop();  //Ferme la verte, le temps de processer
    remLED01.setHigh();
    checkMQTT();
    mqttReceivedCmd = false;
  }

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Envoie la commande de mesure de température à tous les capteurs sur le bus
    sensors.requestTemperatures(); 

    // Récupère la température en Celsius du premier capteur (index 0)
    float tempC = sensors.getTempCByIndex(0);

    // Vérification de la validité de la mesure (-127°C indique une erreur de lecture/câblage)
    if (tempC != DEVICE_DISCONNECTED_C) {
      temperatureCourante = tempC;
     }
    if (sendData2Broker(false)) {
      if (!remLED02.getStatus()) remLED02.start();
      if (digitalRead(lSense_pin)==HIGH) remLED01.start(120); else remLED01.setLow();
      //remLED01.setInterval(500);  //requis?
    } else {remLED02.stop();remLED01.start(50);} 
    loopCount++;
    if (digitalRead(lSense_pin)==HIGH) doorOpenLoopCount++; else doorOpenLoopCount=0;
  }
}
