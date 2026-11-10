#pragma once
#include "Arduino.h"
typedef enum { WIFI_MODE_NULL = 0, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
#define WIFI_OFF WIFI_MODE_NULL
#define WIFI_AP WIFI_MODE_AP
typedef enum { WIFI_POWER_19_5dBm = 78, WIFI_POWER_15dBm = 60, WIFI_POWER_11dBm = 44, WIFI_POWER_8_5dBm = 34, WIFI_POWER_2dBm = 8 } wifi_power_t;
typedef enum { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3 } wifi_auth_mode_t;
typedef enum { WIFI_CIPHER_TYPE_CCMP = 3 } wifi_cipher_type_t;
class WiFiClass {
 public:
  bool mode(wifi_mode_t m);
  bool setTxPower(wifi_power_t power);
  void persistent(bool persistent);
  bool softAP(const char* ssid, const char* passphrase = NULL, int channel = 1, int ssid_hidden = 0, int max_connection = 4, bool ftm_responder = false, wifi_auth_mode_t auth_mode = WIFI_AUTH_WPA2_PSK, wifi_cipher_type_t cipher = WIFI_CIPHER_TYPE_CCMP);
  bool softAPConfig(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dhcp_lease_start = (uint32_t)0, IPAddress dns = (uint32_t)0);
  bool softAPdisconnect(bool wifioff = false);
  uint8_t softAPgetStationNum();
};
extern WiFiClass WiFi;
