#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <Arduino_JSON.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>

String listenerDeviceName = "tally";

/* USER CONFIG VARIABLES
 *  Change the following variables before compiling and sending the code to your device.
 */

const int led_program = 12;
const int led_preview = 13;


// Tally Arbiter Server
char tallyarbiter_host[40] = "10.75.23.105"; // IP address of the Tally Arbiter Server
char tallyarbiter_port[6] = "4455";

/* END OF USER CONFIG */

Preferences preferences;

String actualType = "";
int actualPriority = 0;

SocketIOclient socket;
JSONVar BusOptions;
JSONVar Devices;
JSONVar DeviceStates;
String DeviceId = "unassigned";
String DeviceName = "Unassigned";
WiFiManager wm; // global wm instance

bool networkConnected = false;

void setup() {
  // switch off LEDs
  pinMode(led_program, OUTPUT);
  digitalWrite(led_program, LOW);
  pinMode(led_preview, OUTPUT);
  digitalWrite(led_preview, LOW);
  
  Serial.begin(115200);
  while (!Serial);

  // Initialize ESP32
  logger("Initializing ESP32.");

  setCpuFrequencyMhz(80); // Save battery by turning down the CPU clock
  btStop(); // Save battery by turning off BlueTooth

  uint64_t chipid = ESP.getEfuseMac();
  listenerDeviceName = "tally-" + String((uint16_t)(chipid>>32)) + String((uint32_t)chipid);

  logger("Listener device name: " + listenerDeviceName);

  preferences.begin("tally-arbiter", false);

  // added to clear out corrupt prefs
  // preferences.clear();
  logger("Reading preferences");
  if (preferences.getString("deviceid").length() > 0){
    DeviceId = preferences.getString("deviceid");
  }
  if (preferences.getString("devicename").length() > 0){
    DeviceName = preferences.getString("devicename");
  }
  if (preferences.getString("taHost").length() > 0){
    String newHost = preferences.getString("taHost");
    logger("Setting TallyArbiter host as" + newHost);
    newHost.toCharArray(tallyarbiter_host, 40);
  }
  if (preferences.getString("taPort").length() > 0){
    String newPort = preferences.getString("taPort");
    logger("Setting TallyArbiter port as" + newPort);
    newPort.toCharArray(tallyarbiter_port, 6);
  }
 
  preferences.end();

  delay(100);
  connectToNetwork();
  while (!networkConnected) {
    delay(200);
  }

  ArduinoOTA.setHostname(listenerDeviceName.c_str());
  ArduinoOTA.setPassword("tallyarbiter");
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) logger("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) logger("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) logger("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) logger("Receive Failed");
      else if (error == OTA_END_ERROR) logger("End Failed");
    });

  ArduinoOTA.begin();
  connectToServer();
}

void loop() {
  ArduinoOTA.handle();
  socket.loop();
}

void showDeviceInfo() {
  evaluateMode();
}

void logger(String strLog) {
  Serial.println(strLog);
}

void flashAlternate(int cycles) {
  for (int i = 0; i < cycles; i++) {
    digitalWrite(led_preview, LOW);
    digitalWrite(led_program, HIGH);
    delay(150);
    digitalWrite(led_program, LOW);
    delay(150);
    digitalWrite(led_preview, HIGH);
    delay(150);
    digitalWrite(led_preview, LOW);
    delay(150);
  }
}

