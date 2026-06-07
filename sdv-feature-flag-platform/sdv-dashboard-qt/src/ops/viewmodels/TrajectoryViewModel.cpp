#include "ops/viewmodels/TrajectoryViewModel.h"

namespace sdv::dash::ops {

TrajectoryViewModel::TrajectoryViewModel(QObject* parent) : QObject(parent) {}

void TrajectoryViewModel::apply(const Trajectory& trajectory)
{
    if (!trajectory.isValid()) {
        return;
    }
    trajectory_ = trajectory;
    refreshSummary();
    emit changed();
}

void TrajectoryViewModel::refreshSummary()
{
    summary_ = QStringLiteral("Trajectory (%1) — %2 points")
                   .arg(trajectory_.frameId)
                   .arg(trajectory_.points.size());
}

}  // namespace sdv::dash::ops
