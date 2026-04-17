#!/usr/bin/env bash
# MSBuild lock wrapper — serializes MSBuild access per-solution using PID lock files.
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

# Lock granularity is per-basename: two same-named projects in different directories share one lock (intentional).
SLN_NAME="$(basename "$SLN_PATH")"
SLN_NAME="${SLN_NAME%.sln}"
SLN_NAME="${SLN_NAME%.vcxproj}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOCK_FILE="$SCRIPT_DIR/build-locks/${SLN_NAME}.pid"

# Owner-checked release: only delete the lock if we created it. Safe to call from EXIT trap on any path.
release_lock() {
    if [ -f "$LOCK_FILE" ] && [ "$(cat "$LOCK_FILE" 2>/dev/null)" = "$$" ]; then
        rm -f "$LOCK_FILE"
    fi
}

trap release_lock EXIT

acquire_lock() {
    mkdir -p "$(dirname "$LOCK_FILE")" 2>/dev/null
    local elapsed=0
    while true; do
        # Atomic create-if-not-exists via O_CREAT|O_EXCL semantics that noclobber gives `>`.
        # Subshell prevents the option from leaking into the parent shell.
        if (set -o noclobber; echo $$ > "$LOCK_FILE") 2>/dev/null; then
            return 0
        fi

        local holder_pid
        holder_pid=$(cat "$LOCK_FILE" 2>/dev/null)
        if [ -n "$holder_pid" ] && kill -0 "$holder_pid" 2>/dev/null; then
            if [ "$elapsed" -ge 660 ]; then
                echo "[msbuild.sh] Timed out waiting for lock on $SLN_NAME after ${elapsed}s"
                exit 1
            fi
            echo "[msbuild.sh] Waiting for lock on $SLN_NAME (held by PID $holder_pid, ${elapsed}s elapsed)..."
            sleep 5
            elapsed=$((elapsed + 5))
            continue
        fi

        echo "[msbuild.sh] Removing stale lock (PID ${holder_pid:-empty})"
        rm -f "$LOCK_FILE"
        # Loop back and retry the atomic create — if another process beats us to it, we just wait.
    done
}

acquire_lock

if [ ${#SELECTED_FILES[@]} -gt 0 ]; then
    # Selective compilation mode: delete .obj files for specified .cpp files, then build normally.
    # MSBuild detects the missing .obj and recompiles only those files.

    # Extract Configuration from msbuild args. Handles all four MSBuild property syntaxes.
    CONFIG=""
    for arg in "$@"; do
        case "$arg" in
            /p:Configuration=*|-p:Configuration=*|/property:Configuration=*|-property:Configuration=*)
                CONFIG="${arg#*=}" ;;
        esac
    done
    if [ -z "$CONFIG" ]; then
        echo "[msbuild.sh] ERROR: --files mode requires Configuration=<config> property"
        exit 1
    fi

    # Derive IntDir: <vcxproj-dir>/Build/<ProjectName>/<Configuration>/
    PROJ_DIR="$(cygpath -u "$(dirname "$SLN_PATH")")"
    INT_DIR="$PROJ_DIR/Build/$SLN_NAME/$CONFIG"

    for src in "${SELECTED_FILES[@]}"; do
        case "$src" in
            *.cpp) ;;
            *)
                echo "[msbuild.sh] ERROR: --files only supports .cpp inputs (got: $src)"
                exit 1 ;;
        esac
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
