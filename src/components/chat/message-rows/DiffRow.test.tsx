import { render } from 'solid-js/web'
import { afterEach, describe, expect, it, vi } from 'vitest'

import type { ToolCall } from '../../../types'

vi.mock('../../ui/DiffViewer', () => ({
  DiffViewer: () => <div data-testid="diff-viewer">Diff Content</div>,
}))

import { DiffRow } from './DiffRow'

async function flush(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

describe('DiffRow', () => {
  let container: HTMLDivElement

  afterEach(() => {
    container?.remove()
  })

  it('renders an edit diff', async () => {
    container = document.createElement('div')
    document.body.appendChild(container)

    const toolCall: ToolCall = {
      id: 'tool-1',
      name: 'edit',
      args: { path: 'src/index.ts' },
      status: 'success',
      startedAt: 10,
      completedAt: 20,
      filePath: 'src/index.ts',
      diff: {
        oldContent: 'const x = 1;',
        newContent: 'const x = 2;',
      },
    }

    render(() => <DiffRow toolCall={toolCall} />, container)
    await flush()

    expect(container.textContent).toContain('edit src/index.ts')
    expect(container.textContent).toContain('Applied')
  })

  it('renders a new file diff', async () => {
    container = document.createElement('div')
    document.body.appendChild(container)

    const toolCall: ToolCall = {
      id: 'tool-1',
      name: 'write',
      args: { path: 'src/new.ts' },
      status: 'success',
      startedAt: 10,
      completedAt: 20,
      filePath: 'src/new.ts',
      diff: {
        oldContent: '',
        newContent: 'export const foo = 1;\nexport const bar = 2;',
      },
    }

    render(() => <DiffRow toolCall={toolCall} />, container)
    await flush()

    expect(container.textContent).toContain('write src/new.ts')
    expect(container.textContent).toContain('+2 lines')
  })

  describe('accessibility - keyboard navigation', () => {
    it('has keyboard-focusable scrollable region when expanded', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      // Large diff (more than 30 lines to avoid auto-expand)
      const oldContent = Array(20).fill('original line').join('\n')
      const newContent = Array(20).fill('modified line').join('\n')

      const toolCall: ToolCall = {
        id: 'tool-1',
        name: 'edit',
        args: { path: 'src/large-file.ts' },
        status: 'success',
        startedAt: 10,
        completedAt: 20,
        filePath: 'src/large-file.ts',
        diff: {
          oldContent,
          newContent,
        },
      }

      render(() => <DiffRow toolCall={toolCall} />, container)
      await flush()

      // Initially collapsed for large diffs
      let scrollable = container.querySelector('[data-scrollable]')
      expect(scrollable).toBeNull()

      // Click header to expand
      const header = container.querySelector('[role="button"]')
      expect(header).not.toBeNull()
      header?.dispatchEvent(new MouseEvent('click', { bubbles: true }))
      await flush()

      // Scrollable region should now be visible and keyboard-focusable
      scrollable = container.querySelector('[data-scrollable]')
      expect(scrollable).not.toBeNull()
      expect(scrollable?.getAttribute('tabIndex')).toBe('0')
      expect(scrollable?.getAttribute('role')).toBe('region')
      expect(scrollable?.getAttribute('aria-label')).toContain('Diff view')
      expect(scrollable?.getAttribute('aria-label')).toContain('src/large-file.ts')
    })

    it('scrollable region has visible focus style', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      const toolCall: ToolCall = {
        id: 'tool-1',
        name: 'write',
        args: { path: 'src/file.ts' },
        status: 'success',
        startedAt: 10,
        completedAt: 20,
        filePath: 'src/file.ts',
        diff: {
          oldContent: '',
          newContent: 'export const foo = 1;',
        },
      }

      render(() => <DiffRow toolCall={toolCall} />, container)
      await flush()

      // Auto-expanded for small diffs
      const scrollable = container.querySelector('[data-scrollable]')
      expect(scrollable).not.toBeNull()

      // Should have focus-visible ring classes
      expect(scrollable?.classList.contains('focus-visible:ring-2')).toBe(true)
    })

    it('header has button semantics and focus-visible styling', async () => {
      container = document.createElement('div')
      document.body.appendChild(container)

      const toolCall: ToolCall = {
        id: 'tool-1',
        name: 'edit',
        args: { path: 'src/test.ts' },
        status: 'success',
        startedAt: 10,
        completedAt: 20,
        filePath: 'src/test.ts',
        diff: {
          oldContent: 'const a = 1;',
          newContent: 'const a = 2;',
        },
      }

      render(() => <DiffRow toolCall={toolCall} />, container)
      await flush()

      const header = container.querySelector('.tool-card-header')
      expect(header?.getAttribute('role')).toBe('button')
      expect(header?.getAttribute('tabIndex')).toBe('0')
      // Small diffs auto-expand, so aria-expanded should be 'true'
      expect(header?.getAttribute('aria-expanded')).toBe('true')

      // Should have focus-visible ring
      expect(header?.classList.contains('focus-visible:ring-2')).toBe(true)
    })
  })
})
