import { describe, expect, it } from 'vitest'
import type { Message } from '../types'
import {
  DEFAULT_EXPORT_OPTIONS,
  type ExportOptions,
  messagesToMarkdown,
} from './export-conversation'

// ============================================================================
// Test Helpers
// ============================================================================

function makeMsg(overrides: Partial<Message> = {}): Message {
  return {
    id: 'msg-1',
    sessionId: 'sess-1',
    role: 'user',
    content: 'Hello',
    createdAt: 1700000000000,
    ...overrides,
  }
}

const noRedaction: ExportOptions = {
  redaction: { stripApiKeys: false, stripFilePaths: false, stripEmails: false },
  includeMetadata: false,
  includeArtifacts: false,
}

// ============================================================================
// messagesToMarkdown – basic output
// ============================================================================

describe('messagesToMarkdown – basic', () => {
  it('includes session name as title', () => {
    const md = messagesToMarkdown([makeMsg()], 'My Session', noRedaction)
    expect(md).toContain('# My Session')
  })

  it('defaults to "Conversation" when no session name', () => {
    const md = messagesToMarkdown([makeMsg()], undefined, noRedaction)
    expect(md).toContain('# Conversation')
  })

  it('includes exported timestamp', () => {
    const md = messagesToMarkdown([makeMsg()], 'S', noRedaction)
    expect(md).toContain('Exported')
  })

  it('labels user messages as "You"', () => {
    const md = messagesToMarkdown([makeMsg({ role: 'user' })], 'S', noRedaction)
    expect(md).toContain('### You')
  })

  it('labels assistant messages as "Assistant"', () => {
    const md = messagesToMarkdown([makeMsg({ role: 'assistant' })], 'S', noRedaction)
    expect(md).toContain('### Assistant')
  })

  it('labels system messages as "System"', () => {
    const md = messagesToMarkdown([makeMsg({ role: 'system' })], 'S', noRedaction)
    expect(md).toContain('### System')
  })

  it('includes message content', () => {
    const md = messagesToMarkdown([makeMsg({ content: 'custom text here' })], 'S', noRedaction)
    expect(md).toContain('custom text here')
  })

  it('shows model name when present', () => {
    const md = messagesToMarkdown(
      [makeMsg({ role: 'assistant', model: 'claude-3.5-sonnet' })],
      'S',
      noRedaction
    )
    expect(md).toContain('claude-3.5-sonnet')
  })

  it('shows *No content* for empty content', () => {
    const md = messagesToMarkdown([makeMsg({ content: '' })], 'S', noRedaction)
    expect(md).toContain('*No content*')
  })
})

// ============================================================================
// messagesToMarkdown – metadata
// ============================================================================

describe('messagesToMarkdown – metadata', () => {
  const withMeta: ExportOptions = {
    ...noRedaction,
    includeMetadata: true,
  }

  it('includes session info table', () => {
    const msgs = [
      makeMsg({ role: 'user', createdAt: 1700000000000 }),
      makeMsg({
        id: 'msg-2',
        role: 'assistant',
        createdAt: 1700000300000,
        model: 'gpt-4',
        costUSD: 0.05,
        toolCalls: [
          { id: 'tc-1', name: 'read_file', args: {}, status: 'success' as const, startedAt: 0 },
        ],
      }),
    ]
    const md = messagesToMarkdown(msgs, 'Test', withMeta)

    expect(md).toContain('## Session Info')
    expect(md).toContain('| Messages | 2 |')
    expect(md).toContain('| Duration |')
    expect(md).toContain('5m')
    expect(md).toContain('| Models used | 1 |')
    expect(md).toContain('$0.0500')
    expect(md).toContain('read_file')
  })

  it('shows <1m for instant conversations', () => {
    const msgs = [
      makeMsg({ createdAt: 1700000000000 }),
      makeMsg({ id: 'msg-2', createdAt: 1700000000100 }),
    ]
    const md = messagesToMarkdown(msgs, 'S', withMeta)
    expect(md).toContain('<1m')
  })
})

// ============================================================================
// messagesToMarkdown – artifacts
// ============================================================================

