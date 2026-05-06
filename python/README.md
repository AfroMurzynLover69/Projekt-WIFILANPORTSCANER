# ESP Port Tester

`open_dummy_ports.py` opens random TCP ports so the ESP scanner can detect them as open.

## Quick Start

`run_tester.sh` automatically:

- creates `python/.venv` on the first run
- activates the virtual environment
- runs the port tester

```bash
./python/run_tester.sh
```

With custom parameters:

```bash
./python/run_tester.sh --count 12 --host 0.0.0.0
```

## Direct Run

```bash
python3 python/open_dummy_ports.py
```

## Options

```bash
python3 python/open_dummy_ports.py --count 10 --host 0.0.0.0
```

- `--count` number of ports to open
- `--host` listen interface (`0.0.0.0` for the whole LAN)
- `--backlog` TCP connection backlog size
- `--min-port` lower port range bound
- `--max-port` upper port range bound

The program runs until `Ctrl+C` and accepts TCP connections without an extra application protocol.
