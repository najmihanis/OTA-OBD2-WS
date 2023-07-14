// Import required libraries
#include <CAN.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include "cert.h"
#define TX_GPIO_NUM   5  // Connects to CTX
#define RX_GPIO_NUM   4  // Connects to CRX

//*********************************************OTA pre-setup********************************************************************
// to connect to outside wifi
const char * ssid_out = "Kommu@unifi";
const char * wifiPassword_out = "Kommu2020";
int status = WL_IDLE_STATUS;
int incomingByte;

String FirmwareVer = {
    "2.0"
};

#define URL_fw_Version "https://raw.githubusercontent.com/najmihanis/OTA-OBD2-WS/main/bin-version.txt" //change this text in github everytime got new update
#define URL_fw_Bin "https://raw.githubusercontent.com/najmihanis/OTA-OBD2-WS/main/fw.bin" //need to replace this with newer one in github

//********************************************OBD2-WS pre-setup*****************************************************************
// to act as an AP
const char* ssid_ap = "ESP32-Access-Point";
const char* password_ap = "123456789";

String obd2Data = "";  // Variable to store incoming OBD2 messages

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Simplified index_html. This will just display messages.
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <style>
    body {
      font-family: Arial, sans-serif;
      margin: 0;
      padding: 0;
      background-color: #f0f0f0;
    }
    #messages {
      margin: 20px;
      padding: 20px;
      border-radius: 5px;
      background-color: #fff;
      box-shadow: 0px 0px 10px 0px rgba(0,0,0,0.1);
    }
    #messages:after {
      content: "";
      display: table;
      clear: both;
    }
    #messages p {
      margin: 0;
      padding: 10px 0;
      border-bottom: 1px solid #eee;
    }
  </style>
</head>
<body>
  <div id="messages"></div>
  <script>
    var gateway = `ws://${window.location.hostname}/ws`;
    var websocket;
    window.addEventListener('load', onLoad);
    function initWebSocket() {
      console.log('Trying to open a WebSocket connection...');
      websocket = new WebSocket(gateway);
      websocket.onopen    = onOpen;
      websocket.onclose   = onClose;
      websocket.onmessage = onMessage; 
    }
    function onOpen(event) {
      console.log('Connection opened');
    }
    function onClose(event) {
      console.log('Connection closed');
      setTimeout(initWebSocket, 2000);
    }
    function onMessage(event) {
      var p = document.createElement('p');
      p.textContent = event.data;
      document.getElementById('messages').appendChild(p);
    }
    function onLoad(event) {
      initWebSocket();
    }
  </script>
</body>
</html>
)rawliteral";

//******************************************OBD2-WS functions****************************************************************
//WS part
void notifyClients() {
  ws.textAll(obd2Data);
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      sendFirmwareVersion();
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

//OBD2 part
void sendOBDQuery(uint8_t pid) {
  CAN.beginPacket(0x7DF);  // Diagnostic Request ID
  CAN.write(0x02);         // Diagnostic Size
  CAN.write(0x01);         // Service Mode
  CAN.write(pid);          // PID to query

  CAN.endPacket();
  //Serial.println ("done sending");
}

String buildIsoTpDataString(uint32_t packetId, uint8_t* data, uint8_t length) {
  // Build the ISO-TP data string from the received CAN packet
  String isoTpData = "Received packet with ID: 0x";
  isoTpData += String(packetId, HEX);
  
  for (uint8_t i = 0; i < length; i++) {
    isoTpData += " | ";
    isoTpData += String(data[i], HEX);
  }
  
  return isoTpData;
}

void sendFirmwareVersion() {
  String firmwareInfo = "Current Firmware Version: " + FirmwareVer;
  Serial.println(firmwareInfo); 
  obd2Data = firmwareInfo;
  notifyClients();
}

//******************************************OTA functions****************************************************************
void reconnect() {
  int i = 0;
  // Loop until we're reconnected
  status = WiFi.status();
  if (status != WL_CONNECTED) {
    WiFi.begin(ssid_out, wifiPassword_out);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      if (i == 10) {
        ESP.restart();
      }
      i++;
    }
    Serial.println("Connected to AP");
  }
}

void firmwareUpdate(void) {
  WiFiClientSecure client;
  client.setCACert(rootCACertificate); //check with cert.h
  t_httpUpdate_return ret = httpUpdate.update(client, URL_fw_Bin);//fetching the new bin file from github from the url above

  switch (ret) {
  case HTTP_UPDATE_FAILED:
    Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
    break;

  case HTTP_UPDATE_NO_UPDATES:
    Serial.println("HTTP_UPDATE_NO_UPDATES");
    break;

  case HTTP_UPDATE_OK:
    Serial.println("HTTP_UPDATE_OK");
    break;
  }
}

