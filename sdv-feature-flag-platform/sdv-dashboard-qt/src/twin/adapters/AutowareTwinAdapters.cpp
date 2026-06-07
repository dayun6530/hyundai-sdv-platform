#include "twin/adapters/AutowareTwinAdapters.h"

namespace sdv::dash::twin {

AutowareTwinAdapters::AutowareTwinAdapters(QObject* parent) : QObject(parent) {}

bool AutowareTwinAdapters::connectRos(const QString& domainId)
{
    Q_UNUSED(domainId);
    emit statusMessage(QStringLiteral("Autoware twin adapters — stub (ROS2 not linked in this build)"));
    return false;
}

}  // namespace sdv::dash::twin
