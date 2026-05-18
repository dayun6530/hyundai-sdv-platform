# Static Analysis

## Component Purpose
정적 분석, lint, compile warning 관리를 담당하는 계획 컴포넌트입니다.

## Current Implementation Mapping
- 현재 명확한 독립 static analysis pipeline은 없음`n- CMake compile options 일부

## Current Status
planned

## Do Not Move Yet
정적 분석 도구가 아직 고정되지 않았으므로 기존 build를 건드리지 않습니다.

## Future Refactoring Plan
clang-tidy/cppcheck/eslint 등 후보를 문서화하고 CI에 선택적으로 연결합니다.
