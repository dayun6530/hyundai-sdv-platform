// E2E smoke: RosBridgeClient -> SignalManager -> EgoViewModel (no Widgets/WebEngine).
#include "ops/OpsTelemetryPipeline.h"
#include "ops/viewmodels/EgoViewModel.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>

using namespace sdv::dash::ops;

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    auto* pipeline = new OpsTelemetryPipeline(&app);
    int egoUpdates = 0;
    double lastX = 0.0;
    bool xChanged = false;

    QObject::connect(pipeline->egoViewModel(), &EgoViewModel::changed, &app, [&]() {
        const double x = pipeline->egoViewModel()->x();
        std::printf("%s\n", qPrintable(pipeline->egoViewModel()->summary()));
        if (egoUpdates > 0 && qAbs(x - lastX) > 0.5) {
            xChanged = true;
        }
        lastX = x;
        ++egoUpdates;
    });

    pipeline->start(QUrl(QStringLiteral("ws://127.0.0.1:9090")));

    QTimer::singleShot(8000, &app, [&]() {
        const bool connected = pipeline->isBridgeConnected();
        std::printf("bridge_connected=%s ego_updates=%d x_changed=%s\n",
            connected ? "true" : "false",
            egoUpdates,
            xChanged ? "true" : "false");
        if (connected && egoUpdates >= 3 && xChanged) {
            std::printf("PASS: WebSocket -> RosBridgeClient -> SignalManager -> ViewModel\n");
            app.quit();
            return;
        }
        std::printf("FAIL: connected=%d updates=%d x_changed=%d\n", connected, egoUpdates, xChanged);
        app.exit(1);
    });

    return app.exec();
}
