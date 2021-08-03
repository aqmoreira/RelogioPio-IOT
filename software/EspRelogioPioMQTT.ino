/*****************************
   Relógio PioHub

    - Fita LED WS2812b
    - Temperatura e Umidade DTH11
    - Temperatura DS18b20
    - Luminosidade LDR
    - Chave para habilitar OTA

  Objetivos:
    Testar todos leds dos segmentos ao ligar
    Informar hora
    Informar temperatura e Umidade DHT11
    Informar temperatura DS18B20
    Ajustar o brilho conforme a luminosidade presente no LDR
    Habilitar atualização via OTA quando chave estiver ligada - falta
    Informar dados via MQTT Temperatura, Umidade, Luminosidade e sinal de ativo
    Atualizar a cor do display via MQTT
    Sincronizar o relógio interno via NTP ao ligar e sempre quando hora for 06:00 12:00 18:00
    WifiManager habilitado para configuração da rede Wifi e parametros de acesso MQTT, NTP e fuso horário
    Indicação dos 2 pontos marcando a passagem do tempo e cor dos pontos, indicando o status da conexão:
      Wifi Off - Vermelho
      Wifi ON e MQTT OFF - Verde
      Wifi ON e MQTT ONN - Azul

   @author Allan Moreira
   @date 30/07/2021

 ******************************/

#include <FS.h>          // Isso precisa estar em primeiro para evitar erros no sistema de arquivos
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager V 2.0.3-alpha
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson V5.13.5
#include <Wire.h>               // Only needed for Arduino 1.6.5 and earlier
#include<ESP8266WiFi.h>       //Esp8266 Boards 2.7.4
#include <PubSubClient.h>    //https://github.com/knolleary/pubsubclient/releases/tag/v2.8
#include <Adafruit_NeoPixel.h>
#include "EspRelogioPioMQTT.h"
#include "DHT.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "RTClib.h"
#include <NTPClient.h>
#include <WiFiUdp.h>


RTC_DS1307 rtc;


WiFiUDP ntpUDP;
NTPClient ntp(ntpUDP, "a.st1.ntp.br",-3*60*60);


unsigned long tempoRefreshRelogio = 0; //de qto tempo relógio deve ser acertado

//Inicializa com valores defaul, se for diferente do valores gravados em config serão alterados
char mqttServer[40] =  MQTTServer;
char mqttPort[6]  = MQTTPort;
char mqttUser[20]  = MQTTUser;
char mqttPass[20]  = MQTTPass;
char idDispositivo[5] = IdDispositivo;

WiFiClient espClient; // Cria o objeto espClient
PubSubClient mqtt(espClient); // Instancia o Cliente MQTT passando o objeto espClient

String topicoDispositivo ;

DHT dht(DHTPIN, DHTTYPE); // DHT 11
OneWire  ds(DSPIN); //DS18b20 Este sensor esta conectado com Ds1307
DallasTemperature sensorDallas(&ds);
DeviceAddress tempDeviceAddress;

const char* statusCentral = "Ativo e operando";

WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;

//flag para informa se deve salvar propriedades
bool salvarPropriedades = false;


WiFiManager wm; // WiFiManager

unsigned long contadorEspera = 0;


// Status das conexões WiFi e MQTT:
//
// status |   WiFi   |    MQTT
// -------+----------+------------
//      0 |   OFF    |    OFF
//      1 | Iniciando|    OFF
//      2 |    ON    |    OFF
//      3 |    ON    |  Iniciando
//      4 |    ON    | Registrando
//      5 |    ON    |     ON

uint8_t statusConexao = 0;

unsigned long tempoUltimaAtualizacao = 0;     //time última atualização
unsigned long tempoUltimaAtualizacaoSensor = 0;     //time última atualização
unsigned long lastTask = 0;                  // time última task

unsigned long  tempoUltimaMudancaFase = 0; //Tempo da Ultima Mudança Fase
unsigned long  tempoUltimaPedidoZPP = 0; //Tempo da Ultima Mudança Fase

byte minutoAnterior = 0;

uint32_t corPonto = 0x000000; //vermelho sem WIFI e MQTT , verde com WIFI sem MQTT, Azul TUDO OK e conectado
uint32_t corSelecionada = 0x0000FF;
byte brilhoSelecionado = 30;


DateTime agora ;


