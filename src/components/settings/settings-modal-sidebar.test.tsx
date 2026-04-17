import { createSignal } from 'solid-js'
import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

vi.mock('lucide-solid', () => ({
  ArrowLeft: () => null,
  Search: () => null,
  X: () => null,
}))

vi.mock('./settings-modal-config', () => {
  const Icon = () => null

  return {
    tabGroups: [
      {
        label: 'General',
        tabs: [{ id: 'general', label: 'General', icon: Icon, keywords: ['general'] }],
      },
      {
        label: 'Tools',
        tabs: [
          { id: 'providers', label: 'Providers', icon: Icon, keywords: ['shared'] },
          { id: 'skills', label: 'Skills', icon: Icon, keywords: ['shared'] },
        ],
      },
    ],
    settingsSearchIndex: [
      { label: 'Shared provider setting', tab: 'providers', tabLabel: 'Providers' },
      { label: 'Shared skill setting', tab: 'skills', tabLabel: 'Skills' },
    ],
  }
})

import { SettingsModalSidebar } from './settings-modal-sidebar'

async function flush(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

function click(element: Element): void {
  element.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true }))
}

describe('SettingsModalSidebar', () => {
  let container: HTMLDivElement
  let dispose: (() => void) | undefined

  beforeEach(() => {
    container = document.createElement('div')
    document.body.appendChild(container)
  })

  afterEach(() => {
    dispose?.()
    dispose = undefined
    document.body.innerHTML = ''
  })

  it('keeps a manual tab change while the current query stays active', async () => {
    let activeTab!: () => 'general' | 'providers' | 'skills'

    dispose = render(() => {
      const [active, setActive] = createSignal<'general' | 'providers' | 'skills'>('general')
      const [search, setSearch] = createSignal('')
      activeTab = active

      return (
        <SettingsModalSidebar
          activeTab={active}
          onSelectTab={setActive}
          onBack={() => {}}
          search={search}
          onSearchChange={setSearch}
        />
      )
    }, container)

    const input = container.querySelector('input') as HTMLInputElement | null
    expect(input).toBeInstanceOf(HTMLInputElement)

    input!.value = 'shared'
    input!.dispatchEvent(new Event('input', { bubbles: true }))
    await flush()

    expect(activeTab()).toBe('providers')

    const skillsButton = Array.from(container.querySelectorAll('button')).find(
      (button) => button.textContent?.trim() === 'Skills'
    )
    expect(skillsButton).toBeInstanceOf(HTMLButtonElement)

    click(skillsButton!)
    await flush()

    expect(activeTab()).toBe('skills')

    await flush()
    expect(activeTab()).toBe('skills')
  })
})
