/**
 * Tests for Delta9 Support Agents (Delta Team)
 *
 * Consolidated tests using representative sampling.
 * Verifies key behaviors for 8 agents: RECON, SIGINT, TACCOM, SURGEON, SENTINEL, SCRIBE, FACADE, SPECTRE
 */

import { describe, it, expect, vi } from 'vitest'
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

// Mock the models module
vi.mock('../../src/lib/models.js', () => ({
  getSupportAgentModel: vi.fn((cwd: string, agentType: string) => {
    const modelMap: Record<string, string> = {
      scout: 'anthropic/claude-haiku-4',
      intel: 'anthropic/claude-sonnet-4-5',
      strategist: 'openai/gpt-4o',
      patcher: 'anthropic/claude-haiku-4',
      qa: 'anthropic/claude-sonnet-4-5',
      scribe: 'google/gemini-2.0-flash',
      uiOps: 'google/gemini-2.0-flash',
      optics: 'google/gemini-2.0-flash',
    }
    return modelMap[agentType] ?? 'anthropic/claude-sonnet-4-5'
  }),
  getModelForRole: vi.fn((cwd: string, role: string) => {
    if (role === 'patcher') return 'anthropic/claude-haiku-4'
    return 'anthropic/claude-sonnet-4-5'
  }),
}))

