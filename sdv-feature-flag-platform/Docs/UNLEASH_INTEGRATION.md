# Unleash integration (C++ SDK) — SDV Feature Flag demo

This project can sync `FeatureFlagOn/Off` control events from an [Unleash] feature flag server using the bundled C++ SDK.

## What this does

- `FeatureEvaluator` periodically queries Unleash flags:
  - `NavigationFeature`
  - `AdasFeature`
  - `EvEnergyFeature`
- When a flag changes:
  - enabled → sends `EventId::FeatureFlagOn` to that feature logical name
  - disabled → sends `EventId::FeatureFlagOff`
- Feature processes stay running and only emit domain output when enabled (demo-friendly behavior).

## Start Unleash server (Docker Desktop)

From `팀프로젝트/`:

```bash
docker compose up -d
```

Unleash UI: `http://localhost:4242`  
Default login (first run): `admin` / `admin`

## Create a Frontend API token

In the Unleash UI:
- Go to **API Access** → **API tokens**
- Create a **Frontend** token
- Copy the token value

## Build the platform with Unleash enabled

Unleash SDK requires `libcurl` and `nlohmann_json` as CMake packages.
Enable integration at configure time:

```bash
cmake -S . -B build -DSDV_ENABLE_UNLEASH=ON
cmake --build build -j
```

## Run with Unleash env vars

Set these environment variables for the `FeatureEvaluator` process:

- `UNLEASH_URL`: frontend endpoint, usually `http://localhost:4242/api/frontend`
- `UNLEASH_API_KEY`: your **Frontend** token
- `UNLEASH_APP_NAME`: optional, default `sdv-platform`

Example:

```bash
export UNLEASH_URL="http://localhost:4242/api/frontend"
export UNLEASH_API_KEY="default:development.unleash-insecure-frontend-api-token"
export UNLEASH_APP_NAME="sdv-platform"
```

Then start the platform (via `run_sdv.bat` or WSL).

[Unleash]: https://www.getunleash.io/

