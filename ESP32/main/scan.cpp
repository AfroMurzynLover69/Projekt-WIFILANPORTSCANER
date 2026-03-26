#include <Arduino.h>
#include <WiFi.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>

extern "C" {
#include <lwip/etharp.h>
#include <lwip/inet.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/sockets.h>
#include <lwip/prot/ethernet.h>
#include <lwip/prot/etharp.h>
#include <lwip/prot/iana.h>
#include <lwip/def.h>
}

#include "konfiguracja.h"
#include "portscan.h"

#if (SCAN_MODE != SCAN_MODE_ARP_ONLY) && (SCAN_MODE != SCAN_MODE_BCAST_THEN_ARP)
#error "Invalid SCAN_MODE in konfiguracja.h"
#endif

// Symbole zdefiniowane w main.ino
extern TaskHandle_t g_scan_task;
void set_status(const String &s);
void set_wifi_text(const String &s);
void set_progress(uint32_t done, uint32_t total);
void append_log(const String &line);
void mark_scan_started();
void mark_scan_finished();
bool is_scan_busy();
uint32_t ip_to_u32(const IPAddress &ip);
IPAddress u32_to_ip(uint32_t v);

const char *scan_mode_name() {
#if SCAN_MODE == SCAN_MODE_BCAST_THEN_ARP
  return "BCAST_PING_10S + ARP_60S";
#else
  return "ARP_60S";
#endif
}

namespace {

struct IcmpEchoPacket {
  uint8_t type;
  uint8_t code;
  uint16_t checksum;
  uint16_t identifier;
  uint16_t sequence;
  uint8_t payload[16];
};

static struct netif *g_sta_netif = nullptr;

uint16_t icmp_checksum(const uint8_t *data, size_t len) {
  uint32_t sum = 0;

  while (len > 1) {
    sum += ((uint16_t)data[0] << 8) | data[1];
    data += 2;
    len -= 2;
  }

  if (len == 1) {
    sum += ((uint16_t)data[0] << 8);
  }

  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }

  return (uint16_t)(~sum);
}

ip4_addr_t ip_to_ip4(const IPAddress &ip) {
  ip4_addr_t out;
  IP4_ADDR(&out, ip[0], ip[1], ip[2], ip[3]);
  return out;
}

bool init_sta_netif() {
  esp_netif_t *esp_if = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!esp_if) return false;

  g_sta_netif = reinterpret_cast<struct netif *>(esp_netif_get_netif_impl(esp_if));
  return g_sta_netif != nullptr;
}

bool connect_wifi(unsigned long timeout_ms) {
  if (WiFi.status() == WL_CONNECTED) return true;

  set_status("Status: laczenie WiFi...");
  set_wifi_text("WiFi: laczenie...");
  append_log("WiFi: start laczenia");

  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
#ifdef WIFI_POWER_8_5dBm
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
  WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeout_ms) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (WiFi.status() != WL_CONNECTED) {
    set_wifi_text("WiFi: OFF");
    append_log("WiFi: brak polaczenia");
    return false;
  }

  String ip = WiFi.localIP().toString();
  set_wifi_text(String("WiFi: ") + ip);
  append_log(String("WiFi: polaczono, IP=") + ip);
  return true;
}

bool send_icmp_echo_once(const IPAddress &target) {
  int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sock < 0) return false;

  int one = 1;
  (void)setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = inet_addr(target.toString().c_str());

  IcmpEchoPacket pkt = {};
  pkt.type = 8;
  pkt.code = 0;
  pkt.identifier = htons((uint16_t)(ESP.getEfuseMac() & 0xFFFF));
  pkt.sequence = htons((uint16_t)(millis() & 0xFFFF));
  for (size_t i = 0; i < sizeof(pkt.payload); i++) {
    pkt.payload[i] = (uint8_t)(0x30 + (i % 10));
  }
  pkt.checksum = 0;
  pkt.checksum = icmp_checksum(reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));

  int sent = sendto(sock, &pkt, sizeof(pkt), 0, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  close(sock);
  return sent == (int)sizeof(pkt);
}

bool arp_cache_has_ip(const ip4_addr_t &target_ip4) {
  for (size_t i = 0; i < ARP_TABLE_SIZE; i++) {
    ip4_addr_t *entry_ip = nullptr;
    struct netif *entry_netif = nullptr;
    struct eth_addr *entry_eth = nullptr;
    int ok = etharp_get_entry(i, &entry_ip, &entry_netif, &entry_eth);
    if (ok && entry_ip && entry_netif == g_sta_netif && ip4_addr_cmp(entry_ip, &target_ip4)) {
      return true;
    }
  }
  return false;
}

