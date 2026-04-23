#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="${SCRIPT_DIR}/.venv"
PYTHON_BIN="${PYTHON_BIN:-python3}"

if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
  echo "Blad: nie znaleziono ${PYTHON_BIN} w PATH."
  exit 1
fi

if [ ! -d "${VENV_DIR}" ]; then
  echo "Tworze virtualenv w ${VENV_DIR}..."
  "${PYTHON_BIN}" -m venv "${VENV_DIR}"
fi

# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"

# Domyslne parametry pod testy ESP po LAN.
if [ "$#" -eq 0 ]; then
  set -- --count 10 --host 0.0.0.0
fi

exec python "${SCRIPT_DIR}/open_dummy_ports.py" "$@"
