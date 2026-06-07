#pragma once

#include <QByteArray>
#include <optional>

#include "ops/domain/OpsTypes.h"

namespace sdv::dash::ops {

class TelemetryParser {
public:
    static std::optional<ParsedTelemetryEnvelope> parseFrame(const QByteArray& utf8Json);
};

}  // namespace sdv::dash::ops
