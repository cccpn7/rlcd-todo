#include "auto_pager.hpp"
#include "cJSON.h"
#include "core.hpp"
#include "display_bsp.h"
#include "driver/usb_serial_jtag.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "io.hpp"
#include "lvgl.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "welcome.hpp"
#include <algorithm>
#include <atomic>
#include <ctime>
#include <string>

struct Message {
  int transport;
  char text[4096];
};
static QueueHandle_t inbox;
static std::vector<uint8_t> active, incoming, display_data;
static std::vector<rtd::Page> pages;
static size_t page_index = 0, received = 0;
static rtd::AutoPager auto_pager;
static int active_slot = -1, transfer_transport = 0;
static uint64_t version = 0, transfer_version = 0;
static std::string transfer_hash, owner, device_id, device_name;
static int64_t last_usb = 0, last_ble = 0, last_chunk = 0;
static std::atomic<uint32_t> pairing{0};
static std::atomic<int64_t> pairing_since{0};
static DisplayPort *panel;
static lv_obj_t *picture, *date_label, *status_label, *pair_label;
static lv_image_dsc_t bitmap = {};
static uint16_t *raster;
static uint8_t bits[rtd::Bytes];
static nvs_handle_t settings;
static unsigned long key_long_count = 0;

static std::string digest(const uint8_t *p, size_t n) {
  uint8_t d[32];
  mbedtls_sha256(p, n, d, 0);
  char s[65];
  for (int i = 0; i < 32; i++)
    sprintf(s + i * 2, "%02x", d[i]);
  return s;
}
static const esp_partition_t *cache(int slot) {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                  ESP_PARTITION_SUBTYPE_ANY,
                                  slot == 0 ? "cache0" : "cache1");
}
static bool load_slot(int slot, std::vector<uint8_t> &data) {
  auto partition = cache(slot);
  uint8_t header[80];
  if (!partition ||
      esp_partition_read(partition, 0, header, sizeof(header)) != ESP_OK ||
      memcmp(header, "RTC1", 4))
    return false;
  size_t size = rtd::u32(header + 4);
  if (size < 22 || size > rtd::Limit)
    return false;
  data.resize(size);
  if (esp_partition_read(partition, 80, data.data(), size) != ESP_OK)
    return false;
  return digest(data.data(), size) == std::string((char *)header + 8, 64);
}
static bool save_snapshot() {
  int slot = active_slot == 0 ? 1 : 0;
  auto partition = cache(slot);
  if (!partition)
    return false;
  uint8_t header[80];
  memset(header, 0xff, sizeof(header));
  memcpy(header, "RTC1", 4);
  uint32_t size = incoming.size();
  memcpy(header + 4, &size, 4);
  auto sha = digest(incoming.data(), incoming.size());
  memcpy(header + 8, sha.data(), 64);
  // Commit marker is written last; a power loss leaves the previous slot
  // usable.
  if (esp_partition_erase_range(partition, 0, partition->size) != ESP_OK ||
      esp_partition_write(partition, 80, incoming.data(), incoming.size()) !=
          ESP_OK ||
      esp_partition_write(partition, 0, header, sizeof(header)) != ESP_OK)
    return false;
  std::vector<uint8_t> verify;
  std::vector<rtd::Page> old_pages = pages;
  bool ok = load_slot(slot, verify) && verify == incoming;
  pages = old_pages;
  if (ok) {
    active_slot = slot;
  }
  return ok;
}
void show_pairing(uint32_t code) {
  pairing = code;
  pairing_since = esp_timer_get_time();
}
void clear_pairing() { pairing = 0; }
static void flush(lv_display_t *display, const lv_area_t *area,
                  uint8_t *color) {
  auto pixels = (uint16_t *)color;
  for (int y = area->y1; y <= area->y2; y++)
    for (int x = area->x1; x <= area->x2; x++) {
      auto point = rtd::rotate180(x, y);
      panel->RLCD_SetPixel(point.x, point.y, *pixels++ < 0x7fff ? 0 : 255);
    }
  panel->RLCD_Display();
  lv_display_flush_ready(display);
}
static uint32_t ticks() { return esp_timer_get_time() / 1000; }
static lv_obj_t *label(int x, int y, int width) {
  auto l = lv_label_create(lv_screen_active());
  lv_obj_set_pos(l, x, y);
  lv_obj_set_width(l, width);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(l, lv_color_black(), 0);
  lv_obj_set_style_bg_color(l, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(l, 0, 0);
  return l;
}
static void status() {
  time_t now = time(nullptr);
  tm local;
  localtime_r(&now, &local);
  char text[80];
  if (now > 1704067200)
    strftime(text, sizeof(text), "%m.%d", &local);
  else
    strcpy(text, "--.--");
  lv_label_set_text(date_label, text);
  int64_t monotonic = esp_timer_get_time();
  const char *link = (last_usb && monotonic - last_usb < 6000000)   ? "USB"
                     : (last_ble && monotonic - last_ble < 6000000) ? "BLE"
                                                                    : "OFF";
  if (active.size()) {
    time_t stamp = rtd::u64(active.data() + 12);
    localtime_r(&stamp, &local);
    char t[8];
    strftime(t, sizeof(t), "%H:%M", &local);
    snprintf(text, sizeof(text), "%s  %s", t, link);
  } else
    snprintf(text, sizeof(text), "%s", link);
  lv_label_set_text(status_label, text);
  uint32_t code = pairing.load();
  if (code && monotonic - pairing_since.load() > 90000000) {
    pairing = 0;
    code = 0;
  }
  if (code) {
    snprintf(text, sizeof(text), "BLE PIN\n%06lu", (unsigned long)code);
    lv_label_set_text(pair_label, text);
    lv_obj_remove_flag(pair_label, LV_OBJ_FLAG_HIDDEN);
  } else
    lv_obj_add_flag(pair_label, LV_OBJ_FLAG_HIDDEN);
}
static void show() {
  if (active.size() && !pages.empty()) {
    auto p = pages[page_index];
    if (display_data[3] == '0')
      memcpy(bits, display_data.data() + p.offset, rtd::Bytes);
    else
      rtd::unpack(display_data.data() + p.offset, p.size, bits,
                  display_data[3] == '2');
  } else
    rtd::unpack(welcome, sizeof(welcome), bits);
  for (size_t i = 0; i < rtd::Pixels; i++)
    raster[i] = (bits[i / 8] & (0x80 >> (i % 8))) ? 0xffff : 0;
  lv_image_cache_drop(&bitmap);
  lv_obj_invalidate(picture);
  status();
  lv_refr_now(nullptr);
  auto_pager.shown(esp_timer_get_time());
}
static void init_display() {
  panel = new DisplayPort(12, 11, 5, 40, 41, 300, 400);
  panel->RLCD_Init();
  lv_init();
  lv_tick_set_cb(ticks);
  auto display = lv_display_create(300, 400);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, flush);
  void *draw = heap_caps_malloc(240000, MALLOC_CAP_SPIRAM);
  assert(draw);
  lv_display_set_buffers(display, draw, nullptr, 240000,
                         LV_DISPLAY_RENDER_MODE_FULL);
  raster = (uint16_t *)heap_caps_malloc(240000, MALLOC_CAP_SPIRAM);
  assert(raster);
  bitmap.header.magic = LV_IMAGE_HEADER_MAGIC;
  bitmap.header.cf = LV_COLOR_FORMAT_RGB565;
  bitmap.header.w = 300;
  bitmap.header.h = 400;
  bitmap.header.stride = 600;
  bitmap.data_size = 240000;
  bitmap.data = (uint8_t *)raster;
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), 0);
  lv_obj_remove_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);
  picture = lv_image_create(lv_screen_active());
  lv_image_set_src(picture, &bitmap);
  lv_obj_set_pos(picture, 0, 0);
  date_label = label(204, 9, 84);
  lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_RIGHT, 0);
  status_label = label(174, 380, 114);
  lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_RIGHT, 0);
  pair_label = label(50, 155, 200);
  lv_obj_set_style_text_font(pair_label, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_align(pair_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_all(pair_label, 12, 0);
  lv_obj_add_flag(pair_label, LV_OBJ_FLAG_HIDDEN);
}
void receive_bytes(int transport, const uint8_t *data, size_t size) {
  static char buffers[2][4096];
  static size_t lengths[2] = {};
  static bool overflow[2] = {};
  int idx = transport - 1;
  if (!data) {
    lengths[idx] = 0;
    overflow[idx] = false;
    return;
  }
  for (size_t i = 0; i < size; i++) {
    if (data[i] == '\n') {
      if (!overflow[idx] && lengths[idx] > 6) {
        buffers[idx][lengths[idx]] = 0;
        auto msg = std::make_unique<Message>();
        msg->transport = transport;
        strlcpy(msg->text, buffers[idx], sizeof(msg->text));
        xQueueSend(inbox, msg.get(), 0);
      }
      lengths[idx] = 0;
      overflow[idx] = false;
    } else if (lengths[idx] < sizeof(buffers[idx]) - 1)
      buffers[idx][lengths[idx]++] = char(data[i]);
    else
      overflow[idx] = true;
  }
}
static void usb_reader(void *) {
  uint8_t data[256];
  for (;;) {
    int n = usb_serial_jtag_read_bytes(data, sizeof(data), pdMS_TO_TICKS(100));
    if (n > 0)
      receive_bytes(1, data, n);
  }
}
static const char *string(cJSON *o, const char *key) {
  auto p = cJSON_GetObjectItemCaseSensitive(o, key);
  return cJSON_IsString(p) ? p->valuestring : "";
}
static double number(cJSON *o, const char *key) {
  auto p = cJSON_GetObjectItemCaseSensitive(o, key);
  return cJSON_IsNumber(p) ? p->valuedouble : 0;
}
static void reset_transfer() {
  incoming.clear();
  received = 0;
  transfer_transport = 0;
  transfer_version = 0;
}
static std::string handle(cJSON *request, int transport, cJSON *reply) {
  std::string op = string(request, "op"), token = string(request, "token");
  int64_t now = esp_timer_get_time();
  bool authorized = !owner.empty() && token == owner;
  if (op == "hello") {
    if (transport == 2 && !authorized)
      return "Not bound to this Mac";
    if (authorized) {
      if (transport == 1)
        last_usb = now;
      else
        last_ble = now;
      rtc_sync((int64_t)number(request, "epoch"));
    }
    cJSON_AddNumberToObject(reply, "protocol", 1);
    cJSON_AddStringToObject(reply, "device", device_id.c_str());
    cJSON_AddStringToObject(reply, "name", device_name.c_str());
    cJSON_AddBoolToObject(reply, "bound", !owner.empty());
    cJSON_AddNumberToObject(reply, "version", version);
    if (authorized) {
      cJSON_AddNumberToObject(reply, "page", page_index);
      cJSON_AddNumberToObject(reply, "key", gpio_get_level(GPIO_NUM_18));
      cJSON_AddNumberToObject(reply, "boot", gpio_get_level(GPIO_NUM_0));
      cJSON_AddNumberToObject(reply, "long_presses", key_long_count);
    }
    return "";
  }
  if (op == "bind" && transport == 1) {
    if (token.size() != 32 || (!owner.empty() && !authorized))
      return "Device already bound";
    if (owner.empty()) {
      if (nvs_set_str(settings, "owner", token.c_str()) != ESP_OK ||
          nvs_commit(settings) != ESP_OK)
        return "Cannot save binding";
      owner = token;
    }
    last_usb = now;
    return "";
  }
  if (!authorized)
    return "Unauthorized";
  if (transport == 2 && last_usb && now - last_usb < 6000000)
    return "USB has priority";
  if (transport == 1)
    last_usb = now;
  else
    last_ble = now;
  if (op == "begin") {
    uint64_t v = number(request, "version");
    size_t size = number(request, "size");
    std::string hash = string(request, "sha256");
    if (size < 22 || size > rtd::Limit || hash.size() != 64 || v < version ||
        v > 9007199254740991ULL)
      return "Invalid snapshot";
    incoming.assign(size, 0);
    received = 0;
    transfer_transport = transport;
    transfer_hash = hash;
    transfer_version = v;
    last_chunk = now;
    return "";
  }
  if (transfer_transport != transport || incoming.empty())
    return "No active transfer";
  if (op == "chunk") {
    size_t offset = number(request, "offset");
    if (offset != received)
      return "Incorrect offset";
    const char *encoded = string(request, "data");
    uint8_t bytes[2048];
    size_t size = 0;
    if (mbedtls_base64_decode(bytes, sizeof(bytes), &size,
                              (const uint8_t *)encoded, strlen(encoded)) ||
        !size || received + size > incoming.size())
      return "Invalid chunk";
    memcpy(incoming.data() + received, bytes, size);
    received += size;
    last_chunk = now;
    cJSON_AddNumberToObject(reply, "offset", received);
    return "";
  }
  if (op == "end") {
    std::vector<rtd::Page> next;
    std::vector<uint8_t> decoded;
    if (received != incoming.size() ||
        digest(incoming.data(), incoming.size()) != transfer_hash ||
        !rtd::expand(incoming.data(), incoming.size(), decoded) ||
        !rtd::parse(decoded.data(), decoded.size(), next) ||
        rtd::u64(incoming.data() + 4) != transfer_version) {
      reset_transfer();
      return "Snapshot verification failed";
    }
    if (transfer_version == version) {
      bool same = incoming == active;
      reset_transfer();
      if (!same)
        return "Conflicting version";
      cJSON_AddNumberToObject(reply, "version", version);
      return "";
    }
    if (!save_snapshot()) {
      reset_transfer();
      return "Cache write failed";
    }
    active.swap(incoming);
    display_data.swap(decoded);
    pages = std::move(next);
    version = transfer_version;
    page_index = 0;
    reset_transfer();
    show(); // synchronous SPI transfer completes before the acknowledgement.
    cJSON_AddNumberToObject(reply, "version", version);
    return "";
  }
  return "Unknown operation";
}
static void process(const Message &msg) {
  if (strncmp(msg.text, "@RTD1 ", 6))
    return;
  auto request = cJSON_Parse(msg.text + 6);
  if (!request)
    return;
  auto reply = cJSON_CreateObject();
  cJSON_AddNumberToObject(reply, "id", number(request, "id"));
  auto error = handle(request, msg.transport, reply);
  cJSON_AddBoolToObject(reply, "ok", error.empty());
  if (!error.empty())
    cJSON_AddStringToObject(reply, "error", error.c_str());
  char *json = cJSON_PrintUnformatted(reply);
  std::string line = "@RTD1 " + std::string(json) + "\n";
  if (msg.transport == 1)
    usb_serial_jtag_write_bytes(line.data(), line.size(), pdMS_TO_TICKS(500));
  else
    ble_reply(line.c_str());
  cJSON_free(json);
  cJSON_Delete(reply);
  cJSON_Delete(request);
}
struct Button {
  int gpio;
  bool raw = false, down = false, held = false;
  int64_t changed = 0, pressed = 0;
};
static bool button(Button &b, int64_t now) {
  bool pressed = gpio_get_level((gpio_num_t)b.gpio) == 0;
  if (pressed != b.raw) {
    b.raw = pressed;
    b.changed = now;
  }
  if (now - b.changed < 30000)
    return false;
  if (b.raw != b.down) {
    b.down = b.raw;
    if (b.down) {
      b.pressed = now;
      b.held = false;
    } else if (!b.held && !pages.empty()) {
      page_index =
          (page_index + pages.size() + (b.gpio == 18 ? 1 : -1)) % pages.size();
      return true;
    }
  }
  if (b.down && !b.held && b.gpio == 18 && now - b.pressed >= 800000) {
    b.held = true;
    key_long_count++;
    page_index = 0;
    return true;
  }
  return false;
}
extern "C" void app_main() {
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(nvs_open("rlcd", NVS_READWRITE, &settings));
  char token[33];
  size_t len = sizeof(token);
  if (nvs_get_str(settings, "owner", token, &len) == ESP_OK)
    owner = token;
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  device_id = digest(mac, 6).substr(0, 16);
  device_name = "Todo-" + device_id.substr(0, 6);
  setenv("TZ", "UTC0", 1);
  tzset();
  rtc_init();
  setenv("TZ", "CST-8", 1);
  tzset();
  for (int slot = 0; slot < 2; slot++) {
    std::vector<uint8_t> wire, decoded;
    std::vector<rtd::Page> parsed;
    if (load_slot(slot, wire) &&
        rtd::expand(wire.data(), wire.size(), decoded) &&
        rtd::parse(decoded.data(), decoded.size(), parsed) &&
        rtd::u64(wire.data() + 4) > version) {
      version = rtd::u64(wire.data() + 4);
      active.swap(wire);
      display_data.swap(decoded);
      pages.swap(parsed);
      active_slot = slot;
    }
  }
  init_display();
  show();
  gpio_config_t gpio = {};
  gpio.pin_bit_mask = (1ULL << 18) | 1;
  gpio.mode = GPIO_MODE_INPUT;
  gpio.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK(gpio_config(&gpio));
  inbox = xQueueCreate(4, sizeof(Message));
  assert(inbox);
  usb_serial_jtag_driver_config_t usb = {};
  usb.rx_buffer_size = 4096;
  usb.tx_buffer_size = 4096;
  ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
  xTaskCreate(usb_reader, "usb_rx", 4096, nullptr, 4, nullptr);
  ble_init(device_name.c_str());
  Button key{18}, boot{0};
  int64_t last_status = 0;
  for (;;) {
    Message msg;
    if (xQueueReceive(inbox, &msg, pdMS_TO_TICKS(10)))
      process(msg);
    int64_t now = esp_timer_get_time();
    bool changed = button(key, now);
    changed = button(boot, now) || changed;
    if (changed)
      show();
    else if (auto_pager.advance(page_index, pages.size(), now,
                                pairing.load() != 0 || key.raw || key.down ||
                                    boot.raw || boot.down))
      show();
    if (now - last_status > 500000) {
      status();
      last_status = now;
    }
    if (transfer_transport && now - last_chunk > 15000000)
      reset_transfer();
    lv_timer_handler();
  }
}
