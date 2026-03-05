import { describe, expect, it } from 'vitest'
import {
  DESTRUCTIVE_TOOLS,
  EDIT_TOOLS,
  EXECUTE_TOOLS,
  getAllPermissionModes,
  getPermissionMode,
  isToolAutoApproved,
  META_TOOLS,
  NETWORK_TOOLS,
  PERMISSION_MODES,
  READ_TOOLS,
} from './modes.js'

// ─── Mode Definitions ──────────────────────────────────────────────────────

describe('PERMISSION_MODES', () => {
  it('defines exactly 5 modes', () => {
    expect(Object.keys(PERMISSION_MODES)).toHaveLength(5)
  })

  it('each mode has name, description, autoApprove, requireApproval', () => {
    for (const mode of Object.values(PERMISSION_MODES)) {
      expect(mode.name).toBeTruthy()
      expect(mode.description).toBeTruthy()
      expect(mode.autoApprove).toBeInstanceOf(Set)
      expect(mode.requireApproval).toBeInstanceOf(Set)
    }
  })

  it('suggest mode auto-approves nothing', () => {
    const mode = PERMISSION_MODES.suggest
    expect(mode.autoApprove.size).toBe(0)
    expect(mode.requireApproval.has('*')).toBe(true)
  })

  it('ask mode only auto-approves meta tools', () => {
    const mode = PERMISSION_MODES.ask
    for (const tool of META_TOOLS) {
      expect(mode.autoApprove.has(tool)).toBe(true)
    }
    expect(mode.requireApproval.has('*')).toBe(true)
  })

  it('auto-edit mode auto-approves reads + edits + meta', () => {
    const mode = PERMISSION_MODES['auto-edit']
    for (const tool of READ_TOOLS) {
      expect(mode.autoApprove.has(tool)).toBe(true)
    }
    for (const tool of EDIT_TOOLS) {
      expect(mode.autoApprove.has(tool)).toBe(true)
    }
    for (const tool of META_TOOLS) {
      expect(mode.autoApprove.has(tool)).toBe(true)
    }
  })

  it('auto-edit mode requires approval for execute + destructive + network', () => {
    const mode = PERMISSION_MODES['auto-edit']
    for (const tool of EXECUTE_TOOLS) {
      expect(mode.requireApproval.has(tool)).toBe(true)
    }
    for (const tool of DESTRUCTIVE_TOOLS) {
      expect(mode.requireApproval.has(tool)).toBe(true)
    }
    for (const tool of NETWORK_TOOLS) {
      expect(mode.requireApproval.has(tool)).toBe(true)
    }
  })

  it('auto-safe mode has same shape as auto-edit', () => {
    const autoEdit = PERMISSION_MODES['auto-edit']
    const autoSafe = PERMISSION_MODES['auto-safe']
    // Same autoApprove sets
    expect([...autoSafe.autoApprove].sort()).toEqual([...autoEdit.autoApprove].sort())
    // Same requireApproval sets
    expect([...autoSafe.requireApproval].sort()).toEqual([...autoEdit.requireApproval].sort())
  })

  it('yolo mode auto-approves everything', () => {
    const mode = PERMISSION_MODES.yolo
    expect(mode.autoApprove.has('*')).toBe(true)
    expect(mode.requireApproval.size).toBe(0)
  })
})

// ─── isToolAutoApproved ────────────────────────────────────────────────────

