#include "ops/viewmodels/EgoViewModel.h"

namespace sdv::dash::ops {

EgoViewModel::EgoViewModel(QObject* parent) : QObject(parent) {}

void EgoViewModel::apply(const EgoPose& pose)
{
    if (!pose.isValid()) {
        return;
    }
    pose_ = pose;
    refreshSummary();
    emit changed();
}

void EgoViewModel::refreshSummary()
{
    summary_ = QStringLiteral("Ego (%1) x=%2 y=%3 z=%4 | %5 m/s")
                     .arg(pose_.frameId)
                     .arg(pose_.x, 0, 'f', 2)
                     .arg(pose_.y, 0, 'f', 2)
                     .arg(pose_.z, 0, 'f', 2)
                     .arg(pose_.speedMps, 0, 'f', 1);
}

}  // namespace sdv::dash::ops
