#include "portscan.h"

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "konfiguracja.h"

void set_status(const String &s);
void set_progress(uint32_t done, uint32_t total);
void append_log(const String &line);
void get_port_scan_range(uint32_t &start, uint32_t &end);

namespace {

bool tcp_port_open(const IPAddress &host, uint16_t port, uint32_t timeout_ms) {
  WiFiClient client;
  bool ok = client.connect(host, port, (int)timeout_ms);
  client.stop();
  return ok;
}

}  // namespace

void run_portscan_phase(const std::vector<IPAddress> &hosts) {
  if (hosts.empty()) {
    append_log("PORTSCAN: skipped, no hosts");
    set_status("Status: done, no hosts for port scan");
    return;
  }

  uint32_t port_start = 0;
  uint32_t port_end = 0;
  get_port_scan_range(port_start, port_end);
  const uint32_t per_host_ports = (port_end >= port_start) ? (port_end - port_start + 1) : 0;
  if (per_host_ports == 0) {
    append_log("PORTSCAN: invalid port range");
    set_status("Status: invalid port range");
    return;
  }

  const uint32_t total_steps = (uint32_t)hosts.size() * per_host_ports;
  uint32_t step = 0;
  uint32_t hosts_with_open = 0;

  append_log(String("PORTSCAN: start hosts=") + hosts.size() + ", ports=" + port_start + "-" + port_end);
  set_status("Status: port scan hosts...");
  set_progress(0, total_steps);

  for (uint32_t h = 0; h < hosts.size(); h++) {
    const IPAddress host = hosts[h];
    uint32_t open_count = 0;
    String first_open = "";

    for (uint32_t port = port_start; port <= port_end; port++) {
      if (tcp_port_open(host, (uint16_t)port, SCAN_PORT_TIMEOUT_MS)) {
        open_count++;
        if (first_open.length() < 220) {
          if (first_open.length()) first_open += ",";
          first_open += String(port);
        }
        append_log(String("PORT ") + host.toString() + ":" + port + " OPEN");
      }

      step++;
      if ((step % SCAN_PORT_PROGRESS_EVERY) == 0 || step == total_steps) {
        set_progress(step, total_steps);
      }

      if (SCAN_PORT_PACE_EVERY > 0 && SCAN_PORT_PACE_DELAY_MS > 0 && (step % SCAN_PORT_PACE_EVERY) == 0) {
        vTaskDelay(pdMS_TO_TICKS(SCAN_PORT_PACE_DELAY_MS));
      }
    }

    if (open_count > 0) {
      hosts_with_open++;
      append_log(String("PORTSCAN HOST ") + host.toString() + " OPEN_COUNT=" + open_count +
                 (first_open.length() ? String(" FIRST=") + first_open : String("")));
    } else {
      append_log(String("PORTSCAN HOST ") + host.toString() + " OPEN_COUNT=0");
    }

    set_status(String("Status: port scan host ") + (h + 1) + "/" + hosts.size());
  }

  append_log(String("PORTSCAN: done, hosts with open ports=") + hosts_with_open);
  set_status(String("Status: done, port scan hosts=") + hosts.size());
}
