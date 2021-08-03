
//#define MQTTServer       "test.mosquitto.org"
#define MQTTServer       "broker.hivemq.com"

#define MQTTPort         "1883"
#define MQTTUser         "mqtt_user"
#define MQTTPass         "mqtt_pass"
#define IdDispositivo    "9999"



//Tópicos MQTT
#define TOPICO_CENTRAL_RELOGIO   "pio/relogio/"    



#define TEMPO_PISCA 500
#define TEMPO_INTERVALO_ENTRE_RECONECTAR 5000
#define DHTTYPE DHT11  


#define TEMPERATURE_PRECISION 9 // Lower resolution DS18B20

#define NUM_PIXELS 86
#define NUM_LED_SEGMENTO 3

//Pinos
#define FITAPIN D5
#define DHTPIN D6
#define DSPIN D3
#define LDRPIN A0
#define CHAVEPIN D7 
#define SCL D1
#define SDA D2


#define CSTR(x) x.c_str()   
#define TXTIP(x) CSTR(x.toString())

#define DEBUG
/** DEBUG ***/
#ifdef DEBUG
  #define LOG(s)  Serial.println(s)
#else
  #define LOG(s)
#endif

// Versão Software
const char        SW_VERSAO[]                    = "0.1";
const char        SW_BUILD[]                     = __DATE__ " " __TIME__;
