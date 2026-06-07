#pragma once

#include <QWidget>

namespace sdv::dash::ops {

class OpsTelemetryPipeline;

/// Reference panel: binds ViewModels to read-only labels (Mapbox cockpit can use the same pipeline).
class OpsIngestIntegrationPanel : public QWidget {
    Q_OBJECT
public:
    explicit OpsIngestIntegrationPanel(OpsTelemetryPipeline* pipeline, QWidget* parent = nullptr);
};

}  // namespace sdv::dash::ops
