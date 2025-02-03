#include "esp_camera.h"
#include "modelo.h"
#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"

// Pines para la cámara
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 10
#define SIOD_GPIO_NUM 40
#define SIOC_GPIO_NUM 39
#define Y9_GPIO_NUM 48
#define Y8_GPIO_NUM 11
#define Y7_GPIO_NUM 12
#define Y6_GPIO_NUM 14
#define Y5_GPIO_NUM 16
#define Y4_GPIO_NUM 18
#define Y3_GPIO_NUM 17
#define Y2_GPIO_NUM 15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM 47
#define PCLK_GPIO_NUM 13

#define OUTPUT_PIN D6      // Pin de salida GPIO para enviar datos (HIGH o LOW)
#define CAPTURE_INTERVAL 10000 // Intervalo de captura de 10 segundos

// Variables de TensorFlow Lite
namespace {
tflite::ErrorReporter* error_reporter = nullptr;
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
TfLiteTensor* output = nullptr;
}

constexpr int kTensorArenaSize = 128 * 1024;
uint8_t* tensor_arena = nullptr;

static const char* TAG = "Detector";
unsigned long lastCaptureTime = 0;

// Inicialización de la cámara
bool initCamera() {
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 15;
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Camera init failed");
    return false;
  }
  return true;
}

// Inicialización de TensorFlow Lite
bool initTensorFlow() {
  static tflite::MicroErrorReporter micro_error_reporter;
  error_reporter = &micro_error_reporter;

  model = tflite::GetModel(model_tflite);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    return false;
  }

  static tflite::MicroMutableOpResolver<12> resolver;
  resolver.AddQuantize();
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddPad();
  resolver.AddLogistic();
  resolver.AddShape();
  resolver.AddStridedSlice();
  resolver.AddPack();
  resolver.AddReshape();
  resolver.AddFullyConnected();
  resolver.AddDequantize();
  resolver.AddAveragePool2D();

  static tflite::MicroInterpreter static_interpreter(
    model, resolver, tensor_arena, kTensorArenaSize, error_reporter);
  interpreter = &static_interpreter;

  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    return false;
  }

  input = interpreter->input(0);
  output = interpreter->output(0);

  return (input != nullptr && output != nullptr);
}

// Función para enviar datos al GPIO y mostrar en serial
void sendResults(float good_prob, float bad_prob) {
  String result = (good_prob > bad_prob) ? "01" : "00";

  // Enviar HIGH para "01" y LOW para "00"
  if (result == "01") {
    digitalWrite(OUTPUT_PIN, HIGH);
    Serial.println("Resultado: Sin estrés hídrico (01)");
    ESP_LOGI(TAG, "Enviado: 01 (HIGH)");
  } else {
    digitalWrite(OUTPUT_PIN, LOW);
    Serial.println("Resultado: Con estrés hídrico (00)");
    ESP_LOGI(TAG, "Enviado: 00 (LOW)");
  }
}

// Procesar imagen capturada y realizar inferencia
void processImage(camera_fb_t* fb) {
  if (!fb || fb->format != PIXFORMAT_JPEG) return;

  Serial.println("Procesando imagen...");
  uint8_t* rgb_buf = (uint8_t*)heap_caps_malloc(fb->width * fb->height * 3, MALLOC_CAP_SPIRAM);
  if (!rgb_buf) {
    ESP_LOGE(TAG, "RGB buffer allocation failed");
    return;
  }

  bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb_buf);
  if (!converted) {
    free(rgb_buf);
    return;
  }

  for (int i = 0; i < 96 * 96 && i < input->bytes; i++) {
    int y = i / 96;
    int x = i % 96;
    int src_x = x * fb->width / 96;
    int src_y = y * fb->height / 96;
    int src_idx = (src_y * fb->width + src_x) * 3;

    uint8_t gray = (rgb_buf[src_idx] * 0.299 + rgb_buf[src_idx + 1] * 0.587 + rgb_buf[src_idx + 2] * 0.114);
    input->data.int8[i] = static_cast<int8_t>(gray - 127);
  }

  if (interpreter->Invoke() == kTfLiteOk) {
    float bad_prob = (output->data.int8[0] + 128) / 255.0f * 100;
    float good_prob = (output->data.int8[1] + 128) / 255.0f * 100;

    Serial.print("Probabilidad de Sin Estrés: ");
    Serial.print(good_prob);
    Serial.print("%, Estrés: ");
    Serial.print(bad_prob);
    Serial.println("%");

    sendResults(good_prob, bad_prob); // Enviar resultado al GPIO
  }

  free(rgb_buf);
}

void setup() {
  Serial.begin(115200);
  pinMode(OUTPUT_PIN, OUTPUT); // Configurar el pin como salida

  setCpuFrequencyMhz(240);

  if (!psramInit()) {
    ESP_LOGE(TAG, "PSRAM init failed!");
    return;
  }

  tensor_arena = (uint8_t*)heap_caps_malloc(kTensorArenaSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!tensor_arena) {
    ESP_LOGE(TAG, "Failed to allocate tensor arena");
    return;
  }

  if (!initCamera() || !initTensorFlow()) {
    ESP_LOGE(TAG, "Init failed!");
    return;
  }

  Serial.println("Sistema iniciado correctamente. Capturando imagen...");
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) {
    processImage(fb);
    esp_camera_fb_return(fb);
  }
}

void loop() {
  unsigned long currentTime = millis();
  if (currentTime - lastCaptureTime >= CAPTURE_INTERVAL) {
    Serial.println("Capturando imagen...");
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      processImage(fb);
      esp_camera_fb_return(fb);
    }
    lastCaptureTime = currentTime;
  }
  delay(10);
}