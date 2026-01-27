/**
 * Tests for Delta9 Dynamic Agent Loader
 */

import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import { existsSync, mkdirSync, rmSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import { tmpdir } from 'node:os'
import {
  AgentLoader,
  createAgentLoader,
  clearAgentLoader,
  formatAgentPrompt,
  getAgentTools,
  agentHasTool,
} from '../../src/agents/loader.js'

describe('AgentLoader', () => {
  const testDir = join(tmpdir(), 'delta9-agent-loader-test-' + Date.now())
  const agentsDir = join(testDir, '.delta9', 'agents')
  let loader: AgentLoader

  beforeEach(() => {
    mkdirSync(agentsDir, { recursive: true })
    loader = createAgentLoader({ cwd: testDir })
  })

  afterEach(() => {
    loader.clear()
    clearAgentLoader()
    if (existsSync(testDir)) {
      rmSync(testDir, { recursive: true, force: true })
    }
  })

  describe('loadFromDirectory', () => {
    it('loads agents from directory', () => {
      // Create test agent file
      writeFileSync(
        join(agentsDir, 'test-agent.md'),
        `---
name: test-agent
role: reviewer
model: claude-sonnet-4-20250514
description: A test agent
---

# Test Agent

You are a test agent.
`
      )

      const loaded = loader.loadFromDirectory()

      expect(loaded).toBe(1)
      expect(loader.hasAgent('test-agent')).toBe(true)
    })

    it('handles empty directory', () => {
      const loaded = loader.loadFromDirectory()
      expect(loaded).toBe(0)
    })

    it('handles non-existent directory', () => {
      const emptyLoader = createAgentLoader({ cwd: '/non-existent' })
      const loaded = emptyLoader.loadFromDirectory()
      expect(loaded).toBe(0)
    })
  })

  describe('loadAgent', () => {
    it('parses frontmatter correctly', () => {
      const filePath = join(agentsDir, 'with-frontmatter.md')
      writeFileSync(
        filePath,
        `---
name: my-agent
role: executor
model: claude-sonnet-4-20250514
tools: [read_file, write_file, bash]
constraints:
  - Never delete files
  - Always backup first
description: An agent with full frontmatter
---

# System Prompt

You are a helpful agent.
`
      )

      const agent = loader.loadAgent(filePath)

      expect(agent).not.toBeNull()
      expect(agent?.name).toBe('my-agent')
      expect(agent?.role).toBe('executor')
      expect(agent?.model).toBe('claude-sonnet-4-20250514')
      expect(agent?.tools).toEqual(['read_file', 'write_file', 'bash'])
      expect(agent?.constraints).toEqual(['Never delete files', 'Always backup first'])
      expect(agent?.description).toBe('An agent with full frontmatter')
      expect(agent?.systemPrompt).toContain('You are a helpful agent.')
    })

    it('uses filename as name if not in frontmatter', () => {
      const filePath = join(agentsDir, 'filename-as-name.md')
      writeFileSync(
        filePath,
        `---
role: worker
---

Simple agent.
`
      )

      const agent = loader.loadAgent(filePath)

      expect(agent?.name).toBe('filename-as-name')
    })

    it('handles inline array syntax', () => {
      const filePath = join(agentsDir, 'inline-array.md')
      writeFileSync(
        filePath,
        `---
name: inline-test
tools: [tool1, tool2, tool3]
---

Content.
`
      )

      const agent = loader.loadAgent(filePath)

      expect(agent?.tools).toEqual(['tool1', 'tool2', 'tool3'])
    })

    it('returns null for non-existent file', () => {
      const agent = loader.loadAgent('/non-existent/file.md')
      expect(agent).toBeNull()
    })
  })

  describe('getAgent', () => {
    beforeEach(() => {
      writeFileSync(
        join(agentsDir, 'agent-a.md'),
        `---
name: agent-a
role: worker
---
Agent A content.
`
      )
      loader.loadFromDirectory()
    })

    it('returns agent by name', () => {
      const agent = loader.getAgent('agent-a')
      expect(agent).not.toBeNull()
      expect(agent?.name).toBe('agent-a')
    })

    it('returns null for unknown agent', () => {
      const agent = loader.getAgent('unknown')
      expect(agent).toBeNull()
    })
  })

  describe('getAgentsByRole', () => {
    beforeEach(() => {
      writeFileSync(join(agentsDir, 'reviewer-1.md'), `---\nname: reviewer-1\nrole: reviewer\n---\nContent.`)
      writeFileSync(join(agentsDir, 'reviewer-2.md'), `---\nname: reviewer-2\nrole: reviewer\n---\nContent.`)
      writeFileSync(join(agentsDir, 'executor-1.md'), `---\nname: executor-1\nrole: executor\n---\nContent.`)
      loader.loadFromDirectory()
    })

    it('returns agents filtered by role', () => {
      const reviewers = loader.getAgentsByRole('reviewer')
      expect(reviewers).toHaveLength(2)
      expect(reviewers.map((a) => a.name).sort()).toEqual(['reviewer-1', 'reviewer-2'])
    })

    it('returns empty array for unknown role', () => {
      const agents = loader.getAgentsByRole('unknown-role')
      expect(agents).toEqual([])
    })
  })

  describe('listAgents', () => {
    beforeEach(() => {
      writeFileSync(join(agentsDir, 'agent-x.md'), `---\nname: agent-x\n---\nContent.`)
      writeFileSync(join(agentsDir, 'agent-y.md'), `---\nname: agent-y\n---\nContent.`)
      loader.loadFromDirectory()
    })

    it('lists all loaded agent names', () => {
      const names = loader.listAgents()
      expect(names).toHaveLength(2)
      expect(names).toContain('agent-x')
      expect(names).toContain('agent-y')
    })
  })

  describe('reloadAgent', () => {
    it('reloads a specific agent', () => {
      const filePath = join(agentsDir, 'mutable.md')
      writeFileSync(filePath, `---\nname: mutable\ndescription: Version 1\n---\nV1`)
      loader.loadFromDirectory()

      expect(loader.getAgent('mutable')?.description).toBe('Version 1')

      // Update file
      writeFileSync(filePath, `---\nname: mutable\ndescription: Version 2\n---\nV2`)

      const reloaded = loader.reloadAgent('mutable')

      expect(reloaded?.description).toBe('Version 2')
      expect(loader.getAgent('mutable')?.description).toBe('Version 2')
    })

    it('returns null for unknown agent', () => {
      const result = loader.reloadAgent('unknown')
      expect(result).toBeNull()
    })
  })

  describe('unloadAgent', () => {
    beforeEach(() => {
      writeFileSync(join(agentsDir, 'to-unload.md'), `---\nname: to-unload\n---\nContent.`)
      loader.loadFromDirectory()
    })

    it('unloads an agent', () => {
      expect(loader.hasAgent('to-unload')).toBe(true)

      const result = loader.unloadAgent('to-unload')

      expect(result).toBe(true)
      expect(loader.hasAgent('to-unload')).toBe(false)
    })

    it('returns false for unknown agent', () => {
      const result = loader.unloadAgent('unknown')
      expect(result).toBe(false)
    })
  })

  describe('getStats', () => {
    beforeEach(() => {
      writeFileSync(join(agentsDir, 'r1.md'), `---\nname: r1\nrole: reviewer\n---\nContent.`)
      writeFileSync(join(agentsDir, 'r2.md'), `---\nname: r2\nrole: reviewer\n---\nContent.`)
      writeFileSync(join(agentsDir, 'e1.md'), `---\nname: e1\nrole: executor\n---\nContent.`)
      loader.loadFromDirectory()
    })

    it('returns loader statistics', () => {
      const stats = loader.getStats()

      expect(stats.totalAgents).toBe(3)
      expect(stats.agentsByRole.reviewer).toBe(2)
      expect(stats.agentsByRole.executor).toBe(1)
    })
  })

  describe('ensureAgentsDir', () => {
    it('creates agents directory if not exists', () => {
      const newDir = join(tmpdir(), 'new-agents-dir-' + Date.now())
      const newLoader = createAgentLoader({ cwd: newDir })

      expect(existsSync(join(newDir, '.delta9', 'agents'))).toBe(false)

      newLoader.ensureAgentsDir()

      expect(existsSync(join(newDir, '.delta9', 'agents'))).toBe(true)

      // Cleanup
      rmSync(newDir, { recursive: true, force: true })
    })
  })
})

describe('utility functions', () => {
  describe('formatAgentPrompt', () => {
    it('formats agent with constraints', () => {
      const agent = {
        name: 'test',
        role: 'worker',
        systemPrompt: 'You are a helpful agent.',
        constraints: ['Always be polite', 'Never share secrets'],
        filePath: '/test.md',
        loadedAt: Date.now(),
      }

      const formatted = formatAgentPrompt(agent)

      expect(formatted).toContain('You are a helpful agent.')
      expect(formatted).toContain('## Constraints')
      expect(formatted).toContain('- Always be polite')
      expect(formatted).toContain('- Never share secrets')
    })

    it('formats agent without constraints', () => {
      const agent = {
        name: 'test',
        role: 'worker',
        systemPrompt: 'Simple prompt.',
        filePath: '/test.md',
        loadedAt: Date.now(),
      }

      const formatted = formatAgentPrompt(agent)

      expect(formatted).toBe('Simple prompt.')
      expect(formatted).not.toContain('Constraints')
    })
  })

  describe('getAgentTools', () => {
    it('returns agent tools', () => {
      const agent = {
        name: 'test',
        role: 'worker',
        systemPrompt: '',
        tools: ['read', 'write', 'bash'],
        filePath: '/test.md',
        loadedAt: Date.now(),
      }

      expect(getAgentTools(agent)).toEqual(['read', 'write', 'bash'])
    })

    it('returns empty array if no tools', () => {
      const agent = {
        name: 'test',
        role: 'worker',
        systemPrompt: '',
        filePath: '/test.md',
        loadedAt: Date.now(),
      }

      expect(getAgentTools(agent)).toEqual([])
    })
  })

  describe('agentHasTool', () => {
    it('returns true if agent has tool', () => {
      const agent = {
        name: 'test',
        role: 'worker',
        systemPrompt: '',
        tools: ['read', 'write'],
        filePath: '/test.md',
        loadedAt: Date.now(),
      }

      expect(agentHasTool(agent, 'read')).toBe(true)
    })

    it('returns false if agent does not have tool', () => {
      const agent = {
        name: 'test',
        role: 'worker',
        systemPrompt: '',
        tools: ['read', 'write'],
        filePath: '/test.md',
        loadedAt: Date.now(),
      }

      expect(agentHasTool(agent, 'bash')).toBe(false)
    })

    it('returns true for any tool if no restrictions', () => {
      const agent = {
        name: 'test',
        role: 'worker',
        systemPrompt: '',
        filePath: '/test.md',
        loadedAt: Date.now(),
      }

      expect(agentHasTool(agent, 'any_tool')).toBe(true)
    })
  })
})
