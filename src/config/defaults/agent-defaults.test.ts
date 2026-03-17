import { describe, expect, it } from 'vitest'
import {
  AGENT_ICONS,
  defaultAgentPresets,
  legacyAgentPresets,
  resolveAgentIcon,
} from './agent-defaults'
import { praxisAgentPresets } from './praxis-presets'

// ============================================================================
// AGENT_ICONS registry
// ============================================================================

describe('AGENT_ICONS registry', () => {
  it('contains all expected icon names', () => {
    const expected = [
      'Code',
      'Compass',
      'Crown',
      'Layout',
      'Server',
      'Shield',
      'Layers',
      'TestTube',
      'Eye',
      'Search',
      'Bug',
      'Building',
      'ListTodo',
      'Rocket',
      'GitBranch',
      'Terminal',
      'FileText',
      'Zap',
    ]
    for (const name of expected) {
      expect(AGENT_ICONS[name]).toBeDefined()
    }
  })

  it('has 18 icons', () => {
    expect(Object.keys(AGENT_ICONS).length).toBe(18)
  })

  it('all values are functions (Solid components)', () => {
    for (const icon of Object.values(AGENT_ICONS)) {
      expect(typeof icon).toBe('function')
    }
  })
})

// ============================================================================
// resolveAgentIcon
// ============================================================================

describe('resolveAgentIcon', () => {
  it('returns the icon component for a known name', () => {
    const icon = resolveAgentIcon('Crown')
    expect(icon).toBe(AGENT_ICONS.Crown)
  })

  it('falls back to Code icon for unknown name', () => {
    const icon = resolveAgentIcon('NonexistentIcon')
    expect(icon).toBe(AGENT_ICONS.Code)
  })

  it('falls back to Code icon for undefined', () => {
    const icon = resolveAgentIcon(undefined)
    expect(icon).toBe(AGENT_ICONS.Code)
  })

  it('falls back to Code icon for empty string', () => {
    // Empty string is falsy, so falls back
    const icon = resolveAgentIcon('')
    expect(icon).toBe(AGENT_ICONS.Code)
  })
})

// ============================================================================
// Praxis agent presets
// ============================================================================

describe('praxisAgentPresets', () => {
  it('contains 14 presets', () => {
    expect(praxisAgentPresets.length).toBe(14)
  })

  it('all presets have unique IDs', () => {
    const ids = praxisAgentPresets.map((p) => p.id)
    expect(new Set(ids).size).toBe(ids.length)
  })

  it('all presets are enabled by default', () => {
    for (const preset of praxisAgentPresets) {
      expect(preset.enabled).toBe(true)
    }
  })

  it('has exactly 1 commander', () => {
    const commanders = praxisAgentPresets.filter((p) => p.tier === 'commander')
    expect(commanders.length).toBe(1)
    expect(commanders[0].id).toBe('commander')
  })

  it('has 4 leads', () => {
    const leads = praxisAgentPresets.filter((p) => p.tier === 'lead')
    expect(leads.length).toBe(4)
  })

  it('has 9 workers', () => {
    const workers = praxisAgentPresets.filter((p) => p.tier === 'worker')
    expect(workers.length).toBe(9)
  })

  it('commander delegates only to leads and planner/architect', () => {
    const commander = praxisAgentPresets.find((p) => p.id === 'commander')!
    expect(commander.delegates).toBeDefined()
    // All delegates should be either leads or the planner/architect workers
    const leadIds = praxisAgentPresets.filter((p) => p.tier === 'lead').map((p) => p.id)
    const specialWorkers = ['planner', 'architect']
    for (const delegateId of commander.delegates!) {
      expect([...leadIds, ...specialWorkers]).toContain(delegateId)
    }
  })

  it('leads delegate to workers', () => {
    const leads = praxisAgentPresets.filter((p) => p.tier === 'lead')
    const workerIds = praxisAgentPresets.filter((p) => p.tier === 'worker').map((p) => p.id)
    for (const lead of leads) {
      expect(lead.delegates).toBeDefined()
      for (const delegateId of lead.delegates!) {
        expect(workerIds).toContain(delegateId)
      }
    }
  })

  it('workers do not have delegates', () => {
    const workers = praxisAgentPresets.filter((p) => p.tier === 'worker')
    for (const worker of workers) {
      expect(worker.delegates).toBeUndefined()
    }
  })

  it('all presets have a domain', () => {
    for (const preset of praxisAgentPresets) {
      expect(preset.domain).toBeDefined()
      expect(typeof preset.domain).toBe('string')
    }
  })

  it('all presets have non-empty capabilities', () => {
    for (const preset of praxisAgentPresets) {
      expect(preset.capabilities.length).toBeGreaterThan(0)
    }
  })
})

// ============================================================================
// Legacy agent presets
// ============================================================================

describe('legacyAgentPresets', () => {
  it('contains 5 presets', () => {
    expect(legacyAgentPresets.length).toBe(5)
  })

  it('has coding, git, terminal, docs, and fast types', () => {
    const types = legacyAgentPresets.map((p) => p.type).sort()
    expect(types).toEqual(['coding', 'docs', 'fast', 'git', 'terminal'])
  })

  it('each preset has an icon component', () => {
    for (const preset of legacyAgentPresets) {
      expect(typeof preset.icon).toBe('function')
    }
  })

  it('some presets specify a model', () => {
    const withModel = legacyAgentPresets.filter((p) => p.model)
    expect(withModel.length).toBeGreaterThan(0)
  })
})

// ============================================================================
// defaultAgentPresets (combined)
// ============================================================================

describe('defaultAgentPresets', () => {
  it('combines praxis and legacy presets', () => {
    expect(defaultAgentPresets.length).toBe(praxisAgentPresets.length + legacyAgentPresets.length)
  })

  it('all IDs are unique across combined set', () => {
    const ids = defaultAgentPresets.map((p) => p.id)
    expect(new Set(ids).size).toBe(ids.length)
  })

  it('praxis presets come first', () => {
    // First item should be the commander
    expect(defaultAgentPresets[0].id).toBe('commander')
    // Last items should be legacy
    const lastFive = defaultAgentPresets.slice(-5)
    expect(lastFive.map((p) => p.id)).toEqual(legacyAgentPresets.map((p) => p.id))
  })

  it('each preset has required fields', () => {
    for (const preset of defaultAgentPresets) {
      expect(preset.id).toBeTruthy()
      expect(preset.name).toBeTruthy()
      expect(preset.description).toBeTruthy()
      expect(typeof preset.icon).toBe('function')
      expect(typeof preset.enabled).toBe('boolean')
      expect(Array.isArray(preset.capabilities)).toBe(true)
    }
  })
})
