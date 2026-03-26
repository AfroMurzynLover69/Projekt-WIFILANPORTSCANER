#!/usr/bin/env python3
import datetime
import json
import platform
import socket
import subprocess

HOST = "0.0.0.0"
PORT = 5050
TOKEN = "zmien_to_haslo"


def run_cmd(args, timeout=12):
    p = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    out = (p.stdout or "") + ("\n" + p.stderr if p.stderr else "")
    return out.strip()[:16000]


def cmd_date():
    return datetime.datetime.now().isoformat()


def cmd_hostname():
    return platform.node()


def cmd_ip():
    if platform.system().lower().startswith("win"):
        return run_cmd(["ipconfig"])
    return run_cmd(["ip", "a"])


def cmd_ping(args):
    target = args.get("target", "8.8.8.8")
    if platform.system().lower().startswith("win"):
        return run_cmd(["ping", "-n", "2", target])
    return run_cmd(["ping", "-c", "2", target])


ALLOWED = {
    "date": lambda a: cmd_date(),
    "hostname": lambda a: cmd_hostname(),
    "ip": lambda a: cmd_ip(),
    "ping": cmd_ping,
}


def handle_line(line):
    try:
        req = json.loads(line)
    except Exception:
        return {"ok": False, "error": "bad_json"}

    if req.get("token") != TOKEN:
        return {"ok": False, "error": "unauthorized"}

    cmd = req.get("cmd", "")
    fn = ALLOWED.get(cmd)
    if not fn:
        return {"ok": False, "error": "cmd_not_allowed", "allowed": list(ALLOWED.keys())}

    try:
        out = fn(req.get("args", {}))
        return {"ok": True, "cmd": cmd, "output": out}
    except Exception as e:
        return {"ok": False, "error": str(e)}


def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((HOST, PORT))
        s.listen(5)
        print(f"bridge listening on {HOST}:{PORT}")

        while True:
            conn, addr = s.accept()
            with conn:
                print("client:", addr)
                buf = ""
                while True:
                    data = conn.recv(4096)
                    if not data:
                        break
                    buf += data.decode("utf-8", errors="ignore")

                    while "\n" in buf:
                        line, buf = buf.split("\n", 1)
                        line = line.strip()
                        if not line:
                            continue

                        resp = handle_line(line)
                        conn.sendall((json.dumps(resp, ensure_ascii=False) + "\n").encode("utf-8"))


if __name__ == "__main__":
    main()
