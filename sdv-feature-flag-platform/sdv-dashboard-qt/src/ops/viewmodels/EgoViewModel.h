#pragma once

#include <QObject>

#include "ops/domain/OpsTypes.h"

namespace sdv::dash::ops {

class EgoViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 stampNs READ stampNs NOTIFY changed)
    Q_PROPERTY(QString frameId READ frameId NOTIFY changed)
    Q_PROPERTY(double x READ x NOTIFY changed)
    Q_PROPERTY(double y READ y NOTIFY changed)
    Q_PROPERTY(double z READ z NOTIFY changed)
    Q_PROPERTY(double yawRad READ yawRad NOTIFY changed)
    Q_PROPERTY(double speedMps READ speedMps NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)

public:
    explicit EgoViewModel(QObject* parent = nullptr);

    qint64 stampNs() const { return pose_.stampNs; }
    QString frameId() const { return pose_.frameId; }
    double x() const { return pose_.x; }
    double y() const { return pose_.y; }
    double z() const { return pose_.z; }
    double yawRad() const { return pose_.yawRad; }
    double speedMps() const { return pose_.speedMps; }
    QString summary() const { return summary_; }

    const EgoPose& pose() const { return pose_; }

public slots:
    void apply(const sdv::dash::ops::EgoPose& pose);

signals:
    void changed();

private:
    void refreshSummary();

    EgoPose pose_;
    QString summary_;
};

}  // namespace sdv::dash::ops
