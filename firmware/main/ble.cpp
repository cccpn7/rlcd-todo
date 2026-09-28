#include "esp_err.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "io.hpp"
#include <cstring>
extern "C" {
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
void ble_store_config_init(void);
}
// UUID bytes are little-endian as required by NimBLE.
static const ble_uuid128_t service =
    BLE_UUID128_INIT(1, 0, 0x2f, 0x67, 0x48, 0xe8, 0x20, 0x9d, 0x61, 0x4a, 0x7a,
                     0x5e, 0, 0x10, 0xef, 0xbe);
static const ble_uuid128_t rx =
    BLE_UUID128_INIT(1, 0, 0x2f, 0x67, 0x48, 0xe8, 0x20, 0x9d, 0x61, 0x4a, 0x7a,
                     0x5e, 1, 0x10, 0xef, 0xbe);
static const ble_uuid128_t tx =
    BLE_UUID128_INIT(1, 0, 0x2f, 0x67, 0x48, 0xe8, 0x20, 0x9d, 0x61, 0x4a, 0x7a,
                     0x5e, 2, 0x10, 0xef, 0xbe);
static char response[512] = "@RTD1 {\"id\":0,\"ok\":true}\n";
static SemaphoreHandle_t lock;
static uint8_t addr_type;
void ble_reply(const char *message) {
  xSemaphoreTake(lock, portMAX_DELAY);
  strlcpy(response, message, sizeof(response));
  xSemaphoreGive(lock);
}
static int access(uint16_t, uint16_t, ble_gatt_access_ctxt *context, void *) {
  if (context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    xSemaphoreTake(lock, portMAX_DELAY);
    int rc = os_mbuf_append(context->om, response, strlen(response));
    xSemaphoreGive(lock);
    return rc ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
  }
  uint16_t len = OS_MBUF_PKTLEN(context->om);
  if (len > 512)
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  uint8_t bytes[512];
  if (ble_hs_mbuf_to_flat(context->om, bytes, sizeof(bytes), nullptr))
    return BLE_ATT_ERR_UNLIKELY;
  receive_bytes(2, bytes, len);
  return 0;
}
static ble_gatt_chr_def characteristics[3];
static ble_gatt_svc_def services[2];
static void advertise();
static int event(ble_gap_event *e, void *) {
  switch (e->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (e->connect.status)
      advertise();
    else {
      ble_gap_upd_params p = {};
      p.itvl_min = 12;
      p.itvl_max = 24;
      p.supervision_timeout = 400;
      ble_gap_update_params(e->connect.conn_handle, &p);
    }
    break;
  case BLE_GAP_EVENT_DISCONNECT:
    receive_bytes(2, nullptr, 0);
    clear_pairing();
    advertise();
    break;
  case BLE_GAP_EVENT_ADV_COMPLETE:
    advertise();
    break;
  case BLE_GAP_EVENT_ENC_CHANGE:
    clear_pairing();
    break;
  case BLE_GAP_EVENT_PASSKEY_ACTION:
    if (e->passkey.params.action == BLE_SM_IOACT_DISP) {
      ble_sm_io io = {};
      io.action = BLE_SM_IOACT_DISP;
      io.passkey = 100000 + esp_random() % 900000;
      show_pairing(io.passkey);
      ble_sm_inject_io(e->passkey.conn_handle, &io);
    }
    break;
  case BLE_GAP_EVENT_REPEAT_PAIRING:
    // Require the user to explicitly forget stale pairings instead of deleting
    // bonds remotely.
    return BLE_GAP_REPEAT_PAIRING_IGNORE;
  }
  return 0;
}
static void advertise() {
  ble_hs_adv_fields fields = {};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids128 = const_cast<ble_uuid128_t *>(&service);
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  ble_gap_adv_set_fields(&fields);
  ble_hs_adv_fields rsp = {};
  rsp.name = (uint8_t *)ble_svc_gap_device_name();
  rsp.name_len = strlen((char *)rsp.name);
  rsp.name_is_complete = 1;
  ble_gap_adv_rsp_set_fields(&rsp);
  ble_gap_adv_params p = {};
  p.conn_mode = BLE_GAP_CONN_MODE_UND;
  p.disc_mode = BLE_GAP_DISC_MODE_GEN;
  p.itvl_min = 160;
  p.itvl_max = 240;
  ble_gap_adv_start(addr_type, nullptr, BLE_HS_FOREVER, &p, event, nullptr);
}
static void sync() {
  if (!ble_hs_util_ensure_addr(0) && !ble_hs_id_infer_auto(0, &addr_type))
    advertise();
}
static void host(void *) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}
void ble_init(const char *name) {
  lock = xSemaphoreCreateMutex();
  ESP_ERROR_CHECK(nimble_port_init());
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_svc_gap_device_name_set(name);
  characteristics[0].uuid = &rx.u;
  characteristics[0].access_cb = access;
  characteristics[0].flags =
      BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
      BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN;
  characteristics[1].uuid = &tx.u;
  characteristics[1].access_cb = access;
  characteristics[1].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                             BLE_GATT_CHR_F_READ_AUTHEN;
  services[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
  services[0].uuid = &service.u;
  services[0].characteristics = characteristics;
  ESP_ERROR_CHECK(ble_gatts_count_cfg(services));
  ESP_ERROR_CHECK(ble_gatts_add_svcs(services));
  ble_hs_cfg.sync_cb = sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 1;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_store_config_init();
  nimble_port_freertos_init(host);
}
