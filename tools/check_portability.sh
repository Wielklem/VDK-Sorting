#!/usr/bin/env bash
# Rule: all OS-specific code lives in common/platform/ (M15).
# Fails on OS headers or hardcoded system paths anywhere else.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"

pattern='#[[:space:]]*include[[:space:]]*[<"](unistd\.h|windows\.h|winsock2?\.h|pthread\.h|dlfcn\.h|fcntl\.h|signal\.h|csignal|sys/[^>"]+)[>"]|"/(var|etc|opt|usr|home|tmp)/|"[a-z]:\\\\'

if git ls-files -z -- '*.cpp' '*.hpp' '*.h' '*.cc' \
     ':!:common/platform/**' ':!:firmware/**' \
   | xargs -0 -r grep -nEi "$pattern"; then
    echo "ERROR: OS-specific code outside common/platform/ (see lines above)."
    exit 1
fi
echo "Portability check passed."
