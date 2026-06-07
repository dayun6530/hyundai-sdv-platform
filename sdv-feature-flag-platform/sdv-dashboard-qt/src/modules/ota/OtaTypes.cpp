#include "modules/ota/OtaTypes.h"

namespace sdv::dash::ota {

QString otaPhaseToString(const OtaPhase phase)
{
    switch (phase) {
    case OtaPhase::Idle:
        return QStringLiteral("Idle");
    case OtaPhase::Downloading:
        return QStringLiteral("Downloading");
    case OtaPhase::Verifying:
        return QStringLiteral("Verifying");
    case OtaPhase::Installing:
        return QStringLiteral("Installing");
    case OtaPhase::Switching:
        return QStringLiteral("Switching");
    case OtaPhase::Validating:
        return QStringLiteral("Validating");
    case OtaPhase::Completed:
        return QStringLiteral("Completed");
    case OtaPhase::Failed:
        return QStringLiteral("Failed");
    case OtaPhase::Rollback:
        return QStringLiteral("Rollback");
    }
    return QStringLiteral("Idle");
}

OtaPhase otaPhaseFromString(const QString& raw)
{
    const QString u = raw.trimmed().toUpper();
    if (u.isEmpty() || u == QStringLiteral("IDLE") || u == QStringLiteral("NONE")) {
        return OtaPhase::Idle;
    }
    if (u.contains(QStringLiteral("DOWNLOAD"))) {
        return OtaPhase::Downloading;
    }
    if (u.contains(QStringLiteral("VERIFY")) || u.contains(QStringLiteral("CHECKSUM"))) {
        return OtaPhase::Verifying;
    }
    if (u.contains(QStringLiteral("INSTALL"))) {
        return OtaPhase::Installing;
    }
    if (u.contains(QStringLiteral("SWITCH"))) {
        return OtaPhase::Switching;
    }
    if (u.contains(QStringLiteral("VALID"))) {
        return OtaPhase::Validating;
    }
    if (u.contains(QStringLiteral("ROLLBACK"))) {
        return OtaPhase::Rollback;
    }
    if (u.contains(QStringLiteral("FAIL"))) {
        return OtaPhase::Failed;
    }
    if (u.contains(QStringLiteral("COMPLETE")) || u.contains(QStringLiteral("READY"))) {
        return OtaPhase::Completed;
    }
    return OtaPhase::Idle;
}

int otaPhaseProgress(const OtaPhase phase)
{
    switch (phase) {
    case OtaPhase::Idle:
        return 0;
    case OtaPhase::Downloading:
        return 12;
    case OtaPhase::Verifying:
        return 28;
    case OtaPhase::Installing:
        return 48;
    case OtaPhase::Switching:
        return 68;
    case OtaPhase::Validating:
        return 86;
    case OtaPhase::Completed:
        return 100;
    case OtaPhase::Failed:
        return 100;
    case OtaPhase::Rollback:
        return 50;
    }
    return 0;
}

}  // namespace sdv::dash::ota
