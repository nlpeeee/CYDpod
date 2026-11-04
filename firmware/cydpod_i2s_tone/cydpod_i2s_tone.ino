// CY-70: standalone I2S test tone for the WeAct PCM5100A module on the
// JC2432W328C (board 30 only). No touch, SD, display or Bluetooth; the
// backlight pin (GPIO27) is never touched, so the panel stays dark.
// Pins (board.h): BCK = GPIO16, WS = GPIO17, DOUT = GPIO4, all on connector P6
// (GND, IO16, IO4, IO17). GPIO22/21 stay untouched.
//
// API: ESP-IDF 5.5 `i2s_std` (Arduino-ESP32 3.3.10 ships
// esp32-libs/3.3.10/include/esp_driver_i2s/include/driver/i2s_std.h). The
// legacy `driver/i2s.h` is not used: IDF refuses to link both drivers, and the
// player already uses the new-style `dac_continuous`.
//
// Clocking: the PCM5100A needs no MCLK; its PLL locks to BCK when SCK stays
// low (datasheet 9.3.5.3, Table 11: 44.1 kHz needs BCK 32..64 x fS =
// 1.4112..2.8224 MHz). A 16-bit slot gives 32 x fS, the bottom edge of that
// range; a 32-bit slot gives 64 x fS, the top edge. Default is the 16-bit
// slot (CY-69 section 3); build with -DI2S_TEST_SLOT_BITS=32 as the fallback
// if the PLL does not lock. See hq docs/research/cy70-i2s-tone.md.
#include <Arduino.h>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "board.h"
#include "i2s_tone.h"

#if CYD_BOARD != 30
#error "cydpod_i2s_tone is for the JC2432W328C (CYD_BOARD 30) only"
#endif

#ifndef I2S_TEST_SLOT_BITS
#define I2S_TEST_SLOT_BITS 16
#endif
#if I2S_TEST_SLOT_BITS != 16 && I2S_TEST_SLOT_BITS != 32
#error "I2S_TEST_SLOT_BITS must be 16 or 32"
#endif

static const uint32_t kDmaDescNum = 6;
static const uint32_t kDmaFrameNum = 256;
static const size_t kChunkFrames = 256;
static const TickType_t kWriteTimeoutTicks = pdMS_TO_TICKS(500);  // bounded, never portMAX_DELAY
static const uint32_t kStatsPeriodMs = 10000;

static i2s_chan_handle_t g_tx = nullptr;
static volatile uint32_t g_bytesWritten = 0;
static volatile uint32_t g_errors = 0;
static volatile uint32_t g_stalls = 0;

static bool startI2s() {
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  chan.dma_desc_num = kDmaDescNum;
  chan.dma_frame_num = kDmaFrameNum;
  chan.auto_clear = true;  // zeros, not a repeated buffer, if the writer ever stalls
  esp_err_t err = i2s_new_channel(&chan, &g_tx, nullptr);
  if (err != ESP_OK) {
    Serial.printf("i2s-test: i2s_new_channel failed err=%d (%s)\n", (int)err, esp_err_to_name(err));
    return false;
  }

  i2s_std_config_t cfg = {};
  cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(i2stone::kRate);
  cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
#if I2S_TEST_SLOT_BITS == 32
  cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;  // BCK = 64 x fS
  cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_512;
#endif
  cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  cfg.gpio_cfg.bclk = (gpio_num_t)CYD_I2S_BCK_PIN;
  cfg.gpio_cfg.ws = (gpio_num_t)CYD_I2S_WS_PIN;
  cfg.gpio_cfg.dout = (gpio_num_t)CYD_I2S_DOUT_PIN;
  cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
  err = i2s_channel_init_std_mode(g_tx, &cfg);
  if (err != ESP_OK) {
    Serial.printf("i2s-test: i2s_channel_init_std_mode failed err=%d (%s)\n", (int)err, esp_err_to_name(err));
    return false;
  }
  err = i2s_channel_enable(g_tx);
  if (err != ESP_OK) {
    Serial.printf("i2s-test: i2s_channel_enable failed err=%d (%s)\n", (int)err, esp_err_to_name(err));
    return false;
  }
  return true;
}

// Writer task: the blocking write is the playback clock. Clocks start at
// once, so the 3 s of boot silence is zeros on a running bus.
static void writerTask(void*) {
  static int16_t buf[kChunkFrames * 2];
  i2stone::Generator gen;
  for (;;) {
    gen.fill(buf, kChunkFrames);
    const uint8_t* p = (const uint8_t*)buf;
    size_t left = sizeof(buf);
    while (left > 0) {
      size_t w = 0;
      esp_err_t err = i2s_channel_write(g_tx, p, left, &w, kWriteTimeoutTicks);
      p += w;
      left -= w;
      g_bytesWritten += (uint32_t)w;
      if (err == ESP_OK) continue;
      if (err == ESP_ERR_TIMEOUT) ++g_stalls;
      else ++g_errors;
      Serial.printf("i2s-test: i2s_channel_write err=%d (%s) written=%u of %u\n", (int)err,
                    esp_err_to_name(err), (unsigned)w, (unsigned)sizeof(buf));
      vTaskDelay(pdMS_TO_TICKS(20));  // bounds the log rate if the error repeats
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  // GPIO26 feeds the on-board speaker amp; keep it from floating. GPIO27
  // (backlight) is deliberately left alone.
  pinMode(CYD_SPEAKER_PIN, OUTPUT);
  digitalWrite(CYD_SPEAKER_PIN, LOW);

  if (!startI2s()) {
    Serial.println("i2s-test: init failed, halting");
    return;
  }
  Serial.printf("i2s-test: pins bck=%d ws=%d dout=%d rate=%u slot=%d dma=%ux%u heap=%u\n",
                CYD_I2S_BCK_PIN, CYD_I2S_WS_PIN, CYD_I2S_DOUT_PIN, (unsigned)i2stone::kRate,
                I2S_TEST_SLOT_BITS, (unsigned)kDmaDescNum, (unsigned)kDmaFrameNum,
                (unsigned)ESP.getFreeHeap());
  Serial.printf("i2s-test: tone %u Hz L / %u Hz R, amp -20 dBFS (silent for the first 3 s, then 1 s on / 1 s off)\n",
                (unsigned)i2stone::kFreqLeft, (unsigned)i2stone::kFreqRight);
  xTaskCreatePinnedToCore(writerTask, "i2sw", 6144, nullptr, 5, nullptr, 0);
}

void loop() {
  delay(kStatsPeriodMs);
  if (g_tx == nullptr) return;
  Serial.printf("i2s-test: written=%u errors=%u stalls=%u\n", (unsigned)g_bytesWritten,
                (unsigned)g_errors, (unsigned)g_stalls);
}
