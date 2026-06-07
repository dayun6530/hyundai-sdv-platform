#include "carla/CarlaCameraReceiver.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QTimer>
#include <QUrl>

#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
#include <QWebSocket>
#endif

namespace sdv::dash::carla {

class CarlaCameraReceiver::Private {
public:
#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
    QWebSocket socket;
#endif
    QNetworkAccessManager http;
    QTimer httpPollTimer;
    QUrl wsUrl;
    QUrl httpUrl;
};

CarlaCameraReceiver::CarlaCameraReceiver(QObject* parent) : QObject(parent), d_(new Private)
{
    d_->httpPollTimer.setInterval(66);
    connect(&d_->httpPollTimer, &QTimer::timeout, this, &CarlaCameraReceiver::pollHttpFrame);

#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
    connect(&d_->socket, &QWebSocket::connected, this, &CarlaCameraReceiver::onSocketConnected);
    connect(&d_->socket, &QWebSocket::disconnected, this, &CarlaCameraReceiver::onSocketDisconnected);
    connect(&d_->socket, &QWebSocket::errorOccurred, this, &CarlaCameraReceiver::onSocketError);
    connect(&d_->socket, &QWebSocket::textMessageReceived, this, &CarlaCameraReceiver::onTextMessageReceived);
#endif
}

CarlaCameraReceiver::~CarlaCameraReceiver()
{
    stop();
    delete d_;
}

void CarlaCameraReceiver::start(const QUrl& webSocketUrl)
{
    stopRequested_ = false;
    d_->wsUrl = webSocketUrl;

    const QByteArray httpEnv = qgetenv("CARLA_CAMERA_HTTP");
    if (!httpEnv.isEmpty()) {
        d_->httpUrl = QUrl(QString::fromUtf8(httpEnv));
        d_->httpPollTimer.start();
        emit statusMessage(QStringLiteral("CARLA HTTP camera: %1").arg(d_->httpUrl.toString()));
    }

#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
    if (webSocketUrl.isValid() && webSocketUrl.scheme().startsWith(QStringLiteral("ws"), Qt::CaseInsensitive)) {
        emit statusMessage(QStringLiteral("CARLA camera WS: %1").arg(webSocketUrl.toString()));
        d_->socket.open(webSocketUrl);
        return;
    }
#endif

    showPlaceholder();
    emit statusMessage(QStringLiteral("CARLA camera waiting (no WebSocket URL)"));
}

void CarlaCameraReceiver::stop()
{
    stopRequested_ = true;
    d_->httpPollTimer.stop();
#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
    if (d_->socket.state() != QAbstractSocket::UnconnectedState) {
        d_->socket.close();
    }
#endif
    connected_ = false;
    hasLiveFrame_ = false;
    emit connectionChanged(false);
}

void CarlaCameraReceiver::onSocketConnected()
{
    connected_ = true;
    emit connectionChanged(true);
    emit statusMessage(QStringLiteral("CARLA camera stream connected"));
}

void CarlaCameraReceiver::onSocketDisconnected()
{
    connected_ = false;
    hasLiveFrame_ = false;
    emit connectionChanged(false);
    if (!stopRequested_) {
        showPlaceholder();
    }
}

void CarlaCameraReceiver::onSocketError()
{
#if defined(SDV_HAVE_QT_WEBSOCKETS) && SDV_HAVE_QT_WEBSOCKETS
    emit statusMessage(QStringLiteral("CARLA camera WS error: %1").arg(d_->socket.errorString()));
#endif
    showPlaceholder();
}

void CarlaCameraReceiver::onTextMessageReceived(const QString& message)
{
    decodeCameraEnvelope(message.toUtf8());
}

bool CarlaCameraReceiver::decodeCameraEnvelope(const QByteArray& utf8Frame)
{
    const QJsonDocument doc = QJsonDocument::fromJson(utf8Frame);
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject root = doc.object();
    const QString type = root.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("front_camera") && type != QStringLiteral("carla_camera") &&
        type != QStringLiteral("pygame_map")) {
        return false;
    }
    const QJsonObject data = root.value(QStringLiteral("data")).toObject();
    const QString encoding = data.value(QStringLiteral("encoding")).toString(QStringLiteral("base64"));
    const QByteArray payload = encoding == QStringLiteral("base64")
        ? QByteArray::fromBase64(data.value(QStringLiteral("payload")).toString().toLatin1())
        : data.value(QStringLiteral("payload")).toString().toLatin1();
    if (payload.isEmpty()) {
        return false;
    }
    QImage img;
    if (!img.loadFromData(payload, "JPEG")) {
        if (!img.loadFromData(payload, "PNG")) {
            return false;
        }
    }
    deliverFrame(img);
    return true;
}

void CarlaCameraReceiver::pollHttpFrame()
{
    if (!d_->httpUrl.isValid()) {
        return;
    }
    QNetworkRequest req(d_->httpUrl);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = d_->http.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }
        QImage img;
        if (img.loadFromData(reply->readAll(), "JPEG")) {
            deliverFrame(img);
        }
    });
}

void CarlaCameraReceiver::ingestFrame(const QImage& frame)
{
    deliverFrame(frame);
}

bool CarlaCameraReceiver::ingestJsonEnvelope(const QByteArray& utf8Frame)
{
    return decodeCameraEnvelope(utf8Frame);
}

void CarlaCameraReceiver::showWaitingPlaceholder()
{
    showPlaceholder();
}

void CarlaCameraReceiver::deliverFrame(const QImage& image)
{
    if (image.isNull()) {
        return;
    }
    hasLiveFrame_ = true;
    emit frameReady(image);
}

void CarlaCameraReceiver::showPlaceholder()
{
    emit frameReady(buildPlaceholder(QStringLiteral(
        "Start: python pygame_map_bridge.py\n"
        "Dashboard: --carla-mode bridge · view 1st Person")));
}

QImage CarlaCameraReceiver::buildPlaceholder(const QString& caption) const
{
    QImage img(1280, 720, QImage::Format_RGB32);
    img.fill(QColor(12, 18, 32));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient road(0, 360, 1280, 720);
    road.setColorAt(0.0, QColor(55, 65, 81));
    road.setColorAt(1.0, QColor(30, 41, 59));
    p.fillRect(0, 280, 1280, 440, road);

    p.setPen(QPen(QColor(248, 250, 252, 200), 3));
    for (int lane = -1; lane <= 1; ++lane) {
        const int cx = 640 + lane * 120;
        for (int y = 300; y < 700; y += 28) {
            p.drawLine(cx, y, cx, y + 14);
        }
    }

    p.setPen(QColor(186, 230, 253));
    QFont title = p.font();
    title.setPixelSize(28);
    title.setBold(true);
    p.setFont(title);
    p.drawText(QRect(0, 40, 1280, 50), Qt::AlignCenter, QStringLiteral("CARLA Driver View"));

    QFont sub = p.font();
    sub.setPixelSize(16);
    sub.setBold(false);
    p.setFont(sub);
    p.setPen(QColor(203, 213, 225));
    p.drawText(QRect(80, 620, 1120, 60), Qt::AlignCenter | Qt::TextWordWrap, caption);
    p.end();
    return img;
}

}  // namespace sdv::dash::carla
