#pragma once

#include <cstdint>
#include <string>

namespace sdv::ops::domain {

int64_t stampToNanoseconds(int32_t sec, uint32_t nanosec);

/// ISO-8601 UTC string from epoch nanoseconds (for logs / UI).
std::string nanosecondsToIso8601(int64_t stamp_ns);

/// Yaw (rad) from unit quaternion (w, x, y, z).
double quaternionToYaw(double qw, double qx, double qy, double qz);

}  // namespace sdv::ops::domain
