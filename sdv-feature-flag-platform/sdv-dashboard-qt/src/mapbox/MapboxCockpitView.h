#pragma once

#include <QTimer>
#include <QWidget>

class QKeyEvent;
class QWebChannel;
class QWebEngineView;

namespace sdv::dash::ops {
class OpsTelemetryPipeline;
}

namespace sdv::dash::mapbox {

class MapboxBridge;

class MapboxCockpitView : public QWidget {
    Q_OBJECT
public:
    explicit MapboxCockpitView(QWidget* parent = nullptr);

    MapboxBridge* mapboxBridge() const { return bridge_; }
    void setMapboxBridge(MapboxBridge* bridge);
    void bindPipeline(sdv::dash::ops::OpsTelemetryPipeline* pipeline);
    void postManualArrowKey(int qtKey, bool pressed);

private slots:
    void onPageLoaded(bool ok);
    void pushViewModelsToBridge();
    void tickBuiltinDemo();
    void driveCockpitFromCpp();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void setupWebChannel();
    void ensureDemoTimer();
    void stopDemoTimer();
    bool hasLiveEgo() const;
    void injectManualArrowKey(const QString& arrowKey, bool pressed);

    static QString trajectoryGeoJson(const sdv::dash::ops::OpsTelemetryPipeline* pipeline, MapboxBridge* mapbox);
    static QString perceptionGeoJson(const sdv::dash::ops::OpsTelemetryPipeline* pipeline, MapboxBridge* mapbox);
    static QString syntheticForwardTrajectoryJson(
        double cx, double cy, double yawRad, double lengthM, int points);
    static QString syntheticPerceptionJson(double cx, double cy, double yawRad);

    QWebEngineView* webView_{nullptr};
    QWebChannel* webChannel_{nullptr};
    MapboxBridge* bridge_{nullptr};
    sdv::dash::ops::OpsTelemetryPipeline* pipeline_{nullptr};
    bool pageReady_{false};
    bool demoActive_{false};
    double demoPhase_{0.0};
    QTimer pushTimer_;
    QTimer demoTimer_;
    QTimer cockpitDriveTimer_;
    double cockpitDriveDistM_{0.0};
    bool cockpitAtRouteEnd_{false};
};

}  // namespace sdv::dash::mapbox
