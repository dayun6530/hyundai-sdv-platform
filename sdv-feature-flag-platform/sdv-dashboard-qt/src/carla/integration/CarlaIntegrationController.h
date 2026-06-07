#pragma once

#include <QObject>
#include <QUrl>

namespace sdv::dash::carla {
class CarlaCockpitView;
}

namespace sdv::dash::ops {
class OpsTelemetryPipeline;
}

namespace sdv::dash::carla::integration {

class CarlaDataBridge;
class HudViewModel;

/**
 * Wires CARLA integration into dashboard without modifying OpsTelemetryPipeline.
 */
class CarlaIntegrationController : public QObject {
    Q_OBJECT
public:
    enum class Mode {
        Off,
        /** C++ sim adapter + WS publish + direct pipeline ingest */
        NativeSim,
        /** Python CARLA bridge on 9092 → C++ extractors */
        NativeCarlaWs,
        /** Python publishes Phase-1 on 9090; RosBridgeClient only */
        ExternalWsBridge,
    };

    explicit CarlaIntegrationController(QObject* parent = nullptr);
    ~CarlaIntegrationController() override;

    CarlaDataBridge* dataBridge() const { return bridge_; }
    HudViewModel* hudViewModel() const;

    void configure(Mode mode,
                     sdv::dash::ops::OpsTelemetryPipeline* pipeline,
                     sdv::dash::carla::CarlaCockpitView* cockpit,
                     const QUrl& carlaAdapterWs = QUrl(QStringLiteral("ws://127.0.0.1:9092")),
                     quint16 publishPort = 9090);

    void start();
    void stop();

    Mode mode() const { return mode_; }

signals:
    void statusMessage(const QString& line);

private:
    Mode mode_{Mode::Off};
    CarlaDataBridge* bridge_{nullptr};
    sdv::dash::ops::OpsTelemetryPipeline* pipeline_{nullptr};
    sdv::dash::carla::CarlaCockpitView* cockpit_{nullptr};
};

}  // namespace sdv::dash::carla::integration
