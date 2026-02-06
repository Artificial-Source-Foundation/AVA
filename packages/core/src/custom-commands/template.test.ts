/**
 * Template Engine Tests
 */

import { describe, expect, it, vi } from 'vitest'
import { extractPlaceholders, resolveTemplate } from './template.js'
import type { ShellExecution } from './types.js'

// ============================================================================
// Mock Helpers
// ============================================================================

const mockReadFile = vi.fn(async (path: string) => `Contents of ${path}`)

const mockShell = vi.fn(
  async (command: string): Promise<ShellExecution> => ({
    command,
    output: `Output of: ${command}`,
    exitCode: 0,
    success: true,
  })
)

const defaultOptions = {
  readFile: mockReadFile,
  executeShell: mockShell,
  workingDirectory: '/tmp/test',
}

// ============================================================================
// Tests
// ============================================================================

describe('resolveTemplate', () => {
  beforeEach(() => {
    mockReadFile.mockClear()
    mockShell.mockClear()
  })

  // =========================================================================
  // Argument Substitution ({{args}})
  // =========================================================================

  describe('argument substitution', () => {
    it('should replace {{args}} with raw arguments', async () => {
      const result = await resolveTemplate('Review this: {{args}}', 'my-file.ts', defaultOptions)
      expect(result.prompt).toBe('Review this: my-file.ts')
    })

    it('should replace multiple {{args}} occurrences', async () => {
      const result = await resolveTemplate(
        'Find {{args}} and fix {{args}}',
        'the bug',
        defaultOptions
      )
      expect(result.prompt).toBe('Find the bug and fix the bug')
    })

    it('should be case-insensitive', async () => {
      const result = await resolveTemplate('Fix {{ARGS}} please', 'issue', defaultOptions)
      expect(result.prompt).toBe('Fix issue please')
    })

    it('should append args if no {{args}} placeholder', async () => {
      const result = await resolveTemplate('Do something.', 'with this input', defaultOptions)
      expect(result.prompt).toBe('Do something.\n\nwith this input')
    })

    it('should not append if args is empty', async () => {
      const result = await resolveTemplate('Do something.', '', defaultOptions)
      expect(result.prompt).toBe('Do something.')
    })
  })

  // =========================================================================
  // File Injection (@{path})
  // =========================================================================

  describe('file injection', () => {
    it('should inject file contents', async () => {
      const result = await resolveTemplate('Guidelines: @{docs/guide.md}', '', defaultOptions)
      expect(result.prompt).toBe('Guidelines: Contents of docs/guide.md')
      expect(result.injectedFiles).toEqual(['docs/guide.md'])
    })

    it('should inject multiple files', async () => {
      const result = await resolveTemplate('A: @{a.md}\nB: @{b.md}', '', defaultOptions)
      expect(result.prompt).toContain('Contents of a.md')
      expect(result.prompt).toContain('Contents of b.md')
      expect(result.injectedFiles).toHaveLength(2)
    })

    it('should handle file read errors gracefully', async () => {
      const errorReadFile = vi.fn(async () => {
        throw new Error('Not found')
      })

      const result = await resolveTemplate('File: @{missing.md}', '', {
        ...defaultOptions,
        readFile: errorReadFile,
      })
      expect(result.prompt).toContain('[Error reading file: missing.md')
    })

    it('should not process files if readFile not provided', async () => {
      const result = await resolveTemplate('File: @{some.md}', '', { executeShell: mockShell })
      expect(result.prompt).toBe('File: @{some.md}')
    })
  })

  // =========================================================================
  // Shell Injection (!{command})
  // =========================================================================

  describe('shell injection', () => {
    it('should inject shell command output', async () => {
      const result = await resolveTemplate('Diff: !{git diff}', '', defaultOptions)
      expect(result.prompt).toBe('Diff: Output of: git diff')
      expect(result.shellCommands).toHaveLength(1)
      expect(result.shellCommands[0]!.command).toBe('git diff')
    })

    it('should shell-escape args within shell commands', async () => {
      const capturedCommands: string[] = []
      const captureShell = vi.fn(async (cmd: string): Promise<ShellExecution> => {
        capturedCommands.push(cmd)
        return { command: cmd, output: 'ok', exitCode: 0, success: true }
      })

      await resolveTemplate('Search: !{grep -r {{args}} .}', "it's a test", {
        ...defaultOptions,
        executeShell: captureShell,
      })

      // Args should be shell-escaped within !{...}
      expect(capturedCommands[0]).toContain("'it'\\''s a test'")
    })

    it('should handle shell command failures', async () => {
      const failShell = vi.fn(
        async (cmd: string): Promise<ShellExecution> => ({
          command: cmd,
          output: '',
          stderr: 'command not found',
          exitCode: 127,
          success: false,
        })
      )

      const result = await resolveTemplate('Output: !{nonexistent}', '', {
        ...defaultOptions,
        executeShell: failShell,
      })
      expect(result.prompt).toContain('[Command failed')
      expect(result.hasErrors).toBe(true)
    })

    it('should not process shells if executeShell not provided', async () => {
      const result = await resolveTemplate('Shell: !{ls}', '', { readFile: mockReadFile })
      expect(result.prompt).toBe('Shell: !{ls}')
    })
  })

  // =========================================================================
  // Processing Order
  // =========================================================================

  describe('processing order', () => {
    it('should process files before shells before args', async () => {
      const order: string[] = []

      const trackingReadFile = vi.fn(async (path: string) => {
        order.push('file')
        return `FILE:${path}`
      })

      const trackingShell = vi.fn(async (cmd: string): Promise<ShellExecution> => {
        order.push('shell')
        return { command: cmd, output: `SHELL:${cmd}`, exitCode: 0, success: true }
      })

      await resolveTemplate('@{f.md} !{ls} {{args}}', 'test', {
        readFile: trackingReadFile,
        executeShell: trackingShell,
      })

      expect(order).toEqual(['file', 'shell'])
    })
  })

  // =========================================================================
  // Edge Cases
  // =========================================================================

  describe('edge cases', () => {
    it('should handle template with no placeholders', async () => {
      const result = await resolveTemplate('Just plain text.', '', defaultOptions)
      expect(result.prompt).toBe('Just plain text.')
      expect(result.shellCommands).toHaveLength(0)
      expect(result.injectedFiles).toHaveLength(0)
      expect(result.hasErrors).toBe(false)
    })

    it('should handle empty template', async () => {
      const result = await resolveTemplate('', 'args', defaultOptions)
      expect(result.prompt).toBe('\n\nargs')
    })

    it('should handle complex real-world template', async () => {
      const template = `
Please review the following code changes:

\`\`\`diff
!{git diff --staged}
\`\`\`

Apply these guidelines:
@{docs/review-guidelines.md}

Focus on: {{args}}
`
      const result = await resolveTemplate(template, 'security concerns', defaultOptions)

      expect(result.prompt).toContain('Output of: git diff --staged')
      expect(result.prompt).toContain('Contents of docs/review-guidelines.md')
      expect(result.prompt).toContain('Focus on: security concerns')
    })
  })
})

// ============================================================================
// Placeholder Extraction
// ============================================================================

describe('extractPlaceholders', () => {
  it('should extract all placeholder types', () => {
    const template = 'File: @{a.md} Shell: !{ls} Args: {{args}}'
    const placeholders = extractPlaceholders(template)

    expect(placeholders).toHaveLength(3)
    expect(placeholders[0]!.type).toBe('file')
    expect(placeholders[0]!.content).toBe('a.md')
    expect(placeholders[1]!.type).toBe('shell')
    expect(placeholders[1]!.content).toBe('ls')
    expect(placeholders[2]!.type).toBe('args')
  })

  it('should return empty for no placeholders', () => {
    expect(extractPlaceholders('plain text')).toHaveLength(0)
  })

  it('should return sorted by position', () => {
    const template = '{{args}} @{file} !{cmd}'
    const placeholders = extractPlaceholders(template)

    expect(placeholders[0]!.start).toBeLessThan(placeholders[1]!.start)
    expect(placeholders[1]!.start).toBeLessThan(placeholders[2]!.start)
  })
})

// ============================================================================
// Import for beforeEach
// ============================================================================

import { beforeEach } from 'vitest'
