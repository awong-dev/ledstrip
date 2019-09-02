#include "ws2812_control.h"
#include "driver/rmt.h"

#include <unistd.h>

// Configure these based on your project needs ********
#define LED_RMT_TX_CHANNEL RMT_CHANNEL_0
#define LED_RMT_TX_GPIO GPIO_NUM_16
// ****************************************************

#define BITS_PER_LED_CMD 32 
#define LED_BUFFER_ITEMS ((NUM_LEDS * BITS_PER_LED_CMD))

// These values are determined by measuring pulse timing with logic analyzer and adjusting to match datasheet. 
//#define T0H 14  // 0 bit high time
//#define T1H 52  // 1 bit high time
//#define TL  52  // low time for either bit

#define VAL 3
#if VAL == 0
#define T0H 14 //14  // 0 bit high time
#define T1H (2*T0H) //28  // 1 bit high time
#define T0L (3*T0H)  //42  // low time for either bit
#define T1L (2*T0H) //28  // low time for either bit

#elif VAL == 3

// Gets some reaction. Baed on RGB W-something.
// https://cdn-shop.adafruit.com/datasheets/WS2812.pdf
#define T0H 15 //14  // 0 bit high time
#define T1H (2*T0H) //28  // 1 bit high time
#define T0L (2.3*T0H)  //42  // low time for either bit
#define T1L (1.714*T0H) //28  // low time for either bit

#elif VAL == 1

// Measured via logic analyzer. Doesn't seem to work though.
#define T0H 15  // 0 bit high time
#define T1H 28  // 1 bit high time
#define T0L  33  // low time for either bit
#define T1L  24  // low time for either bit

#else

// Values known to get some response.
#define T0H 14  // 0 bit high time
#define T1H 52  // 1 bit high time
#define T0L  52  // low time for either bit
#define T1L  52  // low time for either bit
#endif

// This is the buffer which the hw peripheral will access while pulsing the output pin
rmt_item32_t led_data_buffer[LED_BUFFER_ITEMS];

void setup_rmt_data_buffer(const struct led_state& new_state);

void ws2812_control_init(void)
{
  rmt_config_t config;
  config.rmt_mode = RMT_MODE_TX;
  config.channel = LED_RMT_TX_CHANNEL;
  config.gpio_num = LED_RMT_TX_GPIO;
  config.mem_block_num = 3;
  config.tx_config.loop_en = false;
  config.tx_config.carrier_en = false;
  config.tx_config.idle_output_en = true;
  config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  config.clk_div = 2;

  ESP_ERROR_CHECK(rmt_config(&config));
  ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));
}

void ws2812_write_leds(const struct led_state& new_state) {
  setup_rmt_data_buffer(new_state);
  ESP_ERROR_CHECK(rmt_write_items(LED_RMT_TX_CHANNEL, led_data_buffer, LED_BUFFER_ITEMS, true));
}

void setup_rmt_data_buffer(const struct led_state& new_state) 
{
  rmt_item32_t high;
  high.duration0 = T1H;
  high.level0 = 1;
  high.duration1 = T0H;
  high.level1 = 0;

  rmt_item32_t low;
  low.duration0 = T1L;
  low.level0 = 1;
  low.duration1 = T0L;
  low.level1 = 0;

  for (uint32_t led = 0; led < NUM_LEDS; led++) {
    uint32_t bits_to_send = new_state.leds[led];
    uint32_t mask = 1 << (BITS_PER_LED_CMD - 1);
    for (uint32_t bit = 0; bit < BITS_PER_LED_CMD; bit++) {
      uint32_t bit_is_set = bits_to_send & mask;
      led_data_buffer[led * BITS_PER_LED_CMD + bit] = bit_is_set ? high : low;
      mask >>= 1;
    }
  }
}
