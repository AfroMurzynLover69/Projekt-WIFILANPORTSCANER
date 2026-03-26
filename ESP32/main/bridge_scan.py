#!/usr/bin/env python3
import ipaddress
import json
import socket
import threading
import time
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait

HOST = "192.168.0.103"
PORT = 5051
TOKEN = "zmien_to_haslo"

MAX_HOSTS = 1024
MAX_PORTS = 65535
DEFAULT_PORT_START = 1
DEFAULT_PORT_END = 1024
DEFAULT_CONNECT_TIMEOUT_MS = 50
DEFAULT_WORKERS = 256


def now_ms() -> int:
    return int(time.time() * 1000)


def pct(done: int, total: int) -> int:
    if total <= 0:
        return 0
    p = int((done * 100) / total)
    return 100 if p > 100 else p


def eta_s(done: int, total: int, start_ms: int) -> int:
    if done <= 0 or total <= done:
        return 0
    elapsed = max(1, now_ms() - start_ms)
    rem = total - done
    return int((elapsed * rem / done) / 1000)


def best_local_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"
    finally:
        s.close()


def default_subnet() -> str:
    ip = best_local_ip()
    try:
        net = ipaddress.ip_network(f"{ip}/24", strict=False)
        return str(net)
    except Exception:
        return "192.168.1.0/24"


def json_send(conn: socket.socket, obj: dict):
    data = json.dumps(obj, ensure_ascii=False) + "\n"
    conn.sendall(data.encode("utf-8"))


def safe_int(val, default: int) -> int:
    try:
        return int(val)
    except Exception:
        return default


def parse_scan_args(args: dict) -> dict:
    subnet = str(args.get("subnet") or default_subnet())
    port_start = safe_int(args.get("port_start"), DEFAULT_PORT_START)
    port_end = safe_int(args.get("port_end"), DEFAULT_PORT_END)
    timeout_ms = safe_int(args.get("timeout_ms"), DEFAULT_CONNECT_TIMEOUT_MS)
    workers = safe_int(args.get("workers"), DEFAULT_WORKERS)
    arp_timeout_ms = safe_int(args.get("arp_timeout_ms"), 350)

    if port_start < 1:
        port_start = 1
    if port_end > MAX_PORTS:
        port_end = MAX_PORTS
    if port_end < port_start:
        port_end = port_start

    if timeout_ms < 10:
        timeout_ms = 10
    if timeout_ms > 3000:
        timeout_ms = 3000

    if workers < 1:
        workers = 1
    if workers > 1024:
        workers = 1024

    if arp_timeout_ms < 60:
        arp_timeout_ms = 60
    if arp_timeout_ms > 3000:
        arp_timeout_ms = 3000

    return {
        "subnet": subnet,
        "port_start": port_start,
        "port_end": port_end,
        "timeout_ms": timeout_ms,
        "workers": workers,
        "arp_timeout_ms": arp_timeout_ms,
    }


def tcp_probe(ip: str, port: int, timeout_ms: int) -> bool:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout_ms / 1000.0)
    try:
        return s.connect_ex((ip, port)) == 0
    except Exception:
        return False
    finally:
        s.close()


def arp_discover_with_scapy(conn: socket.socket, hosts: list[str], arp_timeout_ms: int) -> list[str]:
    # Optional dependency: scapy + (on Windows) Npcap installed.
    from scapy.all import ARP, Ether, srp  # type: ignore

    up_hosts: list[str] = []
    total = len(hosts)
    start = now_ms()

    for idx, ip in enumerate(hosts, start=1):
        pkt = Ether(dst="ff:ff:ff:ff:ff:ff") / ARP(pdst=ip)
        answered, _ = srp(pkt, timeout=arp_timeout_ms / 1000.0, verbose=0)
        if answered:
            up_hosts.append(ip)
            json_send(conn, {"type": "host_up", "ip": ip, "method": "ARP"})

        if idx == total or idx % 8 == 0:
            json_send(
                conn,
                {
                    "type": "progress",
                    "stage": "arp",
                    "done": idx,
                    "total": total,
                    "percent": pct(idx, total),
                    "eta_s": eta_s(idx, total, start),
                },
            )

    return up_hosts


def fallback_discover(conn: socket.socket, hosts: list[str], timeout_ms: int) -> list[str]:
    # Fallback without scapy: lightweight TCP probe on common ports.
    probe_ports = (80, 443, 22, 445, 3389)
    up_hosts: list[str] = []
    total = len(hosts)
    start = now_ms()

    for idx, ip in enumerate(hosts, start=1):
        alive = False
        for p in probe_ports:
            if tcp_probe(ip, p, min(timeout_ms, 120)):
                alive = True
                break

        if alive:
            up_hosts.append(ip)
            json_send(conn, {"type": "host_up", "ip": ip, "method": "PROBE"})

        if idx == total or idx % 8 == 0:
            json_send(
                conn,
                {
                    "type": "progress",
                    "stage": "arp",
                    "done": idx,
                    "total": total,
                    "percent": pct(idx, total),
                    "eta_s": eta_s(idx, total, start),
                },
            )

    return up_hosts


