#!/usr/bin/env python3
"""
Simple port tester:
- opens random TCP ports
- keeps them active so the scanner can detect them as open
- closes everything after Ctrl+C
"""

import argparse
import random
import selectors
import socket


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Open random TCP ports for scanner tests."
    )
    parser.add_argument(
        "--count",
        type=int,
        default=15,
        help="How many ports to open (default: 15).",
    )
    parser.add_argument(
        "--host",
        default="0.0.0.0",
        help='Listen interface, e.g. "0.0.0.0" or "127.0.0.1".',
    )
    parser.add_argument(
        "--backlog",
        type=int,
        default=8,
        help="TCP connection backlog size (default: 8).",
    )
    parser.add_argument(
        "--min-port",
        type=int,
        default=1,
        help="Lower port range bound (default: 1).",
    )
    parser.add_argument(
        "--max-port",
        type=int,
        default=4096,
        help="Upper port range bound (default: 4096).",
    )
    return parser.parse_args()


def open_listener(host: str, port: int, backlog: int) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, port))
    sock.listen(backlog)
    sock.setblocking(False)
    return sock


def bind_random_ports(
    host: str,
    backlog: int,
    start_port: int,
    end_port: int,
    target_count: int,
    used_ports: set[int],
) -> list[socket.socket]:
    listeners: list[socket.socket] = []
    max_attempts = max(200, target_count * 60)
    attempts = 0

    while len(listeners) < target_count and attempts < max_attempts:
        attempts += 1
        port = random.randint(start_port, end_port)
        if port in used_ports:
            continue
        try:
            sock = open_listener(host, port, backlog)
        except OSError:
            continue
        listeners.append(sock)
        used_ports.add(port)

    if len(listeners) < target_count:
        for sock in listeners:
            sock.close()
        raise RuntimeError(
            f"Could not open {target_count} ports from range "
            f"{start_port}-{end_port}."
        )

    return listeners


def main() -> None:
    args = parse_args()
    if args.count < 1:
        raise ValueError("--count must be >= 1")
    if args.min_port < 1:
        raise ValueError("--min-port must be >= 1")
    if args.max_port > 65535:
        raise ValueError("--max-port must be <= 65535")
    if args.min_port > args.max_port:
        raise ValueError("--min-port must be <= --max-port")

    selector = selectors.DefaultSelector()
    listeners = []
    used_ports: set[int] = set()

    try:
        listeners = bind_random_ports(
            host=args.host,
            backlog=args.backlog,
            start_port=args.min_port,
            end_port=args.max_port,
            target_count=args.count,
            used_ports=used_ports,
        )
        for sock in listeners:
            selector.register(sock, selectors.EVENT_READ)

        ports = sorted(sock.getsockname()[1] for sock in listeners)
        ports_csv = ",".join(str(port) for port in ports)
        print("Active random TCP ports:")
        print("  " + ports_csv)
        print(f"Range ({args.min_port}-{args.max_port}): {args.count}")
        print("")
        print("Waiting for connections. Stop with Ctrl+C")

        while True:
            events = selector.select(timeout=1.0)
            for key, _ in events:
                server_sock = key.fileobj
                conn, addr = server_sock.accept()
                port = server_sock.getsockname()[1]
                print(f"[port {port}] connection from {addr[0]}:{addr[1]}")
                conn.close()
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        selector.close()
        for sock in listeners:
            sock.close()


if __name__ == "__main__":
    main()
