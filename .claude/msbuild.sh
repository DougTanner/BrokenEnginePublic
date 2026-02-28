#!/usr/bin/env bash
# MSBuild lock wrapper — serializes MSBuild access per-solution using atomic mkdir locks.
# Usage: bash .claude/msbuild.sh <solution.sln|project.vcxproj> [msbuild args...]
#        bash .claude/msbuild.sh --files file1.cpp file2.cpp -- <project.vcxproj> [msbuild args...]

MSBUILD="/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe"
export MSYS_NO_PATHCONV=1

# Parse --files mode: collects .cpp paths, then deletes their .obj files before building
SELECTED_FILES=()
if [ "$1" = "--files" ]; then
    shift
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do
        SELECTED_FILES+=("$1")
        shift
    done
    [ "$1" = "--" ] && shift
fi

# Normalize path to Windows backslashes so $(SolutionDir) matches VS IDE
SLN_PATH="$(cygpath -w "$1")"
shift

# Strip .sln or .vcxproj extension for lock name
SLN_NAME="$(basename "$SLN_PATH")"
SLN_NAME="${SLN_NAME%.sln}"
SLN_NAME="${SLN_NAME%.vcxproj}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOCK_DIR="$SCRIPT_DIR/build-locks/${SLN_NAME}.lock"

acquire_lock() {
    local elapsed=0
    while ! mkdir "$LOCK_DIR" 2>/dev/null; do
        # Check for stale lock
        if [ -f "$LOCK_DIR/pid" ]; then
            local holder_pid
            holder_pid=$(cat "$LOCK_DIR/pid" 2>/dev/null)
            if [ -n "$holder_pid" ] && ! kill -0 "$holder_pid" 2>/dev/null; then
                echo "[msbuild.sh] Stale lock (PID $holder_pid dead), removing"
                rm -rf "$LOCK_DIR"
                continue
            fi
        else
            # Lock dir exists but no pid file — stale
            echo "[msbuild.sh] Stale lock (no pid file), removing"
            rm -rf "$LOCK_DIR"
            continue
        fi

        if [ "$elapsed" -ge 660 ]; then
            echo "[msbuild.sh] Timed out waiting for lock on $SLN_NAME after ${elapsed}s"
            exit 1
        fi

        echo "[msbuild.sh] Waiting for lock on $SLN_NAME (held by PID $holder_pid, ${elapsed}s elapsed)..."
        sleep 5
        elapsed=$((elapsed + 5))
    done
    echo $$ > "$LOCK_DIR/pid"
}

release_lock() {
    rm -rf "$LOCK_DIR"
}

trap release_lock EXIT

acquire_lock

if [ ${#SELECTED_FILES[@]} -gt 0 ]; then
    # Selective compilation mode: delete .obj files for specified .cpp files, then build normally.
    # MSBuild detects the missing .obj and recompiles only those files.

    # Extract Configuration from msbuild args (e.g. /p:Configuration=Debug -> Debug)
    CONFIG=""
    for arg in "$@"; do
        case "$arg" in
            /p:Configuration=*) CONFIG="${arg#/p:Configuration=}" ;;
        esac
    done
    if [ -z "$CONFIG" ]; then
        echo "[msbuild.sh] ERROR: --files mode requires /p:Configuration=<config>"
        exit 1
    fi

    # Derive IntDir: <vcxproj-dir>/Build/<ProjectName>/<Configuration>/
    PROJ_DIR="$(cygpath -u "$(dirname "$SLN_PATH")")"
    INT_DIR="$PROJ_DIR/Build/$SLN_NAME/$CONFIG"

    for src in "${SELECTED_FILES[@]}"; do
        OBJ_NAME="$(basename "$src" .cpp).obj"
        OBJ_PATH="$INT_DIR/$OBJ_NAME"
        if [ -f "$OBJ_PATH" ]; then
            rm -f "$OBJ_PATH"
            echo "[msbuild.sh] Deleted $OBJ_NAME"
        fi
    done

    echo "[msbuild.sh] Building $SLN_NAME (${#SELECTED_FILES[@]} file(s) invalidated)..."
    "$MSBUILD" "$SLN_PATH" "$@"
else
    echo "[msbuild.sh] Building $SLN_NAME..."
    "$MSBUILD" "$SLN_PATH" "$@"
fi
