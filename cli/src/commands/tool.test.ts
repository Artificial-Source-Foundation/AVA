/**
 * Tests for the tool command
 */

import { getToolDefinitions } from '@ava/core'
import { beforeEach, describe, expect, it, vi } from 'vitest'

// Mock platform
vi.mock('@ava/platform-node', () => ({
  createNodePlatform: () => ({
    fs: {},
    shell: {},
    credentials: {
      get: vi.fn(async () => null),
      set: vi.fn(async () => {}),
      delete: vi.fn(async () => {}),
      has: vi.fn(async () => false),
    },
    database: {
      open: vi.fn(),
      close: vi.fn(),
    },
  }),
}))

describe('tool command', () => {
  beforeEach(() => {
    vi.resetModules()
  })

  describe('tool registry', () => {
    it('should have tools registered after importing @ava/core', () => {
      const definitions = getToolDefinitions()
      expect(definitions.length).toBeGreaterThan(0)
    })

    it('should include core tools', () => {
      const definitions = getToolDefinitions()
      const names = definitions.map((d) => d.name)

      expect(names).toContain('read_file')
      expect(names).toContain('glob')
      expect(names).toContain('grep')
      expect(names).toContain('bash')
      expect(names).toContain('edit')
      expect(names).toContain('attempt_completion')
    })

    it('should have valid tool definitions', () => {
      const definitions = getToolDefinitions()

      for (const def of definitions) {
        expect(def.name).toBeTruthy()
        expect(def.description).toBeTruthy()
        expect(def.input_schema).toBeDefined()
        expect(def.input_schema.type).toBe('object')
      }
    })

    it('should register 22+ tools', () => {
      const definitions = getToolDefinitions()
      // 22 tools registered in tools/index.ts
      expect(definitions.length).toBeGreaterThanOrEqual(22)
    })
  })
})
