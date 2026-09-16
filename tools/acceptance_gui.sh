#!/usr/bin/env bash
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec env -u LD_PRELOAD /usr/bin/python3 "$REPO/tools/acceptance_gui.py" "$@"
