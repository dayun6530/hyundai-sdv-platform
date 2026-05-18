# Operation Data Analyzer

## Component Purpose
Shadow validation, Fleet history 분석, 배포 결과 집계, rollback/abort 판단을 담당할 분석 컴포넌트입니다.

## Current Implementation Mapping
- 현재 명확한 독립 analyzer는 없음`n- 향후 shadow validation, fleet history analysis, deployment result aggregation 기능으로 확장

## Current Status
planned

## Do Not Move Yet
분석 결과를 소비하는 API/UI contract가 아직 없습니다.

## Future Refactoring Plan
먼저 분석 output schema를 정의하고 Data Monitoring Service에 read-only API로 연결합니다.
