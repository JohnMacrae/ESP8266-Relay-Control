#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <FS.h>
//#include <OneWire.h>
//#include <DallasTemperature.h>
//#include <Wire.h>
//#include <Adafruit_PWMServoDriver.h>

const char* vers = "1.2";
const char* ssid = "";
const char* password = "";
const char* host = "StbdFan";
const char* MQ_client = "StbdFan";       // your MQTT Client ID
const char* MQ_user = "";       // your MQTT password
const char* MQ_pass = "";       // your network password
char server[]="192.168.2.10";

int inPin = 0;         // the number of the input pin
int outPin = D7;       // the number of the output pin

int state = HIGH;      // the current state of the output pin
int reading;           // the current reading from the input pin
int previous = LOW;    // the previous reading from the input pin

// the follow variables are long's because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
long atime = 0;         // the last time the output pin was toggled
long debounce = 200;   // the debounce time, increase if the output flickers

const char* adaprefix = ""; //"jramacrae/feeds/"
 
long ADCfactor = 16;
const unsigned int MAX_INPUT = 50;
extern "C" long DS_loop();
int fmode = 0;

struct SettingsStruct
{
  char          WifiSSID[32];
  char          WifiKey[64];
  char          WifiAPKey[64];
  char          ControllerUser[26];
  char          ControllerPassword[64];
  char          Host[26];
  char          Broker[4];
  long          PWM_pin;
  char          BrokerIP[16];
  char          Version[16];
} Settings;

boolean ok;
unsigned long start = 0;
unsigned long timer60 = 0;
unsigned long timer1 = 0;
unsigned long GPIOCounter = 0;
unsigned long TimerReboot = 0;

int ledState = HIGH;         // the current state of the output pin
int buttonState;             // the current reading from the input pin
int lastButtonState = LOW;   // the previous reading from the input pin

// the following variables are long's because the time, measured in miliseconds,
// will quickly become a bigger number than can be stored in an int.
long lastDebounceTime = 0;  // the last time the output pin was toggled
long debounceDelay = 200;    // the debounce time; increase if the output flickers
const int buttonPin = 0; 
 
void process_data (const char * data)
  {
  // for now just display it
  // (but you could compare it to some value, convert to an integer, etc.)
  Serial.println (data);
  char topic[0];
  Controller(topic, data);
  }  // end of process_data
  
void processIncomingByte (const byte inByte)
  {
  static char input_line [MAX_INPUT];
  static unsigned int input_pos = 0;

  switch (inByte)
    {

    case '\n':   // end of text
      input_line [input_pos] = 0;  // terminating null byte
      
      // terminator reached! process input_line here ...
      process_data (input_line);
      
      // reset buffer for next time
      input_pos = 0;  
      break;

    case '\r':   // discard carriage return
      break;

    default:
      // keep adding if not full ... allow for terminating null byte
      if (input_pos < (MAX_INPUT - 1))
        input_line [input_pos++] = inByte;
      break;

    }  // end of switch
   
  } // end of processIncomingByte  

void callback(char* topic, byte* payload, unsigned int length) {
  int pinstate = 1;
  char buf[80];
  // handle message arrived
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  int i = 0;
  for ( i=0;i<length;i++) {
   //Serial.print((char)payload[i]); 
   buf[i] = payload[i];
   //Serial.println("");
  }
   buf[i] = '\0';
  Controller(topic, buf);
}
 WiFiClient WifiClient;
  PubSubClient client(server, 1883, callback, WifiClient);
  
/********************************************************************************************\
  Save data into config file on SPIFFS
  \*********************************************************************************************/
void SaveToFile(char* fname, int index, byte* memAddress, int datasize)
{
  File f = SPIFFS.open(fname, "r+");
  if (f)
  {
    f.seek(index, SeekSet);
    byte *pointerToByteToSave = memAddress;
    for (int x = 0; x < datasize ; x++)
    {
      f.write(*pointerToByteToSave);
      pointerToByteToSave++;
    }
    f.close();
    String log = F("FILE : File saved");
//    addLog(LOG_LEVEL_INFO, log);
  }
}

/********************************************************************************************\
  Load data from config file on SPIFFS
  \*********************************************************************************************/
void LoadFromFile(char* fname, int index, byte* memAddress, int datasize)
{
  File f = SPIFFS.open(fname, "r+");
  if (f)
  {
    f.seek(index, SeekSet);
    byte *pointerToByteToRead = memAddress;
    for (int x = 0; x < datasize; x++)
    {
      *pointerToByteToRead = f.read();
      pointerToByteToRead++;// next byte
    }
    f.close();
  }
}

  /********************************************************************************************\
  Save settings to SPIFFS
  \*********************************************************************************************/
