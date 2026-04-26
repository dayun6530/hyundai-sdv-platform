#!/usr/bin/env bash
set -euo pipefail

git ls-files -z '*.hpp' '*.cpp' '*.h' '*.c' '*.cc' '*.hh' | \
  xargs -0 clang-format -style=file -i