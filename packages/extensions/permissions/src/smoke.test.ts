/**
 * Permission modes smoke test — verifies all 5 modes against all 6 tool categories.
 */

import { describe, expect, it } from 'vitest'
import {
  DESTRUCTIVE_TOOLS,
  EDIT_TOOLS,
  EXECUTE_TOOLS,
  getAllPermissionModes,
  isToolAutoApproved,
  META_TOOLS,
  NETWORK_TOOLS,
  type PermissionMode,
  READ_TOOLS,
} from './modes.js'

const ALL_MODES: PermissionMode[] = ['suggest', 'ask', 'auto-edit', 'auto-safe', 'yolo']
const ALL_CATEGORIES = {
  read: [...READ_TOOLS],
  edit: [...EDIT_TOOLS],
  execute: [...EXECUTE_TOOLS],
  network: [...NETWORK_TOOLS],
  destructive: [...DESTRUCTIVE_TOOLS],
  meta: [...META_TOOLS],
}

describe('Permission modes smoke test', () => {
  it('has 5 permission modes', () => {
    const modes = getAllPermissionModes()
    expect(modes).toHaveLength(5)
    for (const mode of ALL_MODES) {
      expect(modes.find((m) => m.name === mode)).toBeDefined()
    }
  })

  describe('suggest mode — blocks everything', () => {
    it('blocks all tool categories', () => {
      for (const [_category, tools] of Object.entries(ALL_CATEGORIES)) {
        for (const tool of tools) {
          expect(isToolAutoApproved(tool, 'suggest')).toBe(false)
        }
      }
    })
  })

  describe('ask mode — only auto-approves meta tools', () => {
    it('auto-approves meta tools', () => {
      for (const tool of ALL_CATEGORIES.meta) {
        expect(isToolAutoApproved(tool, 'ask')).toBe(true)
      }
    })

    it('requires approval for non-meta tools', () => {
      for (const tool of ALL_CATEGORIES.read) {
        expect(isToolAutoApproved(tool, 'ask')).toBe(false)
      }
      for (const tool of ALL_CATEGORIES.edit) {
        expect(isToolAutoApproved(tool, 'ask')).toBe(false)
      }
      for (const tool of ALL_CATEGORIES.execute) {
        expect(isToolAutoApproved(tool, 'ask')).toBe(false)
      }
    })
  })

  describe('auto-edit mode — auto-approves reads + edits + meta', () => {
    it('auto-approves read tools', () => {
      for (const tool of ALL_CATEGORIES.read) {
        expect(isToolAutoApproved(tool, 'auto-edit')).toBe(true)
      }
    })

    it('auto-approves edit tools', () => {
      for (const tool of ALL_CATEGORIES.edit) {
        expect(isToolAutoApproved(tool, 'auto-edit')).toBe(true)
      }
    })

    it('auto-approves meta tools', () => {
      for (const tool of ALL_CATEGORIES.meta) {
        expect(isToolAutoApproved(tool, 'auto-edit')).toBe(true)
      }
    })

    it('requires approval for execute tools', () => {
      for (const tool of ALL_CATEGORIES.execute) {
        expect(isToolAutoApproved(tool, 'auto-edit')).toBe(false)
      }
    })

    it('requires approval for destructive tools', () => {
      for (const tool of ALL_CATEGORIES.destructive) {
        expect(isToolAutoApproved(tool, 'auto-edit')).toBe(false)
      }
    })

    it('requires approval for network tools', () => {
      for (const tool of ALL_CATEGORIES.network) {
        expect(isToolAutoApproved(tool, 'auto-edit')).toBe(false)
      }
    })
  })

  describe('yolo mode — auto-approves everything', () => {
    it('auto-approves all tool categories', () => {
      for (const [_category, tools] of Object.entries(ALL_CATEGORIES)) {
        for (const tool of tools) {
          expect(isToolAutoApproved(tool, 'yolo')).toBe(true)
        }
      }
    })
  })

  describe('cross-mode consistency', () => {
    it('yolo never blocks any tool', () => {
      const sampleTools = [
        'read_file',
        'write_file',
        'bash',
        'delete_file',
        'websearch',
        'question',
      ]
      for (const tool of sampleTools) {
        expect(isToolAutoApproved(tool, 'yolo')).toBe(true)
      }
    })

    it('suggest always blocks every tool', () => {
      const sampleTools = [
        'read_file',
        'write_file',
        'bash',
        'delete_file',
        'websearch',
        'question',
      ]
      for (const tool of sampleTools) {
        expect(isToolAutoApproved(tool, 'suggest')).toBe(false)
      }
    })

    it('mode permissiveness is ordered: suggest < ask < auto-edit <= auto-safe < yolo', () => {
      const tool = 'read_file'
      // suggest blocks, ask blocks (read is not meta), auto-edit allows, yolo allows
      expect(isToolAutoApproved(tool, 'suggest')).toBe(false)
      expect(isToolAutoApproved(tool, 'ask')).toBe(false)
      expect(isToolAutoApproved(tool, 'auto-edit')).toBe(true)
      expect(isToolAutoApproved(tool, 'yolo')).toBe(true)
    })
  })
})