void SaveSettings(void)
{
  SaveToFile((char*)"config.txt", 0, (byte*)&Settings, sizeof(struct SettingsStruct));
  //SaveToFile((char*)"security.txt", 0, (byte*)&SecuritySettings, sizeof(struct SecurityStruct));
Serial.print("Settings Saved - really!!!!!");
}


/********************************************************************************************\
  Load settings from SPIFFS
  \*********************************************************************************************/
boolean LoadSettings()
{
  LoadFromFile((char*)"config.txt", 0, (byte*)&Settings, sizeof(struct SettingsStruct));
//  LoadFromFile((char*)"security.txt", 0, (byte*)&SecuritySettings, sizeof(struct SecurityStruct));
Serial.print("Settings Loaded - really!!!!!");
}

void ResetSettings()
  {
   
   strcpy_P ( Settings.WifiSSID, ssid);
   strcpy_P ( Settings.WifiKey, password);
   strcpy_P ( Settings.WifiAPKey, host);
   strcpy_P ( Settings.ControllerUser, MQ_user);
   strcpy_P ( Settings.ControllerPassword, MQ_pass);
   strcpy_P ( Settings.Host, MQ_client);
   Settings.PWM_pin = 13;
   strcpy_P ( Settings.BrokerIP , server);
   strcpy_P ( Settings.Version , vers);
  
  SaveSettings();
  SPIFFS.end();
  ESP.restart();
  }

void runEachSecond()
{
  start = micros();
  timer1 = millis() + 500;

  Serial.print("second");
  // read the state of the switch into a local variable:
  int reading = digitalRead(0);

  // check to see if you just pressed the button
  // (i.e. the input went from LOW to HIGH),  and you've waited
  // long enough since the last press to ignore any noise:

  // If the switch changed, due to noise or pressing:
  if (reading != lastButtonState) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != buttonState) {
      buttonState = reading;

      // only toggle the LED if the new button state is HIGH
      if (buttonState == HIGH) {
        ledState = !ledState;
        
      }
    }
}
digitalWrite(12, ledState);
Serial.print(ledState);
  // save the reading.  Next time through the loop,
  // it'll be the lastButtonState:
  lastButtonState = reading;
}


void MQTT_Topic(char *Host, char *Topic, long Value)
{
  char charBuf[50]={0};
  ltoa(Value,charBuf,10);
  char pTopic[30]={0};
  strcat (pTopic,Host);
  strcat (pTopic,"/");
  strcat (pTopic,Topic);
  client.publish(pTopic,charBuf);
  Serial.printf("%s\%s : \"%s\"xxx\n",pTopic, Topic, charBuf);
}