bool send_arp_request_frame(const ip4_addr_t &src_ip4, const ip4_addr_t &dst_ip4) {
  if (!g_sta_netif || !g_sta_netif->linkoutput || g_sta_netif->hwaddr_len < ETH_HWADDR_LEN) return false;

  struct pbuf *p = pbuf_alloc(PBUF_RAW_TX, SIZEOF_ETH_HDR + SIZEOF_ETHARP_HDR, PBUF_RAM);
  if (!p) return false;

  if (p->len < (SIZEOF_ETH_HDR + SIZEOF_ETHARP_HDR)) {
    pbuf_free(p);
    return false;
  }

  uint8_t *raw = reinterpret_cast<uint8_t *>(p->payload);
  auto *eth = reinterpret_cast<struct eth_hdr *>(raw);
  auto *arp = reinterpret_cast<struct etharp_hdr *>(raw + SIZEOF_ETH_HDR);

  memset(eth->dest.addr, 0xFF, ETH_HWADDR_LEN);
  memcpy(eth->src.addr, g_sta_netif->hwaddr, ETH_HWADDR_LEN);
  eth->type = PP_HTONS(ETHTYPE_ARP);

  arp->hwtype = PP_HTONS(LWIP_IANA_HWTYPE_ETHERNET);
  arp->proto = PP_HTONS(ETHTYPE_IP);
  arp->hwlen = ETH_HWADDR_LEN;
  arp->protolen = sizeof(ip4_addr_t);
  arp->opcode = PP_HTONS(ARP_REQUEST);
  memcpy(arp->shwaddr.addr, g_sta_netif->hwaddr, ETH_HWADDR_LEN);
  IPADDR_WORDALIGNED_COPY_FROM_IP4_ADDR_T(&arp->sipaddr, &src_ip4);
  memset(arp->dhwaddr.addr, 0x00, ETH_HWADDR_LEN);
  IPADDR_WORDALIGNED_COPY_FROM_IP4_ADDR_T(&arp->dipaddr, &dst_ip4);

  err_t err = g_sta_netif->linkoutput(g_sta_netif, p);
  pbuf_free(p);
  return err == ERR_OK;
}

bool arp_detect_host(const IPAddress &source, const IPAddress &target) {
  if (!g_sta_netif) return false;

  ip4_addr_t src_ip4 = ip_to_ip4(source);
  ip4_addr_t dst_ip4 = ip_to_ip4(target);
  if (arp_cache_has_ip(dst_ip4)) return true;

  for (int attempt = 0; attempt < 2; attempt++) {
    if (!send_arp_request_frame(src_ip4, dst_ip4)) continue;

    for (int spin = 0; spin < 3; spin++) {
      vTaskDelay(pdMS_TO_TICKS(4));
      if (arp_cache_has_ip(dst_ip4)) return true;
    }
  }

  return false;
}

void log_scan_preamble(const IPAddress &local_ip, const IPAddress &mask) {
  IPAddress net = u32_to_ip(ip_to_u32(local_ip) & ip_to_u32(mask));
  String ssid = WiFi.SSID();
  String mac = WiFi.macAddress();
  int32_t rssi = WiFi.RSSI();

  append_log("=== PREAMBULA ===");
  append_log(String("SSID: ") + (ssid.length() ? ssid : "-"));
  append_log(String("MAC: ") + (mac.length() ? mac : "-"));
  append_log(String("RSSI: ") + rssi + " dBm");
  append_log(String("IP: ") + local_ip.toString());
  append_log(String("MASKA: ") + mask.toString());
  append_log(String("SIEC: ") + net.toString());
  append_log("=================");
}

void finish_scan_task() {
  mark_scan_finished();
  vTaskDelete(nullptr);
}

void run_broadcast_ping_phase(const IPAddress &broadcast_ip) {
  append_log(String("BCAST PING: ") + broadcast_ip.toString());
  set_status("Status: ping broadcast + czekanie...");

  bool sent = send_icmp_echo_once(broadcast_ip);
  append_log(sent ? "BCAST PING: wyslano" : "BCAST PING: blad wysylki");

  unsigned long t0 = millis();
  while ((millis() - t0) < SCAN_BROADCAST_WAIT_MS) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  append_log(String("BCAST PING: czekano ") + (SCAN_BROADCAST_WAIT_MS / 1000) + " s");
}

