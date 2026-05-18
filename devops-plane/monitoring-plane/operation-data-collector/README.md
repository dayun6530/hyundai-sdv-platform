# Operation Data Collector

## Component Purpose
차량/배포/운용 데이터 원천을 수집하는 컴포넌트입니다.

## Current Implementation Mapping
- `sdv-feature-flag-platform/src/Platform/CenterCommunication/src/DataCollector.cpp`n- `sdv-ota-manager/platform/deploy/history/fleet_history.jsonl`n- `Signal_I_F` telemetry handler if present

## Current Status
partially implemented

## Do Not Move Yet
수집 원천이 vehicle runtime과 deploy file 양쪽에 있어 즉시 통합하면 흐름이 깨질 수 있습니다.

## Future Refactoring Plan
Vehicle-side collector와 Control/Monitoring-side ingestion을 구분합니다.