void runEach60Seconds()
{
  start = micros();
  timer60 = millis() + 10000;

  TimerReboot++;
 Serial.println("increment");
  if(TimerReboot > 250)
  {
   Serial.print("Restarting");
   delay(1000);
    ESP.restart();
  }
}
 
  void PrintSettings()
{
    Serial.println("Printing...");
    char pTopic[50]={0};
    char charBuf[10];
    strcat (pTopic, adaprefix);
    strcat (pTopic,Settings.Host);
    strcat (pTopic,"/");
    strcat (pTopic,"outTopic");
    client.publish(pTopic,Settings.WifiSSID);
    client.publish(pTopic,Settings.ControllerUser);
    client.publish(pTopic,Settings.Host);
    ltoa(Settings.PWM_pin,charBuf,10);
    client.publish(pTopic,charBuf);  
    client.publish(pTopic,Settings.BrokerIP);
    client.publish(pTopic,Settings.Version);     
}

  void Controller(const char *topic,const char *buf)
  {

  char * pch;
  int i=0;
  long pos = 0;
  int pos2 = 0;
  char gpio[20];
  char val[20];
  char command[20];
  long int lgpio = 0;
  Serial.printf ("Topic[%s]\n",topic);
  Serial.printf ("Looking for the ':' character in \"%s\"...\n",buf);
  pch=strchr(buf,':');
  Serial.printf ("found at %d\n",pch-buf+1);
  if(pch !=NULL){
   pos = pch-buf+1;
  
   pch=strchr(pch+1,':');
  
   if(pch !=NULL){
    pos2 = pch-buf+1;
    //Serial.printf("pos2 \"%d\"...\n",pos2);
   // Serial.printf("pos \"%d\"...\n",pos);
    }
  }else{
    pos = strlen(buf)+1;
    pos2 = pos;
  }
  
  for (i=0;i<pos-1;i++) {
   
    command[i] = buf[i];
 
   }
    command[i] = '\0';
    // Serial.println("");
     Serial.printf("Command \"%s\"\n",command);
     //Serial.println("");
    
    if((pos2-pos)>0){
      for ( i=pos;i<pos2-1;i++) {
    
        gpio[i-pos] = buf[i];
   
       }
      gpio[i-pos] = '\0';
     Serial.printf("gpio \"%s\"\n",gpio);
     lgpio=atol(gpio);
    }
       
    if(pos2>0){
      Serial.print("val = "); 
      for (i=pos2; i<(unsigned)strlen(buf); i++) {
      
        val[i-pos2] = buf[i];

        }
      val[i-pos2] = '\0';
        }

    long int lval = atol(val);
    Serial.printf("value \"%d\"...\n",lval);

    boolean resetflag = false;
   
    if (strcmp (command ,"resetsettings")==0){
    Serial.printf("command \"%d\".....\n",command);
    ResetSettings();
    }
    else if(strcmp (command ,"BrokerIP")==0){
      //Serial.printf("%d",lval);
      strcpy(Settings.BrokerIP,val);
      resetflag = 1;
    }  
     else if(strcmp (command ,"WiFi")==0){
    strcpy(Settings.WifiSSID,gpio);
    strcpy(Settings.WifiKey,val);
    resetflag = 1;
    }
    else if(strcmp (command , "Host")==0){
    strcpy_P (Settings.Host,val);
     resetflag = true;
    }
     else if(strcmp (command , "Reboot")==0){
     resetflag = true;
    }
     else if(strcmp (command , "Reset")==0){
     ResetSettings();
    }
         
    else if(strcmp (command , "Version")==0){
     char pTopic[30]={0};
     strcat (pTopic,Settings.Host);
     strcat (pTopic,"/");
     strcat (pTopic,"outTopic");
     client.publish(pTopic,vers);
    }
    else if(strcmp (command , "GPIO")==0){
    pinMode(lgpio, OUTPUT);
    char msg[30]={0};
    Serial.print(lval/2);
    if (lval==1){
      Serial.printf("GPIO \"%d\"...\n",lgpio);
      digitalWrite(lgpio, HIGH);
      strcat(msg,"GPIO - On");
      state = HIGH;
      }
      if (lval==0) {
      digitalWrite(lgpio, LOW);
      state = LOW;
      strcat(msg,"GPIO - Off");
      Serial.printf("GPIO-off \"%d\"...\n",lgpio);
      }
      if (lval>1){
      digitalWrite(lgpio, LOW);
      strcat(msg,"GPIO - Timer");
      Serial.printf("GPIO-on - timer \"%d\"...\n",lgpio);
      GPIOCounter = millis() + (1000 * lval);
      }
      if (client.connect("", Settings.ControllerUser , Settings.ControllerPassword)) {
        char topic[30]={0};
        strcat(topic,Settings.Host);
        strcat(topic,"/");
        strcat(topic,"outTopic");
        client.publish( topic, msg);
        }
    }
     else if(strcmp (command , "PrintSettings")==0){
     PrintSettings();
     }
    SaveSettings();
    if(resetflag){
      ESP.restart();
    }
}

void fileSystemCheck()
{
  if (SPIFFS.begin())
  {
    Serial.println("SPIFFS Mount successful");
    //addLog(LOG_LEVEL_INFO, log);
    File f = SPIFFS.open("config.txt", "r");
    if (!f)
    {
      Serial.println("formatting...");
      //addLog(LOG_LEVEL_INFO, log);
      SPIFFS.format();
      Serial.println("format done!");
      //addLog(LOG_LEVEL_INFO, log);
      File f = SPIFFS.open("config.txt", "w");
      if (f)
      {
        for (int x = 0; x < 32768; x++)
          f.write(0);
        f.close();
      }
    }
  }
  else
  {
    Serial.println("SPIFFS Mount failed");
    //addLog(LOG_LEVEL_INFO, log);
  }
}

