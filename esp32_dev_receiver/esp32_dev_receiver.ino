/*
 * ESP32-Dev Image Receiver via NRF24L01+
 * Receives image chunks and reassembles them
 * Uploads received images to web server
 */

#include <SPI.h>
#include <RF24.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <HTTPClient.h>

// WiFi Configuration
const char* ssid = "heyguyswhatsup";      // Change this
const char* password = "myroommatesarecool";  // Change this

// Web Server Configuration
const char* serverUrl = "http://192.168.0.198:5000/upload";  // Change this to your server
// Examples:
// Local server: "http://192.168.1.100:5000/upload"
// Cloud server: "https://yourserver.com/api/upload"
// Python Flask default: "http://192.168.1.100:5000/upload"

// NRF24L01 Configuration
#define CE_PIN 9    // Adjust based on your wiring
#define CSN_PIN 10   // Adjust based on your wiring
RF24 radio(CE_PIN, CSN_PIN);

const byte address[6] = "00001";  // Must match sender

// Packet structure (must match sender)
#define PACKET_SIZE 32
#define HEADER_SIZE 6
#define PAYLOAD_SIZE (PACKET_SIZE - HEADER_SIZE)

struct Packet {
  uint8_t packetType;      // 0=start, 1=data, 2=end
  uint16_t packetNumber;   // Sequential packet number
  uint16_t totalPackets;   // Total packets for this image
  uint8_t dataLength;      // Actual data length in this packet
  uint8_t data[PAYLOAD_SIZE];
};

//function prototypes
void handlePacket(Packet& packet);
void handleStartPacket(Packet& packet);
void handleDataPacket(Packet& packet);
void handleEndPacket(Packet& packet);
void resetReceiver();
bool uploadImageToServer(const char* filepath);
void sendImageOverSerial();

// Image reception state
bool receivingImage = false;
uint16_t expectedPacket = 0;
uint16_t totalPackets = 0;
size_t totalBytesReceived = 0;
File imageFile;
unsigned long lastPacketTime = 0;
const unsigned long TIMEOUT_MS = 5000;  // 5 second timeout

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 NRF24 Receiver Initializing...");

  // Initialize SPIFFS for image storage
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS initialization failed!");
    while (1);
  }
  Serial.println("SPIFFS initialized");

  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed! Will save images locally only.");
  }

  // Initialize NRF24
  //SPI.begin(14, 12, 13, 15);
  if (!radio.begin()) {
    Serial.println("NRF24 initialization failed!");
    while (1);
  }
  
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(108);
  radio.openReadingPipe(1, address);
  radio.startListening();
  
  Serial.println("NRF24 initialized");
  Serial.println("Waiting for images...");
}

void loop() {
  // Check for timeout
  if (receivingImage && (millis() - lastPacketTime > TIMEOUT_MS)) {
    Serial.println("Timeout! Resetting receiver");
    resetReceiver();
  }

  // Check for incoming packets
  if (radio.available()) {
    Packet packet;
    radio.read(&packet, PACKET_SIZE);
    lastPacketTime = millis();
    
    handlePacket(packet);
  }
  //dont move on until a packet is recieved, will require 2 way transmission 
  while (!radio.available()) {
    delay(1);
  }
  
  
}

void handlePacket(Packet& packet) {
  switch (packet.packetType) {
    case 0:  // START packet
      handleStartPacket(packet);
      break;
      
    case 1:  // DATA packet
      handleDataPacket(packet);
      break;
      
    case 2:  // END packet
      handleEndPacket(packet);
      break;
      
    default:
      Serial.printf("Unknown packet type: %d\n", packet.packetType);
  }
}

void handleStartPacket(Packet& packet) {
  Serial.println("\n=== Receiving new image ===");
  Serial.printf("Total packets expected: %d\n", packet.totalPackets);
  
  // Clean up any previous reception
  if (receivingImage) {
    imageFile.close();
  }
  
  // Open new file for writing
  imageFile = SPIFFS.open("/received_image.jpg", FILE_WRITE);
  if (!imageFile) {
    Serial.println("Failed to open file for writing!");
    return;
  }
  
  receivingImage = true;
  expectedPacket = 1;
  totalPackets = packet.totalPackets;
  totalBytesReceived = 0;
}

