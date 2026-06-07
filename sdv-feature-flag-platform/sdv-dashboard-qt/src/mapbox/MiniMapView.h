#pragma once

#include <QWidget>

class QWebChannel;
class QWebEngineView;

namespace sdv::dash::mapbox {

class MapboxBridge;

class MiniMapView : public QWidget {
    Q_OBJECT
public:
    explicit MiniMapView(QWidget* parent = nullptr);

    void setMapboxBridge(MapboxBridge* bridge);

private slots:
    void onPageLoaded(bool ok);

private:
    void setupWebChannel();

    QWebEngineView* webView_{nullptr};
    QWebChannel* webChannel_{nullptr};
    MapboxBridge* bridge_{nullptr};
    bool pageReady_{false};
};

}  // namespace sdv::dash::mapbox
