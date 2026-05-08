function fetchJson(url, options = {}) {
  return fetch(url, {
    ...options,
    headers: {
      "content-type": "application/json",
      ...(options.headers || {}),
    },
  }).then(async (res) => {
    const data = await res.json().catch(() => null);
    if (!res.ok) {
      const msg = data?.error || `Unleash API failed: ${res.status}`;
      throw new Error(msg);
    }
    return data;
  });
}

/**
 * Add or update a targeting strategy for a feature in an environment.
 *
 * Body:
 *  - projectId (optional, default "default")
 *  - environment (optional, default "development")
 *  - featureName (required)
 *  - contextName: "region" | "model" | "group"
 *  - values: string[]
 *  - rollout: 0..100 (optional, default 100)
 *  - stickiness: (optional, default "userId")
 */
export function setTargetingStrategy(body) {
  return fetchJson("/api/unleash/strategy/target", {
    method: "POST",
    body: JSON.stringify(body || {}),
  });
}

/**
 * Overwrite variants for a feature in an environment.
 *
 * Either pass:
 *  - variants: [{ name: "A", weight: 500 }, { name: "B", weight: 500 }]
 * or pass ratioA/ratioB (percent) and optional nameA/nameB.
 */
export function setVariants(body) {
  return fetchJson("/api/unleash/strategy/variants", {
    method: "POST",
    body: JSON.stringify(body || {}),
  });
}

/**
 * Promote a variant by setting it to 100% weight.
 *
 * Body:
 *  - projectId, environment, featureName
 *  - winner: "A" | "B" | any variant name
 */
export function promoteVariant(body) {
  return fetchJson("/api/unleash/promote", {
    method: "POST",
    body: JSON.stringify(body || {}),
  });
}

/**
 * Option 2: Target a segment (region/model/group) and deploy a specific version.
 *
 * This endpoint will:
 *  - write a target strategy in Unleash (variant name = version token, for observability), and
 *  - execute Docker OTA promote for matching cars (via status-server).
 *
 * Body:
 *  - projectId, environment
 *  - featureName: "NavigationFeature" | "AdasFeature" | "EvEnergyFeature"
 *  - contextName: "region" | "model" | "group"
 *  - values: string[]
 *  - version: "2.0" (tag suffix; leading v allowed)
 */
export function deployTargetVersion(body) {
  return fetchJson("/api/unleash/deploy/target-version", {
    method: "POST",
    body: JSON.stringify(body || {}),
  });
}

/**
 * Canary rollout helper: deploy version to a single group (canary/beta/stable).
 *
 * Body:
 *  - projectId, environment
 *  - featureName
 *  - group: "canary" | "beta" | "stable"
 *  - version
 */
export function deployGroupVersion(body) {
  const b = body || {};
  return fetchJson("/api/unleash/deploy/target-version", {
    method: "POST",
    body: JSON.stringify({
      projectId: b.projectId,
      environment: b.environment,
      featureName: b.featureName,
      contextName: "group",
      values: [String(b.group || "").trim()],
      version: b.version,
    }),
  });
}

/**
 * Deploy per-region versions in one click.
 *
 * Body:
 *  - projectId, environment
 *  - featureName
 *  - versionsByRegion: { KR?: "2.0", US?: "3.0", EU?: "" }
 */
export function deployRegionVersions(body) {
  return fetchJson("/api/unleash/deploy/region-versions", {
    method: "POST",
    body: JSON.stringify(body || {}),
  });
}

/**
 * Deploy a single version to all cars (global rollout).
 *
 * Body:
 *  - projectId, environment
 *  - featureName
 *  - version
 */
export function deployGlobalVersion(body) {
  return fetchJson("/api/unleash/deploy/global-version", {
    method: "POST",
    body: JSON.stringify(body || {}),
  });
}

/**
 * Deploy per-model versions in one click.
 *
 * Body:
 *  - projectId, environment
 *  - featureName
 *  - versionsByModel: { IONIQ5?: "2.0", IONIQ6?: "3.0", KONA_EV?: "" }
 */
export function deployModelVersions(body) {
  return fetchJson("/api/unleash/deploy/model-versions", {
    method: "POST",
    body: JSON.stringify(body || {}),
  });
}

/**
 * Deploy versions to the whole fleet by ratio (random assignment).
 *
 * Body:
 *  - projectId, environment
 *  - featureName
 *  - splits: [{ percent: 50, version: "2.0" }, ...]  // must sum to 100
 */
export function deployRatioVersions(body) {
  return fetchJson("/api/unleash/deploy/ratio-versions", {
    method: "POST",
    body: JSON.stringify(body || {}),
  });
}