void run_arp_phase(const IPAddress &local_ip, uint32_t start_u, uint32_t end_u, uint32_t self_ip_u,
                   std::vector<IPAddress> &active_hosts_out) {
  uint32_t host_total = end_u - start_u + 1;
  uint32_t scannable_total = host_total;
  if (self_ip_u >= start_u && self_ip_u <= end_u && scannable_total > 0) {
    scannable_total -= 1;
  }

  if (scannable_total == 0) {
    append_log("ARP: brak hostow do sprawdzenia");
    set_status("Status: zakonczono, brak hostow");
    set_progress(0, 0);
    return;
  }

  append_log("DISCOVER: start (ARP, wielorundowy)");
  set_status("Status: ARP skan...");
  set_progress(0, scannable_total);

  std::vector<uint8_t> seen(host_total, 0);
  active_hosts_out.clear();
  active_hosts_out.reserve(scannable_total > 0 ? scannable_total : 8);
  uint32_t active_hosts = 0;
  uint32_t probe_count = 0;
  uint32_t rounds = 0;

  for (uint32_t idx = 0; idx < host_total; idx++) {
    uint32_t host_u = start_u + idx;
    if (host_u == self_ip_u) seen[idx] = 1;
  }

  unsigned long t0 = millis();
  while ((millis() - t0) < SCAN_ARP_TOTAL_WAIT_MS && active_hosts < scannable_total) {
    rounds++;
    bool any_new = false;

    for (uint32_t idx = 0; idx < host_total; idx++) {
      if (seen[idx]) continue;

      uint32_t host_u = start_u + idx;
      IPAddress target = u32_to_ip(host_u);
      if (arp_detect_host(local_ip, target)) {
        seen[idx] = 1;
        any_new = true;
        active_hosts++;
        active_hosts_out.push_back(target);
        append_log(String("ARP ") + target.toString() + " UP");
      }

      probe_count++;
      if ((probe_count % 16) == 0 || active_hosts == scannable_total) {
        set_progress(active_hosts, scannable_total);
        set_status(String("Status: ARP runda ") + rounds + " " + active_hosts + "/" + scannable_total);
      }

      if (SCAN_ARP_PER_HOST_DELAY_MS > 0) {
        vTaskDelay(pdMS_TO_TICKS(SCAN_ARP_PER_HOST_DELAY_MS));
      }
    }

    if (!any_new) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  unsigned long elapsed_ms = millis() - t0;
  set_progress(active_hosts, scannable_total);

  if (active_hosts == scannable_total) {
    append_log(String("DISCOVER: komplet, hosty=") + active_hosts + ", rundy=" + rounds);
    set_status(String("Status: zakonczono, hosty aktywne=") + active_hosts);
  } else {
    uint32_t missing = scannable_total - active_hosts;
    append_log(String("DISCOVER: timeout ") + (SCAN_ARP_TOTAL_WAIT_MS / 1000) + " s, hosty=" + active_hosts +
               ", brak=" + missing + ", rundy=" + rounds);
    set_status(String("Status: timeout, hosty=") + active_hosts + "/" + scannable_total);
  }

  append_log(String("DISCOVER: czas ") + (elapsed_ms / 1000) + " s");
}

void scan_task(void *arg) {
  (void)arg;

  mark_scan_started();
  append_log(String("SCAN: start (") + scan_mode_name() + ")");

  if (!connect_wifi(SCAN_WIFI_CONNECT_TIMEOUT_MS)) {
    set_status("Status: brak WiFi");
    finish_scan_task();
    return;
  }

  if (!init_sta_netif()) {
    append_log("SCAN: blad inicjalizacji netif");
    set_status("Status: blad netif");
    finish_scan_task();
    return;
  }

  IPAddress local_ip = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  log_scan_preamble(local_ip, mask);

  uint32_t ip_u = ip_to_u32(local_ip);
  uint32_t mask_u = ip_to_u32(mask);
  uint32_t net_u = ip_u & mask_u;
  uint32_t broadcast_u = net_u | (~mask_u);

  uint32_t start_u = net_u + 1;
  uint32_t end_u = broadcast_u - 1;
  if (end_u <= start_u) {
    set_status("Status: zla podsiec");
    append_log("SCAN: niepoprawny zakres hostow");
    finish_scan_task();
    return;
  }

  uint32_t host_total = end_u - start_u + 1;
  if (host_total > SCAN_MAX_HOSTS) {
    end_u = start_u + SCAN_MAX_HOSTS - 1;
    host_total = SCAN_MAX_HOSTS;
    append_log(String("SCAN: ograniczono do ") + SCAN_MAX_HOSTS + " hostow");
  }

  (void)host_total;

#if SCAN_MODE == SCAN_MODE_BCAST_THEN_ARP
  run_broadcast_ping_phase(u32_to_ip(broadcast_u));
#endif

  std::vector<IPAddress> active_hosts;
  run_arp_phase(local_ip, start_u, end_u, ip_u, active_hosts);

  run_portscan_phase(active_hosts);
  finish_scan_task();
}

}  // namespace

bool start_scan_request(const char *source) {
  if (is_scan_busy()) {
    append_log("SCAN: juz trwa");
    return false;
  }

  append_log(source ? String(source) : String("SCAN: start"));
  if (xTaskCreatePinnedToCore(scan_task, "scan_task", 8192, nullptr, 1, &g_scan_task, 0) != pdPASS) {
    append_log("SCAN: blad tworzenia taska");
    return false;
  }

  return true;
}