Adafruit_NeoPixel fita(NUM_PIXELS, FITAPIN, NEO_GRB | NEO_KHZ800);
float temperaturaDS = 0;
float temperaturaDHT = 0;
float umidadeDHT = 0;
float sensacaoDHT = 0;
int luminosidade = 0;

void lerDallas() {
  sensorDallas.requestTemperatures();
  float tempC = sensorDallas.getTempCByIndex(0);

  // Verifica se a leitura foi sucesso
  if (tempC != DEVICE_DISCONNECTED_C)
  {
    Serial.printf("Temperature DS18b20: %f \n ", tempC);
    temperaturaDS = tempC;
  }
  else
  {
    Serial.println("Error: Não foi possivel ler o DS18B20!");
  }
}

void sincronizarNTPHora(){
   LOG("Sincronizado hora com NTP");
   ntp.begin();//Inicia o NTP.
   ntp.forceUpdate();//Força o Update.
   ntp.update();
   
   String hora = ntp.getFormattedTime();//Armazena na váriavel HORA, o horario atual.
   LOG("ntp hora:" + hora);
   unsigned long epochTime = ntp.getEpochTime();
   rtc.adjust(DateTime(epochTime));
   //rtc.adjust(DateTime(2021, 1, 21, ntp.getHours(), ntp.getMinutes(), ntp.getSeconds()));
   
   
    DateTime now = rtc.now();
    Serial.print(now.year(), DEC);
    Serial.print('/');
    Serial.print(now.month(), DEC);
    Serial.print('/');
    Serial.print(now.day(), DEC);
    Serial.print(' ');
    Serial.print(now.hour(), DEC);
    Serial.print(':');
    Serial.print(now.minute(), DEC);
    Serial.print(':');
    Serial.print(now.second(), DEC);
    Serial.println();
    ntp.end();
  
}

void testeInicial() {
  delay(100);
  Serial.println("\n*****************Testes Iniciando*************************");

  testaFitaLed();
  delay(100);
  testaDHT();
  delay(100);
  //lê o status da chave
  if (isChaveDesligada()) {
    Serial.println("Chave :Desligada");
  } else {
    Serial.println("Chave :Ligada");
  }
  delay(100);
  //lê luminosidade
  luminosidade = getLuminosidade();
  Serial.printf("Luminosidade: %i \n", luminosidade );
  delay(30);
  testarDallas();
  delay(30);
  Serial.println("*****************Testes Encerrado*************************");
  delay(300);
}


