#include "OTADeploymentWidget.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextCursor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

using sdv::dash::OTADeploymentWidget;

namespace {

const QStringList kStages = {
    QStringLiteral("Downloading"),
    QStringLiteral("Verifying"),
    QStringLiteral("Installing"),
    QStringLiteral("Switching"),
    QStringLiteral("Validating"),
    QStringLiteral("Completed"),
};

int phaseToIndex(const QString& phaseRaw) {
    const QString p = phaseRaw.toUpper();
    if (p.contains(QLatin1String("DOWNLOAD"))) {
        return 0;
    }
    if (p.contains(QLatin1String("VERIFY")) || p.contains(QLatin1String("CHECKSUM"))) {
        return 1;
    }
    if (p.contains(QLatin1String("INSTALL"))) {
        return 2;
    }
    if (p.contains(QLatin1String("SWITCH"))) {
        return 3;
    }
    if (p.contains(QLatin1String("VALID"))) {
        return 4;
    }
    if (p.contains(QLatin1String("COMPLETE")) || p.contains(QLatin1String("READY"))) {
        return 5;
    }
    return -1;
}

}  // namespace

OTADeploymentWidget::OTADeploymentWidget(QWidget* parent)
    : QWidget(parent), nam_(new QNetworkAccessManager(this)) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(5);

    auto* g = new QGridLayout();
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(3);
    g->addWidget(new QLabel(QStringLiteral("Active"), this), 0, 0);
    activeVer_ = new QLabel(QStringLiteral("—"), this);
    g->addWidget(activeVer_, 0, 1);
    g->addWidget(new QLabel(QStringLiteral("Target"), this), 1, 0);
    targetVer_ = new QLabel(QStringLiteral("—"), this);
    g->addWidget(targetVer_, 1, 1);
    v->addLayout(g);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setTextVisible(true);
    progress_->setFormat(QStringLiteral("%p% - staged"));
    v->addWidget(progress_);

    stages_ = new QLabel(this);
    stages_->setWordWrap(true);
    v->addWidget(stages_);

    log_ = new QTextEdit(this);
    log_->setReadOnly(true);
    log_->setMaximumHeight(116);
    log_->setFont(QFont(QStringLiteral("Consolas"), 8));
    v->addWidget(log_);

    auto* row = new QHBoxLayout();
    rollback_ = new QPushButton(QStringLiteral("Rollback ADAS"), this);
    connect(rollback_, &QPushButton::clicked, this, &OTADeploymentWidget::onRollback);
    row->addStretch();
    row->addWidget(rollback_);
    v->addLayout(row);

    appendLog(QStringLiteral("OTA observer ready - deploy/state + MQTT HUD."));
}

void OTADeploymentWidget::setApiBase(const QString& url) {
    apiBase_ = url.trimmed();
    while (apiBase_.endsWith(QLatin1Char('/'))) {
        apiBase_.chop(1);
    }
}

void OTADeploymentWidget::setCarId(const QString& carId) {
    carId_ = carId.trimmed().isEmpty() ? QStringLiteral("car_1") : carId.trimmed();
}

void OTADeploymentWidget::appendLog(const QString& line) {
    log_->append(line);
    QTextCursor c = log_->textCursor();
    c.movePosition(QTextCursor::End);
    log_->setTextCursor(c);
}

void OTADeploymentWidget::restageProgress(const QJsonObject& ota) {
    const QString phase = ota.value(QStringLiteral("ota_phase")).toString();
    const QString upd = ota.value(QStringLiteral("update_mode")).toString();
    const int idx = phaseToIndex(phase.isEmpty() ? upd : phase);
    int pct = 0;
    if (idx >= 0) {
        pct = qMin(100, (idx + 1) * 16);
    }
    if (upd.compare(QLatin1String("FAILED"), Qt::CaseInsensitive) == 0 ||
        phase.contains(QLatin1String("FAIL"))) {
        pct = 100;
        progress_->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color:#b91c1c; }"));
    } else if (upd.compare(QLatin1String("READY"), Qt::CaseInsensitive) == 0 ||
               phase.contains(QLatin1String("COMPLETE"))) {
        pct = 100;
        progress_->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color:#15803d; }"));
    } else {
        progress_->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color:#2563eb; }"));
    }
    progress_->setValue(pct);

    QStringList bullets;
    const int cur = qMax(0, idx);
    for (int i = 0; i < kStages.size(); ++i) {
        const QString mark = (i < cur) ? QStringLiteral("✓ ") : (i == cur ? QStringLiteral("› ") : QStringLiteral("· "));
        bullets.append(mark + kStages.at(i));
    }
    stages_->setText(QStringLiteral("<span style='font-family:Consolas;font-size:10px;color:#b8c7dc'>%1</span>")
                         .arg(bullets.join(QStringLiteral("<br>"))));
}

void OTADeploymentWidget::updateFromCarState(const QJsonObject& carState) {
    const QJsonObject feats = carState.value(QStringLiteral("features")).toObject();
    const QJsonObject adas = feats.value(QStringLiteral("ADAS")).toObject();
    const QString active = adas.value(QStringLiteral("active_version")).toString();
    const QString pending = adas.value(QStringLiteral("pending_active_version")).toString();
    activeVer_->setText(active.isEmpty() ? QStringLiteral("—") : active);
    targetVer_->setText(pending.isEmpty() ? QStringLiteral("no pending") : pending);

    const QJsonObject ota = carState.value(QStringLiteral("ota")).toObject();
    restageProgress(ota);

    const QString dl = ota.value(QStringLiteral("download_status")).toString();
    const QString ins = ota.value(QStringLiteral("install_status")).toString();
    const QString fail = ota.value(QStringLiteral("failure_reason")).toString();
    const QString phase = ota.value(QStringLiteral("ota_phase")).toString();
    const QString sig =
        phase + QLatin1Char('|') + dl + QLatin1Char('|') + ins + QLatin1Char('|') + pending + QLatin1Char('|') + fail;
    if (sig != lastOtaSig_) {
        lastOtaSig_ = sig;
        appendLog(QStringLiteral("[%1] phase=%2 dl=%3 install=%4 target=%5 %6")
                      .arg(carState.value(QStringLiteral("car_id")).toString(), phase, dl, ins,
                           pending.isEmpty() ? QStringLiteral("—") : pending,
                           fail.isEmpty() ? QString() : QStringLiteral("| ").append(fail)));
    }
    if (log_->document()->blockCount() > 160) {
        log_->clear();
        appendLog(QStringLiteral("(log trimmed)"));
    }
}

void OTADeploymentWidget::onRollback() {
    const QString car = carId_;
    if (car.isEmpty()) {
        return;
    }
    const auto answer =
        QMessageBox::question(this, QStringLiteral("Rollback"),
                              QStringLiteral("Invoke fleet rollback for ADAS on %1?").arg(car));
    if (answer != QMessageBox::Yes) {
        return;
    }

    QUrl url(apiBase_ + QStringLiteral("/api/fleet/action"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject body;
    body.insert(QStringLiteral("action"), QStringLiteral("rollback"));
    body.insert(QStringLiteral("car_id"), car);
    body.insert(QStringLiteral("feature"), QStringLiteral("ADAS"));

    QNetworkReply* reply = nam_->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        appendLog(QStringLiteral("[rollback] HTTP %1 %2").arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()).arg(QString::fromUtf8(raw)));
    });
}
