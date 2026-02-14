import { STORAGE_KEYS } from '../config/constants'

type LastSessionByProjectMap = Record<string, string>

function readLastSessionByProjectMap(): LastSessionByProjectMap {
  const raw = localStorage.getItem(STORAGE_KEYS.LAST_SESSION_BY_PROJECT)
  if (!raw) {
    return {}
  }

  try {
    const parsed: unknown = JSON.parse(raw)
    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
      return {}
    }

    const entries = Object.entries(parsed)
    const validEntries = entries.filter(
      ([projectId, sessionId]) => typeof projectId === 'string' && typeof sessionId === 'string'
    )

    return Object.fromEntries(validEntries)
  } catch {
    return {}
  }
}

export function setLastSessionForProject(
  projectId: string | null | undefined,
  sessionId: string
): void {
  if (!projectId || !sessionId) {
    return
  }

  const current = readLastSessionByProjectMap()
  current[projectId] = sessionId
  localStorage.setItem(STORAGE_KEYS.LAST_SESSION_BY_PROJECT, JSON.stringify(current))
}

export function getLastSessionForProject(projectId: string | null | undefined): string | null {
  if (!projectId) {
    return null
  }

  const current = readLastSessionByProjectMap()
  return current[projectId] ?? null
}
