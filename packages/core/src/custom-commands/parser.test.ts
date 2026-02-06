/**
 * TOML Parser Tests
 */

import { describe, expect, it } from 'vitest'
import { parseCommandToml } from './parser.js'

describe('parseCommandToml', () => {
  // =========================================================================
  // Basic Parsing
  // =========================================================================

  describe('basic parsing', () => {
    it('should parse simple command with prompt and description', () => {
      const toml = `
description = "Run tests"
prompt = "Please run the test suite and report results."
`
      const result = parseCommandToml(toml, 'test', '/path/test.toml', false)

      expect(result.name).toBe('test')
      expect(result.description).toBe('Run tests')
      expect(result.prompt).toBe('Please run the test suite and report results.')
      expect(result.sourcePath).toBe('/path/test.toml')
      expect(result.isProjectLevel).toBe(false)
    })

    it('should parse command with multi-line prompt', () => {
      const toml = `
description = "Code review"
prompt = """
Please review the following code:

1. Check for bugs
2. Suggest improvements
3. Rate readability
"""
`
      const result = parseCommandToml(toml, 'review', '/path/review.toml', true)

      expect(result.prompt).toContain('Check for bugs')
      expect(result.prompt).toContain('Suggest improvements')
      expect(result.prompt).toContain('Rate readability')
      expect(result.isProjectLevel).toBe(true)
    })

    it('should parse prompt-only command (no description)', () => {
      const toml = `prompt = "Do the thing."`

      const result = parseCommandToml(toml, 'thing', '/path/thing.toml', false)

      expect(result.name).toBe('thing')
      expect(result.description).toBeUndefined()
      expect(result.prompt).toBe('Do the thing.')
    })

    it('should throw if prompt is missing', () => {
      const toml = `description = "Missing prompt"`

      expect(() => parseCommandToml(toml, 'bad', '/path/bad.toml', false)).toThrow(
        'missing required "prompt" field'
      )
    })
  })

  // =========================================================================
  // Multi-line Strings
  // =========================================================================

  describe('multi-line strings', () => {
    it('should handle multi-line prompt with placeholders', () => {
      const toml = `
description = "Commit helper"
prompt = """
Generate a commit message for:

\`\`\`diff
!{git diff --staged}
\`\`\`

Apply conventional commits format.
{{args}}
"""
`
      const result = parseCommandToml(toml, 'commit', '/path/commit.toml', false)

      expect(result.prompt).toContain('!{git diff --staged}')
      expect(result.prompt).toContain('{{args}}')
    })

    it('should handle triple-quoted description', () => {
      const toml = `
description = """A long description
that spans multiple lines"""
prompt = "Do something."
`
      const result = parseCommandToml(toml, 'test', '/p.toml', false)
      expect(result.description).toContain('multiple lines')
    })
  })

  // =========================================================================
  // Edge Cases
  // =========================================================================

  describe('edge cases', () => {
    it('should skip comments', () => {
      const toml = `
# This is a comment
description = "Test"
# Another comment
prompt = "Do test."
`
      const result = parseCommandToml(toml, 'test', '/p.toml', false)
      expect(result.description).toBe('Test')
      expect(result.prompt).toBe('Do test.')
    })

    it('should handle empty lines', () => {
      const toml = `

description = "Test"

prompt = "Do test."

`
      const result = parseCommandToml(toml, 'test', '/p.toml', false)
      expect(result.prompt).toBe('Do test.')
    })

    it('should handle single-quoted strings', () => {
      const toml = `
description = 'Single quotes'
prompt = 'Do thing.'
`
      const result = parseCommandToml(toml, 'test', '/p.toml', false)
      expect(result.description).toBe('Single quotes')
    })

    it('should handle escaped characters', () => {
      const toml = `prompt = "Line 1\\nLine 2\\tTabbed"`

      const result = parseCommandToml(toml, 'test', '/p.toml', false)
      expect(result.prompt).toContain('\n')
      expect(result.prompt).toContain('\t')
    })

    it('should handle prompt with braces', () => {
      const toml = `
prompt = """
Find files: !{find . -name "*.ts"}
Review: @{docs/guidelines.md}
User input: {{args}}
"""
`
      const result = parseCommandToml(toml, 'test', '/p.toml', false)
      expect(result.prompt).toContain('!{find . -name "*.ts"}')
      expect(result.prompt).toContain('@{docs/guidelines.md}')
      expect(result.prompt).toContain('{{args}}')
    })
  })
})
