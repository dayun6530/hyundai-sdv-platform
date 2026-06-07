#pragma once

#include "twin/TwinTypes.h"

#include <QObject>
#include <QString>

namespace sdv::dash::carla {
class CarlaCockpitView;
class HudOverlayWidget;
class TrafficLightOverlayRenderer;
}

namespace sdv::dash::carla::control {
class CarlaControlManager;
}

namespace sdv::dash::carla::integration {
class CarlaApiAdapter;
class CarlaDataBridge;
struct CarlaWorldSnapshot;
}

namespace sdv::dash::mapbox {
class MapboxBridge;
}

namespace sdv::dash::ops {
class OpsTelemetryPipeline;
}

namespace sdv::dash::twin {

class CarlaTwinAdapters;

/** SDV Digital Twin orchestrator (presentation + adapters, no pipeline edits). */
class DigitalTwinController : public QObject {
    Q_OBJECT
public:
    explicit DigitalTwinController(QObject* parent = nullptr);

    void configure(const QString& carlaMode,
                   sdv::dash::carla::integration::CarlaDataBridge* bridge,
                   sdv::dash::carla::control::CarlaControlManager* control,
                   sdv::dash::mapbox::MapboxBridge* mapbox,
                   sdv::dash::carla::CarlaCockpitView* cockpit,
                   sdv::dash::ops::OpsTelemetryPipeline* pipeline);

    CarlaTwinAdapters* carlaAdapters() const { return carla_; }

signals:
    void statusMessage(const QString& line);

private:
    void onSnapshot(const sdv::dash::carla::integration::CarlaWorldSnapshot& snap);
    void onDestinationPicked(double x, double y);
    void onWorldState(const TwinWorldState& state);
    void pushRouteToMap(const sdv::dash::carla::integration::CarlaWorldSnapshot& snap);
    QString trajectoryToGeoJson(const sdv::dash::carla::integration::CarlaWorldSnapshot& snap,
                                sdv::dash::mapbox::MapboxBridge* mapbox) const;

    CarlaTwinAdapters* carla_{nullptr};
    sdv::dash::mapbox::MapboxBridge* mapbox_{nullptr};
    sdv::dash::carla::CarlaCockpitView* cockpit_{nullptr};
    sdv::dash::carla::TrafficLightOverlayRenderer* trafficOverlay_{nullptr};
    sdv::dash::ops::OpsTelemetryPipeline* pipeline_{nullptr};
    sdv::dash::carla::integration::CarlaApiAdapter* twinSnapAdapter_{nullptr};
    bool active_{false};
};

}  // namespace sdv::dash::twin
