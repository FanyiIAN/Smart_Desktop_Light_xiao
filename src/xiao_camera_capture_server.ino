#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

// ===========================
// WiFi credentials
// ===========================
const char* ssid = "iPhone 123";
const char* password = "12345678a";

// ===========================
// XIAO ESP32S3 Sense Camera Pins
// ===========================
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39

#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13

WebServer server(80);

// ===========================
// HTTP handlers
// ===========================
void handleRoot() {
  server.send(
    200,
    "text/plain",
    "XIAO ESP32S3 camera server is running.\nUse /capture to get one JPEG image.\nUse /status to check server status.\n"
  );
}

void handleStatus() {
  String msg = "{";
  msg += "\"status\":\"ok\",";
  msg += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  msg += "\"psram\":" + String(psramFound() ? "true" : "false") + ",";
  msg += "\"resolution\":\"QVGA_320x240\",";
  msg += "\"jpeg_quality\":10";
  msg += "}";

  server.send(200, "application/json", msg);
}

void handleCapture() {
  Serial.println("Capture requested");

  // Discard one frame first.
  // This helps avoid stale/unstable exposure/white-balance frames.
  camera_fb_t *dummy = esp_camera_fb_get();
  if (dummy) {
    esp_camera_fb_return(dummy);
  }
  delay(120);

  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("Camera capture failed");
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }

  Serial.printf("Captured image: %d bytes\n", fb->len);

  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);

  esp_camera_fb_return(fb);
  Serial.println("Capture sent");
}

// ===========================
// Camera setup
// ===========================
bool setupCamera() {
  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // QVGA = 320x240. Good balance between stability and detail.
  config.frame_size = FRAMESIZE_QVGA;

  // Smaller number = better JPEG quality but larger file.
  // 10 is clearer than 12/15, still usually stable for QVGA.
  config.jpeg_quality = 10;

  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  if (psramFound()) {
    Serial.println("PSRAM found. Using PSRAM frame buffer.");
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    Serial.println("WARNING: PSRAM not found. Using DRAM frame buffer.");
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  Serial.println("Initializing camera...");
  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  Serial.println("Camera initialized.");

  sensor_t *s = esp_camera_sensor_get();

  // Keep sensor settings consistent with config.
  s->set_framesize(s, FRAMESIZE_QVGA);
  s->set_quality(s, 10);

  // Basic color / exposure settings.
  // These help reduce the dark-green / unstable first-frame issue.
  s->set_brightness(s, 1);      // -2 to 2
  s->set_contrast(s, 1);        // -2 to 2
  s->set_saturation(s, 0);      // -2 to 2

  s->set_whitebal(s, 1);        // Auto white balance ON
  s->set_awb_gain(s, 1);        // AWB gain ON
  s->set_wb_mode(s, 0);         // Auto WB mode

  s->set_exposure_ctrl(s, 1);   // Auto exposure ON
  s->set_aec2(s, 1);            // DSP auto exposure ON
  s->set_ae_level(s, 0);        // -2 to 2

  s->set_gain_ctrl(s, 1);       // Auto gain ON
  s->set_gainceiling(s, (gainceiling_t)6);

  // Optional: adjust if your image is upside down or mirrored.
  // Uncomment only if needed.
  // s->set_vflip(s, 1);
  // s->set_hmirror(s, 1);

  Serial.println("Warming up camera for auto exposure / white balance...");
  delay(1500);

  // Discard several initial frames so AWB/AEC can settle.
  for (int i = 0; i < 6; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);
      Serial.print(".");
    }
    delay(200);
  }
  Serial.println();
  Serial.println("Camera warmup done.");

  return true;
}

// ===========================
// WiFi setup
// ===========================
void setupWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.disconnect(true, true);
  delay(1000);

  WiFi.mode(WIFI_STA);
  delay(500);

  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    retry++;

    if (retry % 10 == 0) {
      Serial.print(" status=");
      Serial.print(WiFi.status());
      Serial.print(" ");
    }

    if (retry > 80) {
      Serial.println();
      Serial.println("WiFi connection failed. Restarting...");
      ESP.restart();
    }
  }

  Serial.println();
  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("RSSI: ");
  Serial.println(WiFi.RSSI());
}

// ===========================
// Arduino setup / loop
// ===========================
void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("Booting minimal XIAO ESP32S3 camera server...");

  // WiFi first, then camera.
  // This order was more stable in your setup.
  setupWiFi();

  if (!setupCamera()) {
    Serial.println("Camera setup failed. Stop here.");
    return;
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/capture", HTTP_GET, handleCapture);

  server.begin();

  Serial.println("HTTP server started.");
  Serial.print("Open root:    http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");

  Serial.print("Open status:  http://");
  Serial.print(WiFi.localIP());
  Serial.println("/status");

  Serial.print("Open capture: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/capture");
}

void loop() {
  server.handleClient();
  delay(5);
}