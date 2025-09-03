#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./scripts/pgo_ninja.sh [build-dir] [jobs] [pm_measure args...]
# Examples:
#   ./scripts/pgo_ninja.sh                 # uses build-pgo and all CPUs, no pm_measure args
#   ./scripts/pgo_ninja.sh mybuild 4 -- --capture 0
#
BUILD_DIR="${1:-build-pgo}"
JOBS="${2:-$(nproc)}"
# pm_measure args start at position 3 (if any)
PM_ARGS=("${@:3}")

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "==> Configuring instrumented build (Ninja) in: $PWD"
cmake -G Ninja -DCMAKE_BUILD_TYPE=PGO_Instrument "$PROJECT_ROOT"

echo "==> Building instrumented binaries (ninja -j $JOBS)"
ninja -j "$JOBS"

echo "==> Running pm_measure to collect profile data"
if [ -x ./pm_measure ]; then
  if [ ${#PM_ARGS[@]} -eq 0 ]; then
    echo "Running './pm_measure' (no extra args). Exit when workload complete or Ctrl-C."
    ./pm_measure
  else
    echo "Running './pm_measure ${PM_ARGS[*]}'"
    ./pm_measure "${PM_ARGS[@]}"
  fi
else
  echo "ERROR: ./pm_measure not found or not executable in $PWD"
  exit 2
fi

echo "==> Re-configuring optimized build (PGO_Optimize)"
cmake -G Ninja -DCMAKE_BUILD_TYPE=PGO_Optimize "$PROJECT_ROOT"

echo "==> Building optimized binaries (ninja -j $JOBS)"
ninja -j "$JOBS"

echo "==> PGO flow complete. Optimized binaries are in: $PWD"

