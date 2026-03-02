import { describe, expect, it } from 'vitest'
import { buildToolSchemaXML, needsToolShim, parseToolCallsFromText } from './toolshim.js'
import type { ToolDefinition } from './types.js'

// ─── parseToolCallsFromText ────────────────────────────────────────────────

describe('parseToolCallsFromText', () => {
  it('parses a single tool call', () => {
    const text = `Let me read that file.
<tool_call>
  <name>read_file</name>
  <arguments>{"path": "/src/index.ts"}</arguments>
</tool_call>`

    const result = parseToolCallsFromText(text)
    expect(result.toolCalls).toHaveLength(1)
    expect(result.toolCalls[0].type).toBe('tool_use')
    expect(result.toolCalls[0].name).toBe('read_file')
    expect(result.toolCalls[0].input).toEqual({ path: '/src/index.ts' })
    expect(result.toolCalls[0].id).toMatch(/^shim-/)
    expect(result.remainingText).toBe('Let me read that file.')
  })

  it('parses multiple tool calls', () => {
    const text = `I will edit two files.
<tool_call>
  <name>edit</name>
  <arguments>{"path": "a.ts", "old": "foo", "new": "bar"}</arguments>
</tool_call>
Then continue with:
<tool_call>
  <name>edit</name>
  <arguments>{"path": "b.ts", "old": "baz", "new": "qux"}</arguments>
</tool_call>`

    const result = parseToolCallsFromText(text)
    expect(result.toolCalls).toHaveLength(2)
    expect(result.toolCalls[0].name).toBe('edit')
    expect(result.toolCalls[0].input).toEqual({ path: 'a.ts', old: 'foo', new: 'bar' })
    expect(result.toolCalls[1].name).toBe('edit')
    expect(result.toolCalls[1].input).toEqual({ path: 'b.ts', old: 'baz', new: 'qux' })
  })

  it('generates unique IDs for each tool call', () => {
    const text = `<tool_call>
  <name>glob</name>
  <arguments>{"pattern": "*.ts"}</arguments>
</tool_call>
<tool_call>
  <name>grep</name>
  <arguments>{"pattern": "TODO"}</arguments>
</tool_call>`

    const result = parseToolCallsFromText(text)
    expect(result.toolCalls).toHaveLength(2)
    expect(result.toolCalls[0].id).not.toBe(result.toolCalls[1].id)
  })

  it('returns empty array for text without tool calls', () => {
    const text = 'Just a normal response without any tool calls.'
    const result = parseToolCallsFromText(text)
    expect(result.toolCalls).toHaveLength(0)
    expect(result.remainingText).toBe(text)
  })

  it('skips tool calls with invalid JSON arguments', () => {
    const text = `<tool_call>
  <name>read_file</name>
  <arguments>not valid json</arguments>
</tool_call>
<tool_call>
  <name>glob</name>
  <arguments>{"pattern": "*.ts"}</arguments>
</tool_call>`

    const result = parseToolCallsFromText(text)
    expect(result.toolCalls).toHaveLength(1)
    expect(result.toolCalls[0].name).toBe('glob')
  })

  it('handles tool calls with extra whitespace', () => {
    const text = `<tool_call>
      <name>  read_file  </name>
      <arguments>  {"path": "/test"}  </arguments>
</tool_call>`

    const result = parseToolCallsFromText(text)
    expect(result.toolCalls).toHaveLength(1)
    expect(result.toolCalls[0].name).toBe('read_file')
    expect(result.toolCalls[0].input).toEqual({ path: '/test' })
  })

  it('strips tool call XML from remaining text', () => {
    const text = `Before.
<tool_call>
  <name>bash</name>
  <arguments>{"command": "ls"}</arguments>
</tool_call>
After.`

    const result = parseToolCallsFromText(text)
    expect(result.remainingText).toBe('Before.\n\nAfter.')
  })

  it('handles empty arguments object', () => {
    const text = `<tool_call>
  <name>question</name>
  <arguments>{}</arguments>
</tool_call>`

    const result = parseToolCallsFromText(text)
    expect(result.toolCalls).toHaveLength(1)
    expect(result.toolCalls[0].input).toEqual({})
  })

  it('can be called multiple times (regex state resets)', () => {
    const text = `<tool_call>
  <name>glob</name>
  <arguments>{"pattern": "*.ts"}</arguments>
</tool_call>`

    const r1 = parseToolCallsFromText(text)
    const r2 = parseToolCallsFromText(text)
    expect(r1.toolCalls).toHaveLength(1)
    expect(r2.toolCalls).toHaveLength(1)
  })
})

