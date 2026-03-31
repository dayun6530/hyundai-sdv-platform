#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./tests/unit/run_unit_report.sh [build_dir]
# Example:
#   ./tests/unit/run_unit_report.sh build-sqa

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-build-sqa}"
ABS_BUILD_DIR="${ROOT_DIR}/${BUILD_DIR}"
UNIT_BIN_DIR="${ABS_BUILD_DIR}/tests/unit"
REPORT_DIR="${ROOT_DIR}/tests/reports/unit"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
REPORT_FILE="${REPORT_DIR}/unit_test_report_${TIMESTAMP}.md"
RAW_COV_FILE="${ABS_BUILD_DIR}/coverage_src_raw_${TIMESTAMP}.txt"

PUBLIC_HEADERS=(
  "${ROOT_DIR}/src/Platform/FeatureCommunication/EventBus/include/FeatureCommunication/EventBus/Event.h"
  "${ROOT_DIR}/src/Platform/FeatureCommunication/EventBus/include/FeatureCommunication/EventBus/EventQueue.h"
  "${ROOT_DIR}/src/Platform/FeatureCommunication/EventBus/include/FeatureCommunication/EventBus/EventBusInterface.h"
  "${ROOT_DIR}/src/Platform/FeatureCommunication/EventBus/include/FeatureCommunication/EventBus/EventBusProxy.h"
  "${ROOT_DIR}/src/Platform/FeatureCommunication/EventBus/include/FeatureCommunication/EventBus/EventBusHost.h"
  "${ROOT_DIR}/src/Platform/LifeCycleManager/include/LifeCycleManager/Feature.h"
  "${ROOT_DIR}/src/Platform/LifeCycleManager/include/LifeCycleManager/Flag.h"
  "${ROOT_DIR}/src/Platform/LifeCycleManager/include/LifeCycleManager/LogData.h"
  "${ROOT_DIR}/src/Platform/LifeCycleManager/include/LifeCycleManager/LifeCycleMgr.h"
  "${ROOT_DIR}/src/Platform/LifeCycleManager/include/LifeCycleManager/FeatureEvaluator.h"
  "${ROOT_DIR}/src/Platform/LifeCycleManager/include/LifeCycleManager/WatchDog.h"
  "${ROOT_DIR}/src/Platform/CenterCommunication/include/CenterCommunication/Signal.h"
  "${ROOT_DIR}/src/Platform/CenterCommunication/include/CenterCommunication/SignalReceiver.h"
  "${ROOT_DIR}/src/Platform/CenterCommunication/include/CenterCommunication/SignalSender.h"
  "${ROOT_DIR}/src/Platform/CenterCommunication/include/CenterCommunication/DataCollector.h"
  "${ROOT_DIR}/src/Feature/include/Feature/AdasFeature.h"
  "${ROOT_DIR}/src/Feature/include/Feature/EvEnergyFeature.h"
  "${ROOT_DIR}/src/Feature/include/Feature/NavigationFeature.h"
)

TEST_BINS=(
  unit_event
  unit_event_queue
  unit_event_bus_interface
  unit_event_bus_proxy
  unit_event_bus_host
  unit_feature_base
  unit_flag
  unit_log_data
  unit_lifecycle_mgr
  unit_feature_evaluator
  unit_watchdog
  unit_signal
  unit_signal_receiver
  unit_signal_sender
  unit_data_collector
  unit_adas_feature
  unit_ev_energy_feature
  unit_navigation_feature
)

mkdir -p "${REPORT_DIR}"

cd "${ROOT_DIR}"
cmake -S . -B "${ABS_BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "${ABS_BUILD_DIR}" \
  --target "${TEST_BINS[@]}" -j >/dev/null

pass_count=0
fail_count=0
declare -a failed_tests

for test_bin in "${TEST_BINS[@]}"; do
  if (cd "${ABS_BUILD_DIR}" && "${UNIT_BIN_DIR}/${test_bin}" >/dev/null 2>&1); then
    pass_count=$((pass_count + 1))
  else
    fail_count=$((fail_count + 1))
    failed_tests+=("${test_bin}")
  fi
done

