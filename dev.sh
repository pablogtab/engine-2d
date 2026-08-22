#!/bin/bash
# ==============================================================================
#  C++ Game Engine - Live Watch & Auto-Reload Dev Script
#  Monitors src/ and CMakeLists.txt for changes, rebuilds & restarts the engine.
# ==============================================================================

set -e

# Ensure Homebrew paths are available
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

LOCKFILE="/tmp/engine2d_dev.lock"
if [ -f "$LOCKFILE" ]; then
    OLD_PID=$(cat "$LOCKFILE" 2>/dev/null || true)
    if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null && [ "$OLD_PID" != "$$" ]; then
        echo "[DEV] Found another active dev.sh session (PID: $OLD_PID). Closing it..."
        kill "$OLD_PID" 2>/dev/null || true
        sleep 0.2
    fi
fi
echo "$$" > "$LOCKFILE"

BUILD_DIR="build"
EXE="./build/Engine2D"
PROJECT_DIR="${ENGINE2D_PROJECT:-../projecto}"
if [ ! -f "$PROJECT_DIR/project.engine2d" ]; then
    echo "[DEV] Invalid Engine2D project: $PROJECT_DIR"
    echo "[DEV] Set ENGINE2D_PROJECT or create ../projecto/project.engine2d"
    exit 1
fi
PROJECT_DIR="$(cd "$PROJECT_DIR" && pwd)"

# Kill any lingering orphaned Engine2D processes before starting
pkill -f "build/Engine2D" 2>/dev/null || true

# Ensure build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "[DEV] Initializing CMake build environment..."
    cmake -B "$BUILD_DIR" -S . -DENGINE_PROJECT_DIR="$PROJECT_DIR"
fi

echo "[DEV] Initial build..."
cmake --build "$BUILD_DIR"

PID=""

stop_app() {
    if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
        echo "[DEV] Stopping previous engine process (PID: $PID)..."
        kill "$PID" 2>/dev/null || true
        sleep 0.1
        if kill -0 "$PID" 2>/dev/null; then
            kill -9 "$PID" 2>/dev/null || true
        fi
        wait "$PID" 2>/dev/null || true
    fi
    pkill -f "build/Engine2D" 2>/dev/null || true
}

start_app() {
    stop_app
    echo "[DEV] Launching Engine2D..."
    $EXE --project "$PROJECT_DIR" &
    PID=$!
}

# Cleanup on exit or interrupt (Ctrl+C)
trap "stop_app; rm -f '$LOCKFILE'; exit 0" INT TERM EXIT

start_app

echo "[DEV] Watching 'src/' for changes... (Press Ctrl+C to stop)"

# Function to compute MD5 checksum of source files modification times
get_checksum() {
    find src CMakeLists.txt -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "CMakeLists.txt" \) -exec stat -f "%m %N" {} + 2>/dev/null | md5
}

LAST_CHECKSUM=$(get_checksum)

while true; do
    sleep 0.5
    CURRENT_CHECKSUM=$(get_checksum)
    if [ "$CURRENT_CHECKSUM" != "$LAST_CHECKSUM" ]; then
        echo -e "\n[DEV] 🔄 File change detected! Rebuilding..."
        LAST_CHECKSUM=$CURRENT_CHECKSUM
        
        if cmake --build "$BUILD_DIR"; then
            echo "[DEV] ✅ Build successful! Relaunching engine..."
            start_app
        else
            echo "[DEV] ❌ Build failed! Fix compiler errors and save file to retry."
        fi
    fi
done
