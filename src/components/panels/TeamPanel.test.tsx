import { render } from 'solid-js/web'
import { describe, expect, it } from 'vitest'
import { TeamPanel } from './TeamPanel'

describe('TeamPanel', () => {
  it('renders 4-tier hierarchy header', () => {
    const container = document.createElement('div')
    render(() => <TeamPanel />, container)
    expect(container.textContent).toContain('Director')
    expect(container.textContent).toContain('Overall:')
  })

  it('updates on praxis progress events', async () => {
    const container = document.createElement('div')
    render(() => <TeamPanel />, container)
    window.dispatchEvent(
      new CustomEvent('praxis:progress-updated', {
        detail: {
          mode: 'full',
          leads: [
            {
              id: 'lead-1',
              domain: 'Frontend',
              status: 'complete',
              engineers: [
                { id: 'eng-1', task: 'streaming-fuzzy.ts', status: 'complete', reviewAttempts: 1 },
              ],
            },
          ],
        },
      })
    )

    expect(container.textContent).toContain('Tech Lead: Frontend')
    expect(container.textContent).toContain('Overall: 1/1 engineers complete')
  })
})
