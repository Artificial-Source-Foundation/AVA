import { render } from 'solid-js/web'
import { afterEach, describe, expect, it } from 'vitest'
import type { PluginCatalogItem, PluginMountRegistration, PluginState } from '../../types/plugin'
import { PluginDetailPanel } from './PluginDetailPanel'

const samplePlugin: PluginCatalogItem = {
  id: 'task-planner',
  name: 'Task Planner',
  description: 'Breaks goals into actionable implementation steps.',
  category: 'workflow',
  version: '1.4.0',
  source: 'official',
  trust: 'verified',
  changelogSummary: 'Added milestone templates and dependency hints.',
}

const enabledState: PluginState = {
  installed: true,
  enabled: true,
}

const mounts: PluginMountRegistration[] = [
  {
    plugin: 'task-planner',
    mount: {
      id: 'task-planner.settings',
      location: 'settings.section',
      label: 'Task Planner',
      description: 'Task Planner settings section',
    },
  },
]

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
    expect(container.textContent).toContain('v1.4.0')
    expect(container.textContent).toContain('official')
    expect(container.textContent).toContain('verified')
    expect(container.textContent).toContain('Installed + enabled')

    dispose()
  })

  it('shows plugin mount metadata when available', () => {
    const container = document.createElement('div')
    document.body.appendChild(container)
    const dispose = render(
      () => <PluginDetailPanel plugin={samplePlugin} state={enabledState} mounts={mounts} />,
      container
    )

    expect(container.textContent).toContain('Exposed UI Mounts')
    expect(container.textContent).toContain('settings.section')
    expect(container.textContent).toContain('Task Planner settings section')
    expect(container.textContent).toContain('task-planner.settings')

    dispose()
  })
})
