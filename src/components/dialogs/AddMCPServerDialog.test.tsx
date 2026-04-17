import { createSignal } from 'solid-js'
import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

vi.mock('lucide-solid', () => ({
  Plus: () => null,
  Server: () => null,
}))

import { AddMCPServerDialog } from './AddMCPServerDialog'

async function flush(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

function click(element: Element): void {
  element.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true }))
}

describe('AddMCPServerDialog', () => {
  let container: HTMLDivElement
  let dispose: (() => void) | undefined
  let parentEscapeListener: ((event: KeyboardEvent) => void) | undefined

  beforeEach(() => {
    container = document.createElement('div')
    document.body.appendChild(container)
  })

  afterEach(() => {
    if (parentEscapeListener) {
      window.removeEventListener('keydown', parentEscapeListener)
      parentEscapeListener = undefined
    }
    dispose?.()
    dispose = undefined
    document.body.innerHTML = ''
  })

  it('closes on Escape without bubbling to the parent settings surface', async () => {
    const onSave = vi.fn()
    const onClose = vi.fn()
    const onParentEscape = vi.fn()

    parentEscapeListener = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        onParentEscape()
      }
    }

    window.addEventListener('keydown', parentEscapeListener)

    dispose = render(() => {
      const [open, setOpen] = createSignal(true)

      return (
        <>
          <button type="button" aria-label="Settings opener">
            Settings opener
          </button>
          <AddMCPServerDialog
            open={open()}
            onClose={() => {
              onClose()
              setOpen(false)
            }}
            onSave={onSave}
          />
        </>
      )
    }, container)

    await flush()

    const opener = document.querySelector(
      '[aria-label="Settings opener"]'
    ) as HTMLButtonElement | null
    expect(opener).toBeInstanceOf(HTMLButtonElement)
    opener?.focus()

    opener?.dispatchEvent(
      new KeyboardEvent('keydown', { key: 'Escape', bubbles: true, cancelable: true })
    )
    await flush()
    expect(onClose).toHaveBeenCalledTimes(1)
    expect(onParentEscape).not.toHaveBeenCalled()
    expect(onSave).not.toHaveBeenCalled()
    expect(container.textContent).not.toContain('Add MCP Server')
  })

  it('resets back to presets and clears manual inputs when reopened', async () => {
    const onSave = vi.fn()

    dispose = render(() => {
      const [open, setOpen] = createSignal(true)

      return (
        <>
          <button type="button" aria-label="Open add MCP dialog" onClick={() => setOpen(true)}>
            Open
          </button>
          <button type="button" aria-label="Close add MCP dialog" onClick={() => setOpen(false)}>
            Close
          </button>
          <AddMCPServerDialog open={open()} onClose={() => setOpen(false)} onSave={onSave} />
        </>
      )
    }, container)

    await flush()

    const manualTab = Array.from(document.querySelectorAll('button')).find(
      (button) => button.textContent?.trim() === 'Manual Configuration'
    )
    expect(manualTab).toBeInstanceOf(HTMLButtonElement)
    click(manualTab!)
    await flush()

    const nameInput = document.querySelector(
      'input[placeholder="my-server"]'
    ) as HTMLInputElement | null
    const commandInput = document.querySelector(
      'input[placeholder="npx"]'
    ) as HTMLInputElement | null

    expect(nameInput).toBeInstanceOf(HTMLInputElement)
    expect(commandInput).toBeInstanceOf(HTMLInputElement)

    nameInput!.value = 'custom-server'
    nameInput!.dispatchEvent(new Event('input', { bubbles: true }))
    commandInput!.value = 'node'
    commandInput!.dispatchEvent(new Event('input', { bubbles: true }))
    await flush()

    const closeButton = document.querySelector(
      '[aria-label="Close add MCP dialog"]'
    ) as HTMLButtonElement | null
    expect(closeButton).toBeInstanceOf(HTMLButtonElement)
    click(closeButton!)
    await flush()

    const openButton = document.querySelector(
      '[aria-label="Open add MCP dialog"]'
    ) as HTMLButtonElement | null
    expect(openButton).toBeInstanceOf(HTMLButtonElement)
    click(openButton!)
    await flush()

    expect(document.querySelector('input[placeholder="my-server"]')).toBeNull()

    const reopenedManualTab = Array.from(document.querySelectorAll('button')).find(
      (button) => button.textContent?.trim() === 'Manual Configuration'
    )
    expect(reopenedManualTab).toBeInstanceOf(HTMLButtonElement)
    click(reopenedManualTab!)
    await flush()

    const reopenedNameInput = document.querySelector(
      'input[placeholder="my-server"]'
    ) as HTMLInputElement | null
    const reopenedCommandInput = document.querySelector(
      'input[placeholder="npx"]'
    ) as HTMLInputElement | null

    expect(reopenedNameInput?.value).toBe('')
    expect(reopenedCommandInput?.value).toBe('')
  })
})