/**
   Função de Inicialização que configura o ESP8266 para ajuste dos pinos de entrada e saída, conforme o hardware.
   Inicializa a serial

*/
void setup() {
  Serial.begin(115200);
  delay(100);
  //Fita
  pinMode(FITAPIN, OUTPUT);
  fita.begin();

  //DHT11
  dht.begin();

  //DS18b20
  sensorDallas.begin();

  //DS1307 - relógio interno
 if (! rtc.begin()) {
    Serial.println("Não encontrou relógio RTC no I2C");
    Serial.flush();
  }

  if (! rtc.isrunning()) {
    Serial.println("RTC não esta rodando,precisa ajusta o tempo!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); //vai ajusta para inicializar e depois acertar com internet
  }
  agora = rtc.now();
  
  
  pinMode(CHAVEPIN, INPUT_PULLUP);

  testeInicial();
  lerPropriedades();
  /*mostrar("0123", 255, 0, 0);
  delay(3000);
  mostrar("4567", 0, 255, 0);
  delay(3000);
  mostrar("89g-", 0, 0, 255);
  delay(3000);
  mostrar(" ECU", 255, 0, 0);
  delay(3000);
  mostrar("PIO", 0, 0, 255);
  delay(3000);
  mostrar("HUB", 255, 0, 0);
  delay(3000);  */
  testaBrilho();
  delay(200);
}

/**
   Loop principal que faz a chamadas as funcções que operam sem bloqueio do loo principal
*/
void loop() {

  conectar();
  wm.process();
  enviaDadosMQTT();

  if (millis() - tempoRefreshRelogio > 1000) { 
    agora = rtc.now();
    mostrar(getHoraMinuto(), corSelecionada);
    doisPontos();
    tempoRefreshRelogio = millis();
    if(agora.second() == 40){
      pioHub();
    }else if(agora.second() == 10){
      mostrarTemp();
    }else if(agora.second() == 25){
      mostrarUmidade();
    }
    if(agora.minute()%5 == 0 && agora.second() == 0){
      fita.setBrightness( map(luminosidade,200,1024,255,40));
      corSelecionada = 0x0000ff;
      delayEsp(1000);
    }
    
  }




  delay(10);

}


void doisPontos () {

   

    switch (statusConexao){
      case 0:
      case 1:
        corPonto = 0xff0000;
        break;

      case 2:
      case 3:
      case 4:
        corPonto = 0x00ff00;
        break;
      case 5:
        corPonto = 0x0000ff;
        break;
      default:
        corPonto = 0xff0000;
        break;
      
    }
 
    fita.setPixelColor(42,corPonto);
    fita.setPixelColor(43,corPonto);
    fita.show();
    delayEsp(500);
    fita.setPixelColor(42, 0);
    fita.setPixelColor(43, 0);
    fita.show();
}

void delayEsp(int tempoDelay){
  long tempo = millis();
    while (millis() - tempo < tempoDelay) { 
      delay(10);
      yield();
    }
}

void mostrarTemp(){
  lerSensores();
  if (temperaturaDS<10){
    mostrar("0" + String(int(temperaturaDS)) + String("gC"), 244, 122, 206);
  }else{
    mostrar(String(int(temperaturaDS)) + "gC", 244, 122, 206);
  }
  delayEsp(3000);  
}

void mostrarUmidade(){
    lerSensores();
    mostrar("U " + String( int(umidadeDHT)), 0, 255, 0);
    delayEsp(3000);
}
void pioHub(){
    mostrar("   H", 255, 0, 0);
    delayEsp(500);
    mostrar("  HU", 255, 0, 0);
    delayEsp(500);
    mostrar(" HUB", 0, 0, 255);
    delayEsp(500);
    mostrar("HUB ", 0, 0, 255);
    delayEsp(500);
    mostrar("HUB ", 0, 255, 0);
    delayEsp(2000);
    mostrar("HUB ", 255, 0, 0);
    delayEsp(2000);
    fita.clear();
    mostrar("   P", 0, 0, 255);
    delayEsp(500);
    mostrar("  PI", 0, 0, 255);
    delayEsp(500);
    mostrar(" PIO", 0, 0, 255);
    delayEsp(500);
    mostrar("PIO ", 0, 0, 255);
    delayEsp(2000);
    fita.clear();

}


void testaBrilho(){

  for(int i=0;i<256;i = i+20){
    fita.setBrightness(i);
    delay(50);
    mostrar(String(i), 255, 0, 0);
    delay(1000);
  }
  
}

String getHoraMinuto(){
  String hora = String(agora.hour()<10? "0"+String(agora.hour()):String(agora.hour()));
  String minuto = String(agora.minute()<10?"0"+String(agora.minute()):String(agora.minute()));
  hora.concat(minuto);
  //LOG("---- Horario ----");
  //LOG(hora);
  return hora;
}



/** Retorna true quando conectado, false quando off*/
bool isConectado() {
  return ((WiFi.status() == WL_CONNECTED) && mqtt.connected());
}


//callback de noticação que propriedades precisa ser salva
void saveConfigCallback () {
  LOG("Deve salvar as propriedades");
  salvarPropriedades = true;
}

/** Função para salvar as propriedades do sistema de arquivos criado na memória */
void gravarPropriedades() {
  Serial.println("Salvando config");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["mqttServer"] = mqttServer;
  json["mqttPort"]   = mqttPort;
  json["mqttUser"]   = mqttUser;
  json["mqttPass"]   = mqttPass;
  json["idDispositivo"]   = idDispositivo;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Falha para abrir o arquivo de config para gravação");
  }

  json.prettyPrintTo(Serial);
  json.printTo(configFile);
  configFile.close();
  //altera flag informando que esse dados já estão salvos
  salvarPropriedades = false;


}
/** Função para remover as propriedades do sistema de arquivos criado na memória */
void removerPropriedades() {
  if (SPIFFS.begin()) {
    LOG("Removendo arquivo config.json");
    SPIFFS.remove("/config.json");

  }

}

/** Função para remover os dados da flash da área de WIFI na memória */
void resetFlashAreaWifi() {
  wm.resetSettings();

}



