#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace sdv::ops::domain {

constexpr int kTelemetrySchemaVersion = 1;

enum class TelemetryType {
  VehicleState,
  EgoPose,
  Trajectory,
  PerceptionObjects,
};

const char* telemetryTypeToString(TelemetryType type);

struct TelemetryEnvelope {
  int schema_version{kTelemetrySchemaVersion};
  TelemetryType type{TelemetryType::VehicleState};
  int64_t stamp_ns{0};
  std::string frame_id;
  nlohmann::json data;

  std::string toJsonString() const;
};

TelemetryEnvelope makeEnvelope(
  TelemetryType type, int64_t stamp_ns, const std::string& frame_id, const nlohmann::json& data);

}  // namespace sdv::ops::domain
