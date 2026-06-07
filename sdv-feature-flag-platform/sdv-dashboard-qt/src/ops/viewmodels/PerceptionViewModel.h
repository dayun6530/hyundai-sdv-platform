#pragma once

#include <QObject>
#include <QVector>

#include "ops/domain/OpsTypes.h"

namespace sdv::dash::ops {

class PerceptionViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int objectCount READ objectCount NOTIFY changed)
    Q_PROPERTY(QString frameId READ frameId NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)

public:
    explicit PerceptionViewModel(QObject* parent = nullptr);

    int objectCount() const { return objects_.objects.size(); }
    QString frameId() const { return objects_.frameId; }
    QString summary() const { return summary_; }

    const PerceptionObjectList& objects() const { return objects_; }

public slots:
    void apply(const sdv::dash::ops::PerceptionObjectList& objects);

signals:
    void changed();

private:
    void refreshSummary();

    PerceptionObjectList objects_;
    QString summary_;
};

}  // namespace sdv::dash::ops