/** Função para ler as propriedades do sistema de arquivos criado na memória */
void lerPropriedades() {
  //read configuration from FS json
  LOG("Montando sistema de arquivos FS...");

  if (SPIFFS.begin()) {
    LOG("Montado file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      LOG("lendo arquivo config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        LOG("Acessando config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          LOG("\nLeitura das propriedades efetuada");

          strcpy(mqttServer, json["mqttServer"]);
          strcpy(mqttPort, json["mqttPort"]);
          strcpy(mqttUser, json["mqttUser"]);
          strcpy(mqttPass, json["mqttPass"]);
          strcpy(idDispositivo, json["idDispositivo"]);
          topicoDispositivo =  TOPICO_CENTRAL_RELOGIO;
          topicoDispositivo += String(idDispositivo);


        } else {
          Serial.println("Falha ao carregar arquivo config json");
        }
      }
    }
  } else {
    Serial.println("Falha na montagem do sistema de arquivos FS");
  }
  //end read

}

/** Assina os Tópicos para comunicação com centro de operações para operação dos relogios */
void eventosMQTT() {
  LOG("Publish a conexão reativada ao MQTT ...");
  // Conectado no MQTT, avisar a central
  mqtt.publish(topicoDispositivo.c_str() , statusCentral);
  // Inscrição para escuta dos eventos para o relogio
  mqtt.subscribe((TOPICO_CENTRAL_RELOGIO + String(idDispositivo)).c_str());
  mqtt.subscribe((TOPICO_CENTRAL_RELOGIO + String(idDispositivo)+ String("/cor")).c_str() );
  mqtt.subscribe((TOPICO_CENTRAL_RELOGIO + String(idDispositivo) + String("/brilho")).c_str() );


}

/** Inicializa o MQTT com sefvidor */
void iniciarMQTT() {
  unsigned short port = (unsigned short) strtoul(mqttPort, NULL, 0);  //COnvert para numero o *char
  mqtt.setServer(mqttServer, port);   //informa qual broker e porta deve ser conectado
  mqtt.setCallback(callbackMQTT);            //atribui função de callback (função chamada quando qualquer informação de um dos tópicos subescritos chega)
  String centraId = "Relogio-";
  centraId += String(idDispositivo);


  mqtt.connect(centraId.c_str());  //validar depois a opção sem usuário ou com conforme as propriedades
  //mqtt.connect(centraId.c_str(), mqttUser.c_str(), mqttPass.c_str());
}

/** Função de tratamento do eventos de mqtt, quando recebe mensagem */
void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message recebida [");
  Serial.print(topic);
  Serial.print("] ");
  String msgPayload;
  for (int i = 0; i < length; i++) {
    msgPayload += (char)payload[i];

  }
  LOG("msg:" + msgPayload);
  mostrarDisplay("MQTT recebida \nTópico:\n" + String(topic) + "\nMensagem:\n" +   msgPayload);

  //Convertendo para String para facilitar
  String strTopico = topic;
  String mensagem = msgPayload;
  mensagem.trim();

  Serial.print("Tópico:" + strTopico);
  Serial.println(" Mensagem:" + msgPayload);
  if (strTopico.indexOf("/cor") != -1) {
    if (mensagem.length() == 6){
      Serial.print("R:");
      Serial.print(mensagem.substring(0,2));
      Serial.print("-");
      Serial.print(byte(strtoul(mensagem.substring(0,2).c_str(), NULL, 16)));

      Serial.print("G:");
      Serial.print(mensagem.substring(2,4));

      Serial.print("B:");
      Serial.println(mensagem.substring(4,6));

      
      
      corSelecionada = fita.Color( byte(strtoul(mensagem.substring(0,2).c_str(), NULL, 16)),byte(strtoul(mensagem.substring(2,4).c_str(), NULL, 16)),
      byte(strtoul(mensagem.substring(4,6).c_str(), NULL, 16)));
      LOG("----Cor ajustada----");
      Serial.println(corSelecionada);
    }
  }else if (strTopico.indexOf("/brilho") != -1) {
    brilhoSelecionado = mensagem.toInt();
    fita.setBrightness(brilhoSelecionado);
    LOG("----Brilho ajustado----");
    Serial.println(brilhoSelecionado);
  }



  return;
}


