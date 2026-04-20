import { render } from 'solid-js/web'
import { afterEach, describe, expect, it, vi } from 'vitest'

import type { ToolCall } from '../../../types'

vi.mock('../../ui/DiffViewer', () => ({
  DiffViewer: () => <div data-testid="diff-viewer">Diff Viewer</div>,
}))

import { CommandOutputRow } from './CommandOutputRow'

async function flush(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

describe('CommandOutputRow', () => {
  let container: HTMLDivElement

  afterEach(() => {
    container?.remove()
  })

  it('renders a bash command with output', async () => {
    container = document.createElement('div')
    document.body.appendChild(container)

    const toolCall: ToolCall = {
      id: 'tool-1',
      name: 'bash',
      args: { command: 'ls -la', exitCode: 0 },
      status: 'success',
      startedAt: 10,
      completedAt: 20,
      output: 'total 10\ndrwxr-xr-x',
    }

    render(() => <CommandOutputRow toolCall={toolCall} />, container)
    await flush()

    expect(container.textContent).toContain('ls -la')
    expect(container.textContent).toContain('exit 0')
  })

  describe('accessibility - interactive attributes', () => {
    it('uses a native enabled button when command has output (expandable)', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      const toolCall: ToolCall = {
        id: 'tool-1',
        name: 'bash',
        args: { command: 'ls -la', exitCode: 0 },
        status: 'success',
        startedAt: 10,
        completedAt: 20,
        output: 'total 10\ndrwxr-xr-x',
      }

      render(() => <CommandOutputRow toolCall={toolCall} />, container)
      await flush()

      const header = container.querySelector('button.tool-card-header')
      expect(header).not.toBeNull()
      expect((header as HTMLButtonElement | null)?.disabled).toBe(false)
      expect(header?.getAttribute('aria-expanded')).toBe('false')
    })

    it('disables the native button when command has no output (not expandable)', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      const toolCall: ToolCall = {
        id: 'tool-1',
        name: 'bash',
        args: { command: 'long-running-cmd' },
        status: 'running',
        startedAt: 10,
        // No output yet
      }

      render(() => <CommandOutputRow toolCall={toolCall} />, container)
      await flush()

      const header = container.querySelector('button.tool-card-header') as HTMLButtonElement | null
      expect(header?.disabled).toBe(true)
      expect(header?.getAttribute('aria-expanded')).toBeNull()
    })

    it('has visible focus styles when expandable (cursor-pointer and focus-visible ring)', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      const toolCall: ToolCall = {
        id: 'tool-1',
        name: 'bash',
        args: { command: 'ls', exitCode: 0 },
        status: 'success',
        startedAt: 10,
        completedAt: 20,
        output: 'file1.txt\nfile2.txt',
      }

      render(() => <CommandOutputRow toolCall={toolCall} />, container)
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
        name: 'bash',
        args: { command: 'pending-cmd' },
        status: 'pending',
        startedAt: 10,
        // No output yet
      }

      render(() => <CommandOutputRow toolCall={toolCall} />, container)
      await flush()

      const header = container.querySelector('.tool-card-header')
      // Should NOT have cursor-pointer when not expandable
      expect(header?.classList.contains('cursor-pointer')).toBe(false)
    })

    it('has interactive attributes with streaming output', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      const toolCall: ToolCall = {
        id: 'tool-1',
        name: 'bash',
        args: { command: 'tail -f log.txt' },
        status: 'running',
        startedAt: 10,
        streamingOutput: 'line 1\nline 2',
      }

      render(() => <CommandOutputRow toolCall={toolCall} />, container)
      await flush()

      const header = container.querySelector('button.tool-card-header')
      expect(header).not.toBeNull()
      // With streaming output, it should be expandable
      expect((header as HTMLButtonElement | null)?.disabled).toBe(false)
      expect(header?.classList.contains('cursor-pointer')).toBe(true)
      expect(header?.classList.contains('focus-visible:ring-2')).toBe(true)
    })

    it('treats error as expandable content (failed bash command)', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      const toolCall: ToolCall = {
        id: 'tool-1',
        name: 'bash',
        args: { command: 'invalid-command', exitCode: 127 },
        status: 'error',
        startedAt: 10,
        completedAt: 20,
        error: 'command not found: invalid-command',
      }

      render(() => <CommandOutputRow toolCall={toolCall} />, container)
      await flush()

      // Should be expandable because it has error text
      const header = container.querySelector('button.tool-card-header')
      expect(header).not.toBeNull()
      expect((header as HTMLButtonElement | null)?.disabled).toBe(false)
      expect(header?.classList.contains('cursor-pointer')).toBe(true)

      // Error text should be visible in the output body (auto-expanded for errors)
      expect(container.textContent).toContain('command not found')
    })

    it('shows error text with error styling when bash fails', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      const toolCall: ToolCall = {
        id: 'tool-1',
        name: 'bash',
        args: { command: 'cat /nonexistent/file', exitCode: 1 },
        status: 'error',
        startedAt: 10,
        completedAt: 20,
        error: 'cat: /nonexistent/file: No such file or directory',
      }

      render(() => <CommandOutputRow toolCall={toolCall} />, container)
      await flush()

      // Should show error badge
      expect(container.textContent).toContain('exit 1')
      // Should show error text
      expect(container.textContent).toContain('No such file or directory')
    })

    it('restored tool calls with only error field are expandable', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      // Simulates a recovered/restored tool call that only has error, no output
      const toolCall: ToolCall = {
        id: 'restored-tool-1',
        name: 'bash',
        args: { command: 'npm test' },
        status: 'error',
        startedAt: 1000,
        completedAt: 5000,
        output: undefined,
        error: 'npm ERR! Tests failed\nnpm ERR! Exit status 1',
      }

      render(() => <CommandOutputRow toolCall={toolCall} />, container)
      await flush()

      // Should be expandable even though output is undefined
      const header = container.querySelector('button.tool-card-header')
      expect(header).not.toBeNull()
      expect(header?.getAttribute('aria-expanded')).toBe('false')

      // Error content should be accessible
      expect(container.textContent).toContain('npm ERR! Tests failed')
    })
  })
})