cd "${ABS_BUILD_DIR}"
gcov src/Platform/FeatureCommunication/EventBus/CMakeFiles/EventBus.dir/src/Event.cpp.o >"${RAW_COV_FILE}" 2>/dev/null || true
gcov src/Platform/FeatureCommunication/EventBus/CMakeFiles/EventBus.dir/src/EventQueue.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov src/Platform/FeatureCommunication/EventBus/CMakeFiles/EventBus.dir/src/EventBusProxy.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov src/Platform/LifeCycleManager/CMakeFiles/LifeCycleManagerLib.dir/src/Feature.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov src/Platform/LifeCycleManager/CMakeFiles/LifeCycleManagerLib.dir/src/Flag.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov src/Platform/LifeCycleManager/CMakeFiles/LifeCycleManagerLib.dir/src/LogData.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov src/Platform/CenterCommunication/CMakeFiles/CenterCommunicationLib.dir/src/Signal.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov src/Platform/CenterCommunication/CMakeFiles/CenterCommunicationLib.dir/src/SignalReceiver.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov src/Platform/CenterCommunication/CMakeFiles/CenterCommunicationLib.dir/src/SignalSender.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov src/Platform/CenterCommunication/CMakeFiles/CenterCommunicationLib.dir/src/DataCollector.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov tests/unit/CMakeFiles/unit_event_bus_host.dir/Platform/communications/eventbus/EventBusHostTest.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov tests/unit/CMakeFiles/unit_flag.dir/Platform/lifecyclemgr/FlagTest.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov tests/unit/CMakeFiles/unit_log_data.dir/Platform/lifecyclemgr/LogDataTest.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov tests/unit/CMakeFiles/unit_lifecycle_mgr.dir/Platform/lifecyclemgr/LifeCycleMgrTest.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov tests/unit/CMakeFiles/unit_feature_evaluator.dir/Platform/lifecyclemgr/FeatureEvaluatorTest.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov tests/unit/CMakeFiles/unit_watchdog.dir/Platform/lifecyclemgr/WatchDogTest.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov tests/unit/CMakeFiles/unit_signal.dir/Platform/centercommunication/SignalTest.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov tests/unit/CMakeFiles/unit_signal_receiver.dir/Platform/centercommunication/SignalReceiverTest.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov tests/unit/CMakeFiles/unit_signal_sender.dir/Platform/centercommunication/SignalSenderTest.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov tests/unit/CMakeFiles/unit_data_collector.dir/Platform/centercommunication/DataCollectorTest.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov tests/unit/CMakeFiles/unit_adas_feature.dir/Feature/AdasFeatureTest.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov tests/unit/CMakeFiles/unit_ev_energy_feature.dir/Feature/EvEnergyFeatureTest.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true
gcov tests/unit/CMakeFiles/unit_navigation_feature.dir/Feature/NavigationFeatureTest.cpp.o >>"${RAW_COV_FILE}" 2>/dev/null || true

mapfile -t cov_lines < <(
  python3 - "${RAW_COV_FILE}" <<'PY'
import re
import sys
from pathlib import Path

raw = Path(sys.argv[1]).read_text(encoding="utf-8", errors="ignore").splitlines()
entries = {}
for i, line in enumerate(raw):
    if line.startswith("File '") and "/src/" in line and line.endswith(".cpp'"):
        m = re.match(r"File '(.+)'", line)
        if not m:
            continue
        if i + 1 >= len(raw):
            continue
        m2 = re.match(r"Lines executed:([0-9.]+)% of ([0-9]+)", raw[i + 1])
        if not m2:
            continue
        path = m.group(1)
        pct = float(m2.group(1))
        total = int(m2.group(2))
        entries[path] = (pct, total)

weighted_total = 0
weighted_covered = 0.0
for path in sorted(entries):
    pct, total = entries[path]
    covered = total * pct / 100.0
    weighted_total += total
    weighted_covered += covered
    print(f"{path}|{pct:.2f}|{int(round(covered))}|{total}")

if weighted_total == 0:
    print("TOTAL|0.00|0|0")
else:
    total_pct = (weighted_covered / weighted_total) * 100.0
    print(f"TOTAL|{total_pct:.2f}|{int(round(weighted_covered))}|{weighted_total}")
PY
)

