#!/usr/bin/env python3
"""
Prosty tester portow:
- otwiera losowe porty TCP (domyslnie 10)
- trzyma je aktywne, zeby skaner (np. na ESP) widzial je jako "open"
- po nacisnieciu Ctrl+C zamyka wszystko
"""

import argparse
import random
import selectors
import socket


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Otworz losowe porty TCP do testow skanera."
    )
    parser.add_argument(
        "--count",
        type=int,
        default=10,
        help="Ile portow otworzyc (domyslnie: 10).",
    )
    parser.add_argument(
        "--host",
        default="0.0.0.0",
        help='Interfejs nasluchu, np. "0.0.0.0" albo "127.0.0.1".',
    )
    parser.add_argument(
        "--backlog",
        type=int,
        default=8,
        help="Rozmiar kolejki polaczen TCP (domyslnie: 8).",
    )
    parser.add_argument(
        "--low-max",
        type=int,
        default=4000,
        help="Gorna granica niskiego zakresu portow (domyslnie: 4000).",
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
            f"Nie udalo sie otworzyc {target_count} portow z zakresu "
            f"{start_port}-{end_port}."
        )

    return listeners


def main() -> None:
    args = parse_args()
    if args.count < 1:
        raise ValueError("--count musi byc >= 1")
    if args.low_max < 1024:
        raise ValueError("--low-max musi byc >= 1024")
    if args.low_max >= 65535:
        raise ValueError("--low-max musi byc < 65535")

    selector = selectors.DefaultSelector()
    listeners = []
    used_ports: set[int] = set()
    low_count = args.count // 2
    high_count = args.count - low_count

    try:
        low_listeners = bind_random_ports(
            host=args.host,
            backlog=args.backlog,
            start_port=1024,
            end_port=args.low_max,
            target_count=low_count,
            used_ports=used_ports,
        )
        high_listeners = bind_random_ports(
            host=args.host,
            backlog=args.backlog,
            start_port=args.low_max + 1,
            end_port=65535,
            target_count=high_count,
            used_ports=used_ports,
        )

        listeners.extend(low_listeners)
        listeners.extend(high_listeners)
        for sock in listeners:
            selector.register(sock, selectors.EVENT_READ)

        ports = sorted(sock.getsockname()[1] for sock in listeners)
        ports_csv = ",".join(str(port) for port in ports)
        print("Aktywne losowe porty TCP:")
        print("  " + ports_csv)
        print(f"Niski zakres (1024-{args.low_max}): {low_count}")
        print(f"Wysoki zakres ({args.low_max + 1}-65535): {high_count}")
        print("")
        print("Program czeka na polaczenia. Zatrzymaj: Ctrl+C")

        while True:
            events = selector.select(timeout=1.0)
            for key, _ in events:
                server_sock = key.fileobj
                conn, addr = server_sock.accept()
                port = server_sock.getsockname()[1]
                print(f"[port {port}] polaczenie od {addr[0]}:{addr[1]}")
                conn.close()
    except KeyboardInterrupt:
        print("\nZatrzymano.")
    finally:
        selector.close()
        for sock in listeners:
            sock.close()


if __name__ == "__main__":
    main()