/** Inicializa o wifi do esp8266 */
void iniciarWIFI() {
  LOG("Iniciando WIFI ...");

  WiFi.mode(WIFI_STA); // ajustar o modo de operação, esp defaults to STA+AP
  // gotIpEventHandler = WiFi.onStationModeGotIP(wifiObteveIPHandler);
  // disconnectedEventHandler = WiFi.onStationModeDisconnected(wifiDisconectadoHandler);

  //set config save notify callback
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setConfigPortalBlocking(false);

  LOG("WIFIManager ativado ...");
  wm.setConfigPortalBlocking(false);
  //Se desejar ressetar as configurações via código
  // wm.resetSettings();
  // wm.erase();

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqttServer, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", MQTTPort, 6);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", MQTTUser, 20);
  WiFiManagerParameter custom_mqtt_pass("pass", "mqtt pass", MQTTPass, 20);
  WiFiManagerParameter custom_idDispositivo("idDispositivo", "id dispositivo", idDispositivo, 4);



  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  wm.addParameter(&custom_idDispositivo);


  bool res;
  // res = wm.autoConnect(); // auto generated AP name from chipid
  // res = wm.autoConnect("AutoConnectAP"); // anonymous ap
  res = wm.autoConnect("ESPIOT", "esp12345"); // password protected ap

  if (!res) {
    LOG("Falha para conectar e tempo excedido de espera");
    delay(3000);
    // Se ainda não tiver conectado podemos fazer o reset iniciar tudo novamente
    //ESP.restart();
    //delay(5000);
  }
  else {
    //if you get here you have connected to the WiFi
    LOG("Conexão OK WIFI... :)");

    //read updated parameters
    strcpy(mqttServer, custom_mqtt_server.getValue());
    strcpy(mqttPort, custom_mqtt_port.getValue());
    strcpy(mqttUser, custom_mqtt_user.getValue());
    strcpy(mqttPass, custom_mqtt_pass.getValue());
    strcpy(idDispositivo, custom_idDispositivo.getValue());
    topicoDispositivo =  TOPICO_CENTRAL_RELOGIO;
    topicoDispositivo += String(idDispositivo);

    //Salvar as propriedades no FS
    if (salvarPropriedades) {
      gravarPropriedades();
    }
  }









}


/** Função de eventos do wifi - Desconectado */
void wifiDisconectadoHandler(const WiFiEventStationModeDisconnected& event) {
  Serial.printf("T=%d Disconectado (reason=%d)\n", millis(), event.reason);
}

/** Função de eventos do wifi - ObteveIP*/
void wifiObteveIPHandler(const WiFiEventStationModeGotIP& event) {
  delay(1);
  Serial.printf("T=%d Conectado na rede %s (%s) com IP: %s (ch: %d) hostname=%s\n", millis(), CSTR(WiFi.SSID()), TXTIP(WiFi.gatewayIP()), TXTIP(WiFi.localIP()), WiFi.channel(), CSTR(WiFi.hostname()));
  delay(1);
}


