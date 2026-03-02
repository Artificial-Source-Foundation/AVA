import * as fs from 'node:fs'
import * as os from 'node:os'
import * as path from 'node:path'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { generateShellFunction, getShellType, installAmbient, uninstallAmbient } from './install.js'

// ─── getShellType ────────────────────────────────────────────────────────────

describe('getShellType', () => {
  const originalShell = process.env.SHELL

  afterEach(() => {
    if (originalShell !== undefined) {
      process.env.SHELL = originalShell
    } else {
      delete process.env.SHELL
    }
  })

  it('detects zsh from $SHELL', () => {
    process.env.SHELL = '/bin/zsh'
    expect(getShellType()).toBe('zsh')
  })

  it('detects bash from $SHELL', () => {
    process.env.SHELL = '/bin/bash'
    expect(getShellType()).toBe('bash')
  })

  it('detects fish from $SHELL', () => {
    process.env.SHELL = '/usr/bin/fish'
    expect(getShellType()).toBe('fish')
  })

  it('defaults to bash when $SHELL is unset', () => {
    delete process.env.SHELL
    expect(getShellType()).toBe('bash')
  })
})

// ─── generateShellFunction ───────────────────────────────────────────────────

describe('generateShellFunction', () => {
  it('generates valid bash/zsh function', () => {
    const output = generateShellFunction('bash')
    expect(output).toContain('ava()')
    expect(output).toContain('agent-v2 run')
    expect(output).toContain('$' + '{1#@}')
    expect(output).toContain('--cwd')
    expect(output).toContain('git branch --show-current')
  })

  it('generates same function for zsh as bash', () => {
    const bash = generateShellFunction('bash')
    const zsh = generateShellFunction('zsh')
    expect(bash).toBe(zsh)
  })

  it('generates valid fish function', () => {
    const output = generateShellFunction('fish')
    expect(output).toContain('function ava')
    expect(output).toContain('agent-v2 run')
    expect(output).toContain('string replace')
    expect(output).toContain('--cwd')
    expect(output).toContain('git branch --show-current')
    expect(output).toContain('end')
  })

  it('bash function includes git context injection', () => {
    const output = generateShellFunction('bash')
    expect(output).toContain('git rev-parse --is-inside-work-tree')
    expect(output).toContain('[branch:')
  })

  it('fish function includes git context injection', () => {
    const output = generateShellFunction('fish')
    expect(output).toContain('git rev-parse --is-inside-work-tree')
    expect(output).toContain('[branch:')
  })
})

// ─── installAmbient / uninstallAmbient ───────────────────────────────────────

describe('installAmbient', () => {
  let tmpDir: string

  beforeEach(() => {
    tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'ava-ambient-test-'))
    vi.spyOn(console, 'log').mockImplementation(() => {})
  })

  afterEach(() => {
    vi.restoreAllMocks()
    fs.rmSync(tmpDir, { recursive: true, force: true })
  })

  it('installs bash shell function', () => {
    installAmbient('bash', tmpDir)
    const filePath = path.join(tmpDir, '.ava', 'shell', 'ava.sh')
    expect(fs.existsSync(filePath)).toBe(true)
    const content = fs.readFileSync(filePath, 'utf-8')
    expect(content).toContain('ava()')
  })

  it('installs fish shell function', () => {
    installAmbient('fish', tmpDir)
    const filePath = path.join(tmpDir, '.ava', 'shell', 'ava.fish')
    expect(fs.existsSync(filePath)).toBe(true)
    const content = fs.readFileSync(filePath, 'utf-8')
    expect(content).toContain('function ava')
  })

  it('uninstall removes shell files', () => {
    installAmbient('bash', tmpDir)
    const filePath = path.join(tmpDir, '.ava', 'shell', 'ava.sh')
    expect(fs.existsSync(filePath)).toBe(true)

    uninstallAmbient(tmpDir)
    expect(fs.existsSync(filePath)).toBe(false)
  })

  it('uninstall handles missing directory gracefully', () => {
    // Use a subdirectory that does not exist
    const noDir = path.join(tmpDir, 'nonexistent')
    uninstallAmbient(noDir)
    expect(console.log).toHaveBeenCalledWith('No ambient shell integration found.')
  })
})
