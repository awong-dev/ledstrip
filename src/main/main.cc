#include <string_view>
#include <optional>

#include "esp_system.h"

#include "nvs_flash.h"

#include "esp_cxx/event_manager.h"
#include "esp_cxx/firebase/firebase_database.h"
#include "esp_cxx/httpd/mongoose_event_manager.h"
#include "esp_cxx/httpd/http_server.h"
#include "esp_cxx/httpd/standard_endpoints.h"
#include "esp_cxx/logging.h"
#include "esp_cxx/ota.h"
#include "esp_cxx/task.h"
#include "esp_cxx/wifi.h"

#include "ws2812_control.h"

#define HTML_DECL(name) \
  extern "C" const uint8_t name##_start[] asm("_binary_" #name "_start"); \
  extern "C" const uint8_t name##_end[] asm("_binary_" #name "_end");
#define HTML_LEN(name) (&name##_end[0] - &name##_start[0] - 1)
#define HTML_CONTENTS(name) (&name##_start[0])

HTML_DECL(resp404_html);
HTML_DECL(index_html);
HTML_DECL(basic_controls_js);

static const char *kTag = "ledstrip";

namespace {
class StripControl {
 public:
  enum class Mode {
    // All pixels in strip have one color. Use SetHSV() to set color.
    kOneColor,

    //// The follow modes run a loop an animate.

    // Fill in strip with color in SetHSV(), one pixel at a time.
    // Use SetFrameDelay() to set the speed of the fill.
    kColorWipe,
  };

  explicit StripControl(int num_pixels) {
    ResetNumPixels(num_pixels);
  }

  void ResetNumPixels(int num_pixels) {
    num_pixels_ = num_pixels;
    pixels_.reset(new uint32_t[num_pixels_]());  // Invoke zero-initialization of uint32_t.
  }

  void SetMode(Mode mode) {
    mode_ = mode;
    // TODO(awong): Do something here.
    if (mode == Mode::kOneColor) {
      // StopAnimation();
    } else {
      // StartAnimation();
    }
  }

  void SetHSV(int hue, uint8_t saturation, uint8_t value) {
  }

  void SetFrameDelay(int frame_delay_ms) {
    frame_delay_ms_ = frame_delay_ms;
  }

 private:
  int frame_delay_ms_ = 50;
  int num_pixels_;
  Mode mode_;
  std::unique_ptr<uint32_t[]> pixels_;
};

void LedDriver(void* arg) {
  led_state& state = *reinterpret_cast<led_state*>(arg);
  while (1) {
    sleep(1);
    for (int i = 0; i < NUM_LEDS; i++) {
      state.leds[i] = 0x0f;
    }
    ws2812_write_leds(state);

    sleep(1);
    for (int i = 0; i < NUM_LEDS; i++) {
      state.leds[i] = state.leds[i] << 8;
    }
    ws2812_write_leds(state);

    sleep(1);
    for (int i = 0; i < NUM_LEDS; i++) {
      state.leds[i] = state.leds[i] << 8;
    }
    ws2812_write_leds(state);
  }
}

}  // namespace

extern "C" void app_main(void) {
  using namespace esp_cxx;

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Setup Wifi access.
  static constexpr char kFallbackSsid[] = "ledstrip_setup";
  static constexpr char kFallbackPassword[] = "ledstrip";
  Wifi wifi;
  if (!wifi.ConnectToAP() && !wifi.CreateSetupNetwork(kFallbackSsid, kFallbackPassword)) {
    ESP_LOGE(kTag, "Failed to connect to AP OR create a Setup Network.");
  }

  // Create Webserver
  MongooseEventManager net_event_manager;
  std::string_view index_html(
      reinterpret_cast<const char*>(HTML_CONTENTS(index_html)),
      HTML_LEN(index_html));
  std::string_view resp404_html(
      reinterpret_cast<const char*>(HTML_CONTENTS(resp404_html)),
      HTML_LEN(resp404_html));
  std::string_view basic_controls_js(
      reinterpret_cast<const char*>(HTML_CONTENTS(basic_controls_js)),
      HTML_LEN(basic_controls_js));
  HttpServer http_server(&net_event_manager, ":80", resp404_html);
  StandardEndpoints standard_endpoints(index_html);
  standard_endpoints.RegisterEndpoints(&http_server);
  JsEndpoint basic_controls_js_endpoint(basic_controls_js);
  http_server.RegisterEndpoint("/basic_controls.js$", &basic_controls_js_endpoint);

  // LED strip setup.
  ws2812_control_init();
  led_state state;
  Task led_driver_task(&LedDriver, &state, "led");

  // Firebase setup.
  FirebaseDatabase firebase_db(
      "anger2action-f3698.firebaseio.com",
      "anger2action-f3698",
      "/lights/ledstrip",
      &net_event_manager);
  firebase_db.SetUpdateHandler(
      [&firebase_db, &state] {
        // TODO(ajwong): Read from the db and update the state.
      });
  firebase_db.Connect();

  net_event_manager.Loop();
  ESP_LOGE(kTag, "This should never be reached!");
}
