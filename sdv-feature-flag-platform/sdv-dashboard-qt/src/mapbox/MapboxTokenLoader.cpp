#include "mapbox/MapboxTokenLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>

namespace sdv::dash::mapbox {

namespace {

QString stripQuotes(QString s)
{
    s = s.trimmed();
    while (s.startsWith(QLatin1Char('"')) || s.startsWith(QLatin1Char('\''))) {
        s.remove(0, 1);
        s = s.trimmed();
    }
    while (s.endsWith(QLatin1Char('"')) || s.endsWith(QLatin1Char('\''))) {
        s.chop(1);
        s = s.trimmed();
    }
    return s;
}

QString readTokenFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    if (line.startsWith(QLatin1Char('#'))) {
        return {};
    }
    return stripQuotes(line);
}

}  // namespace

MapboxTokenInfo resolveMapboxToken(const QString& cliOverride)
{
    MapboxTokenInfo info;

    auto trySet = [&info](const QString& raw, const QString& source) {
        const QString t = stripQuotes(raw);
        if (t.isEmpty()) {
            return;
        }
        if (info.token.isEmpty()) {
            info.token = t;
            info.source = source;
        }
    };

    trySet(cliOverride, QStringLiteral("cli"));
    trySet(qEnvironmentVariable("MAPBOX_TOKEN"), QStringLiteral("MAPBOX_TOKEN"));
    trySet(qEnvironmentVariable("MAPBOX_ACCESS_TOKEN"), QStringLiteral("MAPBOX_ACCESS_TOKEN"));
    trySet(QString::fromUtf8(qgetenv("MAPBOX_TOKEN")), QStringLiteral("MAPBOX_TOKEN(env)"));

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString dashQtRoot = QDir(appDir).filePath(QStringLiteral("../.."));
    const QString cwd = QDir::currentPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/.mapbox_token"),
        appDir + QStringLiteral("/mapbox_token.txt"),
        dashQtRoot + QStringLiteral("/.mapbox_token"),
        cwd + QStringLiteral("/.mapbox_token"),
        cwd + QStringLiteral("/sdv-dashboard-qt/.mapbox_token"),
    };

    for (const QString& path : candidates) {
        const QString fromFile = readTokenFile(path);
        if (!fromFile.isEmpty()) {
            trySet(fromFile, path);
            break;
        }
    }

    if (info.token.isEmpty()) {
        info.warning = QStringLiteral(
            "Mapbox token missing. Create sdv-dashboard-qt/.mapbox_token with your pk. token "
            "(https://account.mapbox.com/access-tokens/) or set MAPBOX_TOKEN.");
        return info;
    }

    if (info.token.startsWith(QStringLiteral("sk."))) {
        info.warning = QStringLiteral(
            "Secret token (sk.) cannot be used in the dashboard. Create a public token (pk.) at "
            "https://account.mapbox.com/access-tokens/");
        info.token.clear();
        return info;
    }

    if (!info.token.startsWith(QStringLiteral("pk."))) {
        info.warning = QStringLiteral(
            "Invalid Mapbox token format (must start with pk.). Check .mapbox_token or MAPBOX_TOKEN.");
        info.token.clear();
        return info;
    }

    return info;
}

}  // namespace sdv::dash::mapbox