def run_portscan(conn: socket.socket, up_hosts: list[str], port_start: int, port_end: int, timeout_ms: int, workers: int):
    if not up_hosts:
        json_send(conn, {"type": "log", "stage": "portscan", "msg": "brak hostow do port scan"})
        json_send(conn, {"type": "progress", "stage": "portscan", "done": 0, "total": 0, "percent": 100, "eta_s": 0})
        return {}

    ports_count = port_end - port_start + 1
    total = len(up_hosts) * ports_count
    done = 0
    start = now_ms()

    open_ports: dict[str, list[int]] = {ip: [] for ip in up_hosts}

    def task(ip: str, port: int):
        return tcp_probe(ip, port, timeout_ms)

    targets = ((ip, port) for ip in up_hosts for port in range(port_start, port_end + 1))
    pending: dict = {}

    with ThreadPoolExecutor(max_workers=workers) as ex:
        for _ in range(workers * 2):
            try:
                ip, port = next(targets)
            except StopIteration:
                break
            pending[ex.submit(task, ip, port)] = (ip, port)

        last_emit = 0
        while pending:
            ready, _ = wait(list(pending.keys()), return_when=FIRST_COMPLETED)
            for fut in ready:
                ip, port = pending.pop(fut)
                is_open = False
                try:
                    is_open = bool(fut.result())
                except Exception:
                    is_open = False

                if is_open:
                    open_ports[ip].append(port)
                    json_send(conn, {"type": "port_open", "ip": ip, "port": port})

                done += 1

                try:
                    nip, nport = next(targets)
                    pending[ex.submit(task, nip, nport)] = (nip, nport)
                except StopIteration:
                    pass

            now = now_ms()
            if done == total or (now - last_emit) > 220:
                last_emit = now
                json_send(
                    conn,
                    {
                        "type": "progress",
                        "stage": "portscan",
                        "done": done,
                        "total": total,
                        "percent": pct(done, total),
                        "eta_s": eta_s(done, total, start),
                    },
                )

    return open_ports


def run_scan(conn: socket.socket, args: dict):
    cfg = parse_scan_args(args)

    try:
        net = ipaddress.ip_network(cfg["subnet"], strict=False)
    except Exception:
        json_send(conn, {"type": "error", "msg": "zly subnet"})
        return

    hosts = [str(h) for h in net.hosts()]
    if len(hosts) > MAX_HOSTS:
        hosts = hosts[:MAX_HOSTS]

    json_send(conn, {"type": "stage", "stage": "init", "status": "start"})
    json_send(conn, {"type": "log", "stage": "init", "msg": f"subnet={net} hosts={len(hosts)}"})

    json_send(conn, {"type": "stage", "stage": "arp", "status": "start"})
    try:
        up_hosts = arp_discover_with_scapy(conn, hosts, cfg["arp_timeout_ms"])
        json_send(conn, {"type": "log", "stage": "arp", "msg": f"ARP done up={len(up_hosts)}"})
    except Exception as e:
        json_send(conn, {"type": "log", "stage": "arp", "msg": f"ARP fallback: {e}"})
        up_hosts = fallback_discover(conn, hosts, cfg["timeout_ms"])
        json_send(conn, {"type": "log", "stage": "arp", "msg": f"fallback done up={len(up_hosts)}"})

    json_send(conn, {"type": "stage", "stage": "arp", "status": "done", "up_hosts": len(up_hosts)})

    json_send(conn, {"type": "stage", "stage": "portscan", "status": "start"})
    json_send(
        conn,
        {
            "type": "log",
            "stage": "portscan",
            "msg": f"ports={cfg['port_start']}-{cfg['port_end']} workers={cfg['workers']} timeout_ms={cfg['timeout_ms']}",
        },
    )

    open_ports = run_portscan(
        conn,
        up_hosts,
        cfg["port_start"],
        cfg["port_end"],
        cfg["timeout_ms"],
        cfg["workers"],
    )

    hosts_with_open = 0
    total_open = 0
    for ip in up_hosts:
        arr = open_ports.get(ip, [])
        if arr:
            hosts_with_open += 1
            total_open += len(arr)

    json_send(conn, {"type": "stage", "stage": "portscan", "status": "done"})
    json_send(
        conn,
        {
            "type": "done",
            "summary": {
                "subnet": str(net),
                "hosts_scanned": len(hosts),
                "hosts_up": len(up_hosts),
                "hosts_with_open_ports": hosts_with_open,
                "open_ports_total": total_open,
            },
        },
    )


def handle_client(conn: socket.socket, addr):
    json_send(conn, {"type": "hello", "msg": "bridge_scan ready", "from": str(addr)})

    buf = ""
    while True:
        data = conn.recv(4096)
        if not data:
            return
        buf += data.decode("utf-8", errors="ignore")

        while "\n" in buf:
            line, buf = buf.split("\n", 1)
            line = line.strip()
            if not line:
                continue

            try:
                req = json.loads(line)
            except Exception:
                json_send(conn, {"type": "error", "msg": "bad_json"})
                continue

            if req.get("token") != TOKEN:
                json_send(conn, {"type": "error", "msg": "unauthorized"})
                continue

            cmd = str(req.get("cmd", ""))
            if cmd == "scan":
                run_scan(conn, req.get("args", {}))
            elif cmd == "ping":
                json_send(conn, {"type": "pong", "ts": now_ms()})
            else:
                json_send(conn, {"type": "error", "msg": "cmd_not_supported", "allowed": ["scan", "ping"]})


def main():
    lock = threading.Lock()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((HOST, PORT))
        srv.listen(5)
        print(f"bridge_scan listening on {HOST}:{PORT}")

        while True:
            conn, addr = srv.accept()
            with conn:
                # one active client at a time to keep stream deterministic for ESP
                with lock:
                    try:
                        handle_client(conn, addr)
                    except Exception as e:
                        try:
                            json_send(conn, {"type": "error", "msg": f"internal_error: {e}"})
                        except Exception:
                            pass


if __name__ == "__main__":
    main()
