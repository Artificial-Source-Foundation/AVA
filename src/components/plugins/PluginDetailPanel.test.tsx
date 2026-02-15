import { render } from 'solid-js/web'
import { afterEach, describe, expect, it } from 'vitest'
import type { PluginCatalogItem, PluginState } from '../../types/plugin'
import { PluginDetailPanel } from './PluginDetailPanel'

const samplePlugin: PluginCatalogItem = {
  id: 'task-planner',
  name: 'Task Planner',
  description: 'Breaks goals into actionable implementation steps.',
  category: 'workflow',
}

const enabledState: PluginState = {
  installed: true,
  enabled: true,
}

afterEach(() => {
  document.body.innerHTML = ''
})

describe('PluginDetailPanel', () => {
  it('shows fallback when no plugin is selected', () => {
    const container = document.createElement('div')
    document.body.appendChild(container)
    const dispose = render(() => <PluginDetailPanel plugin={null} state={null} />, container)

    expect(container.textContent).toContain('Select a plugin to view details.')

    dispose()
  })

  it('shows selected plugin details and status', () => {
    const container = document.createElement('div')
    document.body.appendChild(container)
    const dispose = render(
      () => <PluginDetailPanel plugin={samplePlugin} state={enabledState} />,
      container
    )

    expect(container.textContent).toContain('Task Planner')
    expect(container.textContent).toContain('workflow')
    expect(container.textContent).toContain('Installed + enabled')

    dispose()
  })
})
