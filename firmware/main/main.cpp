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
#include "network.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "text_layout.hpp"
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
static rtd::Font font;
static rtd::TextDocument text_document;
static bool text_mode = false;
static esp_partition_mmap_handle_t font_mapping;
static uint8_t *screen_bits;
static size_t page_count() {
  return text_mode ? text_document.pages.size() : pages.size();
}
static size_t page_index = 0, received = 0;
static rtd::AutoPager auto_pager;
static int active_slot = -1, transfer_transport = 0;
static uint64_t version = 0, transfer_version = 0;
static std::string transfer_hash, owner, device_id, device_name;
static int64_t last_usb = 0, last_ble = 0, last_chunk = 0;
static std::atomic<uint32_t> pairing{0};
static std::atomic<int64_t> pairing_since{0};
static DisplayPort *panel;
static lv_obj_t *picture, *date_label, *status_label, *pair_label,
    *network_label;
static lv_image_dsc_t bitmap = {}, network_bitmap = {};
static uint16_t network_pixels[40 * 16];
static uint16_t *raster;
static uint8_t *bits;
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
      bool white = *pixels++ >= 0x7fff;
      auto index = y * 300 + x;
      if (white)
        screen_bits[index / 8] |= 0x80 >> (index % 8);
      else
        screen_bits[index / 8] &= ~(0x80 >> (index % 8));
      panel->RLCD_SetPixel(point.x, point.y, white ? 255 : 0);
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
static void update_network_icons() {
  static int previous = -1;
  int w = network_wifi(), h = network_http(), state = w * 4 + h;
  if (state == previous)
    return;
  previous = state;
  std::fill(network_pixels, network_pixels + 40 * 16, 0xffff);
  auto pixel = [](int x, int y) {
    if (x >= 0 && x < 40 && y >= 0 && y < 16)
      network_pixels[y * 40 + x] = 0;
  };
  // Wi-Fi fan; hollow centre while associating, diagonal slash when
  // disconnected.
  for (int y = 1; y < 13; y++)
    for (int x = 1; x < 15; x++) {
      int d = (x - 8) * (x - 8) + (y - 13) * (y - 13);
      if (y < 10 && ((d >= 105 && d <= 130) || (d >= 45 && d <= 62)))
        pixel(x, y);
    }
  if (w == 2) {
    for (int y = 11; y < 14; y++)
      for (int x = 7; x < 10; x++)
        pixel(x, y);
  } else if (w == 1) {
    pixel(8, 10);
    pixel(7, 11);
    pixel(9, 11);
    pixel(8, 12);
  } else
    for (int i = 1; i < 15; i++) {
      pixel(i, 15 - i);
      pixel(i, 14 - i);
    }
  // HTTP: unknown dash, connected rings, broken rings, or configuration
  // warning.
  if (h == 0) {
    for (int x = 27; x < 36; x++)
      pixel(x, 8);
  } else if (h == 3) {
    for (int y = 2; y < 10; y++) {
      pixel(31, y);
      pixel(32, y);
    }
    pixel(31, 12);
    pixel(32, 12);
  } else {
    for (int y = 4; y <= 11; y++)
      for (int x = 23; x <= 39; x++) {
        bool edge = (x <= 31 && (x == 23 || x == 31 || y == 4 || y == 11)) ||
                    (x >= 30 && (x == 30 || x == 39 || y == 4 || y == 11));
        if (edge && (h == 1 || x < 29 || x > 33))
          pixel(x, y);
      }
    if (h == 1)
      for (int x = 28; x <= 34; x++)
        pixel(x, 8);
    else {
      pixel(30, 2);
      pixel(33, 13);
    }
  }
  lv_image_cache_drop(&network_bitmap);
  lv_obj_invalidate(network_label);
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
  update_network_icons();
  int64_t monotonic = esp_timer_get_time();
  const char *link = network_enabled() && network_wifi() == 2
                         ? (network_http() == 1 ? "NET" : "WAIT")
                     : (last_usb && monotonic - last_usb < 6000000) ? "USB"
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
  if (text_mode) {
    rtd::render(text_document.pages[page_index], font, bits);
  } else if (active.size() && !pages.empty()) {
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
  date_label = label(166, 12, 48);
  network_bitmap.header.magic = LV_IMAGE_HEADER_MAGIC;
  network_bitmap.header.cf = LV_COLOR_FORMAT_RGB565;
  network_bitmap.header.w = 40;
  network_bitmap.header.h = 16;
  network_bitmap.header.stride = 80;
  network_bitmap.data_size = sizeof(network_pixels);
  network_bitmap.data = (uint8_t *)network_pixels;
  network_label = lv_image_create(lv_screen_active());
  lv_image_set_src(network_label, &network_bitmap);
  lv_obj_set_pos(network_label, 248, 10);
  lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_RIGHT, 0);
  status_label = label(174, 380, 114);
  lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_RIGHT, 0);
  pair_label = label(50, 155, 200);
  lv_obj_set_style_text_font(pair_label, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_align(pair_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_all(pair_label, 12, 0);
  lv_obj_add_flag(pair_label, LV_OBJ_FLAG_HIDDEN);
}
static bool decode_snapshot(const std::vector<uint8_t> &wire,
                            rtd::TextDocument &doc,
                            std::vector<uint8_t> &decoded,
                            std::vector<rtd::Page> &parsed,
                            std::string &error) {
  if (wire.size() >= 24 && memcmp(wire.data(), "RTD4", 4) == 0) {
    if (!rtd::parse_text(wire.data(), wire.size(), doc, error) ||
        !rtd::layout(doc, font, error))
      return false;
    std::vector<uint8_t> check(rtd::Bytes);
    for (auto &p : doc.pages)
      if (!rtd::render(p, font, check.data())) {
        error = "Text outside display bounds";
        return false;
      }
    return true;
  }
  if (!rtd::expand(wire.data(), wire.size(), decoded) ||
      !rtd::parse(decoded.data(), decoded.size(), parsed)) {
    error = "Invalid snapshot";
    return false;
  }
  return true;
}
static std::string accept_snapshot(uint64_t next_version,
                                   const std::string &sha) {
  if (incoming.size() < 24 || incoming.size() > rtd::Limit ||
      digest(incoming.data(), incoming.size()) != sha ||
      rtd::u64(incoming.data() + 4) != next_version)
    return "Snapshot verification failed";
  if (next_version < version)
    return "Older version";
  if (next_version == version)
    return incoming == active ? "" : "Conflicting version";
  if (text_mode && memcmp(incoming.data(), "RTD4", 4))
    return "Legacy image disabled after text migration";
  std::vector<uint8_t> decoded;
  std::vector<rtd::Page> parsed;
  rtd::TextDocument doc;
  std::string error;
  if (!decode_snapshot(incoming, doc, decoded, parsed, error))
    return error;
  if (!save_snapshot())
    return "Cache write failed";
  active.swap(incoming);
  display_data.swap(decoded);
  pages.swap(parsed);
  text_document = std::move(doc);
  text_mode = active[3] == '4';
  version = next_version;
  page_index = 0;
  show(); // SPI refresh completes before acknowledgement.
  return "";
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
      cJSON_AddNumberToObject(reply, "uptime_ms", esp_timer_get_time() / 1000);
      cJSON_AddNumberToObject(reply, "pages", page_count());
      cJSON_AddNumberToObject(reply, "wifi", network_wifi());
      cJSON_AddNumberToObject(reply, "http", network_http());
      cJSON_AddBoolToObject(reply, "text_mode", text_mode);
      cJSON_AddNumberToObject(reply, "heap_internal",
                              heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
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
  if (op == "network" && transport == 1) {
    return network_configure(string(request, "config"));
  }
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
    if (received != incoming.size()) {
      reset_transfer();
      return "Incomplete snapshot";
    }
    auto error = accept_snapshot(transfer_version, transfer_hash);
    reset_transfer();
    if (!error.empty())
      return error;
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
    } else if (!b.held && page_count()) {
      page_index =
          (page_index + page_count() + (b.gpio == 18 ? 1 : -1)) % page_count();
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
  screen_bits = (uint8_t *)heap_caps_malloc(rtd::Bytes, MALLOC_CAP_SPIRAM);
  bits = (uint8_t *)heap_caps_malloc(rtd::Bytes, MALLOC_CAP_SPIRAM);
  assert(screen_bits && bits);
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
  auto fp = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                     ESP_PARTITION_SUBTYPE_ANY, "fonts");
  const void *font_data = nullptr;
  if (fp && esp_partition_mmap(fp, 0, fp->size, ESP_PARTITION_MMAP_DATA,
                               &font_data, &font_mapping) == ESP_OK)
    font.open((const uint8_t *)font_data, fp->size);
  for (int slot = 0; slot < 2; slot++) {
    std::vector<uint8_t> wire, decoded;
    std::vector<rtd::Page> parsed;
    rtd::TextDocument doc;
    std::string error;
    if (load_slot(slot, wire) &&
        decode_snapshot(wire, doc, decoded, parsed, error) &&
        rtd::u64(wire.data() + 4) > version) {
      version = rtd::u64(wire.data() + 4);
      active.swap(wire);
      display_data.swap(decoded);
      pages.swap(parsed);
      text_document = std::move(doc);
      text_mode = active[3] == '4';
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
  network_start();
  Button key{18}, boot{0};
  int64_t last_status = 0;
  for (;;) {
    Message msg;
    if (xQueueReceive(inbox, &msg, pdMS_TO_TICKS(10)))
      process(msg);
    if (auto epoch = network_time())
      rtc_sync(epoch);
    if (auto job = network_pending()) {
      if (job->kind == NetworkJob::Apply) {
        reset_transfer();
        incoming = std::move(job->data);
        job->error = accept_snapshot(job->version, job->sha);
        reset_transfer();
      } else {
        job->version = version;
        job->sha = active.empty() ? "" : digest(active.data(), active.size());
        job->page = page_index;
        if (job->kind == NetworkJob::Capture) {
          lv_refr_now(nullptr);
          job->data.assign(screen_bits, screen_bits + rtd::Bytes);
        }
      }
      network_complete(job);
    }
    int64_t now = esp_timer_get_time();
    bool changed = button(key, now);
    changed = button(boot, now) || changed;
    if (changed)
      show();
    else if (auto_pager.advance(page_index, page_count(), now,
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
