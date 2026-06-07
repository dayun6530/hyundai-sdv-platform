#include "ops/viewmodels/PerceptionViewModel.h"

namespace sdv::dash::ops {

PerceptionViewModel::PerceptionViewModel(QObject* parent) : QObject(parent) {}

void PerceptionViewModel::apply(const PerceptionObjectList& objects)
{
    if (!objects.isValid()) {
        return;
    }
    objects_ = objects;
    refreshSummary();
    emit changed();
}

void PerceptionViewModel::refreshSummary()
{
    summary_ = QStringLiteral("Perception (%1) — %2 objects")
                   .arg(objects_.frameId)
                   .arg(objects_.objects.size());
}

}  // namespace sdv::dash::ops