mapfile -t quality_lines < <(
  python3 - "${ROOT_DIR}" "${PUBLIC_HEADERS[@]}" <<'PY'
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
headers = [Path(p) for p in sys.argv[2:]]

def count_public_interfaces(header: Path) -> int:
    text = header.read_text(encoding="utf-8", errors="ignore").splitlines()
    in_public = False
    count = 0
    brace_depth = 0
    for raw in text:
        line = raw.strip()
        brace_depth += raw.count("{") - raw.count("}")
        if line.startswith("public:"):
            in_public = True
            continue
        if line.startswith("protected:") or line.startswith("private:"):
            in_public = False
            continue
        if not in_public:
            continue
        if "(" in line and ")" in line and line.endswith(";"):
            if "using " in line:
                continue
            count += 1
    return count

public_total = sum(count_public_interfaces(h) for h in headers if h.exists())

test_case_total = 0
for test_file in root.glob("tests/unit/**/*Test.cpp"):
    text = test_file.read_text(encoding="utf-8", errors="ignore")
    test_case_total += len(re.findall(r"sqa::test::check\s*\(", text))

cpp_to_header = {
    "src/Platform/FeatureCommunication/EventBus/src/Event.cpp":
        root / "src/Platform/FeatureCommunication/EventBus/include/FeatureCommunication/EventBus/Event.h",
    "src/Platform/FeatureCommunication/EventBus/src/EventQueue.cpp":
        root / "src/Platform/FeatureCommunication/EventBus/include/FeatureCommunication/EventBus/EventQueue.h",
    "src/Platform/FeatureCommunication/EventBus/src/EventBusProxy.cpp":
        root / "src/Platform/FeatureCommunication/EventBus/include/FeatureCommunication/EventBus/EventBusProxy.h",
    "src/Platform/FeatureCommunication/EventBus/src/EventBusHost.cpp":
        root / "src/Platform/FeatureCommunication/EventBus/include/FeatureCommunication/EventBus/EventBusHost.h",
    "src/Platform/LifeCycleManager/src/Feature.cpp":
        root / "src/Platform/LifeCycleManager/include/LifeCycleManager/Feature.h",
    "src/Platform/LifeCycleManager/src/Flag.cpp":
        root / "src/Platform/LifeCycleManager/include/LifeCycleManager/Flag.h",
    "src/Platform/LifeCycleManager/src/LogData.cpp":
        root / "src/Platform/LifeCycleManager/include/LifeCycleManager/LogData.h",
    "src/Platform/LifeCycleManager/src/LifeCycleMgr.cpp":
        root / "src/Platform/LifeCycleManager/include/LifeCycleManager/LifeCycleMgr.h",
    "src/Platform/LifeCycleManager/src/FeatureEvaluator.cpp":
        root / "src/Platform/LifeCycleManager/include/LifeCycleManager/FeatureEvaluator.h",
    "src/Platform/LifeCycleManager/src/WatchDog.cpp":
        root / "src/Platform/LifeCycleManager/include/LifeCycleManager/WatchDog.h",
    "src/Platform/CenterCommunication/src/Signal.cpp":
        root / "src/Platform/CenterCommunication/include/CenterCommunication/Signal.h",
    "src/Platform/CenterCommunication/src/SignalReceiver.cpp":
        root / "src/Platform/CenterCommunication/include/CenterCommunication/SignalReceiver.h",
    "src/Platform/CenterCommunication/src/SignalSender.cpp":
        root / "src/Platform/CenterCommunication/include/CenterCommunication/SignalSender.h",
    "src/Platform/CenterCommunication/src/DataCollector.cpp":
        root / "src/Platform/CenterCommunication/include/CenterCommunication/DataCollector.h",
    "src/Feature/src/AdasFeature.cpp":
        root / "src/Feature/include/Feature/AdasFeature.h",
    "src/Feature/src/EvEnergyFeature.cpp":
        root / "src/Feature/include/Feature/EvEnergyFeature.h",
    "src/Feature/src/NavigationFeature.cpp":
        root / "src/Feature/include/Feature/NavigationFeature.h",
}

cpp_to_tests = {
    "src/Platform/FeatureCommunication/EventBus/src/Event.cpp": [
        root / "tests/unit/Platform/communications/eventbus/EventTest.cpp"
    ],
    "src/Platform/FeatureCommunication/EventBus/src/EventQueue.cpp": [
        root / "tests/unit/Platform/communications/eventbus/EventQueueTest.cpp"
    ],
    "src/Platform/FeatureCommunication/EventBus/src/EventBusProxy.cpp": [
        root / "tests/unit/Platform/communications/eventbus/EventBusProxyTest.cpp"
    ],
    "src/Platform/FeatureCommunication/EventBus/src/EventBusHost.cpp": [
        root / "tests/unit/Platform/communications/eventbus/EventBusHostTest.cpp"
    ],
    "src/Platform/LifeCycleManager/src/Feature.cpp": [
        root / "tests/unit/Platform/lifecyclemgr/FeatureTest.cpp"
    ],
    "src/Platform/LifeCycleManager/src/Flag.cpp": [
        root / "tests/unit/Platform/lifecyclemgr/FlagTest.cpp"
    ],
    "src/Platform/LifeCycleManager/src/LogData.cpp": [
        root / "tests/unit/Platform/lifecyclemgr/LogDataTest.cpp"
    ],
    "src/Platform/LifeCycleManager/src/LifeCycleMgr.cpp": [
        root / "tests/unit/Platform/lifecyclemgr/LifeCycleMgrTest.cpp"
    ],
    "src/Platform/LifeCycleManager/src/FeatureEvaluator.cpp": [
        root / "tests/unit/Platform/lifecyclemgr/FeatureEvaluatorTest.cpp"
    ],
    "src/Platform/LifeCycleManager/src/WatchDog.cpp": [
        root / "tests/unit/Platform/lifecyclemgr/WatchDogTest.cpp"
    ],
    "src/Platform/CenterCommunication/src/Signal.cpp": [
        root / "tests/unit/Platform/centercommunication/SignalTest.cpp"
    ],
    "src/Platform/CenterCommunication/src/SignalReceiver.cpp": [
        root / "tests/unit/Platform/centercommunication/SignalReceiverTest.cpp"
    ],
    "src/Platform/CenterCommunication/src/SignalSender.cpp": [
        root / "tests/unit/Platform/centercommunication/SignalSenderTest.cpp"
    ],
    "src/Platform/CenterCommunication/src/DataCollector.cpp": [
        root / "tests/unit/Platform/centercommunication/DataCollectorTest.cpp"
    ],
    "src/Feature/src/AdasFeature.cpp": [
        root / "tests/unit/Feature/AdasFeatureTest.cpp"
    ],
    "src/Feature/src/EvEnergyFeature.cpp": [
        root / "tests/unit/Feature/EvEnergyFeatureTest.cpp"
    ],
    "src/Feature/src/NavigationFeature.cpp": [
        root / "tests/unit/Feature/NavigationFeatureTest.cpp"
    ],
}

print(f"PUBLIC_TOTAL|{public_total}")
print(f"TEST_CASE_TOTAL|{test_case_total}")
for cpp_path in sorted(cpp_to_header.keys()):
    h = cpp_to_header[cpp_path]
    pub = count_public_interfaces(h) if h.exists() else 0
    tc = 0
    for tf in cpp_to_tests.get(cpp_path, []):
        if tf.exists():
            tc += len(re.findall(r"sqa::test::check\s*\(", tf.read_text(encoding="utf-8", errors="ignore")))
    print(f"PER_CPP|{cpp_path}|{tc}|{pub}")
PY
)