int FirmwareVersionCheck(void) { //return 1 if there's newer version, return 0 if no later version
  String payload;
  int httpCode;
  String FirmwareURL = "";
  FirmwareURL += URL_fw_Version; //retrieving url of the bin-version.txt
  FirmwareURL += "?";
  FirmwareURL += String(rand());
  Serial.println(FirmwareURL);
  WiFiClientSecure * client = new WiFiClientSecure;


  if (client) {
    client -> setCACert(rootCACertificate);
    HTTPClient https;

    if (https.begin( * client, FirmwareURL)) {
      Serial.print("[HTTPS] GET...\n");
      // start connection and send HTTP header
      delay(100);
      httpCode = https.GET();
      delay(100);
      if (httpCode == HTTP_CODE_OK) // if version received
      {
        payload = https.getString(); // save received version in payload variable
      } else {
        Serial.print("Error Occured During Version Check: ");
        Serial.println(httpCode);
      }
      https.end();
    }
    delete client;
  }

  if (httpCode == HTTP_CODE_OK) // if version received
  {
    payload.trim();
    if (payload.equals(FirmwareVer)) {//if the version received which is stored in 'payload' == FirmwareVer that we defined earlier
      Serial.printf("\nDevice  IS Already on Latest Firmware Version:%s\n", FirmwareVer);
      return 0;
    } else {
      Serial.println(payload);
      Serial.println("New Firmware Detected");
      return 1;
    }
  }
  return 0;
}

void firmwareScan(){
  Serial.print("Active Firmware Version:");
  Serial.println(FirmwareVer);
  //=============================Connecting to Wifi======================================================
  WiFi.begin(ssid_out, wifiPassword_out);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    //try to connect 10 times, if after 10 times still fail to connect, it will restart, and try again
    int i = 0;
    if (i == 10) {
      ESP.restart();
    }
    i++;
  }
  Serial.println("Connected To Wifi");
  
  delay(1000);
  //try to connect 10 times, if after 10 times still fail to connect, it will restart, and try again
  if (WiFi.status() != WL_CONNECTED) {
    reconnect();
  }
 //*******************************Checking and updating*******************************************
  if (FirmwareVersionCheck()) { //if (1) --> update firmware
    firmwareUpdate();
  }

  delay(1000);
  WiFi.disconnect();
}

//*******************************************************************************************************************************

void setup(){ 
//***************************************OTA Part******************************************
  Serial.begin(115200);
  firmwareScan();
  delay(1000);
//display current version on client devices
  //sendFirmwareVersion();
  //delay(1000);
//***********************************OBD2-WS Part*******************************************
//=====================CAN stuff==========================================================
  while (!Serial);
  delay (1000);

  Serial.println ("CAN Receiver/Receiver");

  // Set the pins
  CAN.setPins (RX_GPIO_NUM, TX_GPIO_NUM);

  // start the CAN bus at 500 kbps
  if (!CAN.begin (500E3)) {
    Serial.println ("Starting CAN failed!");
    while (1);
  }
  else {
    Serial.println ("CAN Initialized");
  }
//=====================Setting up AP=======================================================
  Serial.print("Setting AP (Access Point)…");
  // Start Access Point
  WiFi.softAP(ssid_ap, password_ap);

  // Print ESP Local IP Address
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
//=====================Websocket stuff====================================================
  initWebSocket();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, NULL);
  });
//====================launch server=======================================================
  // Start server
  server.begin();
}

void loop() {

  //***************************************OBD2-WS Part******************************************
  ws.cleanupClients(); 
  //==============================Send a query for a specific PID========================
  sendOBDQuery(0x0C);  // Change the PID value as per your requirement
  //========================fetching CAN messages========================================
  
  // simple i received smth message
  //Wait for a response
  // if (CAN.parsePacket()) {
  //   // Received a packet
  //   obd2Data = "I received something";
  //   Serial.println(obd2Data); 
  //   notifyClients();
  // }

  //actual iso-tp message 
  // Wait for a response
  if (CAN.parsePacket()) {
    // Received a packet

    // if (CAN.packetId() == 0x7E8) { //filter
      //====================storing data in a buffer======================
      // Create a buffer to store the packet data
      uint8_t packetData[8];
      uint8_t packetLength = 0;

      while (CAN.available()) {
        // Read data from the packet and store in the buffer
        packetData[packetLength++] = CAN.read();
      }

      //==========if data is extended type================================
      
      if (CAN.packetExtended()) {
        Serial.print("Extended, ");
      }

      //===============printing on serial monitor========================
      Serial.print("Received packet with ID: 0x");
      Serial.print(CAN.packetId(), HEX);
      
      while (CAN.available()) {
        Serial.print(" | ");
        Serial.print(CAN.read(), HEX);
      }
      
      Serial.println();
    //==================displaying on client screen=======================
      // Send ISO-TP data to connected clients
      obd2Data = buildIsoTpDataString(CAN.packetId(), packetData, packetLength);
      notifyClients();
    //}
  }
  // else{
  //   obd2Data = "I received something else";
  //   Serial.println(obd2Data); 
  //   notifyClients();
  // }
  delay(100);
}

