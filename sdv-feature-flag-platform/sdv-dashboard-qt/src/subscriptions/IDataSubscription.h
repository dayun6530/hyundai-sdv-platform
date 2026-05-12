#pragma once

#include <QObject>

namespace sdv::dash {

/** Subscribe-only contract; implementations attach to backend transports without owning domain logic. */
class IDataSubscription : public QObject {
    Q_OBJECT
public:
    explicit IDataSubscription(QObject* parent = nullptr) : QObject(parent) {}
    virtual void open() = 0;
    virtual void close() = 0;
signals:
    void connectivityChanged(bool up);
};

}  // namespace sdv::dash
