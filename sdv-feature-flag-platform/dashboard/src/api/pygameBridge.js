const PYGAME_CONTROL_WS = "ws://127.0.0.1:9093";
const DEFAULT_TIMEOUT_MS = 120_000;
const VEHICLE_SWITCH_MS = 400;

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function selectCockpitVehicle(carId) {
  const id = String(carId || "").trim();
  if (!id) return;
  try {
    await new Promise((resolve) => {
      let ws;
      let settled = false;
      const finish = () => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        try {
          ws?.close();
        } catch {
          /* ignore */
        }
        resolve();
      };
      const timer = setTimeout(finish, 4000);
      ws = new WebSocket(PYGAME_CONTROL_WS);
      ws.onopen = () => {
        ws.send(JSON.stringify({ type: "select_vehicle", car_id: id }));
      };
      ws.onmessage = finish;
      ws.onerror = finish;
      ws.onclose = () => {
        if (!settled) finish();
      };
    });
    await delay(VEHICLE_SWITCH_MS);
  } catch {
    /* cockpit switch is best-effort */
  }
}

function sendPromotePrimaryAnswer(ws, requestId, accepted) {
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  ws.send(
    JSON.stringify({
      type: "promote_primary_answer",
      request_id: requestId,
      accepted,
    }),
  );
}

/**
 * Show Promote Primary confirmation on the CARLA pygame cockpit (not browser UI).
 * Resolves true when the user accepts in pygame, false when declined.
 */
export function requestPygamePromoteConfirm({
  feature = "",
  version = "",
  message = "",
  detail = "",
  carId = "",
  index = 0,
  total = 0,
  timeoutMs = DEFAULT_TIMEOUT_MS,
} = {}) {
  return new Promise((resolve, reject) => {
    const requestId = `promote-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
    let ws;
    let settled = false;
    let keyListenerAttached = false;

    const detachKeyListener = () => {
      if (!keyListenerAttached) return;
      keyListenerAttached = false;
      document.removeEventListener("keydown", onKeyDown, true);
    };

    const finish = (fn, value) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      detachKeyListener();
      try {
        ws?.close();
      } catch {
        /* ignore */
      }
      fn(value);
    };

    const onKeyDown = (e) => {
      if (settled) return;
      const key = e.key;
      if (key === "y" || key === "Y" || key === "Enter") {
        e.preventDefault();
        sendPromotePrimaryAnswer(ws, requestId, true);
      } else if (key === "n" || key === "N" || key === "Escape") {
        e.preventDefault();
        sendPromotePrimaryAnswer(ws, requestId, false);
      }
    };

    const timer = setTimeout(() => {
      finish(
        reject,
        new Error(
          "pygame 확인 대기 시간 초과 — CARLA/pygame_map_bridge가 실행 중인지 확인하세요.",
        ),
      );
    }, timeoutMs);

    try {
      ws = new WebSocket(PYGAME_CONTROL_WS);
    } catch (e) {
      finish(
        reject,
        new Error(
          e?.message ||
            "pygame WebSocket 연결 실패 — pygame_map_bridge를 실행하세요.",
        ),
      );
      return;
    }

    ws.onopen = () => {
      ws.send(
        JSON.stringify({
          type: "promote_primary_prompt",
          request_id: requestId,
          feature,
          version,
          message,
          detail,
          car_id: carId,
          index,
          total,
        }),
      );
      document.addEventListener("keydown", onKeyDown, true);
      keyListenerAttached = true;
    };

    ws.onmessage = (event) => {
      let msg;
      try {
        msg = JSON.parse(event.data);
      } catch {
        return;
      }
      if (
        msg?.type === "promote_primary_response" &&
        msg.request_id === requestId
      ) {
        finish(resolve, Boolean(msg.accepted));
      }
    };

    ws.onerror = () => {
      finish(
        reject,
        new Error(
          "pygame WebSocket 오류 — pygame_map_bridge(CARLA cockpit)를 실행하세요.",
        ),
      );
    };

    ws.onclose = () => {
      if (!settled) {
        finish(reject, new Error("pygame 연결이 종료되었습니다."));
      }
    };
  });
}

/**
 * Per-vehicle pygame confirm loop. Returns accepted/declined car IDs.
 * @param {Array<{carId?: string, feature?: string, version?: string, message?: string, detail?: string}>} items
 */
export async function requestPygamePromoteConfirmSequence(
  items,
  { timeoutMs = DEFAULT_TIMEOUT_MS, onProgress } = {},
) {
  const list = Array.isArray(items) ? items.filter(Boolean) : [];
  const total = list.length;
  const accepted = [];
  const declined = [];

  for (let i = 0; i < list.length; i++) {
    const item = list[i];
    const carId = String(item.carId || "").trim();
    onProgress?.(i + 1, total, carId);

    if (carId) {
      await selectCockpitVehicle(carId);
    }

    try {
      const ok = await requestPygamePromoteConfirm({
        feature: item.feature || "",
        version: item.version || "",
        message: item.message || "",
        detail: item.detail || "",
        carId,
        index: i + 1,
        total,
        timeoutMs,
      });
      if (ok) {
        if (carId) accepted.push(carId);
        else accepted.push(`__item_${i}`);
      } else if (carId) {
        declined.push(carId);
      }
    } catch (e) {
      return {
        accepted,
        declined,
        aborted: true,
        error: e?.message || String(e),
      };
    }
  }

  return { accepted, declined, aborted: false };
}

/**
 * Answer an active promote prompt by click position on the cockpit JPEG
 * (same 1280x720 layout as pygame_map_bridge).
 */
export function answerPygamePromoteByClick(ws, requestId, x, y, width, height) {
  if (!ws || ws.readyState !== WebSocket.OPEN) return false;
  const sx = (1280 / Math.max(width, 1)) * x;
  const sy = (720 / Math.max(height, 1)) * y;
  const panelW = Math.min(760, 1280 - 80);
  const panelH = Math.min(360, 720 - 120);
  const panelX = (1280 - panelW) / 2;
  const panelY = (720 - panelH) / 2;
  const btnW = 160;
  const btnH = 44;
  const gap = 24;
  const totalW = btnW * 2 + gap;
  const btnY = panelY + panelH - btnH - 28;
  const btnX0 = panelX + panelW / 2 - totalW / 2;
  const yes = { x: btnX0, y: btnY, w: btnW, h: btnH };
  const no = { x: btnX0 + btnW + gap, y: btnY, w: btnW, h: btnH };
  const hit = (r) => sx >= r.x && sx <= r.x + r.w && sy >= r.y && sy <= r.y + r.h;
  if (hit(yes)) {
    sendPromotePrimaryAnswer(ws, requestId, true);
    return true;
  }
  if (hit(no)) {
    sendPromotePrimaryAnswer(ws, requestId, false);
    return true;
  }
  return false;
}
