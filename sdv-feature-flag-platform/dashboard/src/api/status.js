export async function fetchStatus() {
  const response = await fetch("/api/status");
  if (!response.ok) {
    throw new Error(`Failed to fetch status: ${response.status}`);
  }
  return response.json();
}
