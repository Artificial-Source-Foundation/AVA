export type PraxisMode = 'full' | 'light' | 'solo'

export interface ModeConfig {
  mode: PraxisMode
  chain: string[]
  codingEnabled: boolean
}

const FULL_PATTERNS = [
  'sprint',
  'refactor entire',
  'redesign',
  'implement all',
  'multi-file',
  'architecture',
]
const LIGHT_PATTERNS = ['fix', 'add', 'update', 'change', 'modify', 'refactor ']
const SOLO_PATTERNS = ['explain', 'research', 'plan', 'analyze', 'document', 'review', 'what is']

export function detectMode(goal: string): PraxisMode {
  const lower = goal.toLowerCase()
  if (FULL_PATTERNS.some((pattern) => lower.includes(pattern))) return 'full'
  if (SOLO_PATTERNS.some((pattern) => lower.includes(pattern))) return 'solo'
  if (LIGHT_PATTERNS.some((pattern) => lower.includes(pattern))) return 'light'
  return 'light'
}

export function getModeConfig(mode: PraxisMode): ModeConfig {
  switch (mode) {
    case 'full':
      return {
        mode,
        chain: ['director', 'tech-lead', 'engineer', 'reviewer'],
        codingEnabled: true,
      }
    case 'light':
      return {
        mode,
        chain: ['director', 'engineer', 'reviewer'],
        codingEnabled: true,
      }
    case 'solo':
      return {
        mode,
        chain: ['director', 'subagent'],
        codingEnabled: false,
      }
  }
}

export function resolveModeFromSlash(arg: string, goal: string): PraxisMode {
  const value = arg.trim().toLowerCase()
  if (value === 'full' || value === 'light' || value === 'solo') return value
  return detectMode(goal)
}
