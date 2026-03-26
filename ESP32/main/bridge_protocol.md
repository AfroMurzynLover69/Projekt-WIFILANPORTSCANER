# ESP <-> Python Bridge Protocol (JSON lines)

TCP server: `bridge_scan.py`
- host: `PC_IP`
- port: `5051`
- framing: one JSON object per line (`\n`)

## Request from ESP

```json
{"token":"zmien_to_haslo","cmd":"scan","args":{"subnet":"192.168.1.0/24","port_start":1,"port_end":65535,"timeout_ms":35,"workers":256,"arp_timeout_ms":350}}
```

## Request ping (health)

```json
{"token":"zmien_to_haslo","cmd":"ping"}
```

## Streamed responses to ESP (examples)

```json
{"type":"hello","msg":"bridge_scan ready","from":"('192.168.1.50', 52341)"}
{"type":"stage","stage":"arp","status":"start"}
{"type":"progress","stage":"arp","done":64,"total":254,"percent":25,"eta_s":3}
{"type":"host_up","ip":"192.168.1.10","method":"ARP"}
{"type":"stage","stage":"arp","status":"done","up_hosts":5}
{"type":"stage","stage":"portscan","status":"start"}
{"type":"progress","stage":"portscan","done":5000,"total":327675,"percent":1,"eta_s":200}
{"type":"port_open","ip":"192.168.1.10","port":80}
{"type":"stage","stage":"portscan","status":"done"}
{"type":"done","summary":{"subnet":"192.168.1.0/24","hosts_scanned":254,"hosts_up":5,"hosts_with_open_ports":2,"open_ports_total":6}}
```

## Notes
- ARP phase uses `scapy` if available; if not, fallback discovery uses TCP probe.
- Full range `1..65535` can take long time.
- Recommended for lab/isolated networks only.