void handleDataPacket(Packet& packet) {
  if (!receivingImage) {
    Serial.println("Received data packet but not in receiving mode!");
    return;
  }
  
  // Check for missing packets
  if (packet.packetNumber != expectedPacket) {
    Serial.printf("Packet mismatch! Expected %d, got %d\n", 
                  expectedPacket, packet.packetNumber);
    // Could implement retry mechanism here
  }
  
  // Write data to file
  size_t written = imageFile.write(packet.data, packet.dataLength);
  if (written != packet.dataLength) {
    Serial.printf("Write error! Expected %d, wrote %d\n", 
                  packet.dataLength, written);
  }
  
  totalBytesReceived += written;
  expectedPacket++;
  
  // Print progress every 50 packets
  if (expectedPacket % 50 == 0) {
    Serial.printf("Progress: %d/%d packets, %d bytes\n", 
                  expectedPacket - 1, totalPackets, totalBytesReceived);
  }
}

void handleEndPacket(Packet& packet) {
  if (!receivingImage) {
    Serial.println("Received END packet but not in receiving mode!");
    return;
  }
  
  Serial.println("\n=== Image reception complete ===");
  Serial.printf("Total packets received: %d/%d\n", expectedPacket - 1, totalPackets);
  Serial.printf("Total bytes: %d\n", totalBytesReceived);
  
  imageFile.close();
  
  // Display file info
  File file = SPIFFS.open("/received_image.jpg", FILE_READ);
  if (file) {
    Serial.printf("Saved file size: %d bytes\n", file.size());
    file.close();
    
    Serial.println("Image saved as: /received_image.jpg");
    
    // Upload to web server
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nUploading to web server...");
      if (uploadImageToServer("/received_image.jpg")) {
        Serial.println("Upload successful!");
      } else {
        Serial.println("Upload failed!");
      }
    } else {
      Serial.println("WiFi not connected. Image saved locally only.");
    }
  }
  
  receivingImage = false;
}

void resetReceiver() {
  if (receivingImage && imageFile) {
    imageFile.close();
  }
  
  receivingImage = false;
  expectedPacket = 0;
  totalPackets = 0;
  totalBytesReceived = 0;
}

// Function to upload image to web server
bool uploadImageToServer(const char* filepath) {
  File file = SPIFFS.open(filepath, FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for upload");
    return false;
  }
  
  size_t fileSize = file.size();
  Serial.printf("Uploading %d bytes...\n", fileSize);
  
  HTTPClient http;
  http.begin(serverUrl);
  
  // Set content type for JPEG image
  http.addHeader("Content-Type", "image/jpeg");
  // Optional: Add custom headers
  http.addHeader("X-Device-ID", "ESP32-Receiver");
  http.addHeader("X-Timestamp", String(millis()));
  
  // Read file into buffer and send
  uint8_t* buffer = (uint8_t*)malloc(fileSize);
  if (!buffer) {
    Serial.println("Failed to allocate buffer");
    file.close();
    return false;
  }
  
  file.read(buffer, fileSize);
  file.close();
  
  // Send POST request
  int httpResponseCode = http.POST(buffer, fileSize);
  
  free(buffer);
  
  if (httpResponseCode > 0) {
    Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    String response = http.getString();
    Serial.println("Server response: " + response);
    http.end();
    return (httpResponseCode == 200 || httpResponseCode == 201);
  } else {
    Serial.printf("HTTP Error: %s\n", http.errorToString(httpResponseCode).c_str());
    http.end();
    return false;
  }
}

// Function to send image over Serial (for testing/debugging)
void sendImageOverSerial() {
  File file = SPIFFS.open("/received_image.jpg", FILE_READ);
  if (!file) {
    Serial.println("No image file found!");
    return;
  }
  
  Serial.println("Sending image over serial...");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
  Serial.println("\nImage sent!");
}
