const DOCKER_ISO_PREFIX = /^(\d{4}-\d{2}-\d{2}T[^\s]+)\s+(.*)$/;
const FORMATTED_PREFIX = /^\[\d{4}-\d{2}-\d{2}[ T]/;

export function formatDockerLogTimestampLine(line) {
  const raw = String(line || '');
  if (FORMATTED_PREFIX.test(raw)) return raw;
  const match = DOCKER_ISO_PREFIX.exec(raw);
  if (!match) return raw;
  const [, iso, message] = match;
  let label = iso;
  try {
    const d = new Date(iso);
    if (!Number.isNaN(d.getTime())) {
      label = d.toLocaleString('sv-SE', { hour12: false });
    }
  } catch {
    /* keep raw docker timestamp */
  }
  return `[${label}] ${message}`;
}

function newestFirst(lines) {
  return [...lines].reverse();
}

export function formatDockerLogDisplay(data) {
  if (!data || data.status !== 'ok') return data;

  const rawLines =
    Array.isArray(data.lines) && data.lines.length
      ? data.lines
      : String(data.text || '')
          .split(/\r?\n/)
          .filter((line, idx, arr) => !(idx === arr.length - 1 && line === ''));

  if (!rawLines.length) {
    return { ...data, timestamps: Boolean(data.timestamps) };
  }

  const alreadyFormatted = rawLines.every((line) => FORMATTED_PREFIX.test(line));
  const formatted = alreadyFormatted
    ? rawLines
    : rawLines.map(formatDockerLogTimestampLine);
  const hasTimestamps =
    Boolean(data.timestamps) ||
    alreadyFormatted ||
    formatted.some((line, idx) => line !== rawLines[idx]);
  const lines = newestFirst(formatted);

  return {
    ...data,
    lines,
    text: lines.join('\n'),
    timestamps: hasTimestamps,
  };
}
