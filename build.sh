#!/usr/bin/env bash
# Compatibility entry point for the checked-in C seed bootstrap.

set -euo pipefail

exec bash "$(dirname "$0")/bootstrap/build.sh" "$@"