void setup() {
   pinMode(inPin, INPUT);
   pinMode(outPin, OUTPUT);
  
   Serial.begin(115200);
   fileSystemCheck();
   LoadSettings();
   Serial.println(vers);
   Serial.println(Settings.Version);
   if(strcmp(Settings.Version, vers) != 0)//if its a new version Change the settings back
     {
      ResetSettings();
     }
   
   Serial.println("Booting");
   WiFi.hostname(Settings.Host);
   WiFi.mode(WIFI_STA);
   Serial.println(Settings.WifiSSID);
  // Serial.println(Settings.WifiKey);
   Serial.println(Settings.Host);
   WiFi.begin(Settings.WifiSSID, Settings.WifiKey);
   
   int count =0;
    while ((count<15)&&(WiFi.status() != WL_CONNECTED)){
     
     //WiFi.begin(Settings.WifiSSID, Settings.WifiKey);    
     Serial.print(WiFi.status());
     Serial.println(" Retrying connection...");
     delay(1000);
     count++;
     while (Serial.available () > 0)
     processIncomingByte (Serial.read ());
    }
   
   Serial.println(Settings.BrokerIP);
   Serial.print("Host: ");
   Serial.print(Settings.Host);
    if (client.connect("", Settings.ControllerUser , Settings.ControllerPassword)) {
    char topic[30]={0};
    char msg[30]= {0};
    strcat(msg,Settings.Host);
    strcat(msg,"Hello World");
    strcat(topic,adaprefix);
    strcat(topic,Settings.Host);
    strcat(topic,"/");
    strcat(topic,"outTopic");
    client.publish( topic, msg);
    Serial.println(topic);
    memset(&topic[0], 0, sizeof(topic));
    strcat(topic,adaprefix);
    strcat(topic,Settings.Host); 
    client.subscribe(topic,1);
    Serial.println(topic);
    Serial.print("Connected ");
    Serial.println(Settings.BrokerIP);
   }

   //  Initialise OTA
   ArduinoOTA.setHostname(Settings.Host);
   ArduinoOTA.onStart([]() { // switch off all the PWMs during upgrade
                       SPIFFS.end();
   });

   ArduinoOTA.onEnd([]() { // do a fancy thing with our board led at end
                        
                        });

   ArduinoOTA.onError([](ota_error_t error) { ESP.restart(); });

   /* setup the OTA server */
   ArduinoOTA.begin();
   Serial.println("Ready");
   Serial.print("IP address: ");
   Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  int count = 0;
  
  while( (count<15)&&(WiFi.status() != WL_CONNECTED)){
     //WiFi.begin(Settings.WifiSSID, Settings.WifiKey);    
     Serial.print(WiFi.status());
     Serial.println(" Retrying connection...");
     delay(1000);
     count++;
     while (Serial.available () > 0)
     processIncomingByte (Serial.read ());
    
   }
   
  if(WiFi.status() != WL_CONNECTED);
  {
    count=0;
    while (!client.connected()&&(count < 5)) {
     Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    Serial.print(Settings.BrokerIP);
    Serial.print(Settings.ControllerUser);
    Serial.print(Settings.ControllerPassword);
    if (client.connect(Settings.Host, Settings.ControllerUser , Settings.ControllerPassword)){ 
    Serial.println("connected");
    char topic[30]={0};
    strcat(topic,adaprefix);
    strcat(topic,Settings.Host);
    strcat(topic,"/outTopic");
    client.publish( topic, "hello world");
    memset(&topic[0], 0, sizeof(topic));
    strcat(topic,adaprefix);
    strcat(topic,Settings.Host);
    strcat(topic,"/system"); 
    client.subscribe(topic,1);
    Serial.print(topic);
      } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 1 second");
      // Wait 5 seconds before retrying
        delay(1000);
      }
    count++;
    }
  }
}

void loop() {

  if (!client.connected()) {
    reconnect();
  }
  
  ArduinoOTA.handle();
  
  if (millis() > timer60){
      runEach60Seconds();
  }

  if (millis() > GPIOCounter){
      digitalWrite(2, HIGH);
  }
    
  client.loop();
  
  while (Serial.available () > 0){
    processIncomingByte (Serial.read ());
  }
  
  reading = digitalRead(inPin);

  // if the input just went from LOW and HIGH and we've waited long enough
  // to ignore any noise on the circuit, toggle the output pin and remember
  // the time
  if (reading == HIGH && previous == LOW && millis() - atime > debounce) {
    if (state == HIGH)
      state = LOW;
    else
      state = HIGH;

    atime = millis();    
  }

  digitalWrite(outPin, state);

  previous = reading;
  delay(1000);
}
