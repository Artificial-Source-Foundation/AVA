/**
 * Tests for Delta9 Support Agents (Delta Team)
 *
 * Tests the 8 support agents with military codenames:
 * RECON, SIGINT, TACCOM, SURGEON, SENTINEL, SCRIBE, FACADE, SPECTRE
 */

import { describe, it, expect, beforeEach, vi } from 'vitest'
import {
  // Profiles
  RECON_PROFILE,
  SIGINT_PROFILE,
  TACCOM_PROFILE,
  SURGEON_PROFILE,
  SENTINEL_PROFILE,
  SCRIBE_PROFILE,
  FACADE_PROFILE,
  SPECTRE_PROFILE,
  // Factories
  createReconAgent,
  createSigintAgent,
  createTaccomAgent,
  createSurgeonAgent,
  createSentinelAgent,
  createScribeAgent,
  createFacadeAgent,
  createSpectreAgent,
  // Registry
  supportAgentFactories,
  supportProfiles,
  supportConfigs,
  codenameToConfigKey,
  configKeyToCodename,
  createSupportAgent,
  createSupportAgentByConfigKey,
  listSupportAgents,
  isSupportAgentAvailable,
  getSupportAgentProfile,
  type SupportAgentName,
  type SupportAgentConfigKey,
} from '../../src/agents/support/index.js'

// Mock the models module to avoid needing actual config
vi.mock('../../src/lib/models.js', () => ({
  getSupportAgentModel: vi.fn((cwd: string, agentType: string) => {
    const modelMap: Record<string, string> = {
      scout: 'anthropic/claude-haiku-4',
      intel: 'anthropic/claude-sonnet-4',
      strategist: 'openai/gpt-4o',
      patcher: 'anthropic/claude-haiku-4',
      qa: 'anthropic/claude-sonnet-4',
      scribe: 'google/gemini-2.0-flash',
      uiOps: 'google/gemini-2.0-flash',
      optics: 'google/gemini-2.0-flash',
    }
    return modelMap[agentType] ?? 'anthropic/claude-sonnet-4'
  }),
  getModelForRole: vi.fn((cwd: string, role: string) => {
    if (role === 'patcher') return 'anthropic/claude-haiku-4'
    return 'anthropic/claude-sonnet-4'
  }),
}))

