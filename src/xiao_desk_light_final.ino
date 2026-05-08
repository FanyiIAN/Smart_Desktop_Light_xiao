/* Edge Impulse + XIAO ESP32S3 Sense + CW/WW LED Strip Control
 *
 * Labels expected:
 *   computer_mouse
 *   empty_desk
 *   phone_use
 *   reading
 */

#include <xiao_final_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"

#include "esp_camera.h"
#include <string.h>

// ===========================
// XIAO ESP32S3 Sense Camera Pins
// ===========================
#define CAMERA_MODEL_XIAO_ESP32S3

#if defined(CAMERA_MODEL_XIAO_ESP32S3)
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
#else
#error "Camera model not selected"
#endif

// ===========================
// Camera constants
// ===========================
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS  320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS  240
#define EI_CAMERA_FRAME_BYTE_SIZE        3

// ===========================
// LED strip PWM settings
// D1 = cool white, D0 = warm white
// ===========================

int CW_pin = D1;
int WW_pin = D0;

// Camera uses LEDC channel 0, so do NOT use channel 0 for lights.
int CW_channel = 2;
int WW_channel = 3;

int pwm_freq = 5000;
int pwm_resolution = 8;   // duty range 0~255

// Optional confidence threshold
float CONFIDENCE_THRESHOLD = 0.60f;

// ===========================
// Private variables
// ===========================

static bool debug_nn = false;
static bool is_initialised = false;
uint8_t *snapshot_buf;

static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,

    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,

    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,

    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_QVGA,      // 320x240, same as your dataset capture
    .jpeg_quality = 10,                // lower = better quality
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

// ===========================
// Function declarations
// ===========================

bool ei_camera_init(void);
void ei_camera_deinit(void);
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf);
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr);

void setupLightPwm();
void setLight(int cw, int ww);
void applyLightMode(const char* label, float confidence);

// ===========================
// LED functions
// ===========================

void setupLightPwm() {
    ledcSetup(CW_channel, pwm_freq, pwm_resolution);
    ledcSetup(WW_channel, pwm_freq, pwm_resolution);

    ledcAttachPin(CW_pin, CW_channel);
    ledcAttachPin(WW_pin, WW_channel);

    // Start with lights off
    ledcWrite(CW_channel, 0);
    ledcWrite(WW_channel, 0);
}

void setLight(int cw, int ww) {
    cw = constrain(cw, 0, 255);
    ww = constrain(ww, 0, 255);

    ledcWrite(CW_channel, cw);
    ledcWrite(WW_channel, ww);
}

void applyLightMode(const char* label, float confidence) {
    if (confidence < CONFIDENCE_THRESHOLD) {
        ei_printf("Low confidence %.3f, keep previous light state.\n", confidence);
        return;
    }

    if (strcmp(label, "empty") == 0) {
        setLight(0, 0);
        ei_printf("Light mode: empty -> OFF\n");
    }
    else if (strcmp(label, "computer") == 0) {
        setLight(255, 0);
        ei_printf("Light mode: computer -> COOL WORK\n");
    }
    else if (strcmp(label, "ipad") == 0) {
        setLight(255, 255);
        ei_printf("Light mode: ipad -> BRIGHT READING\n");
    }
    else if (strcmp(label, "phone") == 0) {
        setLight(0, 255);
        ei_printf("Light mode: phone -> DIM WARM\n");
    }
    else {
        setLight(0, 0);
        ei_printf("Light mode: unknown label [%s] -> OFF\n", label);
    }
}

// ===========================
// Arduino setup
// ===========================

void setup()
{
    Serial.begin(115200);

    // Do NOT block on Serial. This lets the board run standalone.
    // while (!Serial);

    setupLightPwm();

    Serial.println("Edge Impulse Inferencing + LED Control Demo");

    if (ei_camera_init() == false) {
        ei_printf("Failed to initialize Camera!\r\n");
        setLight(0, 0);
    }
    else {
        ei_printf("Camera initialized\r\n");
    }

    ei_printf("\nStarting continuous inference in 2 seconds...\n");
    ei_sleep(2000);
}

// ===========================
// Arduino loop
// ===========================

void loop()
{
    if (ei_sleep(5) != EI_IMPULSE_OK) {
        return;
    }

    snapshot_buf = (uint8_t*)malloc(
        EI_CAMERA_RAW_FRAME_BUFFER_COLS *
        EI_CAMERA_RAW_FRAME_BUFFER_ROWS *
        EI_CAMERA_FRAME_BYTE_SIZE
    );

    if (snapshot_buf == nullptr) {
        ei_printf("ERR: Failed to allocate snapshot buffer!\n");
        return;
    }

    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &ei_camera_get_data;

    if (ei_camera_capture(
            (size_t)EI_CLASSIFIER_INPUT_WIDTH,
            (size_t)EI_CLASSIFIER_INPUT_HEIGHT,
            snapshot_buf
        ) == false) {
        ei_printf("Failed to capture image\r\n");
        free(snapshot_buf);
        return;
    }

    ei_impulse_result_t result = { 0 };

    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);
    if (err != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\n", err);
        free(snapshot_buf);
        return;
    }

    ei_printf("Predictions (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.):\n",
              result.timing.dsp,
              result.timing.classification,
              result.timing.anomaly);

