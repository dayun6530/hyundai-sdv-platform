#include "mapbox/MiniMapView.h"

#include "mapbox/MapboxBridge.h"
#include "mapbox/MapboxWebConfig.h"

#include <QSizePolicy>
#include <QVBoxLayout>
#include <QUrl>
#include <QWebChannel>
#include <QWebEngineSettings>
#include <QWebEngineView>

namespace sdv::dash::mapbox {

MiniMapView::MiniMapView(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(140);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet(QStringLiteral(
        "MiniMapView { background: rgba(4,10,22,0.55); border: 1px solid rgba(34,211,238,0.45);"
        " border-radius: 12px; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    webView_ = new QWebEngineView(this);
    webView_->setContextMenuPolicy(Qt::NoContextMenu);
    configureMapWebView(webView_);
    layout->addWidget(webView_);

    connect(webView_, &QWebEngineView::loadFinished, this, &MiniMapView::onPageLoaded);
}

void MiniMapView::setMapboxBridge(MapboxBridge* bridge)
{
    bridge_ = bridge;
    setupWebChannel();
    if (webView_ && !webView_->url().isValid()) {
        webView_->load(QUrl(QStringLiteral("qrc:/mapbox/minimap.html")));
    }
}

void MiniMapView::setupWebChannel()
{
    if (!webView_ || !bridge_) {
        return;
    }

    if (!webChannel_) {
        webChannel_ = new QWebChannel(this);
        webView_->page()->setWebChannel(webChannel_);
    }

    webChannel_->registerObject(QStringLiteral("mapboxBridge"), bridge_);

    if (pageReady_) {
        bridge_->notifyTokenReady();
    }
}

void MiniMapView::onPageLoaded(const bool ok)
{
    if (!ok) {
        return;
    }
    pageReady_ = true;
    setupWebChannel();
    if (bridge_) {
        bridge_->notifyTokenReady();
    }
}

}  // namespace sdv::dash::mapbox
