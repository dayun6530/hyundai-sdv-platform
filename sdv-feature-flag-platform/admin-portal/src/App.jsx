import { useCallback, useEffect, useMemo, useState } from "react";
import {
  ADMIN_VIEWS,
  DEFAULT_VIEW_ID,
  STORAGE_KEY,
  buildPortalUrl,
  parseViewIdFromLocation,
  resolveDirectUrl,
  resolveIframeSrc,
} from "./views";

function loadStoredViewId() {
  try {
    const stored = localStorage.getItem(STORAGE_KEY);
    if (stored && ADMIN_VIEWS.some((v) => v.id === stored)) {
      return stored;
    }
  } catch {
    // private mode / blocked storage
  }
  return DEFAULT_VIEW_ID;
}

function initialViewId() {
  if (typeof window === "undefined") {
    return DEFAULT_VIEW_ID;
  }
  return parseViewIdFromLocation(new URL(window.location.href)) ?? loadStoredViewId();
}

async function probeDirectUrl(directUrl, healthPath) {
  const base = directUrl.replace(/\/$/, "").replace(/\/login.*$/, "");
  const target = healthPath ? `${base}${healthPath}` : base;
  try {
    const res = await fetch(target, { mode: "no-cors", cache: "no-store" });
    if (res.type === "opaque") {
      return "up";
    }
    return res.ok ? "up" : "down";
  } catch {
    return "down";
  }
}

function StatusDot({ status }) {
  const className =
    status === "up" ? "status-dot status-dot--up" : status === "down" ? "status-dot status-dot--down" : "status-dot";
  const title = status === "up" ? "Reachable" : status === "down" ? "Unreachable" : "Checking…";
  return <span className={className} title={title} aria-hidden="true" />;
}

function ExternalToolPanel({ view, serviceUp }) {
  const directUrl = resolveDirectUrl(view);

  return (
    <section className="embed-panel" aria-label={view.label}>
      <header className="embed-toolbar">
        <div className="embed-toolbar-text">
          <h2>{view.label}</h2>
          <p>{view.description}</p>
          {view.hint ? <p className="embed-hint">{view.hint}</p> : null}
          {view.externalNote ? <p className="embed-note">{view.externalNote}</p> : null}
        </div>
      </header>
      <div className="external-tool-body">
        <div className={`external-tool-status${serviceUp ? " external-tool-status--up" : " external-tool-status--down"}`}>
          {serviceUp ? "서비스 응답 중" : "서비스에 연결되지 않음 — docker compose up -d pgadmin grafana 확인"}
        </div>
        <p className="external-tool-url">{directUrl}</p>
        <div className="external-tool-actions">
          <a className="btn btn-primary" href={directUrl} target="_blank" rel="noopener noreferrer">
            {view.label} 새 탭에서 열기
          </a>
        </div>
        <p className="embed-note">위 버튼은 포털(5170)이 아니라 <strong>{directUrl}</strong> 로 이동합니다.</p>
      </div>
    </section>
  );
}

function IframeEmbedPanel({ view }) {
  const embedSrc = useMemo(() => resolveIframeSrc(view), [view]);
  const directUrl = resolveDirectUrl(view);

  return (
    <section className="embed-panel" aria-label={view.label}>
      <header className="embed-toolbar">
        <div className="embed-toolbar-text">
          <h2>{view.label}</h2>
          <p>{view.description}</p>
        </div>
        <div className="embed-toolbar-actions">
          <a className="btn btn-ghost" href={directUrl} target="_blank" rel="noopener noreferrer">
            새 탭에서 열기
          </a>
        </div>
      </header>
      <iframe
        key={view.id}
        className="embed-frame"
        src={embedSrc}
        title={view.label}
        allow="fullscreen"
        referrerPolicy="no-referrer-when-downgrade"
      />
    </section>
  );
}

function EmbedPanel({ view, serviceUp }) {
  if (!view) {
    return null;
  }
  if (view.useIframe) {
    return <IframeEmbedPanel view={view} />;
  }
  return <ExternalToolPanel view={view} serviceUp={serviceUp} />;
}