describe('Delta Team Support Agents', () => {
  const testCwd = '/test/project'

  // =========================================================================
  // Agent Profiles
  // =========================================================================

  describe('Agent Profiles', () => {
    const allProfiles = [
      { profile: RECON_PROFILE, codename: 'RECON', role: 'Reconnaissance Agent' },
      { profile: SIGINT_PROFILE, codename: 'SIGINT', role: 'Intelligence Research Agent' },
      { profile: TACCOM_PROFILE, codename: 'TACCOM', role: 'Tactical Command Advisor' },
      { profile: SURGEON_PROFILE, codename: 'SURGEON', role: 'Surgical Precision Fixer' },
      { profile: SENTINEL_PROFILE, codename: 'SENTINEL', role: 'Quality Assurance Guardian' },
      { profile: SCRIBE_PROFILE, codename: 'SCRIBE', role: 'Documentation Writer' },
      { profile: FACADE_PROFILE, codename: 'FACADE', role: 'Frontend Operations Specialist' },
      { profile: SPECTRE_PROFILE, codename: 'SPECTRE', role: 'Visual Intelligence Analyst' },
    ]

    it.each(allProfiles)('$codename has correct profile structure', ({ profile, codename, role }) => {
      expect(profile.codename).toBe(codename)
      expect(profile.role).toBe(role)
      expect(profile.temperature).toBeGreaterThanOrEqual(0)
      expect(profile.temperature).toBeLessThanOrEqual(1)
      expect(profile.specialty).toBeDefined()
      expect(Array.isArray(profile.traits)).toBe(true)
      expect(profile.traits.length).toBeGreaterThanOrEqual(3)
    })

    it('all profiles have unique codenames', () => {
      const codenames = allProfiles.map(p => p.profile.codename)
      const uniqueCodenames = [...new Set(codenames)]
      expect(uniqueCodenames.length).toBe(allProfiles.length)
    })

    it('all profiles have unique specialties', () => {
      const specialties = allProfiles.map(p => p.profile.specialty)
      const uniqueSpecialties = [...new Set(specialties)]
      expect(uniqueSpecialties.length).toBe(allProfiles.length)
    })
  })

  // =========================================================================
  // Agent Factories
  // =========================================================================

  describe('Agent Factories', () => {
    describe('createReconAgent', () => {
      it('creates agent with correct structure', () => {
        const agent = createReconAgent(testCwd)

        expect(agent.mode).toBe('subagent')
        expect(agent.model).toBe('anthropic/claude-haiku-4')
        expect(agent.temperature).toBe(RECON_PROFILE.temperature)
        expect(agent.prompt).toContain('RECON')
        expect(agent.description).toContain('RECON')
        expect(agent.maxTokens).toBeDefined()
      })

      it('prompt contains reconnaissance instructions', () => {
        const agent = createReconAgent(testCwd)

        expect(agent.prompt).toContain('Reconnaissance')
        expect(agent.prompt).toContain('codebase')
        expect(agent.prompt).toContain('JSON')
      })
    })

    describe('createSigintAgent', () => {
      it('creates agent with correct structure', () => {
        const agent = createSigintAgent(testCwd)

        expect(agent.mode).toBe('subagent')
        expect(agent.model).toBe('anthropic/claude-sonnet-4')
        expect(agent.temperature).toBe(SIGINT_PROFILE.temperature)
        expect(agent.prompt).toContain('SIGINT')
        expect(agent.deniedTools).toBeDefined()
      })

      it('has research-focused denied tools', () => {
        const agent = createSigintAgent(testCwd)

        expect(agent.deniedTools).toContain('Write')
        expect(agent.deniedTools).toContain('Edit')
        expect(agent.deniedTools).toContain('Task')
      })

      it('prompt contains research phases', () => {
        const agent = createSigintAgent(testCwd)

        expect(agent.prompt).toContain('PHASE 0')
        expect(agent.prompt).toContain('PHASE 1')
        expect(agent.prompt).toContain('PHASE 2')
        expect(agent.prompt).toContain('Evidence')
      })
    })

    describe('createTaccomAgent', () => {
      it('creates agent with advisory focus', () => {
        const agent = createTaccomAgent(testCwd)

        expect(agent.mode).toBe('subagent')
        expect(agent.model).toBe('openai/gpt-4o')
        expect(agent.prompt).toContain('TACCOM')
        expect(agent.prompt).toContain('Tactical')
        expect(agent.deniedTools).toContain('Bash')
      })

      it('prompt references other Delta agents', () => {
        const agent = createTaccomAgent(testCwd)

        expect(agent.prompt).toContain('RECON')
        expect(agent.prompt).toContain('SIGINT')
        expect(agent.prompt).toContain('SURGEON')
        expect(agent.prompt).toContain('SENTINEL')
      })
    })

    describe('createSurgeonAgent', () => {
      it('creates agent optimized for quick fixes', () => {
        const agent = createSurgeonAgent(testCwd)

        expect(agent.mode).toBe('subagent')
        expect(agent.model).toBe('anthropic/claude-haiku-4')
        expect(agent.temperature).toBe(0.1) // Very low for precision
        expect(agent.maxTokens).toBe(1024) // Concise responses
        expect(agent.prompt).toContain('SURGEON')
        expect(agent.prompt).toContain('Surgical')
      })
    })

    describe('createSentinelAgent', () => {
      it('creates agent for quality assurance', () => {
        const agent = createSentinelAgent(testCwd)

        expect(agent.mode).toBe('subagent')
        expect(agent.model).toBe('anthropic/claude-sonnet-4')
        expect(agent.prompt).toContain('SENTINEL')
        expect(agent.prompt).toContain('Quality')
        expect(agent.prompt).toContain('test')
      })

      it('prompt covers multiple test frameworks', () => {
        const agent = createSentinelAgent(testCwd)

        expect(agent.prompt).toContain('Jest')
        expect(agent.prompt).toContain('Vitest')
        expect(agent.prompt).toContain('Playwright')
      })
    })

    describe('createScribeAgent', () => {
      it('creates agent for documentation', () => {
        const agent = createScribeAgent(testCwd)

        expect(agent.mode).toBe('subagent')
        expect(agent.model).toBe('google/gemini-2.0-flash')
        expect(agent.prompt).toContain('SCRIBE')
        expect(agent.prompt).toContain('Documentation')
      })

      it('prompt covers doc formats', () => {
        const agent = createScribeAgent(testCwd)

        expect(agent.prompt).toContain('README')
        expect(agent.prompt).toContain('JSDoc')
        expect(agent.prompt).toContain('API')
      })
    })

    describe('createFacadeAgent', () => {
      it('creates agent for frontend work', () => {
        const agent = createFacadeAgent(testCwd)

        expect(agent.mode).toBe('subagent')
        expect(agent.model).toBe('google/gemini-2.0-flash')
        expect(agent.prompt).toContain('FACADE')
        expect(agent.prompt).toContain('Frontend')
      })

      it('prompt covers frontend frameworks', () => {
        const agent = createFacadeAgent(testCwd)

        expect(agent.prompt).toContain('React')
        expect(agent.prompt).toContain('Vue')
        expect(agent.prompt).toContain('Tailwind')
        expect(agent.prompt).toContain('accessibility')
      })
    })

    describe('createSpectreAgent', () => {
      it('creates agent for visual analysis', () => {
        const agent = createSpectreAgent(testCwd)

        expect(agent.mode).toBe('subagent')
        expect(agent.model).toBe('google/gemini-2.0-flash')
        expect(agent.prompt).toContain('SPECTRE')
        expect(agent.prompt).toContain('Visual')
      })

      it('prompt covers visual capabilities', () => {
        const agent = createSpectreAgent(testCwd)

        expect(agent.prompt).toContain('screenshot')
        expect(agent.prompt).toContain('diagram')
        expect(agent.prompt).toContain('PDF')
        expect(agent.prompt).toContain('image')
      })
    })
  })

  // =========================================================================
  // Registry
  // =========================================================================

  describe('Registry', () => {
    describe('supportAgentFactories', () => {
      it('has all 8 Delta Team agents', () => {
        const agentNames = Object.keys(supportAgentFactories)

        expect(agentNames).toHaveLength(8)
        expect(agentNames).toContain('RECON')
        expect(agentNames).toContain('SIGINT')
        expect(agentNames).toContain('TACCOM')
        expect(agentNames).toContain('SURGEON')
        expect(agentNames).toContain('SENTINEL')
        expect(agentNames).toContain('SCRIBE')
        expect(agentNames).toContain('FACADE')
        expect(agentNames).toContain('SPECTRE')
      })

      it('all factories are callable functions', () => {
        for (const [name, factory] of Object.entries(supportAgentFactories)) {
          expect(typeof factory).toBe('function')
          const agent = factory(testCwd)
          expect(agent).toBeDefined()
          expect(agent.mode).toBe('subagent')
        }
      })
    })

    describe('supportProfiles', () => {
      it('has profile for each agent', () => {
        expect(Object.keys(supportProfiles)).toHaveLength(8)
        expect(supportProfiles.RECON).toBe(RECON_PROFILE)
        expect(supportProfiles.SPECTRE).toBe(SPECTRE_PROFILE)
      })
    })

    describe('supportConfigs', () => {
      it('has config for each agent', () => {
        expect(Object.keys(supportConfigs)).toHaveLength(8)

        for (const config of Object.values(supportConfigs)) {
          expect(config.name).toBeDefined()
          expect(config.role).toBeDefined()
          expect(config.configKey).toBeDefined()
          expect(config.enabled).toBe(true)
          expect(config.timeoutSeconds).toBeGreaterThan(0)
        }
      })
    })

    describe('codenameToConfigKey', () => {
      it('maps all codenames to config keys', () => {
        expect(codenameToConfigKey.RECON).toBe('scout')
        expect(codenameToConfigKey.SIGINT).toBe('intel')
        expect(codenameToConfigKey.TACCOM).toBe('strategist')
        expect(codenameToConfigKey.SURGEON).toBe('patcher')
        expect(codenameToConfigKey.SENTINEL).toBe('qa')
        expect(codenameToConfigKey.SCRIBE).toBe('scribe')
        expect(codenameToConfigKey.FACADE).toBe('uiOps')
        expect(codenameToConfigKey.SPECTRE).toBe('optics')
      })
    })

    describe('configKeyToCodename', () => {
      it('maps all config keys to codenames', () => {
        expect(configKeyToCodename.scout).toBe('RECON')
        expect(configKeyToCodename.intel).toBe('SIGINT')
        expect(configKeyToCodename.strategist).toBe('TACCOM')
        expect(configKeyToCodename.patcher).toBe('SURGEON')
        expect(configKeyToCodename.qa).toBe('SENTINEL')
        expect(configKeyToCodename.scribe).toBe('SCRIBE')
        expect(configKeyToCodename.uiOps).toBe('FACADE')
        expect(configKeyToCodename.optics).toBe('SPECTRE')
      })

      it('is inverse of codenameToConfigKey', () => {
        for (const [codename, configKey] of Object.entries(codenameToConfigKey)) {
          expect(configKeyToCodename[configKey as SupportAgentConfigKey]).toBe(codename)
        }
      })
    })
  })

  // =========================================================================
  // Helper Functions
  // =========================================================================

  describe('Helper Functions', () => {
    describe('createSupportAgent', () => {
      it('creates agent by codename', () => {
        const agent = createSupportAgent('RECON', testCwd)

        expect(agent).toBeDefined()
        expect(agent.prompt).toContain('RECON')
      })

      it('throws for unknown codename', () => {
        expect(() => createSupportAgent('UNKNOWN' as SupportAgentName, testCwd))
          .toThrow('Unknown support agent')
      })
    })

    describe('createSupportAgentByConfigKey', () => {
      it('creates agent by config key', () => {
        const agent = createSupportAgentByConfigKey('scout', testCwd)

        expect(agent).toBeDefined()
        expect(agent.prompt).toContain('RECON')
      })

      it('maps all config keys correctly', () => {
        const configKeys: SupportAgentConfigKey[] = [
          'scout', 'intel', 'strategist', 'patcher',
          'qa', 'scribe', 'uiOps', 'optics',
        ]

        for (const key of configKeys) {
          const agent = createSupportAgentByConfigKey(key, testCwd)
          expect(agent).toBeDefined()
          expect(agent.mode).toBe('subagent')
        }
      })
    })

    describe('listSupportAgents', () => {
      it('returns all 8 codenames', () => {
        const agents = listSupportAgents()

        expect(agents).toHaveLength(8)
        expect(agents).toContain('RECON')
        expect(agents).toContain('SPECTRE')
      })
    })

    describe('isSupportAgentAvailable', () => {
      it('returns true for valid codenames', () => {
        expect(isSupportAgentAvailable('RECON')).toBe(true)
        expect(isSupportAgentAvailable('SIGINT')).toBe(true)
        expect(isSupportAgentAvailable('SPECTRE')).toBe(true)
      })

      it('returns false for invalid codenames', () => {
        expect(isSupportAgentAvailable('UNKNOWN' as SupportAgentName)).toBe(false)
        expect(isSupportAgentAvailable('scout' as SupportAgentName)).toBe(false)
      })
    })

    describe('getSupportAgentProfile', () => {
      it('returns correct profile', () => {
        expect(getSupportAgentProfile('RECON')).toBe(RECON_PROFILE)
        expect(getSupportAgentProfile('SPECTRE')).toBe(SPECTRE_PROFILE)
      })
    })
  })

  // =========================================================================
  // Prompt Quality
  // =========================================================================

  describe('Prompt Quality', () => {
    const allFactories = [
      { name: 'RECON', factory: createReconAgent },
      { name: 'SIGINT', factory: createSigintAgent },
      { name: 'TACCOM', factory: createTaccomAgent },
      { name: 'SURGEON', factory: createSurgeonAgent },
      { name: 'SENTINEL', factory: createSentinelAgent },
      { name: 'SCRIBE', factory: createScribeAgent },
      { name: 'FACADE', factory: createFacadeAgent },
      { name: 'SPECTRE', factory: createSpectreAgent },
    ]

    it.each(allFactories)('$name prompt identifies agent by codename', ({ name, factory }) => {
      const agent = factory(testCwd)
      // Agent prompts should identify themselves with their codename (header or intro)
      const hasHeader = agent.prompt!.includes(`# ${name}`)
      const hasIntro = agent.prompt!.includes(`You are ${name}`)
      expect(hasHeader || hasIntro).toBe(true)
    })

    it.each(allFactories)('$name prompt defines mission or role', ({ factory }) => {
      const agent = factory(testCwd)
      // Agents should explain their purpose (mission, role, or identity)
      expect(agent.prompt).toMatch(/YOUR MISSION|Your Identity|Your Role|You are/i)
    })

    it.each(allFactories)('$name prompt contains response format', ({ factory }) => {
      const agent = factory(testCwd)
      expect(agent.prompt).toMatch(/JSON|Response|Format|Output/i)
    })

    it.each(allFactories)('$name prompt has rules or remember section', ({ factory }) => {
      const agent = factory(testCwd)
      // Agents should have behavioral guidelines (rules, remember, or always/never)
      expect(agent.prompt).toMatch(/Remember|CRITICAL RULES|ALWAYS|NEVER/i)
    })

    it.each(allFactories)('$name prompt is substantial', ({ factory }) => {
      const agent = factory(testCwd)
      expect(agent.prompt!.length).toBeGreaterThan(1000)
    })
  })
})
