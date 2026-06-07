#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <QtMath>

namespace sdv::dash::mapbox::geo {

inline QJsonArray metersToLngLatCoord(
    const double x, const double y, const double z, const double originLng, const double originLat)
{
    const double metersPerDegLat = 111320.0;
    const double metersPerDegLng = 111320.0 * qCos(originLat * M_PI / 180.0);
    return QJsonArray{originLng + x / metersPerDegLng, originLat + y / metersPerDegLat, z};
}

inline QString lineFeatureJsonFromLocalCoords(const QJsonArray& localCoords, const double originLng, const double originLat)
{
    if (localCoords.isEmpty()) {
        return {};
    }
    QJsonArray lngLatCoords;
    for (const auto& c : localCoords) {
        const auto arr = c.toArray();
        if (arr.size() < 2) {
            continue;
        }
        const double z = arr.size() > 2 ? arr.at(2).toDouble() : 0.0;
        lngLatCoords.append(
            metersToLngLatCoord(arr.at(0).toDouble(), arr.at(1).toDouble(), z, originLng, originLat));
    }
    if (lngLatCoords.isEmpty()) {
        return {};
    }
    const QJsonObject feature{
        {QStringLiteral("type"), QStringLiteral("Feature")},
        {QStringLiteral("geometry"),
         QJsonObject{
             {QStringLiteral("type"), QStringLiteral("LineString")},
             {QStringLiteral("coordinates"), lngLatCoords},
         }},
    };
    return QString::fromUtf8(QJsonDocument(feature).toJson(QJsonDocument::Compact));
}

}  // namespace sdv::dash::mapbox::geo
