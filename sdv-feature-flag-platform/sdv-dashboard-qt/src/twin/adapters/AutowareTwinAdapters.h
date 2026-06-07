#pragma once

#include "twin/adapters/TwinAdapterInterfaces.h"

#include <QObject>

namespace sdv::dash::twin {

/**
 * Phase E — Autoware ROS2 twin source (stub).
 * Implement by subscribing to /localization, /perception, /planning, /control
 * and mapping into the same ITwin* contracts as CarlaTwinAdapters.
 */
class AutowareTwinAdapters : public QObject {
    Q_OBJECT
public:
    explicit AutowareTwinAdapters(QObject* parent = nullptr);
    bool connectRos(const QString& domainId = QString());

signals:
    void statusMessage(const QString& line);
};

}  // namespace sdv::dash::twin
