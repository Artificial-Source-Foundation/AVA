/**
 * Auto-Approval System Tests
 * Tests: disabled mode, workspace reads, external writes, yolo mode,
 * blocked patterns override yolo, risk thresholds, command safety
 */

import { afterEach, describe, expect, it } from 'vitest'
import {
  type AutoApprovalSettings,
  checkBrowserAutoApproval,
  checkCommandAutoApproval,
  checkFileAutoApproval,
  checkMcpAutoApproval,
  checkWebFetchAutoApproval,
  DEFAULT_AUTO_APPROVAL_SETTINGS,
  isCommandSafe,
  isPathBlocked,
  isPathLocal,
  isPathTrusted,
  resetAutoApprovalSettings,
  setAutoApprovalSettings,
  shouldAutoApprove,
  YOLO_AUTO_APPROVAL_SETTINGS,
} from './auto-approve.js'

afterEach(() => {
  resetAutoApprovalSettings()
})

// ============================================================================
// Path Checking
// ============================================================================

describe('isPathLocal', () => {
  it('returns true for paths within workspace', () => {
    expect(isPathLocal('/project/src/file.ts', '/project')).toBe(true)
    expect(isPathLocal('src/file.ts', '/project')).toBe(true)
  })

  it('returns false for paths outside workspace', () => {
    expect(isPathLocal('/etc/passwd', '/project')).toBe(false)
    expect(isPathLocal('/home/user/other/file.ts', '/project')).toBe(false)
  })

  it('handles parent directory traversal', () => {
    expect(isPathLocal('../other/file.ts', '/project')).toBe(false)
  })
})

describe('isPathTrusted', () => {
  it('returns true for workspace paths', () => {
    expect(isPathTrusted('/project/file.ts', '/project', [])).toBe(true)
  })

  it('returns true for trusted external paths', () => {
    expect(isPathTrusted('/shared/lib/util.ts', '/project', ['/shared/lib'])).toBe(true)
  })

  it('returns false for untrusted external paths', () => {
    expect(isPathTrusted('/etc/passwd', '/project', ['/shared/lib'])).toBe(false)
  })
})

describe('isPathBlocked', () => {
  it('blocks exact matches', () => {
    expect(isPathBlocked('/etc/passwd', ['/etc/passwd'])).toBe(true)
  })

  it('blocks glob pattern matches', () => {
    expect(isPathBlocked('/project/.env', ['**/.env'])).toBe(true)
    expect(isPathBlocked('/project/.env.local', ['**/.env.*'])).toBe(true)
  })

  it('does not block non-matching paths', () => {
    expect(isPathBlocked('/project/src/app.ts', ['**/.env'])).toBe(false)
  })
})

// ============================================================================
// Command Safety
// ============================================================================

describe('isCommandSafe', () => {
  it('recognizes safe commands', () => {
    expect(isCommandSafe('ls -la')).toBe(true)
    expect(isCommandSafe('git status')).toBe(true)
    expect(isCommandSafe('cat file.ts')).toBe(true)
    expect(isCommandSafe('grep -r pattern')).toBe(true)
  })

  it('rejects unsafe commands', () => {
    expect(isCommandSafe('rm -rf /')).toBe(false)
    expect(isCommandSafe('npm install')).toBe(false)
    expect(isCommandSafe('curl http://example.com')).toBe(false)
  })

  it('is case-insensitive', () => {
    expect(isCommandSafe('LS -la')).toBe(true)
    expect(isCommandSafe('GIT STATUS')).toBe(true)
  })
})

// ============================================================================
// File Auto-Approval
// ============================================================================

describe('checkFileAutoApproval', () => {
  it('blocks all when disabled', () => {
    const settings: AutoApprovalSettings = { ...DEFAULT_AUTO_APPROVAL_SETTINGS, enabled: false }
    const result = checkFileAutoApproval('read', '/project/file.ts', settings)
    expect(result.approved).toBe(false)
    expect(result.reason).toContain('disabled')
  })

  it('approves workspace reads with defaults', () => {
    const settings: AutoApprovalSettings = {
      ...DEFAULT_AUTO_APPROVAL_SETTINGS,
      workspaceRoot: '/project',
    }
    const result = checkFileAutoApproval('read', '/project/src/file.ts', settings)
    expect(result.approved).toBe(true)
    expect(result.isLocal).toBe(true)
  })

  it('blocks external reads with defaults', () => {
    const settings: AutoApprovalSettings = {
      ...DEFAULT_AUTO_APPROVAL_SETTINGS,
      workspaceRoot: '/project',
    }
    const result = checkFileAutoApproval('read', '/etc/hosts', settings)
    expect(result.approved).toBe(false)
    expect(result.isLocal).toBe(false)
  })

  it('blocks workspace writes with defaults', () => {
    const settings: AutoApprovalSettings = {
      ...DEFAULT_AUTO_APPROVAL_SETTINGS,
      workspaceRoot: '/project',
    }
    const result = checkFileAutoApproval('write', '/project/src/file.ts', settings)
    expect(result.approved).toBe(false)
  })

  it('blocks workspace deletes with defaults', () => {
    const settings: AutoApprovalSettings = {
      ...DEFAULT_AUTO_APPROVAL_SETTINGS,
      workspaceRoot: '/project',
    }
    const result = checkFileAutoApproval('delete', '/project/src/file.ts', settings)
    expect(result.approved).toBe(false)
  })

  it('yolo mode approves all operations', () => {
    const settings: AutoApprovalSettings = {
      ...YOLO_AUTO_APPROVAL_SETTINGS,
      workspaceRoot: '/project',
    }
    expect(checkFileAutoApproval('read', '/external/file.ts', settings).approved).toBe(true)
    expect(checkFileAutoApproval('write', '/external/file.ts', settings).approved).toBe(true)
    expect(checkFileAutoApproval('delete', '/external/file.ts', settings).approved).toBe(true)
  })

  it('blocked patterns override yolo mode', () => {
    const settings: AutoApprovalSettings = {
      ...YOLO_AUTO_APPROVAL_SETTINGS,
      workspaceRoot: '/project',
    }
    const result = checkFileAutoApproval('read', '/etc/passwd', settings)
    expect(result.approved).toBe(false)
    expect(result.reason).toContain('blocked')
  })

  it('blocked patterns override yolo for .env files', () => {
    const settings: AutoApprovalSettings = {
      ...DEFAULT_AUTO_APPROVAL_SETTINGS,
      yolo: true,
      workspaceRoot: '/project',
      blockedPatterns: ['**/.env'],
    }
    const result = checkFileAutoApproval('read', '/project/.env', settings)
    expect(result.approved).toBe(false)
  })
})

