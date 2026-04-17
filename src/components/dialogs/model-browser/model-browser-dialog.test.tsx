import { type Component, createSignal, type JSX } from 'solid-js'
import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { LLMProviderConfig, ProviderModel } from '../../../config/defaults/provider-defaults'

vi.mock('../../ui/Dialog', () => ({
  Dialog: (props: { children: JSX.Element }) => <div>{props.children}</div>,
}))

vi.mock('../../../services/providers/curated-model-catalog', () => ({
  getModelFromCatalog: () => undefined,
  isBlockedModelId: () => false,
}))

vi.mock('./model-browser-grid', () => ({
  ModelBrowserGrid: (props: { models: Array<{ id: string; providerId: string }> }) => (
    <div data-testid="grid-models" data-count={String(props.models.length)}>
      {props.models.map((model) => `${model.providerId}/${model.id}`).join(',')}
    </div>
  ),
}))

import { ModelBrowserDialog } from './model-browser-dialog'

const TestIcon: Component<{ class?: string }> = () => null

function createProvider(id: string, name: string, models: ProviderModel[]): LLMProviderConfig {
  return {
    id,
    name,
    icon: TestIcon,
    description: `${name} provider`,
    enabled: true,
    models,
    status: 'connected',
  }
}

function createModel(index: number): ProviderModel {
  const suffix = String(index).padStart(2, '0')
  return {
    id: `model-${suffix}`,
    name: `Model ${suffix}`,
    contextWindow: 100_000,
  }
}

function getVisibleModelRefs(container: HTMLElement): string[] {
  const grid = container.querySelector('[data-testid="grid-models"]')
  if (!(grid instanceof HTMLDivElement)) {
    throw new Error('Grid output not found')
  }
  const text = grid.textContent ?? ''
  return text.length > 0 ? text.split(',') : []
}

function findShowMoreButton(container: HTMLElement): HTMLButtonElement | undefined {
  return Array.from(container.querySelectorAll('button')).find((button) =>
    /\bremaining\b/i.test(button.textContent ?? '')
  ) as HTMLButtonElement | undefined
}

