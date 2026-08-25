#!/bin/bash
# ==============================================================================
#  C++ Game Engine - Live Watch & Auto-Reload Dev Script
#  Monitors src/ and CMakeLists.txt for changes, rebuilds & restarts the engine.
# ==============================================================================

set -e

# Ensure Homebrew paths are available
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

wait_for_process_exit() {
    local process_pid="$1"
    local attempt
    local process_state
    for attempt in {1..40}; do
        if ! kill -0 "$process_pid" 2>/dev/null; then
            return 0
        fi
        process_state=$(ps -o state= -p "$process_pid" 2>/dev/null | tr -d '[:space:]')
        if [ "$process_state" = "Z" ]; then
            return 0
        fi
        sleep 0.05
    done
    return 1
}

LOCKFILE="/tmp/engine2d_dev.lock"
if [ -f "$LOCKFILE" ]; then
    OLD_PID=$(cat "$LOCKFILE" 2>/dev/null || true)
    if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null && [ "$OLD_PID" != "$$" ]; then
        echo "[DEV] Found another active dev.sh session (PID: $OLD_PID). Closing it..."
        # Bash defers TERM traps while a foreground build is running. Stop its
        # direct child first so the watcher can run cleanup instead of leaving
        # an orphaned build or editor process behind.
        pkill -TERM -P "$OLD_PID" 2>/dev/null || true
        kill "$OLD_PID" 2>/dev/null || true
        if ! wait_for_process_exit "$OLD_PID"; then
            echo "[DEV] Previous watcher did not stop after 2s; forcing shutdown..."
            pkill -KILL -P "$OLD_PID" 2>/dev/null || true
            kill -9 "$OLD_PID" 2>/dev/null || true
            if ! wait_for_process_exit "$OLD_PID"; then
                echo "[DEV] Could not stop previous dev.sh session. Aborting."
                exit 1
            fi
        fi
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
        if ! wait_for_process_exit "$PID"; then
            echo "[DEV] Engine did not stop after 2s; forcing shutdown..."
            kill -9 "$PID" 2>/dev/null || true
        fi
        wait "$PID" 2>/dev/null || true
    fi
    PID=""
}

start_app() {
    stop_app
    echo "[DEV] Launching Engine2D..."
    "$EXE" --project "$PROJECT_DIR" &
    PID=$!

    if [ "$(uname -s)" = "Darwin" ]; then
        # Modern macOS may reject focus requests made by a newly launched CLI
        # process even when SDL reports its window as focused. System Events
        # can hand focus to the exact PID once Cocoa has registered it.
        local attempt
        local frontmost_pid
        for attempt in {1..100}; do
            frontmost_pid=$(osascript \
                -e "tell application \"System Events\" to set frontmost of first application process whose unix id is $PID to true" \
                -e 'tell application "System Events" to get unix id of first application process whose frontmost is true' \
                2>/dev/null || true)
            if [ "$frontmost_pid" = "$PID" ]; then
                break
            fi
            sleep 0.05
        done
        if [ "$frontmost_pid" != "$PID" ]; then
            echo "[DEV] Warning: macOS did not grant foreground focus; click the editor window to retry."
        else
            echo "[DEV] macOS keyboard focus assigned to Engine2D (PID: $PID)."
        fi
    fi
}

# Cleanup on exit or interrupt (Ctrl+C). Only remove a lock still owned by this
# watcher; a replacement dev.sh may already have installed its own lock.
cleanup() {
    local exit_code=$?
    trap - INT TERM EXIT
    stop_app
    if [ -f "$LOCKFILE" ] && [ "$(cat "$LOCKFILE" 2>/dev/null)" = "$$" ]; then
        rm -f "$LOCKFILE"
    fi
    exit "$exit_code"
}
trap cleanup INT TERM EXIT

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
