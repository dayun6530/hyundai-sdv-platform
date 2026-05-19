/** Embedded tools (sdv-dashboard-qt / cockpit are intentionally excluded). */
export const ADMIN_VIEWS = [
  {
    id: "dashboard",
    label: "Deployment / Release",
    shortLabel: "OTA Dashboard",
    description: "Fleet OTA, campaigns, packages, vehicle actions",
    directUrl: "http://127.0.0.1:5173/",
    useIframe: true,
    healthPath: null,
  },
  {
    id: "test-ui",
    label: "Test UI",
    shortLabel: "MQTT Test",
    description: "Feature-flag simulator (FLAG ON/OFF per vehicle)",
    directUrl: "http://127.0.0.1:5174/",
    useIframe: true,
    healthPath: null,
  },
  {
    id: "postgres",
    label: "PostgreSQL",
    shortLabel: "pgAdmin",
    description: "Operational DB browser (sdv_operational)",
    directUrl: "http://127.0.0.1:5050/login?next=/browser/",
    useIframe: false,
    healthPath: null,
    hint: "Login: admin@admin.com / admin — server Team_SDV_DB is preconfigured.",
    externalNote: "pgAdmin은 새 브라우저 탭에서 엽니다. (Docker: sdv-pgadmin)",
  },
  {
    id: "grafana",
    label: "Grafana",
    shortLabel: "Logs & metrics",
    description: "Loki-backed dashboards and exploration",
    directUrl: "http://127.0.0.1:3000/login",
    useIframe: false,
    healthPath: "/api/health",
    hint: "Login: admin / admin",
    externalNote: "Grafana는 새 브라우저 탭에서 엽니다.",
  },
];

/** Legacy iframe proxy paths → view id (do not use /embed/* in the browser). */
export const LEGACY_EMBED_PATH_MAP = {
  dashboard: "dashboard",
  "test-ui": "test-ui",
  pgadmin: "postgres",
  grafana: "grafana",
};

export const DEFAULT_VIEW_ID = "dashboard";
export const STORAGE_KEY = "sdv-admin-portal-view";

export function resolveIframeSrc(view) {
  return view.directUrl;
}

export function resolveDirectUrl(view) {
  return view.directUrl;
}

export function parseViewIdFromLocation(loc = window.location) {
  const fromQuery = loc.searchParams.get("view");
  if (fromQuery && ADMIN_VIEWS.some((v) => v.id === fromQuery)) {
    return fromQuery;
  }

  const hash = loc.hash.replace(/^#\/?/, "").split("?")[0];
  if (hash && ADMIN_VIEWS.some((v) => v.id === hash)) {
    return hash;
  }

  const legacy = loc.pathname.match(/^\/embed\/([^/]+)\/?$/);
  if (legacy) {
    return LEGACY_EMBED_PATH_MAP[legacy[1]] ?? DEFAULT_VIEW_ID;
  }

  return null;
}

export function buildPortalUrl(viewId) {
  const base = typeof window !== "undefined" ? window.location.origin : "http://127.0.0.1:5170";
  const host = base.replace("://localhost:", "://127.0.0.1:");
  return `${host}/?view=${encodeURIComponent(viewId)}`;
}
