export function StatusBadge({ text, className }) {
  return <span className={`status-badge ${className || "status-gray"}`}>{text}</span>;
}

export const DEMO_OTA_FAILURE_HINT =
  "Vehicle OTA state is not READY. For demo, prepare OTA READY before running this step (UI does not change state).";

export function RawJsonDetails({ label, data }) {
  if (data == null || data === "") return null;
  const text = typeof data === "string" ? data : JSON.stringify(data, null, 2);
  return (
    <details className="campaign-raw-details">
      <summary>{label}</summary>
      <pre className="json-box mono tiny">{text}</pre>
    </details>
  );
}