void connectToNetwork() {
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  logger("Connecting to SSID: " + String(WiFi.SSID()));

  // reset settings - wipe credentials for testing
  // wm.resetSettings();

  WiFiManagerParameter custom_taServer("taHostIP", "Tally Arbiter Server", tallyarbiter_host, 40);
  WiFiManagerParameter custom_taPort("taHostPort", "Port", tallyarbiter_port, 6);

  wm.addParameter(&custom_taServer);
  wm.addParameter(&custom_taPort);
  wm.setSaveParamsCallback(saveParamCallback);

  // custom menu via array or vector
  std::vector<const char *> menu = {"wifi","param","info","sep","restart","exit"};
  wm.setMenu(menu);

  // set dark theme
  wm.setClass("invert");

  wm.setConfigPortalTimeout(120); // auto close configportal after n seconds

  bool res;

  res = wm.autoConnect(listenerDeviceName.c_str()); // AP name for setup

  if (!res) {
    logger("Failed to connect");
    // ESP.restart();
  } else {
    //if you get here you have connected to the WiFi
    logger("Connected to WiFi successfully");
    networkConnected = true;

    // flash LEDs as confirmation
    flashAlternate(5);

    //TODO: fix MDNS discovery
    /*
    int nrOfServices = MDNS.queryService("tally-arbiter", "tcp");

    if (nrOfServices == 0) {
      logger("No server found.");
    } else {
      logger("Number of servers found: ");
      Serial.print(nrOfServices);
     
      for (int i = 0; i < nrOfServices; i=i+1) {
 
        Serial.println("---------------");
       
        Serial.print("Hostname: ");
        Serial.println(MDNS.hostname(i));
 
        Serial.print("IP address: ");
        Serial.println(MDNS.IP(i));
 
        Serial.print("Port: ");
        Serial.println(MDNS.port(i));
 
        Serial.println("---------------");
      }
    }
    */
  }
}

String getParam(String name) {
  //read parameter from server, for custom input
  String value;
  if (wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}


void saveParamCallback() {
  logger("[CALLBACK] saveParamCallback fired");
  logger("PARAM tally Arbiter Server = " + getParam("taHostIP"));
  String str_taHost = getParam("taHostIP");
  String str_taPort = getParam("taHostPort");

  // str_taHost.toCharArray(tallyarbiter_host, 40);
  // saveEEPROM();
  logger("Saving new TallyArbiter host");
  logger(str_taHost);
  preferences.begin("tally-arbiter", false);
  preferences.putString("taHost", str_taHost);
  preferences.putString("taPort", str_taPort);
  preferences.end();
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_STA_GOT_IP:
      logger("Network connected!");
      logger(WiFi.localIP().toString());
      networkConnected = true;
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      logger("Network connection lost!");
      networkConnected = false;
      break;
    default:
      break;
  }
}

void wsEmit(String event, const char *payload = NULL) {
  if (payload) {
    String msg = "[\"" + event + "\"," + payload + "]";
    // Serial.println(msg);
    socket.sendEVENT(msg);
  } else {
    String msg = "[\"" + event + "\"]";
    // Serial.println(msg);
    socket.sendEVENT(msg);
  }
}

void connectToServer() {
  logger("Connecting to Tally Arbiter host: " + String(tallyarbiter_host));
  socket.onEvent(onSocketEvent);
  socket.begin(tallyarbiter_host, atol(tallyarbiter_port));
}

void onSocketEvent(socketIOmessageType_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case sIOtype_CONNECT:
      onSocketConnected((char*)payload, length);
      break;

    case sIOtype_DISCONNECT:
    case sIOtype_ACK:
    case sIOtype_ERROR:
    case sIOtype_BINARY_EVENT:
    case sIOtype_BINARY_ACK:
      // Not handled
      break;

    case sIOtype_EVENT:
      String msg = (char*)payload;
      String type = msg.substring(2, msg.indexOf("\"",2));
      String content = msg.substring(type.length() + 4);
      content.remove(content.length() - 1);

      logger("Got event '" + type + "', data: " + content);

      if (type == "bus_options") BusOptions = JSON.parse(content);
      if (type == "reassign") onSocketReassign(content);
      if (type == "flash") socket_Flash();

      if (type == "deviceId") {
        DeviceId = content.substring(1, content.length()-1);
        setDeviceName();
        showDeviceInfo();
      }

      if (type == "devices") {
        Devices = JSON.parse(content);
        setDeviceName();
      }

      if (type == "device_states") {
        DeviceStates = JSON.parse(content);
        processTallyData();
      }

      break;
  }
}

