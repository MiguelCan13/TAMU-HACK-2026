#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <FastLED.h>

const char* ssid = "heyguyswhatsup";
const char* password = "myroommatesarecool";
const char* flaskServerUrl = "http://192.168.0.198:5000/upload"; 

#define LED_PIN 48
CRGB leds[1];

RF24 radio(9, 10); 
const byte address[] = "00001";

uint16_t frame_buffer[76800]; 
struct IndexedChunk {
  uint32_t pixel_index;
  uint16_t pixels[14];
};

struct Node{
  uint8_t id;
};

uint8_t nodes[] = {1, 2};

unsigned long lastPacketTime = 0;
bool hasNewData = false;

void uploadToFlask() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("\n[HTTP] Transmission gap detected. Uploading frame...");
  HTTPClient http;
  http.begin(flaskServerUrl); 
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("X-Device-ID", "ESP32-S3-Receiver");

  // Send the raw frame_buffer to the Python server
  int httpResponseCode = http.POST((uint8_t*)frame_buffer, sizeof(frame_buffer));

  if (httpResponseCode > 0) {
    Serial.printf("[HTTP] Success! Server Response: %d\n", httpResponseCode);
  } else {
    Serial.printf("[HTTP] Failed. Error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2811, LED_PIN, GRB>(leds, 1);
  leds[0] = CRGB::Red; FastLED.show();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(100); Serial.print("."); }
  Serial.println("\n[WIFI] Connected.");

  radio.begin();
  radio.setAutoAck(false);
  radio.setChannel(115);
  radio.setDataRate(RF24_2MBPS);
  radio.openReadingPipe(1, address);
  radio.startListening();

  leds[0] = CRGB::Green; FastLED.show();
  Serial.println("[SYSTEM] Receiver listening...");
}

void loop() {
  if (radio.available()) {
    IndexedChunk incoming;
    //there are 2 nodes, in order to not recieve 2 different images at the same time,
    //each node will have an id and the reciever will alternate between them, checking if 
    //data is ready to be recieved. This will be done by sending the register of the node
    //and if the node recieves its register back it will send data, otherwise it will wait.

    //check to see which node is ready to send data
    while(true){
      for(int i = 0; i < sizeof(nodes); i++){
        radio.write(&nodes[i], sizeof(uint8_t));
        delay(10); //give some time for the node to respond
        if(radio.available()){
          radio.read(&incoming, sizeof(uint8_t));
          if(incoming == nodes[i]){
            //node is ready to send data
            Serial.printf("[SYSTEM] Incoming package from Node %d.\n", nodes[i]);
            break;
          }
        }
      }
    }

    lastPacketTime = millis();
    hasNewData = true;
    radio.read(&incoming, sizeof(IndexedChunk));
    

    if (incoming.pixel_index <= (76800 - 14)) {
      for (int i = 0; i < 14; i++) {
        uint16_t p = incoming.pixels[i];
        
        // 1. Separate the bytes (Camera is Big-Endian)
        uint8_t highByte = p >> 8;
        uint8_t lowByte  = p & 0xFF;

        // 2. Re-assemble as Little-Endian for the BMP format
        // This is the standard way ESP32 stores 16-bit values in RAM
        frame_buffer[incoming.pixel_index + i] = (lowByte << 8) | highByte;
      }

      // Progress tracking
      if (incoming.pixel_index % 11200 == 0) {
        Serial.printf("[DEBUG] Processing frame: %d%%\n", (incoming.pixel_index * 100) / 76800);
      }
    }
  }

  // GAP DETECTION: If we had data but haven't heard anything for 100ms
  // it means the sender finished its loop and is in its delay() period.
  if (hasNewData && (millis() - lastPacketTime > 100)) {
    leds[0] = CRGB::Blue; FastLED.show();
    uploadToFlask();
    hasNewData = false; // Reset until next frame starts
    leds[0] = CRGB::Green; FastLED.show();
    Serial.println("[SYSTEM] Ready for next frame.");
  }
}