async function flushMicrotasks(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

describe('ModelBrowserDialog', () => {
  let container: HTMLDivElement
  let dispose: (() => void) | undefined

  beforeEach(() => {
    vi.useFakeTimers()
    container = document.createElement('div')
    document.body.append(container)
  })

  afterEach(() => {
    dispose?.()
    vi.useRealTimers()
    container.remove()
    document.body.innerHTML = ''
    vi.clearAllMocks()
  })

  it('applies debounced search from the latest rapid input only', async () => {
    const provider = createProvider('provider-a', 'Provider A', [
      { id: 'alpha', name: 'Alpha', contextWindow: 100_000 },
      { id: 'beta', name: 'Beta', contextWindow: 100_000 },
      { id: 'gamma', name: 'Gamma', contextWindow: 100_000 },
    ])

    dispose = render(
      () => (
        <ModelBrowserDialog
          open={() => true}
          onOpenChange={() => undefined}
          selectedModel={() => 'alpha'}
          selectedProvider={() => 'provider-a'}
          onSelect={() => undefined}
          enabledProviders={() => [provider]}
        />
      ),
      container
    )

    const input = container.querySelector('input[aria-label="Search models"]')
    if (!(input instanceof HTMLInputElement)) {
      throw new Error('Search input not found')
    }

    expect(getVisibleModelRefs(container)).toEqual([
      'provider-a/alpha',
      'provider-a/beta',
      'provider-a/gamma',
    ])

    input.value = 'gam'
    input.dispatchEvent(new Event('input', { bubbles: true }))
    await flushMicrotasks()

    expect(getVisibleModelRefs(container)).toEqual([
      'provider-a/alpha',
      'provider-a/beta',
      'provider-a/gamma',
    ])

    await vi.advanceTimersByTimeAsync(80)
    await flushMicrotasks()

    input.value = 'bet'
    input.dispatchEvent(new Event('input', { bubbles: true }))
    await flushMicrotasks()

    // At t=120ms from the first input (only 40ms from the second), debounce must not fire.
    await vi.advanceTimersByTimeAsync(40)
    await flushMicrotasks()
    expect(getVisibleModelRefs(container)).toEqual([
      'provider-a/alpha',
      'provider-a/beta',
      'provider-a/gamma',
    ])

    await vi.advanceTimersByTimeAsync(79)
    await flushMicrotasks()
    expect(getVisibleModelRefs(container)).toEqual([
      'provider-a/alpha',
      'provider-a/beta',
      'provider-a/gamma',
    ])

    await vi.advanceTimersByTimeAsync(1)
    await flushMicrotasks()
    expect(getVisibleModelRefs(container)).toEqual(['provider-a/beta'])
  })

  it('keeps selected model visible in the initial slice when it is beyond page limit', () => {
    const provider = createProvider(
      'provider-a',
      'Provider A',
      Array.from({ length: 40 }, (_, index) => createModel(index))
    )

    dispose = render(
      () => (
        <ModelBrowserDialog
          open={() => true}
          onOpenChange={() => undefined}
          selectedModel={() => 'model-39'}
          selectedProvider={() => 'provider-a'}
          onSelect={() => undefined}
          enabledProviders={() => [provider]}
        />
      ),
      container
    )

    const visible = getVisibleModelRefs(container)
    expect(visible).toHaveLength(18) // PAGE_SIZE reduced from 30 to 18
    expect(visible).toContain('provider-a/model-39')
    expect(visible).not.toContain('provider-a/model-17')
  })

  it('shows more models when pagination button is clicked', async () => {
    const provider = createProvider(
      'provider-a',
      'Provider A',
      Array.from({ length: 40 }, (_, index) => createModel(index))
    )

    dispose = render(
      () => (
        <ModelBrowserDialog
          open={() => true}
          onOpenChange={() => undefined}
          selectedModel={() => 'model-39'}
          selectedProvider={() => 'provider-a'}
          onSelect={() => undefined}
          enabledProviders={() => [provider]}
        />
      ),
      container
    )

    const showMoreButton = findShowMoreButton(container)
    expect(showMoreButton).toBeDefined()
    expect(showMoreButton?.textContent).toMatch(/22\s+remaining/i) // 40 - 18 = 22 remaining

    showMoreButton?.click()
    await flushMicrotasks()

    const visible = getVisibleModelRefs(container)
    expect(visible).toHaveLength(36) // 18 + 18 = 36
    expect(visible).toContain('provider-a/model-17')
    expect(findShowMoreButton(container)).toBeDefined() // Still has 4 more
  })

  it('disambiguates selected model by provider when model ids overlap', () => {
    const providerA = createProvider(
      'provider-a',
      'Provider A',
      Array.from({ length: 30 }, (_, index) => {
        if (index === 0) return { id: 'shared-model', name: 'A Shared', contextWindow: 100_000 }
        return createModel(index)
      })
    )
    const providerB = createProvider('provider-b', 'Provider B', [
      { id: 'shared-model', name: 'Z Shared', contextWindow: 100_000 },
    ])

    dispose = render(
      () => (
        <ModelBrowserDialog
          open={() => true}
          onOpenChange={() => undefined}
          selectedModel={() => 'shared-model'}
          selectedProvider={() => 'provider-b'}
          onSelect={() => undefined}
          enabledProviders={() => [providerA, providerB]}
        />
      ),
      container
    )

    const visible = getVisibleModelRefs(container)
    expect(visible).toHaveLength(18) // PAGE_SIZE = 18
    expect(visible).toContain('provider-b/shared-model')
    expect(visible).not.toContain('provider-a/model-17')
  })

  it('resets pagination when filter changes reduce result set', async () => {
    const provider = createProvider(
      'provider-a',
      'Provider A',
      Array.from({ length: 70 }, (_, index) => ({
        id: `model-${String(index).padStart(2, '0')}`,
        name: index < 35 ? `Target ${index}` : `Other ${index}`,
        contextWindow: 100_000,
      }))
    )

    dispose = render(
      () => (
        <ModelBrowserDialog
          open={() => true}
          onOpenChange={() => undefined}
          selectedModel={() => 'model-00'}
          selectedProvider={() => 'provider-a'}
          onSelect={() => undefined}
          enabledProviders={() => [provider]}
        />
      ),
      container
    )

    const initialShowMoreButton = findShowMoreButton(container)
    expect(initialShowMoreButton?.textContent).toMatch(/52\s+remaining/i) // 70 - 18 = 52

    initialShowMoreButton?.click()
    await flushMicrotasks()
    expect(findShowMoreButton(container)?.textContent).toMatch(/34\s+remaining/i) // 70 - 36 = 34

    const input = container.querySelector('input[aria-label="Search models"]')
    if (!(input instanceof HTMLInputElement)) {
      throw new Error('Search input not found')
    }

    input.value = 'target'
    input.dispatchEvent(new Event('input', { bubbles: true }))
    await flushMicrotasks()

    await vi.advanceTimersByTimeAsync(120)
    await flushMicrotasks()

    const visible = getVisibleModelRefs(container)
    expect(visible).toHaveLength(18) // PAGE_SIZE reset to 18 after filter change
    expect(findShowMoreButton(container)?.textContent).toMatch(/17\s+remaining/i) // 35 - 18 = 17
  })

  it('focuses search input when dialog opens', async () => {
    const provider = createProvider('provider-a', 'Provider A', [
      { id: 'alpha', name: 'Alpha', contextWindow: 100_000 },
      { id: 'beta', name: 'Beta', contextWindow: 100_000 },
    ])

    // Start with dialog closed
    const [open, setOpen] = createSignal(false)

    dispose = render(
      () => (
        <ModelBrowserDialog
          open={open}
          onOpenChange={(o) => setOpen(o)}
          selectedModel={() => 'alpha'}
          selectedProvider={() => 'provider-a'}
          onSelect={() => undefined}
          enabledProviders={() => [provider]}
        />
      ),
      container
    )

    // Initially input should exist but not be focused (dialog is closed)
    let input = container.querySelector('input[aria-label="Search models"]')
    expect(input).not.toBeNull()

    // Open the dialog
    setOpen(true)
    await flushMicrotasks()
    // rAF ~16ms per frame — advance by one frame to let rAF callback fire
    await vi.advanceTimersByTimeAsync(16)
    await flushMicrotasks()

    input = container.querySelector('input[aria-label="Search models"]')
    expect(input).toBe(document.activeElement)
  })
})