public_total=0
test_case_total=0
declare -a per_cpp_metrics
for row in "${quality_lines[@]}"; do
  IFS='|' read -r key value <<<"${row}"
  if [[ "${key}" == "PUBLIC_TOTAL" ]]; then
    public_total="${value}"
  elif [[ "${key}" == "TEST_CASE_TOTAL" ]]; then
    test_case_total="${value}"
  elif [[ "${key}" == "PER_CPP" ]]; then
    per_cpp_metrics+=("${row}")
  fi
done

mapfile -t gap_lines < <(
  python3 - "${ROOT_DIR}" <<'PY'
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
gaps = []

eventbus_if = root / "src/Platform/FeatureCommunication/EventBus/include/FeatureCommunication/EventBus/EventBusInterface.h"
feature_h = root / "src/Platform/LifeCycleManager/include/LifeCycleManager/Feature.h"
lcm_h = root / "src/Platform/LifeCycleManager/include/LifeCycleManager/LifeCycleMgr.h"

def has_pattern(path: Path, pattern: str) -> bool:
    if not path.exists():
        return False
    text = path.read_text(encoding="utf-8", errors="ignore")
    return re.search(pattern, text, re.MULTILINE) is not None

if not has_pattern(eventbus_if, r"virtual\s+bool\s+sendEvent\s*\(\s*const\s+Event&\s+event\s*\)\s*=\s*0\s*;"):
    gaps.append("`EventBusInterface::sendEvent()` must return `bool`.")

if not has_pattern(feature_h, r"\bint\s+run\s*\(\s*\)\s*;"):
    gaps.append("`Feature::run()` must return `int`.")

if not has_pattern(lcm_h, r"\bbool\s+loadConfig\s*\(\s*const\s+std::string&\s+configPath\s*\)\s*;"):
    gaps.append("`LifeCycleMgr::loadConfig()` must return `bool`.")

if not gaps:
    print("NO_GAP")
else:
    for g in gaps:
        print(g)
PY
)