// ============================================================================
// Command Auto-Approval
// ============================================================================

describe('checkCommandAutoApproval', () => {
  it('blocks when disabled', () => {
    const settings: AutoApprovalSettings = { ...DEFAULT_AUTO_APPROVAL_SETTINGS, enabled: false }
    const result = checkCommandAutoApproval('ls', settings)
    expect(result.approved).toBe(false)
  })

  it('yolo mode approves all commands', () => {
    const result = checkCommandAutoApproval('rm -rf /', YOLO_AUTO_APPROVAL_SETTINGS)
    expect(result.approved).toBe(true)
  })

  it('executeAllCommands approves everything', () => {
    const settings: AutoApprovalSettings = {
      ...DEFAULT_AUTO_APPROVAL_SETTINGS,
      actions: { ...DEFAULT_AUTO_APPROVAL_SETTINGS.actions, executeAllCommands: true },
    }
    const result = checkCommandAutoApproval('npm install', settings)
    expect(result.approved).toBe(true)
  })

  it('executeSafeCommands approves only safe commands', () => {
    const settings: AutoApprovalSettings = {
      ...DEFAULT_AUTO_APPROVAL_SETTINGS,
      actions: { ...DEFAULT_AUTO_APPROVAL_SETTINGS.actions, executeSafeCommands: true },
    }
    expect(checkCommandAutoApproval('ls -la', settings).approved).toBe(true)
    expect(checkCommandAutoApproval('npm install', settings).approved).toBe(false)
  })

  it('rejects commands when nothing is enabled', () => {
    const result = checkCommandAutoApproval('ls', DEFAULT_AUTO_APPROVAL_SETTINGS)
    expect(result.approved).toBe(false)
  })
})

// ============================================================================
// Browser, MCP, WebFetch
// ============================================================================

describe('checkBrowserAutoApproval', () => {
  it('blocks when disabled', () => {
    expect(
      checkBrowserAutoApproval({ ...DEFAULT_AUTO_APPROVAL_SETTINGS, enabled: false }).approved
    ).toBe(false)
  })

  it('blocks by default', () => {
    expect(checkBrowserAutoApproval(DEFAULT_AUTO_APPROVAL_SETTINGS).approved).toBe(false)
  })

  it('approves in yolo mode', () => {
    expect(checkBrowserAutoApproval(YOLO_AUTO_APPROVAL_SETTINGS).approved).toBe(true)
  })
})

describe('checkMcpAutoApproval', () => {
  it('blocks by default', () => {
    expect(checkMcpAutoApproval(DEFAULT_AUTO_APPROVAL_SETTINGS).approved).toBe(false)
  })

  it('approves in yolo mode', () => {
    expect(checkMcpAutoApproval(YOLO_AUTO_APPROVAL_SETTINGS).approved).toBe(true)
  })
})

describe('checkWebFetchAutoApproval', () => {
  it('approves by default', () => {
    expect(checkWebFetchAutoApproval(DEFAULT_AUTO_APPROVAL_SETTINGS).approved).toBe(true)
  })

  it('blocks when disabled', () => {
    expect(
      checkWebFetchAutoApproval({ ...DEFAULT_AUTO_APPROVAL_SETTINGS, enabled: false }).approved
    ).toBe(false)
  })
})

// ============================================================================
// State Management
// ============================================================================

describe('settings management', () => {
  it('setAutoApprovalSettings merges actions', () => {
    const updated = setAutoApprovalSettings({ actions: { editFiles: true } as never })
    expect(updated.actions.editFiles).toBe(true)
    // Other actions should retain defaults
    expect(updated.actions.readFiles).toBe(true)
    expect(updated.actions.deleteFiles).toBe(false)
  })

  it('resetAutoApprovalSettings returns defaults', () => {
    setAutoApprovalSettings({ yolo: true })
    const reset = resetAutoApprovalSettings()
    expect(reset.yolo).toBe(false)
  })
})

// ============================================================================
// shouldAutoApprove (integration)
// ============================================================================

describe('shouldAutoApprove', () => {
  it('routes read tools to file check', () => {
    setAutoApprovalSettings({ workspaceRoot: '/project' })
    const result = shouldAutoApprove('read', 'read', { path: '/project/file.ts' })
    expect(result.approved).toBe(true)
  })

  it('routes bash to command check', () => {
    const result = shouldAutoApprove('bash', 'execute', { command: 'ls' })
    // Default settings don't auto-approve commands
    expect(result.approved).toBe(false)
  })

  it('routes webfetch to web fetch check', () => {
    const result = shouldAutoApprove('webfetch', 'read', {})
    expect(result.approved).toBe(true) // Default enables webfetch
  })

  it('routes mcp_ tools to MCP check', () => {
    const result = shouldAutoApprove('mcp_github', 'execute', {})
    expect(result.approved).toBe(false) // Default disables MCP
  })
})
