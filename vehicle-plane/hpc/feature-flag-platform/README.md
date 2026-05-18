# Feature Flag Platform

## Component Purpose
차량 내부 Feature lifecycle, EventBus, Feature evaluation, Signal 송수신, DataCollector를 담당하는 논리 컴포넌트입니다.

## Current Implementation Mapping
- `sdv-feature-flag-platform/src/Platform/LifeCycleManager/`n- `sdv-feature-flag-platform/src/Platform/FeatureCommunication/EventBus/`n- `sdv-feature-flag-platform/src/Platform/CenterCommunication/`

## Current Status
implemented

## Do Not Move Yet
CMake target, unit test, 런타임 실행 파일이 현재 경로와 include path에 강하게 연결되어 있습니다.

## Future Refactoring Plan
먼저 lifecycle-manager/event-bus/feature-evaluator/signal/data-collector 역할 문서를 분리하고 이후 빌드 경로를 조정합니다.
