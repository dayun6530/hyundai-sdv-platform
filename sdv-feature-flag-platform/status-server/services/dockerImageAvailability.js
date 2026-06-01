function createDockerImageChecker(execWslBash) {
  const cache = { at: 0, map: new Map(), ttlMs: 15000 };

  async function checkOne(imageRef) {
    const image = String(imageRef || '').trim();
    if (!image) return false;

    const now = Date.now();
    if (now - cache.at > cache.ttlMs) {
      cache.map.clear();
      cache.at = now;
    }
    if (cache.map.has(image)) {
      return cache.map.get(image);
    }

    const escaped = image.replace(/'/g, `'\\''`);
    try {
      const r = await execWslBash(
        `docker image inspect '${escaped}' >/dev/null 2>&1`,
        { timeout: 25000 },
      );
      const ok = Number(r.code) === 0;
      cache.map.set(image, ok);
      return ok;
    } catch {
      cache.map.set(image, false);
      return false;
    }
  }

  async function resolveAvailabilityMap(images) {
    const unique = [...new Set((images || []).map(String).filter(Boolean))];
    const pairs = await Promise.all(
      unique.map(async (img) => [img, await checkOne(img)]),
    );
    return Object.fromEntries(pairs);
  }

  return { checkOne, resolveAvailabilityMap };
}

module.exports = { createDockerImageChecker };
