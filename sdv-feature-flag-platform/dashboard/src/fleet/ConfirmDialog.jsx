/**
 * In-app confirm dialog (replaces window.confirm for deployment actions).
 */
export default function ConfirmDialog({
  open,
  title = "확인",
  message,
  detail,
  confirmLabel = "예",
  cancelLabel = "아니오",
  confirmVariant = "primary",
  onConfirm,
  onCancel,
}) {
  if (!open) return null;

  const confirmClass =
    confirmVariant === "danger" ? "btn btn-danger" : "btn btn-primary";

  return (
    <div
      className="sdv-modal-overlay"
      role="presentation"
      onClick={(e) => {
        if (e.target === e.currentTarget) onCancel?.();
      }}
    >
      <div
        className="sdv-modal"
        role="dialog"
        aria-modal="true"
        aria-labelledby="sdv-modal-title"
        onClick={(e) => e.stopPropagation()}
      >
        <h3 id="sdv-modal-title" className="sdv-modal-title">
          {title}
        </h3>
        {message ? <p className="sdv-modal-message">{message}</p> : null}
        {detail ? (
          <pre className="sdv-modal-detail mono tiny">{detail}</pre>
        ) : null}
        <div className="sdv-modal-actions">
          <button type="button" className="btn btn-secondary" onClick={onCancel}>
            {cancelLabel}
          </button>
          <button type="button" className={confirmClass} onClick={onConfirm}>
            {confirmLabel}
          </button>
        </div>
      </div>
    </div>
  );
}
