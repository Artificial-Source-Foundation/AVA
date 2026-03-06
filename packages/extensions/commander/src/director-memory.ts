export interface DirectorMemoryEntry {
  id: string
  text: string
  score?: number
}

export interface DirectorMemoryContext {
  goal: string
  cwd: string
}

export interface DirectorMemoryDeps {
  search?: (query: string, limit: number) => Promise<DirectorMemoryEntry[]>
  recent?: (limit: number) => Promise<DirectorMemoryEntry[]>
}

export async function loadDirectorMemory(
  context: DirectorMemoryContext,
  deps: DirectorMemoryDeps = {}
): Promise<DirectorMemoryEntry[]> {
  const search = deps.search
  const recent = deps.recent
  if (!search || !recent) return []

  const [byCwd, byGoal, latest] = await Promise.all([
    search(context.cwd, 5),
    search(context.goal, 5),
    recent(5),
  ])

  const merged = [...byCwd, ...byGoal, ...latest]
  const seen = new Set<string>()
  const unique: DirectorMemoryEntry[] = []
  for (const entry of merged) {
    if (seen.has(entry.id)) continue
    seen.add(entry.id)
    unique.push(entry)
    if (unique.length >= 5) break
  }
  return unique
}

export function buildDirectorMemoryPrompt(memories: DirectorMemoryEntry[]): string {
  if (memories.length === 0) return ''
  const lines = memories.map((m, index) => `${index + 1}. ${m.text}`)
  return `Previous relevant memories:\n${lines.join('\n')}`
}
