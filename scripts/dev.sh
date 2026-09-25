#!/usr/bin/env bash
# Run any command inside the nids-dev toolchain container with the repo mounted
# at /work. Usage (from the repo root, Git Bash on Windows or any Linux shell):
#
#   scripts/dev.sh scripts/ci.sh gcc
#   scripts/dev.sh bash
#   scripts/dev.sh ./build/gcc/bench results.json --mb 64
set -euo pipefail

# On Git Bash `pwd -W` gives a Windows path Docker Desktop accepts; elsewhere use pwd.
HOSTPATH="$(pwd -W 2>/dev/null || pwd)"
export MSYS_NO_PATHCONV=1
exec docker run --rm -it -v "${HOSTPATH}:/work" -w /work nids-dev "$@"
