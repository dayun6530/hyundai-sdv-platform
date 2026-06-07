#pragma once

#include <QString>

namespace sdv::dash::mapbox {

struct MapboxTokenInfo {
    QString token;
    QString source;
    QString warning;
    bool usableForGlJs() const { return token.startsWith(QStringLiteral("pk.")); }
};

MapboxTokenInfo resolveMapboxToken(const QString& cliOverride = QString());

}  // namespace sdv::dash::mapbox
