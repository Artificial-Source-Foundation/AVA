import { describe, expect, it } from 'vitest'
import {
  AGENT_ICONS,
  defaultAgentPresets,
  legacyAgentPresets,
  resolveAgentIcon,
} from './agent-defaults'

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
  it('contains only the core legacy presets', () => {
    expect(defaultAgentPresets.length).toBe(legacyAgentPresets.length)
  })

  it('all IDs are unique across combined set', () => {
    const ids = defaultAgentPresets.map((p) => p.id)
    expect(new Set(ids).size).toBe(ids.length)
  })

  it('matches the legacy preset order exactly', () => {
    expect(defaultAgentPresets.map((p) => p.id)).toEqual(legacyAgentPresets.map((p) => p.id))
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
