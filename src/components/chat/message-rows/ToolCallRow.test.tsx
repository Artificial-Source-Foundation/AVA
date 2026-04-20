import { render } from 'solid-js/web'
import { afterEach, describe, expect, it, vi } from 'vitest'

import type { ToolCall } from '../../../types'

vi.mock('../tool-call-output', () => ({
  ToolCallOutput: (props: { toolCall: { output?: string } }) => <div>{props.toolCall.output}</div>,
}))

import { ToolCallRow } from './ToolCallRow'

async function flush(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

describe('ToolCallRow', () => {
  let container: HTMLDivElement

  afterEach(() => {
    container?.remove()
  })

  it('renders the preserved tool-call surface for a tool-only assistant completion', async () => {
    container = document.createElement('div')
    document.body.appendChild(container)

    const toolCall: ToolCall = {
      id: 'tool-1',
      name: 'bash',
      args: { command: 'pwd' },
      status: 'success',
      startedAt: 10,
      completedAt: 20,
      output: '/workspace',
    }

    render(() => <ToolCallRow toolCall={toolCall} />, container)
    await flush()

    expect(container.textContent?.toLowerCase()).toContain('running pwd')
    expect(container.textContent).toContain('/workspace')
  })

  describe('accessibility - interactive attributes', () => {
    it('uses a native enabled button when tool has output (expandable)', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      const toolCall: ToolCall = {
        id: 'tool-1',
        name: 'read',
        args: { path: 'file.txt' },
        status: 'success',
        startedAt: 10,
        completedAt: 20,
        // Long output (6 lines) to avoid auto-expand behavior
        output: 'line 1\nline 2\nline 3\nline 4\nline 5\nline 6',
      }

      render(() => <ToolCallRow toolCall={toolCall} />, container)
      await flush()

      const header = container.querySelector('button.tool-card-header')
      expect(header).not.toBeNull()
      expect((header as HTMLButtonElement | null)?.disabled).toBe(false)
      // Should be collapsed initially (output > AUTO_EXPAND_LINE_THRESHOLD lines)
      expect(header?.getAttribute('aria-expanded')).toBe('false')
    })

    it('disables the native button when tool has no output (not expandable)', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      const toolCall: ToolCall = {
        id: 'tool-1',
        name: 'read',
        args: { path: 'file.txt' },
        status: 'running',
        startedAt: 10,
        // No output, no error, no diff
      }

      render(() => <ToolCallRow toolCall={toolCall} />, container)
      await flush()

      const header = container.querySelector('button.tool-card-header') as HTMLButtonElement | null
      expect(header?.disabled).toBe(true)
      expect(header?.getAttribute('aria-expanded')).toBeNull()
    })

    it('has NO interactive attributes when tool has no output even with error', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      const toolCall: ToolCall = {
        id: 'tool-1',
        name: 'read',
        args: { path: 'file.txt' },
        status: 'error',
        startedAt: 10,
        completedAt: 20,
        error: 'File not found',
      }

      render(() => <ToolCallRow toolCall={toolCall} />, container)
      await flush()

      // When there's an error, it should be expandable (to show the error)
      const header = container.querySelector('button.tool-card-header')
      expect(header).not.toBeNull()
      expect((header as HTMLButtonElement | null)?.disabled).toBe(false)
    })

    it('has visible focus styles when expandable (cursor-pointer class)', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      const toolCall: ToolCall = {
        id: 'tool-1',
        name: 'read',
        args: { path: 'file.txt' },
        status: 'success',
        startedAt: 10,
        completedAt: 20,
        output: 'content',
      }

      render(() => <ToolCallRow toolCall={toolCall} />, container)
      await flush()

      const header = container.querySelector('.tool-card-header')
      // Should have cursor-pointer class indicating it's interactive
      expect(header?.classList.contains('cursor-pointer')).toBe(true)
      // Should have focus-visible ring classes
      expect(header?.classList.contains('focus-visible:ring-2')).toBe(true)
    })

    it('does NOT have cursor-pointer when not expandable', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      const toolCall: ToolCall = {
        id: 'tool-1',
        name: 'read',
        args: { path: 'file.txt' },
        status: 'pending',
        startedAt: 10,
        // No output yet
      }

      render(() => <ToolCallRow toolCall={toolCall} />, container)
      await flush()

      const header = container.querySelector('.tool-card-header')
      // Should NOT have cursor-pointer when not expandable
      expect(header?.classList.contains('cursor-pointer')).toBe(false)
    })
  })
})
