/**
 * Shared Settings Components Tests
 *
 * Verifies that shared components render correctly and use theme tokens.
 */

import { render } from 'solid-js/web'
import { describe, expect, it } from 'vitest'
import {
  SETTINGS_CARD_GAP,
  SettingsButton,
  SettingsCard,
  SettingsCardSimple,
  SettingsInput,
  SettingsPageTitle,
  SettingsSelect,
  SettingsStatusBadge,
  SettingsTabContainer,
  SettingsTextarea,
} from './shared-settings-components'

// Simple icon component for testing
const TestIcon = (props: { class?: string; style?: Record<string, string> }) => (
  <svg class={props.class} style={props.style} data-testid="test-icon" aria-label="Test icon">
    <title>Test icon</title>
    <circle cx="8" cy="8" r="6" />
  </svg>
)

describe('SettingsCard', () => {
  it('renders with title and children', () => {
    const container = document.createElement('div')
    render(
      () => (
        <SettingsCard title="Test Card">
          <div data-testid="child">Child content</div>
        </SettingsCard>
      ),
      container
    )

    expect(container.textContent).toContain('Test Card')
    expect(container.textContent).toContain('Child content')
  })

  it('renders with description', () => {
    const container = document.createElement('div')
    render(
      () => (
        <SettingsCard title="Test Card" description="Card description">
          <div>Content</div>
        </SettingsCard>
      ),
      container
    )

    expect(container.textContent).toContain('Card description')
  })

  it('renders with icon', () => {
    const container = document.createElement('div')
    render(
      () => (
        <SettingsCard title="Test Card" icon={TestIcon}>
          <div>Content</div>
        </SettingsCard>
      ),
      container
    )

    expect(container.querySelector('svg')).toBeTruthy()
  })

  it('uses compact gap when specified', () => {
    const container = document.createElement('div')
    render(
      () => (
        <SettingsCard title="Compact Card" compact>
          <div>Content</div>
        </SettingsCard>
      ),
      container
    )

    const card = container.firstChild as HTMLElement
    expect(card?.getAttribute('style')).toContain('gap: 12px')
  })

  it('uses standard gap by default', () => {
    const container = document.createElement('div')
    render(
      () => (
        <SettingsCard title="Standard Card">
          <div>Content</div>
        </SettingsCard>
      ),
      container
    )

    const card = container.firstChild as HTMLElement
    expect(card?.getAttribute('style')).toContain('gap: 16px')
  })

  it('renders theme tokens in nested elements', () => {
    const container = document.createElement('div')
    render(
      () => (
        <SettingsCard title="Theme Card">
          <div>Content</div>
        </SettingsCard>
      ),
      container
    )

    // Verify theme tokens are used in the rendered output
    // (text-primary is used in the title)
    expect(container.innerHTML).toContain('var(--text-primary)')
  })
})

describe('SettingsCardSimple', () => {
  it('renders without header', () => {
    const container = document.createElement('div')
    render(
      () => (
        <SettingsCardSimple>
          <div>Simple content</div>
        </SettingsCardSimple>
      ),
      container
    )

    expect(container.textContent).toContain('Simple content')
    // Should not have a title element
    expect(container.querySelector('h3')).toBeFalsy()
  })

  it('renders as div element', () => {
    const container = document.createElement('div')
    render(
      () => (
        <SettingsCardSimple>
          <div>Content</div>
        </SettingsCardSimple>
      ),
      container
    )

    // Verify it renders a div container
    expect(container.querySelector('div')).toBeTruthy()
  })
})

describe('SettingsPageTitle', () => {
  it('renders title text', () => {
    const container = document.createElement('div')
    render(() => <SettingsPageTitle>Page Title</SettingsPageTitle>, container)

    expect(container.textContent).toContain('Page Title')
  })

  it('renders as h1 element', () => {
    const container = document.createElement('div')
    render(() => <SettingsPageTitle>Title</SettingsPageTitle>, container)

    expect(container.querySelector('h1')).toBeTruthy()
  })

  it('uses theme tokens for text color', () => {
    const container = document.createElement('div')
    render(() => <SettingsPageTitle>Title</SettingsPageTitle>, container)

    const title = container.querySelector('h1')
    const style = title?.getAttribute('style') || ''
    expect(style).toContain('var(--text-primary)')
  })
})

