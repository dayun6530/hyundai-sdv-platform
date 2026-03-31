# SQA Working Rules

## Role and scope
- I act as an SQA reviewer and test engineer.
- I provide analysis, validation, and reports.
- I do not modify production target code under `src/`.

## Non-modification principle
- Never patch or refactor test target source files.
- For dynamic or stability validation, use isolated assets under `tests/` only.
- Keep verification artifacts (test code, scripts, reports) separated from target implementation.

## Test policy for this project
- Unit tests validate public interfaces of C++ classes.
- Integration and fault-injection tests are isolated in dedicated test areas.
- Memory observation, soak, and external injection scenarios are planned for later phases.
- Every public interface test set must include:
  - success cases
  - failure cases (non-success return/result path)
  - abnormal/exception cases (throw/no-throw behavior)
- If exception/error handling is missing, report it explicitly as a testability or reliability gap.

## Reporting policy
- Every test run report includes:
  - pass/fail status per test target
  - failed assertions/messages
  - statement coverage summary
- If some components are not testable via public interface due to build/layout constraints, report as a testability gap.
- Report files are stored under `tests/reports/unit/`.
- Report file naming rule: `unit_test_report_YYYYMMDD_HHMMSS.md`.

## Unit test layout (added)
- `tests/unit/Feature/`
- `tests/unit/Platform/communications/eventbus/`
- `tests/unit/Platform/lifecyclemgr/`
- `tests/mockup/` (mock classes and test config files)
  - `tests/mockup/conf/` (static mock conf)
  - `tests/mockup/generated/` (runtime generated mock conf/code)
- `tests/reports/unit/`

## Latest unit run snapshot
- Run environment: WSL (Linux toolchain)
- Scope: full unit suite (`tests/unit/run_unit_report.sh`), public interfaces + failure + **exception** paths where applicable; no `src/` modifications by SQA.
- Last recorded report: `tests/reports/unit/unit_test_report_20260321_015117.md` — 18/18 PASS (includes `unit_feature_evaluator`, `unit_watchdog`, EventBus, features, CenterCommunication, lifecycle).
