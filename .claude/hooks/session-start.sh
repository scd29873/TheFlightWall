#!/bin/bash
#
# SessionStart hook for Claude Code on the web: puts the toolchain in place so
# a cloud session can build the firmware, run the host tests and run the
# server's tests from its first command, instead of spending its first minutes
# installing them.
#
# - Cloud only. Anywhere else it exits at once, so a local Claude Code session
#   keeps whatever you installed yourself.
# - Synchronous. The session starts when this finishes.
# - Idempotent. Every step is a no-op when its work is already done, and the
#   container is saved after the hook runs, so the big download (~2.4 GB of
#   PlatformIO packages) is paid by the first session, not every one.
# - Never fatal. A step that fails (a registry down, a network policy change)
#   is reported, and the session still starts with everything else in place.
#
# What it installs:
#   PlatformIO, in a venv at ~/.platformio/penv (where PlatformIO's own
#     installer puts it), and put first on PATH for the session -- so the
#     venv's python3, which has the tools/ packages below, is the one found;
#   every package the envs in firmware/platformio.ini declare -- the ESP32 and
#     ESP32-S3 toolchains, the Arduino core, the libraries, the debugger;
#   Pillow and pyserial for tools/ (tools/requirements.txt), in the same venv;
#   server/'s npm dependencies, for `npm test` and `npm run typecheck`.
# Progress goes to ~/.cache/flightwall-session-start.log; stdout, which the
# session sees, gets one summary line.
set -uo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

ROOT="${CLAUDE_PROJECT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
VENV="$HOME/.platformio/penv"
LOG="$HOME/.cache/flightwall-session-start.log"
mkdir -p "$(dirname "$LOG")"
: >"$LOG"

failed=()
step() { # step <name> <command...>: run it, log it, remember a failure
  local name="$1"
  shift
  echo "== $name" >>"$LOG"
  if ! "$@" >>"$LOG" 2>&1; then
    failed+=("$name")
    echo "== $name FAILED" >>"$LOG"
  fi
}

# `pio pkg install` and `npm install` can each rewrite a tracked file:
# PlatformIO's config writer strips every comment from platformio.ini when it
# saves a dependency, and npm may rewrite package-lock.json. Neither should
# happen here, so each file is copied first and put back if it changed. The
# copy is the file as it was, uncommitted edits and all, so a resumed session's
# work is kept.
guarded() { # guarded <file> <command...>
  local file="$1"
  shift
  if [ ! -f "$file" ]; then
    "$@"
    return $?
  fi
  local saved
  saved="$(mktemp)"
  cp "$file" "$saved"
  "$@"
  local rc=$?
  if ! cmp -s "$file" "$saved"; then
    cp "$saved" "$file"
    echo "restored $file, which the step above rewrote" >>"$LOG"
  fi
  rm -f "$saved"
  return $rc
}

install_platformio() {
  [ -x "$VENV/bin/pio" ] && return 0
  python3 -m venv "$VENV" && "$VENV/bin/pip" install --quiet "platformio>=6.1,<7"
}

install_tool_python_deps() {
  "$VENV/bin/pip" install --quiet -r "$ROOT/tools/requirements.txt"
}

install_platformio_packages() {
  # No -e: every env in platformio.ini. No package names: nothing to save.
  # Then drop PlatformIO's download cache: ~600 MB of archives it has already
  # unpacked into ~/.platformio/packages, which would otherwise ride along in
  # every saved container.
  (cd "$ROOT/firmware" && "$VENV/bin/pio" pkg install --no-save &&
    "$VENV/bin/pio" system prune --cache --force)
}

install_server_deps() {
  (cd "$ROOT/server" && npm install --no-audit --no-fund --loglevel=error)
}

step "PlatformIO" install_platformio
if [ -x "$VENV/bin/pio" ]; then
  step "tools/requirements.txt" install_tool_python_deps
  step "PlatformIO packages" guarded "$ROOT/firmware/platformio.ini" install_platformio_packages
else
  failed+=("tools/requirements.txt (needs PlatformIO's venv)" "PlatformIO packages (needs PlatformIO)")
fi
step "server npm packages" guarded "$ROOT/server/package-lock.json" install_server_deps

if [ -n "${CLAUDE_ENV_FILE:-}" ] && [ -x "$VENV/bin/pio" ]; then
  echo "export PATH=\"$VENV/bin:\$PATH\"" >>"$CLAUDE_ENV_FILE"
fi

if [ "${#failed[@]}" -eq 0 ]; then
  echo "Session setup: PlatformIO ($("$VENV/bin/pio" --version 2>/dev/null | sed 's/.*version //')) and every firmware env's packages installed, pio on PATH; tools/ Python deps and server/ npm deps installed. Log: $LOG"
else
  printf 'Session setup INCOMPLETE -- failed: %s. Everything else is installed. Details: %s\n' \
    "$(IFS=';'; echo "${failed[*]}")" "$LOG"
fi
exit 0
