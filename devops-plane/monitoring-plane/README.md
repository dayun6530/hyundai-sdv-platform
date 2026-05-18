# Monitoring Plane

## Component Purpose
Signal I/F, 운용 데이터 수집, 운용 데이터 분석, telemetry/history/log 흐름을 담당합니다.

## Current Implementation Mapping
- `Signal_I_F/`n- `DataCollector.cpp`n- `deploy/history/fleet_history.jsonl`

## Current Status
partially implemented

## Do Not Move Yet
운용 데이터가 차량 코드, Signal_I_F, deploy files에 분산되어 있습니다.

## Future Refactoring Plan
collector/analyzer 경계를 먼저 문서화하고 Control Plane Data Monitoring Service와 API를 맞춥니다.
