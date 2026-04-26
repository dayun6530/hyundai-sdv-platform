set -euo pipefail

TEAM_DIR="${1:?TEAM_DIR required}"
cd "${TEAM_DIR}"
docker compose -p sdv up -d
