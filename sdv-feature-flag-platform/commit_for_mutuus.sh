#!/usr/bin/env bash
set -euo pipefail

git add .
git commit -m "$(cat <<'EOF'
Update platform interfaces and align SQA unit tests/report automation.

Applies interface return-type changes across runtime and tests, regenerates verification reports, and keeps unit reporting consistent with latest SQA checks.
EOF
)"
