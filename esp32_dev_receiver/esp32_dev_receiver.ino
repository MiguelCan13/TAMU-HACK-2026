/*
 * ESP32-Dev Image Receiver via NRF24L01+
 * Receives image chunks and reassembles them
 */

#include <SPI.h>
#include <RF24.h>
#include <SPIFFS.h>

// NRF24L01 Configuration
#define CE_PIN 4    // Adjust based on your wiring
#define CSN_PIN 5   // Adjust based on your wiring
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

  // Initialize NRF24
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
    Serial.println("You can now retrieve it via Serial or SD card");
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