describe('messagesToMarkdown – artifacts', () => {
  const withArtifacts: ExportOptions = {
    ...noRedaction,
    includeArtifacts: true,
  }

  it('shows created files', () => {
    const msgs = [
      makeMsg({
        role: 'assistant',
        toolCalls: [
          {
            id: 'tc-1',
            name: 'create_file',
            args: {},
            status: 'success' as const,
            startedAt: 0,
            filePath: 'src/new.ts',
          },
        ],
      }),
    ]
    const md = messagesToMarkdown(msgs, 'S', withArtifacts)
    expect(md).toContain('## Artifacts')
    expect(md).toContain('**Created:**')
    expect(md).toContain('`src/new.ts`')
  })

  it('shows modified files', () => {
    const msgs = [
      makeMsg({
        role: 'assistant',
        toolCalls: [
          {
            id: 'tc-1',
            name: 'edit',
            args: {},
            status: 'success' as const,
            startedAt: 0,
            filePath: 'src/existing.ts',
          },
        ],
      }),
    ]
    const md = messagesToMarkdown(msgs, 'S', withArtifacts)
    expect(md).toContain('**Modified:**')
    expect(md).toContain('`src/existing.ts`')
  })

  it('shows deleted files', () => {
    const msgs = [
      makeMsg({
        role: 'assistant',
        toolCalls: [
          {
            id: 'tc-1',
            name: 'delete_file',
            args: {},
            status: 'success' as const,
            startedAt: 0,
            filePath: 'old-file.ts',
          },
        ],
      }),
    ]
    const md = messagesToMarkdown(msgs, 'S', withArtifacts)
    expect(md).toContain('**Deleted:**')
    expect(md).toContain('`old-file.ts`')
  })

  it('omits artifacts section when no file operations exist', () => {
    const md = messagesToMarkdown([makeMsg()], 'S', withArtifacts)
    expect(md).not.toContain('## Artifacts')
  })

  it('deduplicates created vs modified (create wins)', () => {
    const msgs = [
      makeMsg({
        role: 'assistant',
        toolCalls: [
          {
            id: 'tc-1',
            name: 'create_file',
            args: {},
            status: 'success' as const,
            startedAt: 0,
            filePath: 'src/new.ts',
          },
          {
            id: 'tc-2',
            name: 'edit',
            args: {},
            status: 'success' as const,
            startedAt: 1,
            filePath: 'src/new.ts',
          },
        ],
      }),
    ]
    const md = messagesToMarkdown(msgs, 'S', withArtifacts)
    expect(md).toContain('**Created:**')
    // Should NOT appear in modified since it's in created
    const modifiedSection = md.split('**Modified:**')
    // If modified section doesn't exist, the file is only in created
    expect(modifiedSection.length).toBe(1)
  })
})

// ============================================================================
// messagesToMarkdown – tool calls
// ============================================================================

describe('messagesToMarkdown – tool calls', () => {
  it('renders tool calls in details block', () => {
    const msgs = [
      makeMsg({
        role: 'assistant',
        toolCalls: [
          {
            id: 'tc-1',
            name: 'bash',
            args: { command: 'ls' },
            status: 'success' as const,
            startedAt: 0,
          },
          {
            id: 'tc-2',
            name: 'write_file',
            args: {},
            status: 'error' as const,
            startedAt: 0,
            error: 'Permission denied',
          },
        ],
      }),
    ]
    const md = messagesToMarkdown(msgs, 'S', noRedaction)
    expect(md).toContain('<details>')
    expect(md).toContain('Tool calls (2)')
    expect(md).toContain('**bash** (success)')
    expect(md).toContain('**write_file** (failed)')
    expect(md).toContain('Error: Permission denied')
  })
})

// ============================================================================
// messagesToMarkdown – thinking
// ============================================================================

describe('messagesToMarkdown – thinking blocks', () => {
  it('renders thinking in a details block', () => {
    const msgs = [
      makeMsg({
        role: 'assistant',
        content: 'Answer',
        metadata: { thinking: 'Let me think about this...' },
      }),
    ]
    const md = messagesToMarkdown(msgs, 'S', noRedaction)
    expect(md).toContain('<summary>Thinking</summary>')
    expect(md).toContain('Let me think about this...')
  })

  it('applies redaction to thinking blocks', () => {
    const msgs = [
      makeMsg({
        role: 'assistant',
        metadata: { thinking: `token: sk-${'a'.repeat(30)}` },
      }),
    ]
    const md = messagesToMarkdown(msgs, 'S', {
      ...DEFAULT_EXPORT_OPTIONS,
      includeMetadata: false,
      includeArtifacts: false,
    })
    expect(md).toContain('[REDACTED_KEY]')
    expect(md).not.toContain('sk-')
  })
})

// ============================================================================
// messagesToMarkdown – redaction integration
// ============================================================================

describe('messagesToMarkdown – redaction', () => {
  it('redacts API keys in message content', () => {
    const msgs = [makeMsg({ content: `My key is sk-${'x'.repeat(30)}` })]
    const md = messagesToMarkdown(msgs, 'S', DEFAULT_EXPORT_OPTIONS)
    expect(md).toContain('[REDACTED_KEY]')
    expect(md).not.toContain('sk-')
  })

  it('does not redact when all options are off', () => {
    const apiKey = `sk-${'y'.repeat(30)}`
    const msgs = [makeMsg({ content: `key: ${apiKey}` })]
    const md = messagesToMarkdown(msgs, 'S', noRedaction)
    expect(md).toContain(apiKey)
  })
})

// ============================================================================
// DEFAULT_EXPORT_OPTIONS
// ============================================================================

describe('DEFAULT_EXPORT_OPTIONS', () => {
  it('strips API keys by default', () => {
    expect(DEFAULT_EXPORT_OPTIONS.redaction.stripApiKeys).toBe(true)
  })

  it('does not strip file paths by default', () => {
    expect(DEFAULT_EXPORT_OPTIONS.redaction.stripFilePaths).toBe(false)
  })

  it('does not strip emails by default', () => {
    expect(DEFAULT_EXPORT_OPTIONS.redaction.stripEmails).toBe(false)
  })

  it('includes metadata by default', () => {
    expect(DEFAULT_EXPORT_OPTIONS.includeMetadata).toBe(true)
  })

  it('includes artifacts by default', () => {
    expect(DEFAULT_EXPORT_OPTIONS.includeArtifacts).toBe(true)
  })
})
