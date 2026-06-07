#pragma once

#include <QString>

namespace sdv::dash::ota {

enum class OtaPhase {
    Idle = 0,
    Downloading,
    Verifying,
    Installing,
    Switching,
    Validating,
    Completed,
    Failed,
    Rollback,
};

QString otaPhaseToString(OtaPhase phase);
OtaPhase otaPhaseFromString(const QString& raw);
int otaPhaseProgress(OtaPhase phase);

}  // namespace sdv::dash::ota
