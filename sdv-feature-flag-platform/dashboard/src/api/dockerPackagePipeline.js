async function parseJson(res) {
  const data = await res.json().catch(() => ({}));
  if (!res.ok) {
    throw new Error(data?.error || `Request failed: ${res.status}`);
  }
  return data;
}

export async function fetchDockerPipelineSteps() {
  const res = await fetch("/api/docker-pipeline/steps");
  return parseJson(res);
}

export async function fetchDockerPipelinePreflight(feature, version, registry) {
  const q = new URLSearchParams({
    feature: feature || "Energy",
    version: version || "",
    registry: registry || "localhost:5000",
  });
  const res = await fetch(`/api/docker-pipeline/preflight?${q}`);
  return parseJson(res);
}

export async function runDockerPipelineStep(body) {
  const res = await fetch("/api/docker-pipeline/run", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
  return parseJson(res);
}
