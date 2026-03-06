/**
 * Ollama API helpers — fetch, pull, delete models
 */

// ============================================================================
// Types
// ============================================================================

export interface OllamaModel {
  name: string
  size: number
  family: string
  parameterSize: string
  quantizationLevel: string
  modifiedAt: string
}

// ============================================================================
// Helpers
// ============================================================================

export function formatBytes(bytes: number): string {
  if (bytes < 1_000_000) return `${(bytes / 1_000).toFixed(1)} KB`
  if (bytes < 1_000_000_000) return `${(bytes / 1_000_000).toFixed(1)} MB`
  return `${(bytes / 1_000_000_000).toFixed(2)} GB`
}

// ============================================================================
// API
// ============================================================================

interface OllamaTagsResponse {
  models: Array<{
    name: string
    size: number
    details: {
      family: string
      parameter_size: string
      quantization_level: string
    }
    modified_at: string
  }>
}

export async function fetchOllamaModels(baseUrl: string): Promise<OllamaModel[]> {
  const res = await fetch(`${baseUrl}/api/tags`)
  if (!res.ok) throw new Error(`HTTP ${res.status}`)
  const data = (await res.json()) as OllamaTagsResponse
  return (data.models ?? []).map((m) => ({
    name: m.name,
    size: m.size,
    family: m.details?.family ?? 'unknown',
    parameterSize: m.details?.parameter_size ?? '',
    quantizationLevel: m.details?.quantization_level ?? '',
    modifiedAt: m.modified_at,
  }))
}

export async function pullOllamaModel(baseUrl: string, name: string): Promise<void> {
  const res = await fetch(`${baseUrl}/api/pull`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ name, stream: false }),
  })
  if (!res.ok) {
    const body = await res.text()
    throw new Error(body || `HTTP ${res.status}`)
  }
}

export async function deleteOllamaModel(baseUrl: string, name: string): Promise<void> {
  const res = await fetch(`${baseUrl}/api/delete`, {
    method: 'DELETE',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ name }),
  })
  if (!res.ok) throw new Error(`HTTP ${res.status}`)
}