describe('SettingsButton', () => {
  it('renders with text', () => {
    const container = document.createElement('div')
    render(() => <SettingsButton onClick={() => {}}>Click me</SettingsButton>, container)

    expect(container.textContent).toContain('Click me')
  })

  it('renders as a button element', () => {
    const container = document.createElement('div')
    render(() => <SettingsButton onClick={() => {}}>Click me</SettingsButton>, container)

    // Button should be rendered
    expect(container.querySelector('button')).toBeTruthy()
    expect(container.querySelector('button')?.textContent).toContain('Click me')
  })

  it('uses primary variant styling', () => {
    const container = document.createElement('div')
    render(
      () => (
        <SettingsButton onClick={() => {}} variant="primary">
          Primary
        </SettingsButton>
      ),
      container
    )

    const button = container.querySelector('button')
    const style = button?.getAttribute('style') || ''
    expect(style).toContain('var(--accent)')
    expect(style).toContain('var(--text-on-accent)')
  })

  it('uses secondary variant by default', () => {
    const container = document.createElement('div')
    render(() => <SettingsButton onClick={() => {}}>Secondary</SettingsButton>, container)

    const button = container.querySelector('button')
    const style = button?.getAttribute('style') || ''
    expect(style).toContain('var(--surface-raised)')
    expect(style).toContain('var(--text-primary)')
  })
})

describe('SettingsInput', () => {
  it('renders with value and placeholder', () => {
    const container = document.createElement('div')
    render(
      () => <SettingsInput value="test value" onInput={() => {}} placeholder="Enter text" />,
      container
    )

    const input = container.querySelector('input')
    expect(input?.getAttribute('placeholder')).toBe('Enter text')
    expect(input?.value).toBe('test value')
  })

  it('renders with visible label when provided', () => {
    const container = document.createElement('div')
    render(
      () => <SettingsInput value="" onInput={() => {}} label="Test Label" id="test-input" />,
      container
    )

    const label = container.querySelector('label')
    expect(label).toBeTruthy()
    expect(label?.textContent).toBe('Test Label')
    expect(label?.getAttribute('for')).toBe('test-input')

    const input = container.querySelector('input')
    expect(input?.getAttribute('id')).toBe('test-input')
  })

  it('uses aria-label when visible label is not provided', () => {
    const container = document.createElement('div')
    render(
      () => <SettingsInput value="" onInput={() => {}} ariaLabel="Accessible label" />,
      container
    )

    const input = container.querySelector('input')
    expect(input?.getAttribute('aria-label')).toBe('Accessible label')
  })

  it('has visible focus indication', () => {
    const container = document.createElement('div')
    render(() => <SettingsInput value="" onInput={() => {}} />, container)

    // Check for focus ring classes
    expect(container.innerHTML).toContain('focus-within:ring-2')
    expect(container.innerHTML).toContain('focus-within:ring-[var(--accent)]')
  })

  it('uses theme tokens', () => {
    const container = document.createElement('div')
    render(() => <SettingsInput value="" onInput={() => {}} />, container)

    // Verify theme tokens are used in the rendered HTML
    expect(container.innerHTML).toContain('var(--surface-sunken)')
    expect(container.innerHTML).toContain('var(--border-subtle)')
  })
})

describe('SettingsTextarea', () => {
  it('renders with value and placeholder', () => {
    const container = document.createElement('div')
    render(
      () => <SettingsTextarea value="test content" onInput={() => {}} placeholder="Enter text" />,
      container
    )

    const textarea = container.querySelector('textarea')
    expect(textarea?.getAttribute('placeholder')).toBe('Enter text')
    expect(textarea?.value).toBe('test content')
  })

  it('renders with visible label when provided', () => {
    const container = document.createElement('div')
    render(
      () => (
        <SettingsTextarea value="" onInput={() => {}} label="Textarea Label" id="test-textarea" />
      ),
      container
    )

    const label = container.querySelector('label')
    expect(label).toBeTruthy()
    expect(label?.textContent).toBe('Textarea Label')
    expect(label?.getAttribute('for')).toBe('test-textarea')

    const textarea = container.querySelector('textarea')
    expect(textarea?.getAttribute('id')).toBe('test-textarea')
  })

  it('has visible focus indication', () => {
    const container = document.createElement('div')
    render(() => <SettingsTextarea value="" onInput={() => {}} />, container)

    expect(container.innerHTML).toContain('focus-within:ring-2')
    expect(container.innerHTML).toContain('focus-within:ring-[var(--accent)]')
  })

  it('renders textarea with monospace font token', () => {
    const container = document.createElement('div')
    render(() => <SettingsTextarea value="" onInput={() => {}} />, container)

    // Verify monospace font token is used
    expect(container.innerHTML).toContain('var(--font-mono)')
  })
})

