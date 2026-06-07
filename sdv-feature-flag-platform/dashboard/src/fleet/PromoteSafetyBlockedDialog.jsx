import { StatusBadge } from "./campaignShared.jsx";

export default function PromoteSafetyBlockedDialog({
  open,
  blockedCars = [],
  forcing = false,
  forceError = "",
  onForceReady,
  onClose,
}) {
  if (!open || !blockedCars.length) return null;

  return (
    <div
      className="sdv-modal-overlay"
      role="presentation"
      onClick={(e) => {
        if (e.target === e.currentTarget && !forcing) onClose?.();
      }}
    >
      <div
        className="sdv-modal sdv-modal-wide"
        role="dialog"
        aria-modal="true"
        aria-labelledby="promote-safety-blocked-title"
        onClick={(e) => e.stopPropagation()}
      >
        <h3 id="promote-safety-blocked-title" className="sdv-modal-title">
          OTA Safety — Promote 불가
        </h3>
        <p className="sdv-modal-message">
          Primary Promote는 OTA Safety가 <strong>READY</strong>인 차량만 실행할 수
          있습니다. 아래 차량의 안전 조건을 확인하거나, 데모용으로 READY 상태를
          강제 설정한 뒤 다시 <strong>Promote Primary</strong>를 눌러 주세요.
        </p>
        <p className="tiny subtle">
          Run Candidate(shadow)는 안전 조건과 무관합니다.
        </p>

        <ul className="promote-safety-blocked-list">
          {blockedCars.map((row) => (
            <li key={`${row.carId}:${row.feature}`} className="promote-safety-blocked-item">
              <div className="promote-safety-blocked-head">
                <strong className="mono">{row.carId}</strong>
                <span className="tiny subtle"> · {row.feature}</span>
                <StatusBadge text="BLOCKED" className="status-red" />
              </div>
              {row.failed_checks?.length ? (
                <p className="tiny campaign-logs-alert campaign-logs-alert-error">
                  실패 항목: {row.failed_checks.join(", ")}
                </p>
              ) : null}
              {row.checks?.length ? (
                <ul className="campaign-validate-items tiny">
                  {row.checks
                    .filter((c) => !c.passed)
                    .map((c) => (
                      <li key={c.key} className="campaign-validate-item-warn">
                        <StatusBadge text="FAIL" className="status-red" />
                        <span>
                          {c.label}: {String(c.actual)} (필요: {c.required})
                          {c.reason ? ` — ${c.reason}` : ""}
                        </span>
                      </li>
                    ))}
                </ul>
              ) : null}
            </li>
          ))}
        </ul>

        {forceError ? (
          <div className="campaign-logs-alert campaign-logs-alert-error tiny">
            {forceError}
          </div>
        ) : null}

        <div className="sdv-modal-actions">
          <button type="button" className="btn btn-secondary" disabled={forcing} onClick={onClose}>
            닫기
          </button>
          <button
            type="button"
            className="btn btn-primary"
            disabled={forcing}
            onClick={onForceReady}
          >
            {forcing
              ? "READY 설정 중…"
              : blockedCars.length > 1
                ? `데모 READY 강제 설정 (${blockedCars.length}대)`
                : "데모 READY 강제 설정"}
          </button>
        </div>
      </div>
    </div>
  );
}
