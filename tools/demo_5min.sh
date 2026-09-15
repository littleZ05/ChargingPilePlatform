#!/usr/bin/env bash
# Defaults to preserving the dedicated demo DB. Explicit --reset creates a backup.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec env -u LD_PRELOAD python3 "$REPO/tools/run_demo.py" "$@"
