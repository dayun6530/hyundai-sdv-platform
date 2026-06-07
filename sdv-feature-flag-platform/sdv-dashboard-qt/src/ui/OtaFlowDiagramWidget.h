#pragma once

#include <QString>
#include <QWidget>

namespace sdv::dash::ui {

/** Visual OTA pipeline: Download → Verify → Install → Switch → Validate → Complete. */
class OtaFlowDiagramWidget : public QWidget {
    Q_OBJECT
public:
    explicit OtaFlowDiagramWidget(QWidget* parent = nullptr);

    void setDeploymentState(const QString& state);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int activeIndex_{-1};
    bool failed_{false};
    float pulse_{0.f};
};

}  // namespace sdv::dash::ui