#if EI_CLASSIFIER_OBJECT_DETECTION == 1
    ei_printf("This code expects image classification, not object detection.\n");
#else
    float max_value = 0.0f;
    const char* max_label = "unknown";

    ei_printf("Predictions:\r\n");

    for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        const char* label = ei_classifier_inferencing_categories[i];
        float value = result.classification[i].value;

        ei_printf("  %s: %.5f\r\n", label, value);

        if (value > max_value) {
            max_value = value;
            max_label = label;
        }
    }

    ei_printf("Top prediction: %s (%.5f)\r\n", max_label, max_value);

    // Control the light from the top prediction
    applyLightMode(max_label, max_value);
#endif

#if EI_CLASSIFIER_HAS_ANOMALY
    ei_printf("Anomaly prediction: %.3f\r\n", result.anomaly);
#endif

#if EI_CLASSIFIER_HAS_VISUAL_ANOMALY
    ei_printf("Visual anomalies:\r\n");
    for (uint32_t i = 0; i < result.visual_ad_count; i++) {
        ei_impulse_result_bounding_box_t bb = result.visual_ad_grid_cells[i];
        if (bb.value == 0) {
            continue;
        }
        ei_printf("  %s (%f) [ x: %u, y: %u, width: %u, height: %u ]\r\n",
                  bb.label,
                  bb.value,
                  bb.x,
                  bb.y,
                  bb.width,
                  bb.height);
    }
#endif

    free(snapshot_buf);
}

// ===========================
// Camera init
// ===========================

bool ei_camera_init(void) {
    if (is_initialised) return true;

    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();

    // Match dataset capture camera settings as much as possible
    s->set_framesize(s, FRAMESIZE_QVGA);
    s->set_quality(s, 10);

    s->set_brightness(s, 1);      // -2 to 2
    s->set_contrast(s, 1);        // -2 to 2
    s->set_saturation(s, 0);      // -2 to 2

    s->set_whitebal(s, 1);        // auto white balance ON
    s->set_awb_gain(s, 1);        // AWB gain ON
    s->set_wb_mode(s, 0);         // auto WB mode

    s->set_exposure_ctrl(s, 1);   // auto exposure ON
    s->set_aec2(s, 1);            // DSP auto exposure ON
    s->set_ae_level(s, 0);

    s->set_gain_ctrl(s, 1);       // auto gain ON

    // If the image is too noisy/color-speckled, reduce this to 2.
    s->set_gainceiling(s, (gainceiling_t)6);

    // Do not flip/mirror unless your training images were also flipped.
    // s->set_vflip(s, 1);
    // s->set_hmirror(s, 1);

    // Warmup for auto exposure / white balance
    ei_printf("Warming up camera...\n");
    ei_sleep(1500);

    for (int i = 0; i < 6; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            esp_camera_fb_return(fb);
        }
        ei_sleep(200);
    }

    ei_printf("Camera warmup done.\n");

    is_initialised = true;
    return true;
}

// ===========================
// Camera deinit
// ===========================

void ei_camera_deinit(void) {
    esp_err_t err = esp_camera_deinit();

    if (err != ESP_OK) {
        ei_printf("Camera deinit failed\n");
        return;
    }

    is_initialised = false;
}

// ===========================
// Capture, resize, crop
// ===========================

bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf) {
    bool do_resize = false;

    if (!is_initialised) {
        ei_printf("ERR: Camera is not initialized\r\n");
        return false;
    }

    // Discard one frame to avoid stale exposure / white balance frame
    camera_fb_t *dummy = esp_camera_fb_get();
    if (dummy) {
        esp_camera_fb_return(dummy);
    }
    ei_sleep(80);

    camera_fb_t *fb = esp_camera_fb_get();

    if (!fb) {
        ei_printf("Camera capture failed\n");
        return false;
    }

    bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);

    esp_camera_fb_return(fb);

    if (!converted) {
        ei_printf("Conversion failed\n");
        return false;
    }

    if ((img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS)
        || (img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS)) {
        do_resize = true;
    }

    if (do_resize) {
        ei::image::processing::crop_and_interpolate_rgb888(
            out_buf,
            EI_CAMERA_RAW_FRAME_BUFFER_COLS,
            EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
            out_buf,
            img_width,
            img_height
        );
    }

    return true;
}

// ===========================
// EI image data callback
// ===========================

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr)
{
    size_t pixel_ix = offset * 3;
    size_t pixels_left = length;
    size_t out_ptr_ix = 0;

    while (pixels_left != 0) {
        // Swap BGR to RGB
        out_ptr[out_ptr_ix] =
            (snapshot_buf[pixel_ix + 2] << 16) +
            (snapshot_buf[pixel_ix + 1] << 8) +
            snapshot_buf[pixel_ix];

        out_ptr_ix++;
        pixel_ix += 3;
        pixels_left--;
    }

    return 0;
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "Invalid model for current sensor"
#endif