describe('isToolAutoApproved', () => {
  describe('suggest mode', () => {
    it('never auto-approves any tool', () => {
      expect(isToolAutoApproved('read_file', 'suggest')).toBe(false)
      expect(isToolAutoApproved('write_file', 'suggest')).toBe(false)
      expect(isToolAutoApproved('bash', 'suggest')).toBe(false)
      expect(isToolAutoApproved('question', 'suggest')).toBe(false)
      expect(isToolAutoApproved('delete_file', 'suggest')).toBe(false)
    })
  })

  describe('ask mode', () => {
    it('auto-approves meta tools', () => {
      expect(isToolAutoApproved('question', 'ask')).toBe(true)
      expect(isToolAutoApproved('attempt_completion', 'ask')).toBe(true)
      expect(isToolAutoApproved('plan_enter', 'ask')).toBe(true)
      expect(isToolAutoApproved('plan_exit', 'ask')).toBe(true)
      expect(isToolAutoApproved('batch', 'ask')).toBe(true)
      expect(isToolAutoApproved('task', 'ask')).toBe(true)
    })

    it('does not auto-approve reads', () => {
      expect(isToolAutoApproved('read_file', 'ask')).toBe(false)
      expect(isToolAutoApproved('glob', 'ask')).toBe(false)
      expect(isToolAutoApproved('grep', 'ask')).toBe(false)
    })

    it('does not auto-approve writes', () => {
      expect(isToolAutoApproved('write_file', 'ask')).toBe(false)
      expect(isToolAutoApproved('edit', 'ask')).toBe(false)
    })

    it('does not auto-approve bash', () => {
      expect(isToolAutoApproved('bash', 'ask')).toBe(false)
    })
  })

  describe('auto-edit mode', () => {
    it('auto-approves read tools', () => {
      expect(isToolAutoApproved('read_file', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('glob', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('grep', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('ls', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('todoread', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('memory_read', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('memory_list', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('lsp_diagnostics', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('lsp_hover', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('lsp_definition', 'auto-edit')).toBe(true)
    })

    it('auto-approves edit tools', () => {
      expect(isToolAutoApproved('write_file', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('edit', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('create_file', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('multiedit', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('apply_patch', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('todowrite', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('memory_write', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('memory_delete', 'auto-edit')).toBe(true)
    })

    it('auto-approves meta tools', () => {
      expect(isToolAutoApproved('question', 'auto-edit')).toBe(true)
      expect(isToolAutoApproved('attempt_completion', 'auto-edit')).toBe(true)
    })

    it('does not auto-approve execute tools', () => {
      expect(isToolAutoApproved('bash', 'auto-edit')).toBe(false)
    })

    it('does not auto-approve destructive tools', () => {
      expect(isToolAutoApproved('delete_file', 'auto-edit')).toBe(false)
    })

    it('does not auto-approve network tools', () => {
      expect(isToolAutoApproved('websearch', 'auto-edit')).toBe(false)
      expect(isToolAutoApproved('webfetch', 'auto-edit')).toBe(false)
    })

    it('does not auto-approve unknown tools', () => {
      expect(isToolAutoApproved('unknown_tool', 'auto-edit')).toBe(false)
    })
  })

  describe('auto-safe mode', () => {
    it('matches auto-edit behavior for known categories', () => {
      // Same auto-approvals
      expect(isToolAutoApproved('read_file', 'auto-safe')).toBe(true)
      expect(isToolAutoApproved('edit', 'auto-safe')).toBe(true)
      expect(isToolAutoApproved('question', 'auto-safe')).toBe(true)
      // Same denials
      expect(isToolAutoApproved('bash', 'auto-safe')).toBe(false)
      expect(isToolAutoApproved('delete_file', 'auto-safe')).toBe(false)
      expect(isToolAutoApproved('websearch', 'auto-safe')).toBe(false)
    })
  })

  describe('yolo mode', () => {
    it('auto-approves everything', () => {
      expect(isToolAutoApproved('read_file', 'yolo')).toBe(true)
      expect(isToolAutoApproved('write_file', 'yolo')).toBe(true)
      expect(isToolAutoApproved('bash', 'yolo')).toBe(true)
      expect(isToolAutoApproved('delete_file', 'yolo')).toBe(true)
      expect(isToolAutoApproved('websearch', 'yolo')).toBe(true)
      expect(isToolAutoApproved('unknown_tool', 'yolo')).toBe(true)
    })
  })
})

// ─── getPermissionMode ─────────────────────────────────────────────────────

describe('getPermissionMode', () => {
  it('returns config for valid mode names', () => {
    expect(getPermissionMode('suggest')?.name).toBe('suggest')
    expect(getPermissionMode('ask')?.name).toBe('ask')
    expect(getPermissionMode('auto-edit')?.name).toBe('auto-edit')
    expect(getPermissionMode('auto-safe')?.name).toBe('auto-safe')
    expect(getPermissionMode('yolo')?.name).toBe('yolo')
  })

  it('returns undefined for invalid mode names', () => {
    expect(getPermissionMode('invalid')).toBeUndefined()
    expect(getPermissionMode('')).toBeUndefined()
    expect(getPermissionMode('YOLO')).toBeUndefined()
  })
})

// ─── getAllPermissionModes ──────────────────────────────────────────────────

describe('getAllPermissionModes', () => {
  it('returns all 5 modes', () => {
    const modes = getAllPermissionModes()
    expect(modes).toHaveLength(5)
    const names = modes.map((m) => m.name)
    expect(names).toContain('suggest')
    expect(names).toContain('ask')
    expect(names).toContain('auto-edit')
    expect(names).toContain('auto-safe')
    expect(names).toContain('yolo')
  })

  it('each mode has a non-empty description', () => {
    for (const mode of getAllPermissionModes()) {
      expect(mode.description.length).toBeGreaterThan(0)
    }
  })
})

// ─── Tool Categories ───────────────────────────────────────────────────────

describe('Tool categories', () => {
  it('READ_TOOLS contains expected tools', () => {
    expect(READ_TOOLS.has('read_file')).toBe(true)
    expect(READ_TOOLS.has('glob')).toBe(true)
    expect(READ_TOOLS.has('grep')).toBe(true)
    expect(READ_TOOLS.has('ls')).toBe(true)
    expect(READ_TOOLS.has('lsp_diagnostics')).toBe(true)
  })

  it('EDIT_TOOLS contains expected tools', () => {
    expect(EDIT_TOOLS.has('write_file')).toBe(true)
    expect(EDIT_TOOLS.has('edit')).toBe(true)
    expect(EDIT_TOOLS.has('create_file')).toBe(true)
    expect(EDIT_TOOLS.has('multiedit')).toBe(true)
  })

  it('EXECUTE_TOOLS contains bash', () => {
    expect(EXECUTE_TOOLS.has('bash')).toBe(true)
  })

  it('NETWORK_TOOLS contains web tools', () => {
    expect(NETWORK_TOOLS.has('websearch')).toBe(true)
    expect(NETWORK_TOOLS.has('webfetch')).toBe(true)
  })

  it('DESTRUCTIVE_TOOLS contains delete_file', () => {
    expect(DESTRUCTIVE_TOOLS.has('delete_file')).toBe(true)
  })

  it('META_TOOLS contains control tools', () => {
    expect(META_TOOLS.has('question')).toBe(true)
    expect(META_TOOLS.has('attempt_completion')).toBe(true)
    expect(META_TOOLS.has('plan_enter')).toBe(true)
    expect(META_TOOLS.has('plan_exit')).toBe(true)
    expect(META_TOOLS.has('batch')).toBe(true)
    expect(META_TOOLS.has('task')).toBe(true)
  })

  it('tool categories do not overlap', () => {
    const allSets = [
      READ_TOOLS,
      EDIT_TOOLS,
      EXECUTE_TOOLS,
      NETWORK_TOOLS,
      DESTRUCTIVE_TOOLS,
      META_TOOLS,
    ]
    const allTools = new Set<string>()
    for (const s of allSets) {
      for (const tool of s) {
        expect(allTools.has(tool)).toBe(false)
        allTools.add(tool)
      }
    }
  })
})
