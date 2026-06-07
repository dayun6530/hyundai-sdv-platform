// Smoke test: SignalManager + ViewModels (no WebSocket / no GUI).
// Run: ops_ingest_smoke.exe

#include "ops/domain/TelemetryParser.h"
#include "ops/ingest/SignalManager.h"
#include "ops/viewmodels/EgoViewModel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>

#include <cstdio>

using namespace sdv::dash::ops;

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    SignalManager manager;
    EgoViewModel ego;

    int egoUpdates = 0;
    QObject::connect(&manager, &SignalManager::egoPoseUpdated, &app, [&](const EgoPose& pose) {
        ego.apply(pose);
        ++egoUpdates;
        std::printf("%s\n", qPrintable(ego.summary()));
    });

    const QByteArray frame = R"({
      "schema_version": 1,
      "type": "ego_pose",
      "stamp_ns": 1000,
      "frame_id": "map",
      "data": {
        "stamp_ns": 1000,
        "frame_id": "map",
        "position": { "x": 123.4, "y": 567.8, "z": 0.5 },
        "orientation": { "w": 1, "x": 0, "y": 0, "z": 0 },
        "yaw_rad": 0.1,
        "speed_mps": 16.94
      }
    })";

    QTimer tick;
    double x = 123.4;
    QObject::connect(&tick, &QTimer::timeout, &app, [&]() {
        x += 3.0;
        const QByteArray dynamic = QString(R"({
          "schema_version": 1,
          "type": "ego_pose",
          "stamp_ns": %1,
          "frame_id": "map",
          "data": {
            "position": { "x": %2, "y": 567.8, "z": 0.5 },
            "orientation": { "w": 1, "x": 0, "y": 0, "z": 0 },
            "yaw_rad": 0.1,
            "speed_mps": 16.94
          }
        })")
                                   .arg(QDateTime::currentMSecsSinceEpoch() * 1000000LL)
                                   .arg(x)
                                   .toUtf8();
        manager.ingestJsonFrame(dynamic);
    });
    tick.start(200);

    QTimer::singleShot(3000, &app, [&]() {
        if (egoUpdates >= 3) {
            std::printf("PASS: SignalManager -> EgoViewModel (%d updates)\n", egoUpdates);
            app.quit();
            return;
        }
        std::printf("FAIL: expected >= 3 ego updates, got %d\n", egoUpdates);
        app.exit(1);
    });

    return app.exec();
}
