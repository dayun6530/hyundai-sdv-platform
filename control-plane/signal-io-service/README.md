# Signal I/O Service

## Component Purpose
Control Plane에서 Kill/Shadow/Deploy 제어 신호를 Signal I/F 또는 차량 SignalReceiver/Sender로 전달하는 인터페이스입니다.

## Current Implementation Mapping
- `Signal_I_F` REST API`n- `Signal_I_F` MQTT publish 흐름`n- `sdv-feature-flag-platform/src/Platform/CenterCommunication/SignalReceiver`, `SignalSender`와 연계

## Current Status
partially implemented

## Do Not Move Yet
MQTT protocol v1과 REST API가 이미 존재하므로 schema 변경은 위험합니다.

## Future Refactoring Plan
기존 v1 API를 유지하고 Control Plane용 client adapter를 추가합니다.
