#!/usr/bin/env python3
import socket
import threading
import time


def get_lan_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("1.1.1.1", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def handle_client(conn: socket.socket, addr):
    try:
        conn.sendall(b"ESP32 test service OK\n")
        time.sleep(0.1)
    except OSError:
        pass
    finally:
        conn.close()


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", 0))  # 0 = losowy wolny port
    srv.listen(16)

    ip = get_lan_ip()
    port = srv.getsockname()[1]

    print("=== TEST SERVICE ===")
    print(f"IP:   {ip}")
    print(f"PORT: {port}")
    print("Uruchom na ESP:")
    print(f"  TARGET {ip}")
    print("  START")
    print("Ctrl+C aby zatrzymac.")
    print("====================")

    try:
        while True:
            conn, addr = srv.accept()
            threading.Thread(target=handle_client, args=(conn, addr), daemon=True).start()
    except KeyboardInterrupt:
        print("\nZatrzymano.")
    finally:
        srv.close()


if __name__ == "__main__":
    main()