/** Máquina de estados que monitora a conexão e reconecta */
void conectar() {
  //Serial.println("Status:" + statusConexao);
  if ((WiFi.status() != WL_CONNECTED) && (statusConexao != 1)) {
    statusConexao = 0;
  }
  if ((WiFi.status() == WL_CONNECTED) && !mqtt.connected() && (statusConexao != 3))  {
    statusConexao = 2;
  }
  if ((WiFi.status() == WL_CONNECTED) && mqtt.connected() && (statusConexao != 5)) {
    statusConexao = 4;
  }
  switch (statusConexao) {
    case 0:                                               // MQTT e WiFi OFF: inicia WiFi
      LOG("MQTT e WiFi OFF: Iniciando WiFi");
      mostrarDisplay("Atenção \nWIFI OFF \nMQTT OFF \nIniciando WiFi");
      iniciarWIFI();
      statusConexao = 1;
      break;
    case 1:                                                       // WiFi starting, do nothing here
      LOG("WIFI iniciando, aguardando : " + String(contadorEspera));
      contadorEspera++;
      if (millis() - tempoUltimaAtualizacao > 180000) { //Senão não iniciou em x min, reinicia a máquia de status
        statusConexao = 0;
        tempoUltimaAtualizacao = millis();                                        //atualiza a ultima atualização status
      }
      break;
    case 2:                                                       // WiFi ON, MQTT OFF: Iniciar MQTT
      LOG("WIFI ON, MQTT OFF: iniciar MQTT");
      LOG("SSID: " + (String)wm.getWiFiSSID());
      mostrarDisplay("Conectado   WIFI ON\nIP: " + WiFi.localIP().toString() + "\nSSID: " + WiFi.SSID());
      sincronizarNTPHora();
      iniciarMQTT();
      statusConexao = 3;
      contadorEspera = 0;
      break;
    case 3:                                                       // WiFi ON, MQTT iniciando
      LOG("WIFI ON, MQTT iniciando, esperando : " + String(contadorEspera));
      contadorEspera++;
      if (millis() - tempoUltimaAtualizacao > 180000) { //Senão não iniciou em x min, reinicia a máquia de status
        statusConexao = 0;
        tempoUltimaAtualizacao = millis();                                        //atualiza a ultima atualização status
      }
      break;
    case 4:                                                       // WiFi ON, MQTT ON: finalizado MQTT config
      LOG("WIFI ON, MQTT ON: Servico MQTT OK");
      mostrarDisplay("Conectado\nIP: " + WiFi.localIP().toString() + "\nSSID: " + WiFi.SSID() + "\nMQTT State: " + String(mqtt.state()) + "\nCruzamento:" + String(idDispositivo)  );
      eventosMQTT();
      statusConexao = 5;
      contadorEspera = 0;
      break;
  }


  if (statusConexao == 5) {  //WIFI e MQTT Operando
    if (millis() - tempoUltimaAtualizacao > 30000) {                            //A cada 30 seg
      LOG(statusCentral);
      mqtt.publish(topicoDispositivo.c_str() , statusCentral);                  // Envia status a central

      mqtt.loop();                                                              //Aciona loop mqtt
      tempoUltimaAtualizacao = millis();                                        //atualiza a ultima atualização status
    }
    if (millis() - tempoUltimaAtualizacaoSensor > 300000) {       //A cada 5min seg
      LOG("Enviando dados MQTT tópico:" + topicoDispositivo);
      lerSensores();

      enviaDadosMQTT();

      tempoUltimaAtualizacaoSensor = millis();                                        //atualiza a ultima atualização status dos sensores
    }

    mqtt.loop();                                              // loop MQTT
  }


  // Executando tarefas que não depende de WIFI E MQTT
  if (millis() - lastTask > 5000) {                                 // A cada 5seg print na serial
    LOG("Operando. ID:" + String(idDispositivo));
    lastTask = millis();
  }
  delay(100);

}


void mostrarDisplay(String str) {
  Serial.println(str);
}

void mostrar(String valor, byte colorR, byte colorG, byte colorB) {
  mostrar(valor, fita.Color( colorR,colorG,colorB) );
}

void mostrar(String valor,uint32_t cor ) {
  LOG(valor);
  byte tamanho =  valor.length();
  for (byte iDig = 0; iDig < 4; iDig++) {
    if (iDig < tamanho) {
      mostrarDigito(iDig, valor.charAt(iDig), cor);
    } else {
      mostrarDigito(iDig, ' ', cor);
    }
  }
  fita.show();

}
void mostrarDigito(byte digito, char valor, byte colorR, byte colorG, byte colorB) {
  mostrarDigito( digito, valor, fita.Color( colorR,colorG,colorB));
}

void mostrarDigito(byte digito, char valor,uint32_t cor ) {
  byte ledInicio = 0;
  const String letras = "0123456789g-EUC PIOHB";
  const byte segmentos[] = {63, 12, 118, 94, 77, 91, 123, 14, 127, 79, 71, 64, 115, 61, 51, 0, 103, 33, 63, 109, 121};
  ledInicio = digito * 21 + (digito > 1 ? 2 : 0); // Posiçao * (7segmentos * 3led por segmento)
  for (byte iSeg = 0; iSeg <= 6; iSeg++) {
    if (bitRead(segmentos[letras.indexOf(valor)], iSeg)) {
      for (byte index = 0; index < NUM_LED_SEGMENTO; index++) {
        fita.setPixelColor(ledInicio, cor);
        ledInicio += 1;
      }
    } else {
      for (byte index = 0; index < NUM_LED_SEGMENTO; index++) {
        fita.setPixelColor(ledInicio, 0);
        ledInicio += 1;
      }
    }
  }

  yield();
}

/** Efeito fita de led */
void rainbow(int wait) {
  long firstPixelHue = 0;
  for (int i = 0; i < fita.numPixels(); i++) { // For each pixel in strip...
    int pixelHue = firstPixelHue + (i * 65536L / fita.numPixels());
    fita.setPixelColor(i, fita.gamma32(fita.ColorHSV(pixelHue)));
    fita.show(); // Update strip with new contents
    delay(wait);  // Pause for a moment
  }

}


