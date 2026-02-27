import { describe, expect, it } from 'vitest'
import { parseCommandFile } from './parser.js'

describe('parseCommandFile', () => {
  it('parses a valid command file', () => {
    const content = `
name = "deploy"
description = "Deploy the application"
prompt = "Run the deployment pipeline"
mode = "normal"
`
    const cmd = parseCommandFile(content, '/commands/deploy.toml')
    expect(cmd).not.toBeNull()
    expect(cmd!.name).toBe('deploy')
    expect(cmd!.description).toBe('Deploy the application')
    expect(cmd!.prompt).toBe('Run the deployment pipeline')
    expect(cmd!.mode).toBe('normal')
    expect(cmd!.source).toBe('/commands/deploy.toml')
  })

  it('returns null for missing required fields', () => {
    expect(parseCommandFile('name = "test"', 'test.toml')).toBeNull()
    expect(parseCommandFile('prompt = "do stuff"', 'test.toml')).toBeNull()
  })

  it('handles quoted values', () => {
    const content = `name = "my-cmd"\nprompt = "Do the thing"`
    const cmd = parseCommandFile(content, 'test.toml')
    expect(cmd!.name).toBe('my-cmd')
  })

  it('handles single-quoted values', () => {
    const content = `name = 'my-cmd'\nprompt = 'Do the thing'`
    const cmd = parseCommandFile(content, 'test.toml')
    expect(cmd!.name).toBe('my-cmd')
  })

  it('skips comments and empty lines', () => {
    const content = `# This is a comment\n\nname = "test"\n# Another comment\nprompt = "hello"`
    const cmd = parseCommandFile(content, 'test.toml')
    expect(cmd).not.toBeNull()
    expect(cmd!.name).toBe('test')
  })

  it('parses allowed_tools as comma-separated list', () => {
    const content = `name = "test"\nprompt = "do it"\nallowed_tools = "read_file, write_file, bash"`
    const cmd = parseCommandFile(content, 'test.toml')
    expect(cmd!.allowedTools).toEqual(['read_file', 'write_file', 'bash'])
  })

  it('parses allowed_tools in array format', () => {
    const content = `name = "test"\nprompt = "do it"\nallowed_tools = ["read_file", "bash"]`
    const cmd = parseCommandFile(content, 'test.toml')
    expect(cmd!.allowedTools).toEqual(['read_file', 'bash'])
  })

  it('handles multiline strings with triple quotes', () => {
    const content = `name = "test"\nprompt = """\nLine 1\nLine 2\nLine 3\n"""`
    const cmd = parseCommandFile(content, 'test.toml')
    expect(cmd!.prompt).toContain('Line 1')
    expect(cmd!.prompt).toContain('Line 2')
  })

  it('defaults description to empty string', () => {
    const content = `name = "test"\nprompt = "do it"`
    const cmd = parseCommandFile(content, 'test.toml')
    expect(cmd!.description).toBe('')
  })
})
