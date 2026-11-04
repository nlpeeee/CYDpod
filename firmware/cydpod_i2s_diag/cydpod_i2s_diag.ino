// CY-75, compile-only qualification. User flashes and listens.
// P6 GND/IO16/IO4/IO17 -> GND/BCK/DIN/WS. SD -> 3V3, MC -> GND.
// No display, touch, SD, Bluetooth. GPIO21/22/27 are never touched.
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_timer.h>
#include <initializer_list>
#include "board.h"
#include "i2s_diag.h"
#if CYD_BOARD != 30
#error "CY-75 diagnostic is only for JC2432W328C (board 30)"
#endif

static i2s_chan_handle_t tx = nullptr;
static bool enabled = false;
static bool teardownFault = false;
static uint64_t epoch;
static unsigned active = 99, statusBucket = 99;
static uint32_t written = 0, errors = 0, stalls = 0;
static int previousLed = -2;
static int16_t samples[256 * 2];
static i2sdiag::Generator generator(i2sdiag::phase(0));

static bool checked(esp_err_t err, const char* operation) {
  if (err == ESP_OK) return true;
  ++errors;
  Serial.printf("i2s-diag: %s err=%d (%s)\n", operation, int(err), esp_err_to_name(err));
  return false;
}
static void stopChannel() {
  if (teardownFault) return;
  if (tx) {
    if (enabled && !checked(i2s_channel_disable(tx), "disable")) {
      enabled = false; teardownFault = true; return;
    }
    enabled = false;
    if (!checked(i2s_del_channel(tx), "delete")) { teardownFault = true; return; }
    tx = nullptr;
  }
  // Only after detaching the peripheral: restore plain GPIO for the LED test.
  for (int pin : {CYD_I2S_DOUT_PIN, CYD_I2S_BCK_PIN, CYD_I2S_WS_PIN}) {
    gpio_reset_pin(gpio_num_t(pin));
    gpio_set_level(gpio_num_t(pin), 1);
    gpio_set_direction(gpio_num_t(pin), GPIO_MODE_OUTPUT);
  }
}
static void startChannel(const i2sdiag::Phase& p) {
  if (tx) { Serial.println("i2s-diag: stale channel; skipping audio"); return; }
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  chan.dma_desc_num = 6; chan.dma_frame_num = 256; chan.auto_clear = true;
  if (!checked(i2s_new_channel(&chan, &tx, nullptr), "new")) return;
  i2s_std_config_t cfg = {};
  cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(p.rate);
  cfg.clk_cfg.clk_src = I2S_CLK_SRC_PLL_160M;
  cfg.clk_cfg.mclk_multiple = p.slotBits == 32 ? I2S_MCLK_MULTIPLE_512 : I2S_MCLK_MULTIPLE_256;
  cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  cfg.slot_cfg.slot_bit_width = p.slotBits == 32 ? I2S_SLOT_BIT_WIDTH_32BIT : I2S_SLOT_BIT_WIDTH_16BIT;
  cfg.slot_cfg.ws_width = p.slotBits;
  cfg.slot_cfg.bit_shift = p.bitShift;
  cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  cfg.gpio_cfg.bclk = gpio_num_t(CYD_I2S_BCK_PIN);
  cfg.gpio_cfg.ws = gpio_num_t(CYD_I2S_WS_PIN);
  cfg.gpio_cfg.dout = gpio_num_t(CYD_I2S_DOUT_PIN);
  cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
  if (!checked(i2s_channel_init_std_mode(tx, &cfg), "init")) { stopChannel(); return; }
  if (!checked(i2s_channel_enable(tx), "enable")) { stopChannel(); return; }
  enabled = true;
}
static void enterPhase(unsigned n) {
  // Previous phase has supplied 500 ms of zeros before teardown.
  stopChannel();
  active = n; written = errors = stalls = 0; statusBucket = 99; previousLed = -2;
  const auto p = i2sdiag::phase(n);
  generator = i2sdiag::Generator(p);
  Serial.printf("i2s-diag: phase %u %s\n", n, p.description);
  if (n == 0) Serial.println("i2s-diag: LED GPIO4=red, GPIO16=green, GPIO17=blue; each LOW 1 s then all HIGH");
  if (p.audio) startChannel(p);
  if (n == 6) Serial.println("i2s-diag: WARNING full scale; expect 4 clicks/thumps per second, keep earphones out initially");
}
void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(CYD_SPEAKER_PIN, OUTPUT); digitalWrite(CYD_SPEAKER_PIN, LOW);
  Serial.println("i2s-diag: bck=16 din=4 ws=17; 12 s phases + 500 ms zeros + 40 ms drain; PLL_160M dma=6x256");
  Serial.println("i2s-diag: start -12 dBFS; phase6 FULL SCALE; SD=3V3 MC=GND; no MCLK wire");
  epoch = uint64_t(esp_timer_get_time()) / 1000;
  enterPhase(0);
}
void loop() {
  auto pos = i2sdiag::position(uint64_t(esp_timer_get_time()) / 1000 - epoch);
  if (pos.phase != active) enterPhase(pos.phase);
  const unsigned bucket = pos.ms / i2sdiag::kStatusMs;
  if (bucket != statusBucket && !pos.zeros) {
    statusBucket = bucket;
    Serial.printf("i2s-diag: status phase=%u elapsed_ms=%u written=%u errors=%u stalls=%u heap=%u\n",
                  active, unsigned(pos.ms), unsigned(written), unsigned(errors), unsigned(stalls), unsigned(ESP.getFreeHeap()));
  }
  if (active == 0 && !tx) {
    const int pin = i2sdiag::ledPin(pos.ms);
    if (pin != previousLed) {
      for (int p : {4,16,17}) gpio_set_level(gpio_num_t(p), p == pin ? 0 : 1);
      Serial.printf("i2s-diag: LED pin=%d %s\n", pin, pin == 4 ? "red" : pin == 16 ? "green" : pin == 17 ? "blue" : "all off");
      previousLed = pin;
    }
  }
  if (!enabled) { delay(1); return; }
  if (pos.ms >= i2sdiag::kPhaseMs + i2sdiag::kZeroMs) { delay(1); return; }
  generator.fill(samples, 256, pos.zeros);
  size_t remaining = sizeof(samples), offset = 0;
  while (remaining) {
    size_t sent = 0;
    // New driver timeout is milliseconds, not RTOS ticks; bound a failed phase.
    const esp_err_t err = i2s_channel_write(tx, reinterpret_cast<uint8_t*>(samples)+offset, remaining, &sent, 50);
    written += sent; offset += sent; remaining -= sent;
    if (err != ESP_OK || sent == 0) {
      if (err == ESP_ERR_TIMEOUT) ++stalls;
      checked(err == ESP_OK ? ESP_FAIL : err, "write");
      delay(20);
      break;  // discard remainder; the next loop still advances the scheduler
    }
  }
}
