import { createSignal } from 'solid-js'
import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { DEFAULT_SETTINGS } from '../../../stores/settings/settings-defaults'
import type { AppSettings } from '../../../stores/settings/settings-types'

const settingsHarness = vi.hoisted(() => ({
  settings: undefined as unknown as () => AppSettings,
  updateSettings: vi.fn<(patch: Partial<AppSettings>) => void>(),
}))

vi.mock(import('lucide-solid'), async (importOriginal) => {
  const actual = await importOriginal()
  return {
    ...actual,
    FolderOpen: () => null,
    Plus: () => null,
    Sparkles: () => null,
  }
})

vi.mock('../../../stores/settings', () => ({
  useSettings: () => ({
    settings: settingsHarness.settings,
    updateSettings: settingsHarness.updateSettings,
  }),
}))

vi.mock('./SkillsTab', () => ({
  RulesAndCommandsContent: () => <div data-testid="rules-and-commands" />,
}))

vi.mock('./skills-tab-card', () => ({
  SkillForm: () => null,
}))

import { SkillsSettingsTab } from './SkillsSettingsTab'

async function flush(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

function click(element: Element): void {
  element.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true }))
}

describe('SkillsSettingsTab', () => {
  let container: HTMLDivElement
  let dispose: (() => void) | undefined

  beforeEach(() => {
    container = document.createElement('div')
    document.body.appendChild(container)

    const [settings, setSettings] = createSignal<AppSettings>({
      ...DEFAULT_SETTINGS,
      hiddenBuiltInSkills: ['react-patterns'],
    })

    settingsHarness.settings = settings
    settingsHarness.updateSettings.mockReset()
    settingsHarness.updateSettings.mockImplementation((patch) => {
      setSettings((prev) => ({ ...prev, ...patch }))
    })
  })

  afterEach(() => {
    dispose?.()
    dispose = undefined
    document.body.innerHTML = ''
  })

  it('shows a recovery path for hidden built-in skills and restores them', async () => {
    dispose = render(() => <SkillsSettingsTab />, container)

    await flush()

    expect(container.textContent).toContain('Hidden built-in skills')
    expect(container.textContent).toContain('React Patterns')

    const restoreButton = Array.from(container.querySelectorAll('button')).find(
      (button) => button.textContent?.trim() === 'Restore'
    )
    expect(restoreButton).toBeInstanceOf(HTMLButtonElement)

    click(restoreButton!)
    await flush()

    expect(settingsHarness.updateSettings).toHaveBeenCalledWith({ hiddenBuiltInSkills: [] })
    expect(container.textContent).not.toContain('Hidden built-in skills')
    expect(container.textContent).toContain('React Patterns')
  })
})
