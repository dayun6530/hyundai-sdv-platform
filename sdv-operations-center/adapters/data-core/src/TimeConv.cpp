#include "sdv/ops/domain/TimeConv.hpp"

#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace sdv::ops::domain {

int64_t stampToNanoseconds(const int32_t sec, const uint32_t nanosec)
{
  return static_cast<int64_t>(sec) * 1'000'000'000LL + static_cast<int64_t>(nanosec);
}

std::string nanosecondsToIso8601(const int64_t stamp_ns)
{
  const auto sec = static_cast<time_t>(stamp_ns / 1'000'000'000LL);
  const auto ms = static_cast<int>((stamp_ns % 1'000'000'000LL) / 1'000'000LL);

  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &sec);
#else
  gmtime_r(&sec, &utc);
#endif

  std::ostringstream oss;
  oss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms << 'Z';
  return oss.str();
}

double quaternionToYaw(const double qw, const double qx, const double qy, const double qz)
{
  const double siny_cosp = 2.0 * (qw * qz + qx * qy);
  const double cosy_cosp = 1.0 - 2.0 * (qy * qy + qz * qz);
  return std::atan2(siny_cosp, cosy_cosp);
}

}  // namespace sdv::ops::domain