{
  echo "# Unit Test Report"
  echo
  echo "- Run timestamp: ${TIMESTAMP}"
  echo "- Build dir: \`${BUILD_DIR}\`"
  echo
  echo "## Result Summary"
  echo
  echo "- Total tests: ${#TEST_BINS[@]}"
  echo "- Passed: ${pass_count}"
  echo "- Failed: ${fail_count}"
  echo
  echo "## Test Status"
  echo
  for test_bin in "${TEST_BINS[@]}"; do
    status="PASS"
    for failed in "${failed_tests[@]:-}"; do
      if [[ "${failed}" == "${test_bin}" ]]; then
        status="FAIL"
        break
      fi
    done
    echo "- ${test_bin}: ${status}"
  done
  echo
  echo "## Statement Coverage (src/*.cpp)"
  echo
  echo "| File | Coverage | Covered/Total |"
  echo "|---|---:|---:|"
  for row in "${cov_lines[@]}"; do
    IFS='|' read -r f p c t <<<"${row}"
    if [[ "${f}" == "TOTAL" ]]; then
      total_pct="${p}"
      total_cov="${c}/${t}"
      continue
    fi
    rel="${f#${ROOT_DIR}/}"
    echo "| \`${rel}\` | ${p}% | ${c}/${t} |"
  done
  echo "| **TOTAL** | **${total_pct:-0.00}%** | **${total_cov:-0/0}** |"
  echo
  echo "## Interface/Test Case Metrics"
  echo
  echo "- Test case count: ${test_case_total}"
  echo "- Total public interface count: ${public_total}"
  echo
  echo "## Interface/Test Case by CPP"
  echo
  echo "| CPP File | Test Cases | Public Interfaces |"
  echo "|---|---:|---:|"
  for row in "${per_cpp_metrics[@]}"; do
    IFS='|' read -r _ cpp tc pub <<<"${row}"
    echo "| \`${cpp}\` | ${tc} | ${pub} |"
  done
  echo
  echo "## Below 90% Coverage Files"
  echo
  below_exists=0
  for row in "${cov_lines[@]}"; do
    IFS='|' read -r f p c t <<<"${row}"
    if [[ "${f}" == "TOTAL" ]]; then
      continue
    fi
    pct_int="${p%.*}"
    if [[ "${pct_int}" -lt 90 ]]; then
      rel="${f#${ROOT_DIR}/}"
      echo "- \`${rel}\`: ${p}% (${c}/${t})"
      below_exists=1
    fi
  done
  if [[ "${below_exists}" -eq 0 ]]; then
    echo "- None"
  fi
  echo
  echo "## Notes"
  echo
  if [[ "${fail_count}" -eq 0 ]]; then
    echo "- All unit tests passed."
    echo "- Exception-oriented coverage (examples): \`FeatureEvaluatorTest\` (propagated throws from \`sendEvent\` / \`getEventQueue\`, \`sendEvent\` returning \`false\`, missing inbound path, invalid command lines); \`WatchDogTest\` (unknown/silent health / propagated queue errors / FLAG and KILL events)."
  else
    echo "- Failed tests: ${failed_tests[*]}"
  fi
  echo
  echo "## Exception Handling Gap Report"
  echo
  if [[ "${#gap_lines[@]}" -eq 1 && "${gap_lines[0]}" == "NO_GAP" ]]; then
    echo "- No gap detected in checked interface contracts."
  else
    for g in "${gap_lines[@]}"; do
      echo "- ${g}"
    done
  fi
} >"${REPORT_FILE}"

echo "Report generated: ${REPORT_FILE}"
