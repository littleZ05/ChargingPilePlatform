#!/usr/bin/env bash
# Explicit reset of build-demo/chargingpile.db, never the normal application DB.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec env -u LD_PRELOAD python3 "$REPO/tools/run_demo.py" --seed-only --reset "$@"
