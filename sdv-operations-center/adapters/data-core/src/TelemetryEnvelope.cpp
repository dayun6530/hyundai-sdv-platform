#include "sdv/ops/domain/TelemetryEnvelope.hpp"

#include "sdv/ops/domain/TimeConv.hpp"

namespace sdv::ops::domain {

const char* telemetryTypeToString(const TelemetryType type)
{
  switch (type) {
    case TelemetryType::VehicleState:
      return "vehicle_state";
    case TelemetryType::EgoPose:
      return "ego_pose";
    case TelemetryType::Trajectory:
      return "trajectory";
    case TelemetryType::PerceptionObjects:
      return "perception_objects";
  }
  return "unknown";
}

std::string TelemetryEnvelope::toJsonString() const
{
  const nlohmann::json root{
    {"schema_version", schema_version},
    {"type", telemetryTypeToString(type)},
    {"stamp_ns", stamp_ns},
    {"stamp_iso", nanosecondsToIso8601(stamp_ns)},
    {"frame_id", frame_id},
    {"data", data},
  };
  return root.dump();
}

TelemetryEnvelope makeEnvelope(
  const TelemetryType type, const int64_t stamp_ns, const std::string& frame_id, const nlohmann::json& data)
{
  TelemetryEnvelope env;
  env.schema_version = kTelemetrySchemaVersion;
  env.type = type;
  env.stamp_ns = stamp_ns;
  env.frame_id = frame_id;
  env.data = data;
  return env;
}

}  // namespace sdv::ops::domain