// ─── buildToolSchemaXML ────────────────────────────────────────────────────

describe('buildToolSchemaXML', () => {
  const sampleTools: ToolDefinition[] = [
    {
      name: 'read_file',
      description: 'Read a file from disk',
      input_schema: {
        type: 'object',
        properties: { path: { type: 'string', description: 'File path' } },
        required: ['path'],
      },
    },
    {
      name: 'write_file',
      description: 'Write content to a file',
      input_schema: {
        type: 'object',
        properties: {
          path: { type: 'string' },
          content: { type: 'string' },
        },
        required: ['path', 'content'],
      },
    },
  ]

  it('generates valid XML structure', () => {
    const xml = buildToolSchemaXML(sampleTools)
    expect(xml).toContain('<available_tools>')
    expect(xml).toContain('</available_tools>')
    expect(xml).toContain('<tool>')
    expect(xml).toContain('</tool>')
  })

  it('includes tool names', () => {
    const xml = buildToolSchemaXML(sampleTools)
    expect(xml).toContain('<name>read_file</name>')
    expect(xml).toContain('<name>write_file</name>')
  })

  it('includes tool descriptions', () => {
    const xml = buildToolSchemaXML(sampleTools)
    expect(xml).toContain('Read a file from disk')
    expect(xml).toContain('Write content to a file')
  })

  it('includes JSON parameters', () => {
    const xml = buildToolSchemaXML(sampleTools)
    expect(xml).toContain('<parameters>')
    expect(xml).toContain('"type": "object"')
    expect(xml).toContain('"path"')
  })

  it('includes usage instructions', () => {
    const xml = buildToolSchemaXML(sampleTools)
    expect(xml).toContain('To use a tool, respond with:')
    expect(xml).toContain('<tool_call>')
    expect(xml).toContain('<name>tool_name</name>')
    expect(xml).toContain('<arguments>{"param": "value"}</arguments>')
  })

  it('escapes XML special characters in descriptions', () => {
    const tools: ToolDefinition[] = [
      {
        name: 'test',
        description: 'Compare a < b & c > d',
        input_schema: { type: 'object', properties: {} },
      },
    ]
    const xml = buildToolSchemaXML(tools)
    expect(xml).toContain('Compare a &lt; b &amp; c &gt; d')
    expect(xml).not.toContain('Compare a < b & c > d')
  })

  it('handles empty tools array', () => {
    const xml = buildToolSchemaXML([])
    expect(xml).toContain('<available_tools>')
    expect(xml).toContain('</available_tools>')
    expect(xml).not.toContain('<tool>')
  })
})

// ─── needsToolShim ─────────────────────────────────────────────────────────

describe('needsToolShim', () => {
  it('returns true for ollama models', () => {
    expect(needsToolShim('ollama', 'llama3')).toBe(true)
    expect(needsToolShim('ollama', 'codellama')).toBe(true)
    expect(needsToolShim('ollama', 'mistral')).toBe(true)
  })

  it('returns true for text-davinci models', () => {
    expect(needsToolShim('openai', 'text-davinci-003')).toBe(true)
    expect(needsToolShim('openai', 'text-davinci-002')).toBe(true)
  })

  it('returns true for gpt-3.5-turbo-instruct', () => {
    expect(needsToolShim('openai', 'gpt-3.5-turbo-instruct')).toBe(true)
  })

  it('returns false for modern OpenAI models', () => {
    expect(needsToolShim('openai', 'gpt-4o')).toBe(false)
    expect(needsToolShim('openai', 'gpt-4-turbo')).toBe(false)
    expect(needsToolShim('openai', 'gpt-3.5-turbo')).toBe(false)
  })

  it('returns false for Anthropic models', () => {
    expect(needsToolShim('anthropic', 'claude-sonnet-4-20250514')).toBe(false)
    expect(needsToolShim('anthropic', 'claude-3-haiku-20240307')).toBe(false)
  })

  it('returns false for Google models', () => {
    expect(needsToolShim('google', 'gemini-pro')).toBe(false)
    expect(needsToolShim('google', 'gemini-1.5-pro')).toBe(false)
  })

  it('returns false for OpenRouter with modern models', () => {
    expect(needsToolShim('openrouter', 'anthropic/claude-sonnet-4')).toBe(false)
    expect(needsToolShim('openrouter', 'openai/gpt-4o')).toBe(false)
  })

  it('returns true for OpenRouter with ollama prefix', () => {
    // model name starts with ollama/
    expect(needsToolShim('openrouter', 'ollama/llama3')).toBe(true)
  })
})
