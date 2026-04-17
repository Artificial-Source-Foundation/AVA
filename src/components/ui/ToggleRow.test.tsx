import { render } from 'solid-js/web'
import { describe, expect, it, vi } from 'vitest'

vi.mock('./Toggle', () => ({
  Toggle: (props: {
    checked?: boolean
    disabled?: boolean
    'aria-labelledby'?: string
    'aria-describedby'?: string
  }) => <button type="button" data-testid="mock-toggle" {...props} />,
}))

import { ToggleRow } from './ToggleRow'

describe('ToggleRow', () => {
  it('wires the visible label and description into switch accessibility', () => {
    const container = document.createElement('div')
    render(
      () => (
        <ToggleRow
          label="Show memory panel"
          description="Display the memory panel in the sidebar"
          checked
          onChange={() => {}}
        />
      ),
      container
    )

    const toggle = container.querySelector('[data-testid="mock-toggle"]') as HTMLElement | null
    expect(toggle).toBeTruthy()

    const labelId = toggle?.getAttribute('aria-labelledby')
    const descriptionId = toggle?.getAttribute('aria-describedby')

    expect(labelId).toBeTruthy()
    expect(descriptionId).toBeTruthy()
    expect(container.querySelector(`#${labelId}`)?.textContent).toBe('Show memory panel')
    expect(container.querySelector(`#${descriptionId}`)?.textContent).toBe(
      'Display the memory panel in the sidebar'
    )
  })
})
