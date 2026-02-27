/**
 * Tests for the tool command
 */

import { getToolDefinitions, registerCoreTools } from '@ava/core-v2'
import { resetTools } from '@ava/core-v2/tools'
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
    resetTools()
    registerCoreTools()
  })

  describe('tool registry', () => {
    it('should have tools registered after registerCoreTools()', () => {
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
      expect(names).toContain('write_file')
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

    it('should register 6 core tools', () => {
      const definitions = getToolDefinitions()
      // 6 core tools: read_file, write_file, edit, bash, glob, grep
      expect(definitions.length).toBe(6)
    })
  })
})