void setColor(uint32_t color) {
  for (int i = 0; i < NUM_PIXELS; i++) {
    fita.setPixelColor(i, color);
    fita.show();
    delay(50);
  }
}

void testaFitaLed() {
  LOG("Testando fita vermelho");
  setColor(fita.Color(100, 0, 0));
  delay(800);
  yield();
  LOG("Testando fita verde");
  setColor(fita.Color(0, 100, 0));
  delay(800);
  yield();
  LOG("Testando fita azul");
  setColor(fita.Color(0, 0, 100));
  delay(800);
  yield();
  LOG("Testando fita colorida");
  rainbow(40);
  delay(500);
  yield();
  LOG("Apagando a fita");
  fita.clear(); //Apagar a fita
  fita.show();
  yield();

}


void testarDallas() {
  Serial.println("******   Testando DS18B20   ***");

  int numberOfDevices = sensorDallas.getDeviceCount();

  // locate devices on the bus
  Serial.print("Locating devices...");

  Serial.print("Found ");
  Serial.print(numberOfDevices, DEC);
  Serial.println(" devices.");

  // report parasite power requirements
  Serial.print("Parasite power is: ");
  if (sensorDallas.isParasitePowerMode()) Serial.println("ON");
  else Serial.println("OFF");

  // Loop through each device, print out address
  for (int i = 0; i < numberOfDevices; i++)
  {
    // Search the wire for address
    if (sensorDallas.getAddress(tempDeviceAddress, i))
    {
      Serial.print("Found device ");
      Serial.print(i, DEC);
      Serial.print(" with address: ");
      printAddress(tempDeviceAddress);
      Serial.println();

      Serial.print("Setting resolution to ");
      Serial.println(TEMPERATURE_PRECISION, DEC);

      // set the resolution to TEMPERATURE_PRECISION bit (Each Dallas/Maxim device is capable of several different resolutions)
      sensorDallas.setResolution(tempDeviceAddress, TEMPERATURE_PRECISION);

      Serial.print("Resolution actually set to: ");
      Serial.print(sensorDallas.getResolution(tempDeviceAddress), DEC);
      Serial.println();
    } else {
      Serial.print("Encontrou ");
      Serial.print(i, DEC);
      Serial.print(" sensores, mas não consegui ler o endereço. Verifique as ligações");
    }
  }


  lerDallas();
  Serial.println("\n******   FIM DS18B20   *****");
}

void lerSensores() {
  lerDHT();
  lerDallas();
  luminosidade = getLuminosidade();
}

void enviaDadosMQTT() {

      mqtt.publish((topicoDispositivo + "/ldr").c_str(), String(luminosidade).c_str());
      mqtt.publish((topicoDispositivo + "/dht-temp").c_str() , String(temperaturaDHT).c_str());
      mqtt.publish((topicoDispositivo + "/dht-umi").c_str() , String(umidadeDHT).c_str());
      mqtt.publish((topicoDispositivo + "/dht-sen").c_str() , String(sensacaoDHT).c_str());
      mqtt.publish((topicoDispositivo + "/ds").c_str() , String(temperaturaDS).c_str());



}


void testaDHT() {
  Serial.println("******   Testando DHT 11   ***");
  lerDHT();
  delay(50);
  Serial.printf("Umidade DHT: %f \n", umidadeDHT);
  delay(50);
  Serial.printf("Temperatura DHT: %f \n" , temperaturaDHT);
  delay(50);
  Serial.printf("Sensacao Termica DHT: %f \n" , sensacaoDHT);
  delay(200);
}

// Realiza a leitura do DHT11 e salva os valores nas váriasveis globais
void lerDHT() {

  float umidade = dht.readHumidity();
  float temperatura = dht.readTemperature();

  if (isnan(umidade) || isnan(temperatura) ) {
    Serial.println(F("Falha ao ler o sensor DHT!"));
    return;
  } else {
    temperaturaDHT = temperatura;
    umidadeDHT = umidade;
    sensacaoDHT = dht.computeHeatIndex(temperatura, umidade, false); //calcula sensaçao termica

  }

}
bool isChaveDesligada() {
  if (digitalRead(CHAVEPIN) == HIGH)
    return true;
  else
    return false;
}

int getLuminosidade() {
  return analogRead(LDRPIN);
}

// function to print a device address DS18B20
void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}
