#include "mapbox/MapboxWebConfig.h"

#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

namespace sdv::dash::mapbox {

void configureMapWebView(QWebEngineView* view)
{
    if (!view) {
        return;
    }

    auto* settings = view->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    settings->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, true);
    settings->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    settings->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, true);
    settings->setAttribute(QWebEngineSettings::ErrorPageEnabled, false);

    auto* profile = view->page()->profile();
    profile->setHttpCacheType(QWebEngineProfile::NoCache);
    profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
}

}  // namespace sdv::dash::mapbox