describe('SettingsSelect', () => {
  it('renders options', () => {
    const options = [
      { value: 'a', label: 'Option A' },
      { value: 'b', label: 'Option B' },
    ]

    const container = document.createElement('div')
    render(() => <SettingsSelect value="a" onChange={() => {}} options={options} />, container)

    const select = container.querySelector('select')
    expect(select?.innerHTML).toContain('Option A')
    expect(select?.innerHTML).toContain('Option B')
  })

  it('renders with visible label when provided', () => {
    const options = [{ value: 'a', label: 'Option A' }]
    const container = document.createElement('div')
    render(
      () => (
        <SettingsSelect
          value="a"
          onChange={() => {}}
          options={options}
          label="Select Label"
          id="test-select"
        />
      ),
      container
    )

    const label = container.querySelector('label')
    expect(label).toBeTruthy()
    expect(label?.textContent).toBe('Select Label')
    expect(label?.getAttribute('for')).toBe('test-select')

    const select = container.querySelector('select')
    expect(select?.getAttribute('id')).toBe('test-select')
  })

  it('has visible focus indication', () => {
    const options = [{ value: 'a', label: 'Option A' }]
    const container = document.createElement('div')
    render(() => <SettingsSelect value="a" onChange={() => {}} options={options} />, container)

    expect(container.innerHTML).toContain('focus-within:ring-2')
    expect(container.innerHTML).toContain('focus-within:ring-[var(--accent)]')
  })
})

describe('SettingsStatusBadge', () => {
  it('renders with text', () => {
    const container = document.createElement('div')
    render(() => <SettingsStatusBadge variant="default">Status</SettingsStatusBadge>, container)

    expect(container.textContent).toContain('Status')
  })

  it('uses success variant styling', () => {
    const container = document.createElement('div')
    render(() => <SettingsStatusBadge variant="success">Success</SettingsStatusBadge>, container)

    const badge = container.firstChild as HTMLElement
    const style = badge?.getAttribute('style') || ''
    expect(style).toContain('var(--success-subtle)')
    expect(style).toContain('var(--success)')
  })

  it('uses error variant styling', () => {
    const container = document.createElement('div')
    render(() => <SettingsStatusBadge variant="error">Error</SettingsStatusBadge>, container)

    const badge = container.firstChild as HTMLElement
    const style = badge?.getAttribute('style') || ''
    expect(style).toContain('var(--error-subtle)')
    expect(style).toContain('var(--error)')
  })
})

describe('SettingsTabContainer', () => {
  it('renders children', () => {
    const container = document.createElement('div')
    render(
      () => (
        <SettingsTabContainer>
          <div>Child 1</div>
          <div>Child 2</div>
        </SettingsTabContainer>
      ),
      container
    )

    expect(container.textContent).toContain('Child 1')
    expect(container.textContent).toContain('Child 2')
  })

  it('uses consistent gap spacing', () => {
    const container = document.createElement('div')
    render(
      () => (
        <SettingsTabContainer>
          <div>Content</div>
        </SettingsTabContainer>
      ),
      container
    )

    const wrapper = container.firstChild as HTMLElement
    const style = wrapper?.getAttribute('style') || ''
    // Style attribute may not have space after colon
    expect(style).toContain('gap:')
    expect(style).toContain('24px')
  })
})

describe('SETTINGS_CARD_GAP', () => {
  it('exports the standard gap constant', () => {
    expect(SETTINGS_CARD_GAP).toBe('32px')
  })
})
