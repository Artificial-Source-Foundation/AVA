import * as path from 'node:path'
import type { getAgentModes } from '@ava/core-v2/extensions'

type AgentModesMap = ReturnType<typeof getAgentModes>
type SelectAgentMode = (goal: string, availableModes: AgentModesMap) => string | undefined

let selectAgentMode: SelectAgentMode = (_goal, availableModes) => {
  return availableModes.has('praxis') ? undefined : undefined
}

export function resolveAgentMode(goal: string, availableModes: AgentModesMap): string | undefined {
  return selectAgentMode(goal, availableModes)
}

export async function importWithDistFallback(
  sourcePath: string,
  distPath: string
): Promise<Record<string, unknown>> {
  try {
    return (await import(sourcePath)) as Record<string, unknown>
  } catch {
    return (await import(distPath)) as Record<string, unknown>
  }
}

export async function loadAgentModeSelector(extensionsDir: string): Promise<void> {
  try {
    const mod = (await importWithDistFallback(
      path.resolve(extensionsDir, 'agent-modes/src/selector.ts'),
      path.resolve(extensionsDir, 'dist/agent-modes/src/selector.js')
    )) as { selectAgentMode?: SelectAgentMode }
    if (mod.selectAgentMode) {
      selectAgentMode = mod.selectAgentMode
    }
  } catch {
    // Use fallback — no auto-select.
  }
}
