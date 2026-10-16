#include "driver/i2c_master.h"
#include "io.hpp"
#include <ctime>
#include <sys/time.h>
static i2c_master_dev_handle_t rtc;
static int from_bcd(uint8_t n) { return (n >> 4) * 10 + (n & 15); }
static uint8_t bcd(int n) { return uint8_t((n / 10) * 16 + n % 10); }
void rtc_init() {
  i2c_master_bus_config_t bus = {};
  bus.i2c_port = I2C_NUM_0;
  bus.sda_io_num = GPIO_NUM_13;
  bus.scl_io_num = GPIO_NUM_14;
  bus.clk_source = I2C_CLK_SRC_DEFAULT;
  bus.glitch_ignore_cnt = 7;
  bus.flags.enable_internal_pullup = true;
  i2c_master_bus_handle_t handle;
  if (i2c_new_master_bus(&bus, &handle) != ESP_OK)
    return;
  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = 0x51;
  cfg.scl_speed_hz = 100000;
  if (i2c_master_bus_add_device(handle, &cfg, &rtc) != ESP_OK) {
    rtc = nullptr;
    return;
  }
  uint8_t reg = 4, b[7];
  if (i2c_master_transmit_receive(rtc, &reg, 1, b, 7, 100) != ESP_OK ||
      (b[0] & 0x80))
    return;
  tm t = {};
  t.tm_sec = from_bcd(b[0] & 127);
  t.tm_min = from_bcd(b[1] & 127);
  t.tm_hour = from_bcd(b[2] & 63);
  t.tm_mday = from_bcd(b[3] & 63);
  t.tm_mon = from_bcd(b[5] & 31) - 1;
  t.tm_year = 100 + from_bcd(b[6]);
  if (t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31 ||
      t.tm_hour > 23 || t.tm_min > 59 || t.tm_sec > 59)
    return;
  time_t epoch = mktime(&t); // TZ is UTC during initialization; RTC stores UTC.
  if (epoch > 1704067200) {
    timeval tv = {epoch, 0};
    settimeofday(&tv, nullptr);
  }
}
void rtc_sync(int64_t epoch) {
  if (epoch < 1704067200 || epoch > 4102444800LL)
    return;
  time_t now = time(nullptr);
  timeval tv = {time_t(epoch), 0};
  settimeofday(&tv, nullptr);
  // Limit external RTC writes to clock corrections, not every heartbeat.
  static bool first = true;
  if (!rtc || (!first && llabs(now - epoch) < 3))
    return;
  first = false;
  time_t seconds = epoch;
  tm t;
  gmtime_r(&seconds, &t);
  uint8_t control[] = {0, 0};
  i2c_master_transmit(rtc, control, 2, 100);
  uint8_t b[] = {4,
                 bcd(t.tm_sec),
                 bcd(t.tm_min),
                 bcd(t.tm_hour),
                 bcd(t.tm_mday),
                 bcd(t.tm_wday),
                 bcd(t.tm_mon + 1),
                 bcd(t.tm_year - 100)};
  i2c_master_transmit(rtc, b, sizeof(b), 100);
}
