import { render } from 'solid-js/web'
import { describe, expect, it } from 'vitest'
import { TeamPanel } from './TeamPanel'

describe('TeamPanel', () => {
  it('renders HQ Team header', () => {
    const container = document.createElement('div')
    render(() => <TeamPanel />, container)
    expect(container.textContent).toContain('HQ Team')
  })

  it('shows empty state when no team active', () => {
    const container = document.createElement('div')
    render(() => <TeamPanel />, container)
    expect(container.textContent).toContain('No team active')
  })

  it('renders TEAM METRICS footer', () => {
    const container = document.createElement('div')
    render(() => <TeamPanel />, container)
    expect(container.textContent).toContain('TEAM METRICS')
  })
})