void onSocketConnected(const char * payload, size_t length) {
  logger("Connected to Tally Arbiter server.");
  logger("DeviceId: " + DeviceId);
  String deviceObj = "{\"deviceId\": \"" + DeviceId + "\", \"listenerType\": \"" + listenerDeviceName.c_str() + "\", \"canBeReassigned\": true, \"canBeFlashed\": true, \"supportsChat\": true }";
  char charDeviceObj[1024];
  strcpy(charDeviceObj, deviceObj.c_str());
  wsEmit("listenerclient_connect", charDeviceObj);
}

void socket_Flash() {
  flashAlternate(2);
  showDeviceInfo();
}


String stripQuotes(String str) {
  if (str[0] == '"') {
    str.remove(0, 1);
  }
  if (str.endsWith("\"")) {
    str.remove(str.length()-1, 1);
  }
  return str;
}

void onSocketReassign(String payload) {
  String oldDeviceId = payload.substring(0, payload.indexOf(','));
  String newDeviceId = payload.substring(oldDeviceId.length()+1);
  newDeviceId = newDeviceId.substring(0, newDeviceId.indexOf(','));
  oldDeviceId = stripQuotes(oldDeviceId);
  newDeviceId = stripQuotes(newDeviceId);

  String reassignObj = "{\"oldDeviceId\": \"" + oldDeviceId + "\", \"newDeviceId\": \"" + newDeviceId + "\"}";
  char charReassignObj[1024];
  strcpy(charReassignObj, reassignObj.c_str());
  wsEmit("listener_reassign_object", charReassignObj);
  wsEmit("devices");
  
  digitalWrite(led_program, HIGH);
  digitalWrite(led_preview, HIGH);
  delay(200);
  digitalWrite(led_program, LOW);
  digitalWrite(led_preview, LOW);
  delay(200);
  digitalWrite(led_program, HIGH);
  digitalWrite(led_preview, HIGH);
  delay(200);
  digitalWrite(led_program, LOW);
  digitalWrite(led_preview, LOW);

  logger("newDeviceId: " + newDeviceId);
  DeviceId = newDeviceId;
  preferences.begin("tally-arbiter", false);
  preferences.putString("deviceid", newDeviceId);
  preferences.end();
  setDeviceName();
}

void processTallyData() {
  bool typeChanged = false;
  for (int i = 0; i < DeviceStates.length(); i++) {
    if (DeviceStates[i]["sources"].length() > 0) {
      typeChanged = true;
      actualType = stripQuotes(getBusTypeById(JSON.stringify(DeviceStates[i]["busId"])));
      // actualPriority = getBusPriorityById(JSON.stringify(DeviceStates[i]["busId"]));
    }
  }
  if(!typeChanged) {
    actualType = "";
    // actualPriority = 0;
  }
  evaluateMode();
}

String getBusTypeById(String busId) {
  for (int i = 0; i < BusOptions.length(); i++) {
    if (JSON.stringify(BusOptions[i]["id"]) == busId) {
      return JSON.stringify(BusOptions[i]["type"]);
    }
  }

  return "invalid";
}


/*int getBusPriorityById(String busId) {
  for (int i = 0; i < BusOptions.length(); i++) {
    if (JSON.stringify(BusOptions[i]["id"]) == busId) {
      return (int) JSON.stringify(BusOptions[i]["priority"]).toInt();
    }
  }

  return 0;
}*/

void setDeviceName() {
  for (int i = 0; i < Devices.length(); i++) {
    if (JSON.stringify(Devices[i]["id"]) == "\"" + DeviceId + "\"") {
      String strDevice = JSON.stringify(Devices[i]["name"]);
      DeviceName = strDevice.substring(1, strDevice.length() - 1);
      break;
    }
  }
  preferences.begin("tally-arbiter", false);
  preferences.putString("devicename", DeviceName);
  preferences.end();
  evaluateMode();
}

void evaluateMode() {
  if (actualType == "program") {
    digitalWrite(led_program, HIGH);
    digitalWrite (led_preview, LOW);
  } else if (actualType == "preview") {
    digitalWrite(led_program, LOW);
    digitalWrite (led_preview, HIGH);
  } else {
    digitalWrite(led_program, LOW);
    digitalWrite (led_preview, LOW);
  }

  logger("type " + actualType + ", priority " + String(actualPriority));
}
