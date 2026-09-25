#!/usr/bin/env bash
# Compatibility entry point: bootstrap/build.sh.

set -euo pipefail

exec bash "$(dirname "$0")/bootstrap/build.sh" "$@"
