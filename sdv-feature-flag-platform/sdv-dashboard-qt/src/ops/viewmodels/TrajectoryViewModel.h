#pragma once

#include <QObject>
#include <QVector>

#include "ops/domain/OpsTypes.h"

namespace sdv::dash::ops {

class TrajectoryViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int pointCount READ pointCount NOTIFY changed)
    Q_PROPERTY(QString frameId READ frameId NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)

public:
    explicit TrajectoryViewModel(QObject* parent = nullptr);

    int pointCount() const { return trajectory_.points.size(); }
    QString frameId() const { return trajectory_.frameId; }
    QString summary() const { return summary_; }

    const Trajectory& trajectory() const { return trajectory_; }
    QVector<TrajectoryWaypoint> waypoints() const { return trajectory_.points; }

public slots:
    void apply(const sdv::dash::ops::Trajectory& trajectory);

signals:
    void changed();

private:
    void refreshSummary();

    Trajectory trajectory_;
    QString summary_;
};

}  // namespace sdv::dash::ops
