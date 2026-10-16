#pragma once
#include <cstddef>
#include <cstdint>
void receive_bytes(int transport, const uint8_t *bytes, size_t size);
void ble_init(const char *name);
void ble_reply(const char *message);
void show_pairing(uint32_t code);
void clear_pairing();
void rtc_init();
void rtc_sync(int64_t epoch);
