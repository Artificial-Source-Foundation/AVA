import { describe, expect, it } from 'vitest'
import { parsePolicyFile } from './parser.js'
import type { PolicyLoadItem } from './types.js'

function file(path: string, content: string): PolicyLoadItem {
  return { path, content, source: 'project' }
}

describe('parsePolicyFile', () => {
  it('parses yaml policy file', () => {
    const parsed = parsePolicyFile(
      file(
        '/repo/.ava-policy.yml',
        [
          'version: 1',
          'rules:',
          '  - name: allow-read',
          '    tool: read_file',
          '    decision: allow',
          '    priority: 50',
        ].join('\n')
      )
    )
    expect(parsed.rules).toHaveLength(1)
    expect(parsed.rules[0]?.name).toBe('allow-read')
    expect(parsed.rules[0]?.priority).toBe(50)
  })

  it('parses toml policy file', () => {
    const parsed = parsePolicyFile(
      file(
        '/repo/.ava-policy.toml',
        [
          'version = 1',
          '[[rules]]',
          'name = "deny-bash"',
          'tool = "bash"',
          'decision = "deny"',
        ].join('\n')
      )
    )
    expect(parsed.rules).toHaveLength(1)
    expect(parsed.rules[0]?.decision).toBe('deny')
  })

  it('throws for unsupported policy version', () => {
    expect(() =>
      parsePolicyFile(file('/repo/.ava-policy.yml', ['version: 2', 'rules: []'].join('\n')))
    ).toThrow(/Unsupported policy version/)
  })

  it('throws for missing required rule fields', () => {
    expect(() =>
      parsePolicyFile(
        file('/repo/.ava-policy.yml', ['version: 1', 'rules:', '  - name: incomplete'].join('\n'))
      )
    ).toThrow(/name\/tool\/decision are required/)
  })

  it('throws for invalid args regex', () => {
    expect(() =>
      parsePolicyFile(
        file(
          '/repo/.ava-policy.yml',
          [
            'version: 1',
            'rules:',
            '  - name: bad-regex',
            '    tool: bash',
            '    decision: ask',
            '    argsPattern: "[invalid"',
          ].join('\n')
        )
      )
    ).toThrow()
  })
})