describe('Delta Team Support Agents', () => {
  const testCwd = '/test/project'

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

  const allFactories = [
    { name: 'RECON', factory: createReconAgent, profile: RECON_PROFILE },
    { name: 'SIGINT', factory: createSigintAgent, profile: SIGINT_PROFILE },
    { name: 'TACCOM', factory: createTaccomAgent, profile: TACCOM_PROFILE },
    { name: 'SURGEON', factory: createSurgeonAgent, profile: SURGEON_PROFILE },
    { name: 'SENTINEL', factory: createSentinelAgent, profile: SENTINEL_PROFILE },
    { name: 'SCRIBE', factory: createScribeAgent, profile: SCRIBE_PROFILE },
    { name: 'FACADE', factory: createFacadeAgent, profile: FACADE_PROFILE },
    { name: 'SPECTRE', factory: createSpectreAgent, profile: SPECTRE_PROFILE },
  ]

  describe('Agent Profiles', () => {
    it('all 8 profiles have correct structure', () => {
      expect(allProfiles).toHaveLength(8)
      for (const { profile, codename, role } of allProfiles) {
        expect(profile.codename).toBe(codename)
        expect(profile.role).toBe(role)
        expect(profile.temperature).toBeGreaterThanOrEqual(0)
        expect(profile.temperature).toBeLessThanOrEqual(1)
        expect(profile.specialty).toBeDefined()
        expect(profile.traits.length).toBeGreaterThanOrEqual(3)
      }
    })

    it('all profiles have unique codenames and specialties', () => {
      const codenames = allProfiles.map((p) => p.profile.codename)
      const specialties = allProfiles.map((p) => p.profile.specialty)
      expect([...new Set(codenames)]).toHaveLength(8)
      expect([...new Set(specialties)]).toHaveLength(8)
    })
  })

  describe('Agent Factories', () => {
    it('all factories create valid subagents', () => {
      for (const { name, factory, profile } of allFactories) {
        const agent = factory(testCwd)
        expect(agent.mode).toBe('subagent')
        expect(agent.temperature).toBe(profile.temperature)
        expect(agent.prompt).toContain(name)
        expect(agent.description).toContain(name)
        expect(agent.maxTokens).toBeDefined()
      }
    })

    it('SIGINT has research-focused denied tools', () => {
      const agent = createSigintAgent(testCwd)
      expect(agent.deniedTools).toContain('Write')
      expect(agent.deniedTools).toContain('Edit')
      expect(agent.prompt).toContain('PHASE 0')
    })

    it('TACCOM references other Delta agents', () => {
      const agent = createTaccomAgent(testCwd)
      expect(agent.prompt).toContain('RECON')
      expect(agent.prompt).toContain('SURGEON')
      expect(agent.deniedTools).toContain('Bash')
    })

    it('SURGEON is optimized for quick fixes', () => {
      const agent = createSurgeonAgent(testCwd)
      expect(agent.temperature).toBe(0.1)
      expect(agent.maxTokens).toBe(1024)
    })

    it('SENTINEL covers multiple test frameworks', () => {
      const agent = createSentinelAgent(testCwd)
      expect(agent.prompt).toContain('Jest')
      expect(agent.prompt).toContain('Vitest')
    })

    it('FACADE covers frontend frameworks', () => {
      const agent = createFacadeAgent(testCwd)
      expect(agent.prompt).toContain('React')
      expect(agent.prompt).toContain('Tailwind')
    })

    it('SPECTRE covers visual capabilities', () => {
      const agent = createSpectreAgent(testCwd)
      expect(agent.prompt).toContain('screenshot')
      expect(agent.prompt).toContain('diagram')
    })
  })

  describe('Registry', () => {
    it('supportAgentFactories has all 8 agents', () => {
      const names = Object.keys(supportAgentFactories)
      expect(names).toHaveLength(8)
      expect(names).toContain('RECON')
      expect(names).toContain('SPECTRE')
    })

    it('supportProfiles and supportConfigs have all 8 agents', () => {
      expect(Object.keys(supportProfiles)).toHaveLength(8)
      expect(Object.keys(supportConfigs)).toHaveLength(8)
      for (const config of Object.values(supportConfigs)) {
        expect(config.enabled).toBe(true)
        expect(config.timeoutSeconds).toBeGreaterThan(0)
      }
    })

    it('codename/configKey mappings are bidirectional', () => {
      expect(codenameToConfigKey.RECON).toBe('scout')
      expect(configKeyToCodename.scout).toBe('RECON')
      for (const [codename, configKey] of Object.entries(codenameToConfigKey)) {
        expect(configKeyToCodename[configKey as SupportAgentConfigKey]).toBe(codename)
      }
    })
  })

  describe('Helper Functions', () => {
    it('createSupportAgent works by codename', () => {
      const agent = createSupportAgent('RECON', testCwd)
      expect(agent.prompt).toContain('RECON')
      expect(() => createSupportAgent('UNKNOWN' as SupportAgentName, testCwd)).toThrow('Unknown support agent')
    })

    it('createSupportAgentByConfigKey maps all config keys', () => {
      const configKeys: SupportAgentConfigKey[] = ['scout', 'intel', 'strategist', 'patcher', 'qa', 'scribe', 'uiOps', 'optics']
      for (const key of configKeys) {
        const agent = createSupportAgentByConfigKey(key, testCwd)
        expect(agent.mode).toBe('subagent')
      }
    })

    it('listSupportAgents returns all 8 codenames', () => {
      const agents = listSupportAgents()
      expect(agents).toHaveLength(8)
      expect(agents).toContain('RECON')
      expect(agents).toContain('SPECTRE')
    })

    it('isSupportAgentAvailable validates correctly', () => {
      expect(isSupportAgentAvailable('RECON')).toBe(true)
      expect(isSupportAgentAvailable('UNKNOWN' as SupportAgentName)).toBe(false)
    })

    it('getSupportAgentProfile returns correct profile', () => {
      expect(getSupportAgentProfile('RECON')).toBe(RECON_PROFILE)
      expect(getSupportAgentProfile('SPECTRE')).toBe(SPECTRE_PROFILE)
    })
  })

  describe('Prompt Quality', () => {
    it('all prompts identify agent, define role, and have rules', () => {
      for (const { name, factory } of allFactories) {
        const agent = factory(testCwd)
        // Identifies by codename
        const hasHeader = agent.prompt!.includes(`# ${name}`)
        const hasIntro = agent.prompt!.includes(`You are ${name}`)
        expect(hasHeader || hasIntro).toBe(true)
        // Defines role
        expect(agent.prompt).toMatch(/YOUR MISSION|Your Identity|Your Role|You are/i)
        // Has rules
        expect(agent.prompt).toMatch(/Remember|CRITICAL RULES|ALWAYS|NEVER/i)
        // Substantial length
        expect(agent.prompt!.length).toBeGreaterThan(1000)
      }
    })
  })
})
