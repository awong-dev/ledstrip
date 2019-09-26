#include <string_view>
#include <optional>

#include "esp_system.h"

#include "nvs_flash.h"

#include "esp_cxx/backoff.h"
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
std::string_view index_html_str(
    reinterpret_cast<const char*>(HTML_CONTENTS(index_html)),
    HTML_LEN(index_html));
std::string_view resp404_html_str(
    reinterpret_cast<const char*>(HTML_CONTENTS(resp404_html)),
    HTML_LEN(resp404_html));
std::string_view basic_controls_js_str(
    reinterpret_cast<const char*>(HTML_CONTENTS(basic_controls_js)),
    HTML_LEN(basic_controls_js));

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

std::atomic_uint32_t g_current_color_ = 0x10ae0f;

led_state g_state;
void LedDriver(void* arg) {
  while (1) {
    sleep(1);
    uint32_t color = g_current_color_;
    for (int i = 0; i < NUM_LEDS; i++) {
      g_state.leds[i] = color;
    }
    ws2812_write_leds(g_state);

    /*
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
    */
  }
}

using namespace esp_cxx;

class MongooseNetworkContext {
 public:
  using OnNetworkUpCb = std::function<void(bool, ip_event_got_ip_t* got_ip)>;
  using OnDisconnectCb = std::function<void(int)>;

  MongooseNetworkContext() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Setup main event loop.
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    net_event_manager_ == std::make_unique<MongooseEventManager>();
  }

  void Start(OnNetworkUpCb on_network_up, OnDisconnectCb on_disconnect) {
    on_network_up_ = std::move(on_network_up);
    on_disconnect_ = std::move(on_disconnect);
    StartWifi();
  }

  MongooseEventManager* event_manager() { return net_event_manager_.get(); }

 private:
  std::unique_ptr<MongooseEventManager> net_event_manager_;
  OnNetworkUpCb on_network_up_;
  OnDisconnectCb on_disconnect_;

  // Trying every ~30s for wifi seems reaosnable.
  BackoffCalculator<100,30*1000> wifi_backoff_;

  bool first_run_ = true;

  void StartWifi() {
    // Setup Wifi access.
    static constexpr char kFallbackSsid[] = "ledstrip_setup";
    static constexpr char kFallbackPassword[] = "ledstrip";
    Wifi* wifi = Wifi::GetInstance();

    wifi->SetApEventHandlers(
        [this](ip_event_got_ip_t* got_ip) {
          wifi_backoff_.Reset();
          on_network_up_(first_run_, got_ip);
          first_run_ = false;
        },
        [this, wifi] (uint8_t reason) {
          on_disconnect_(reason);
          int next_try_ms = wifi_backoff_.MsToNextTry();
          // If wifi->ReconnectToAP() fails, this handler gets called again.
          ESP_LOGW(kTag, "Wifi Next Reconnect in %dms", next_try_ms);
          net_event_manager_->RunDelayed([wifi]{ wifi->ReconnectToAP(); },
                                       next_try_ms);
        }
    );
    if (!wifi->ConnectToAP() && !wifi->CreateSetupNetwork(kFallbackSsid, kFallbackPassword)) {
      ESP_LOGE(kTag, "Failed to connect to AP OR create a Setup Network.");
      abort();
    }
  }
};

class LedStrip {
 public:
  LedStrip()
    : firebase_db_(
        "iotzombie-153122.firebaseio.com", // "anger2action-f3698.firebaseio.com",
        "iotzombie-153122",  // "anger2action-f3698",
        "/devicesdev/parlor-ledstrip", // "/lights/ledstrip",
        network_context_.event_manager(),
        "https://us-central1-iotzombie-153122.cloudfunctions.net/get_firebase_id_token",
        "parlor-ledstrip",
        "b4563d9bb77fff268e18"),
       http_server_(network_context_.event_manager(), ":80", resp404_html_str),
       standard_endpoints_(index_html_str),
       basic_controls_js_endpoint_(basic_controls_js_str) {
    standard_endpoints_.RegisterEndpoints(&http_server_);
    http_server_.RegisterEndpoint("/basic_controls.js$", &basic_controls_js_endpoint_);
  }

  void Start() {
    network_context_.Start(
        [this](bool is_first_run, ip_event_got_ip_t* got_ip){
          ESP_LOGI(kTag, "Network start: %d", is_first_run);
          OnNetworkUp(is_first_run);
        },
        [this](int reason){
          firebase_db_.Disconnect();
        });
    network_context_.event_manager()->Loop();
  }

 private:
  MongooseNetworkContext network_context_;
  FirebaseDatabase firebase_db_;
  HttpServer http_server_;
  StandardEndpoints standard_endpoints_;
  JsEndpoint basic_controls_js_endpoint_;

  // Firebase setup.
  void OnNetworkUp(bool is_first_run) {
    if (is_first_run) {
      // LED strip setup.
      ws2812_control_init();
      static Task led_driver_task(&LedDriver, nullptr, "led");

      firebase_db_.SetUpdateHandler(
          [this] {
          cJSON* data = firebase_db_.Get("devicesdev/parlor-ledstrip");
          if (data) {
            ESP_LOGI(kTag, "%s", cJSON_PrintUnformatted(data));
          }
          cJSON* r = cJSON_GetObjectItemCaseSensitive(data, "r");
          cJSON* g = cJSON_GetObjectItemCaseSensitive(data, "g");
          cJSON* b = cJSON_GetObjectItemCaseSensitive(data, "b");
          cJSON* w = cJSON_GetObjectItemCaseSensitive(data, "w");
          if (cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b) && cJSON_IsNumber(w)) {
            uint32_t grbw = g->valueint;
            grbw = (grbw << 8) | r->valueint;
            grbw = (grbw << 8) | b->valueint;
            grbw = (grbw << 8) | w->valueint;
            ESP_LOGI("ledstrip", "setting color to %x", grbw);
            g_current_color_ = grbw;
          }
          });
    }

    ESP_LOGI("ledstrip", "Connecting to FB now");
    firebase_db_.Connect();
  }
};

}  // namespace

extern "C" void app_main(void) {
  using namespace esp_cxx;

  auto led_strip = std::make_unique<LedStrip>();
  led_strip->Start();
  ESP_LOGE(kTag, "This should never be reached!");
}