export default function App() {
  const [activeId, setActiveId] = useState(initialViewId);
  const [health, setHealth] = useState({});

  const activeView = useMemo(() => ADMIN_VIEWS.find((v) => v.id === activeId) ?? ADMIN_VIEWS[0], [activeId]);

  useEffect(() => {
    const loc = new URL(window.location.href);
    let hostFixed = false;
    if (loc.hostname === "localhost") {
      loc.hostname = "127.0.0.1";
      hostFixed = true;
    }

    const legacyEmbed = loc.pathname.match(/^\/embed\//);
    const viewFromLoc = parseViewIdFromLocation(loc);
    if (legacyEmbed && viewFromLoc) {
      loc.pathname = "/";
      loc.search = `?view=${encodeURIComponent(viewFromLoc)}`;
      loc.hash = "";
      window.location.replace(loc.toString());
      return;
    }

    if (hostFixed) {
      window.location.replace(loc.toString());
    }
  }, []);

  const syncViewToUrl = useCallback((id) => {
    const next = buildPortalUrl(id);
    window.history.replaceState({ view: id }, "", next);
  }, []);

  const selectView = useCallback(
    (id) => {
      setActiveId(id);
      syncViewToUrl(id);
      try {
        localStorage.setItem(STORAGE_KEY, id);
      } catch {
        // ignore
      }
    },
    [syncViewToUrl]
  );

  useEffect(() => {
    syncViewToUrl(activeId);
  }, [activeId, syncViewToUrl]);

  const refreshHealth = useCallback(async () => {
    const checking = Object.fromEntries(ADMIN_VIEWS.map((view) => [view.id, "checking"]));
    setHealth(checking);

    const results = await Promise.all(
      ADMIN_VIEWS.map(async (view) => {
        const status = await probeDirectUrl(view.directUrl, view.healthPath);
        return [view.id, status];
      })
    );
    setHealth(Object.fromEntries(results));
  }, []);

  useEffect(() => {
    refreshHealth();
    const timer = setInterval(refreshHealth, 30_000);
    return () => clearInterval(timer);
  }, [refreshHealth]);

  const serviceUp = health[activeView.id] === "up";

  return (
    <div className="admin-layout">
      <aside className="admin-sidebar">
        <div className="brand">
          <div className="brand-mark">SDV</div>
          <div>
            <div className="brand-title">Admin Portal</div>
            <div className="brand-sub">통합 운영 콘솔</div>
          </div>
        </div>

        <nav className="nav" aria-label="관리자 메뉴">
          {ADMIN_VIEWS.map((view) => {
            const isActive = view.id === activeId;
            return (
              <button
                key={view.id}
                type="button"
                className={`nav-item${isActive ? " nav-item--active" : ""}`}
                onClick={() => selectView(view.id)}
                aria-current={isActive ? "page" : undefined}
              >
                <span className="nav-item-label">
                  <StatusDot status={health[view.id]} />
                  <span>
                    <span className="nav-item-title">{view.shortLabel}</span>
                    <span className="nav-item-desc">{view.label}</span>
                  </span>
                </span>
              </button>
            );
          })}
        </nav>

        <footer className="sidebar-footer">
          <p>접속: http://127.0.0.1:5170/?view=…</p>
          <p className="sidebar-footer-qt">
            <strong>Qt SDV Dashboard</strong> (Windows 앱)은 브라우저가 아닙니다.
          </p>
          <p className="sidebar-footer-cmd">
            PowerShell:
            <br />
            <code>.\scripts\launch_qt_dashboard.ps1</code>
          </p>
          <p className="sidebar-footer-cmd">
            또는 <code>.\open_qt_dashboard.bat</code> 더블클릭
          </p>
          <button type="button" className="btn btn-ghost btn-block" onClick={refreshHealth}>
            연결 상태 새로고침
          </button>
        </footer>
      </aside>

      <main className="admin-main">
        <EmbedPanel view={activeView} serviceUp={serviceUp} />
      </main>
    </div>
  );
}
