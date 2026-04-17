import type { JSX } from 'solid-js'
import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const setSelectedModelMock = vi.fn()

type MockProvider = {
  id: string
  name: string
  enabled: boolean
  models: Array<{
    id: string
    name: string
    contextWindow: number | null | undefined
  }>
}

let mockProviders: MockProvider[] = [
  {
    id: 'openai',
    name: 'OpenAI',
    enabled: true,
    models: [
      {
        id: 'gpt-5.4',
        name: 'GPT-5.4',
        contextWindow: 1_000_000,
      },
    ],
  },
]

vi.mock('@kobalte/core/dialog', () => {
  const Dialog = (props: { children: JSX.Element }) => <>{props.children}</>
  Dialog.Portal = (props: { children: JSX.Element }) => <>{props.children}</>
  Dialog.Overlay = () => <div />
  Dialog.Content = (props: {
    children: JSX.Element
    onKeyDown?: (event: KeyboardEvent) => void
  }) => (
    <div role="dialog" onKeyDown={props.onKeyDown}>
      {props.children}
    </div>
  )
  return { Dialog }
})

vi.mock('../../stores/session', () => ({
  useSession: () => ({
    selectedModel: () => 'gpt-5.4',
    selectedProvider: () => 'openai',
    setSelectedModel: (...args: unknown[]) => setSelectedModelMock(...args),
  }),
}))

vi.mock('../../stores/settings', () => ({
  useSettings: () => ({
    settings: () => ({
      providers: mockProviders,
    }),
  }),
}))

import { QuickModelPicker } from './QuickModelPicker'

describe('QuickModelPicker', () => {
  let dispose: (() => void) | undefined

  beforeEach(() => {
    vi.clearAllMocks()
    mockProviders = [
      {
        id: 'openai',
        name: 'OpenAI',
        enabled: true,
        models: [
          {
            id: 'gpt-5.4',
            name: 'GPT-5.4',
            contextWindow: 1_000_000,
          },
        ],
      },
    ]
    vi.stubGlobal('requestAnimationFrame', (cb: FrameRequestCallback) => {
      cb(0)
      return 0
    })
  })

  afterEach(() => {
    dispose?.()
    dispose = undefined
    vi.unstubAllGlobals()
    document.body.innerHTML = ''
  })

  it('keeps provider and model together when selecting from the quick picker', () => {
    const onClose = vi.fn()
    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => <QuickModelPicker open={true} onClose={onClose} />, container)

    const modelButton = Array.from(container.querySelectorAll('button')).find((button) =>
      button.textContent?.includes('GPT-5.4')
    )
    if (!(modelButton instanceof HTMLButtonElement)) {
      throw new Error('Could not find GPT-5.4 button')
    }

    modelButton.click()

    expect(setSelectedModelMock).toHaveBeenCalledWith('gpt-5.4', 'openai')
    expect(onClose).toHaveBeenCalledTimes(1)
  })

  it('handles models with null or unknown context windows without malformed rendering', () => {
    mockProviders = [
      {
        id: 'openai',
        name: 'OpenAI',
        enabled: true,
        models: [
          {
            id: 'gpt-5.4',
            name: 'GPT-5.4',
            contextWindow: 1_000_000,
          },
          {
            id: 'gpt-null-context',
            name: 'GPT Null Context',
            contextWindow: null,
          },
          {
            id: 'gpt-unknown-context',
            name: 'GPT Unknown Context',
            contextWindow: undefined,
          },
        ],
      },
    ]

    const onClose = vi.fn()
    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => <QuickModelPicker open={true} onClose={onClose} />, container)

    const nullContextButton = Array.from(container.querySelectorAll('button')).find((button) =>
      button.textContent?.includes('GPT Null Context')
    )
    if (!(nullContextButton instanceof HTMLButtonElement)) {
      throw new Error('Could not find GPT Null Context button')
    }

    const unknownContextButton = Array.from(container.querySelectorAll('button')).find((button) =>
      button.textContent?.includes('GPT Unknown Context')
    )
    if (!(unknownContextButton instanceof HTMLButtonElement)) {
      throw new Error('Could not find GPT Unknown Context button')
    }

    expect(nullContextButton.textContent).toContain('GPT Null Context')
    expect(unknownContextButton.textContent).toContain('GPT Unknown Context')
    expect(nullContextButton.textContent).not.toContain('N/A')
    expect(nullContextButton.textContent).not.toContain('undefined')
    expect(unknownContextButton.textContent).not.toContain('N/A')
    expect(unknownContextButton.textContent).not.toContain('undefined')

    unknownContextButton.click()

    expect(setSelectedModelMock).toHaveBeenCalledWith('gpt-unknown-context', 'openai')
    expect(onClose).toHaveBeenCalledTimes(1)
  })
})
