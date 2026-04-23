# Tester portow dla ESP

Skrypt `open_dummy_ports.py` otwiera losowe porty TCP (domyslnie 10), zeby skaner na ESP mogl wykryc je jako otwarte.
Domyslnie polowa portow jest z zakresu `1024-4000`, a polowa z zakresu `4001-65535`.

## Szybki start (sh)

Skrypt `run_tester.sh` automatycznie:
- tworzy `venv` w `python/.venv` (tylko przy pierwszym uruchomieniu),
- aktywuje srodowisko,
- uruchamia tester portow.

```bash
./python/run_tester.sh
```

Z wlasnymi parametrami:

```bash
./python/run_tester.sh --count 12 --host 0.0.0.0
```

## Uruchomienie

```bash
python3 python/open_dummy_ports.py
```

## Opcje

```bash
python3 python/open_dummy_ports.py --count 10 --host 0.0.0.0
```

- `--count` ile portow otworzyc
- `--host` interfejs nasluchu (`0.0.0.0` dla calej sieci LAN)
- `--backlog` wielkosc kolejki polaczen TCP
- `--low-max` granica "niskiego" zakresu (domyslnie `4000`)

Program dziala do `Ctrl+C` i przyjmuje polaczenia bez dodatkowej uslugi aplikacyjnej.
