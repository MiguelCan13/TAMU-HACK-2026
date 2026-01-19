#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include "esp_camera.h"

// --- Camera Pins (AI-Thinker) ---
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

RF24 radio(2, 15); 
const byte address[] = "00001";

struct IndexedChunk {
  uint32_t pixel_index; 
  uint16_t pixels[14];  
}; 

void setup() {
  Serial.begin(115200);
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = FRAMESIZE_QVGA; 
  config.fb_count = 1;

  esp_camera_init(&config);
  sensor_t * s = esp_camera_sensor_get();
  s->set_vflip(s, 1); 
  s->set_brightness(s, 0);     
  s->set_contrast(s, 0);       
  s->set_saturation(s, -1);    // Lower saturation helps with color "bleeding"
  s->set_whitebal(s, 1);       // Enable Auto White Balance
  s->set_awb_gain(s, 1);       
  s->set_exposure_ctrl(s, 1);  // Enable Auto Exposure
  s->set_aec2(s, 1);           // Enable DSP Auto Exposure
  s->set_gain_ctrl(s, 1);      // Enable Auto Gain

  SPI.begin(14, 12, 13, 15);
  radio.begin();
  radio.setAutoAck(false);
  radio.setChannel(115);
  radio.setDataRate(RF24_2MBPS);
  radio.setPALevel(RF24_PA_MAX);
  radio.openWritingPipe(address);
  radio.stopListening();
  Serial.println("Sender Ready.");
}

void loop() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) return;

  uint16_t* raw_pixels = (uint16_t*)fb->buf;
  IndexedChunk chunk;

  Serial.println("Sending frame...");
  for (uint32_t i = 0; i < 76800; i += 14) {
    chunk.pixel_index = i;
    for (int j = 0; j < 14; j++) {
      if (i + j < 76800) chunk.pixels[j] = raw_pixels[i + j];
    }
    radio.write(&chunk, sizeof(IndexedChunk));
    delayMicroseconds(150); // Crucial: Gives receiver time to process
  }

  esp_camera_fb_return(fb);
  Serial.println("Frame sent. Waiting 100ms...");
  delay(100); 
}
