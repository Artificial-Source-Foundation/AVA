import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { QuestionRequest } from '../../hooks/useAgent'
import { QuestionDock } from './QuestionDock'

function makeRequest(overrides: Partial<QuestionRequest> = {}): QuestionRequest {
  return {
    id: 'test-question-1',
    question: 'Which option do you prefer?',
    options: ['Option A', 'Option B', 'Option C'],
    ...overrides,
  }
}

describe('QuestionDock - Multiple-Choice Keyboard Contract', () => {
  let container: HTMLElement
  let dispose: () => void

  beforeEach(() => {
    container = document.createElement('div')
    document.body.appendChild(container)
  })

  afterEach(() => {
    dispose?.()
    document.body.removeChild(container)
    vi.restoreAllMocks()
  })

  describe('Keyboard: Enter submission', () => {
    it('submits selected option when Enter pressed while radio option is focused (dock owns focus)', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      // Find the second radio input and focus it
      const radioInputs = container.querySelectorAll('input[type="radio"]')
      const optionBRadio = radioInputs[1] as HTMLInputElement
      expect(optionBRadio).toBeDefined()

      // Select it by clicking (native radio behavior)
      optionBRadio.click()
      optionBRadio.focus()

      // Verify focus is within the dock
      const dock = container.querySelector('[data-testid="question-dock"]')
      expect(dock?.contains(document.activeElement)).toBe(true)

      // Dispatch Enter on document - should submit because dock owns focus
      document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }))

      // Should submit with the selected option value
      expect(onResolve).toHaveBeenCalledTimes(1)
      expect(onResolve).toHaveBeenCalledWith('Option B')
    })

    it('does not submit when composer textarea is focused and dock has a preselected option', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(
        () => (
          <div>
            <textarea aria-label="Message composer" />
            <QuestionDock request={request} onResolve={onResolve} />
          </div>
        ),
        container
      )

      const optionBRadio = container.querySelectorAll('input[type="radio"]')[1] as HTMLInputElement
      optionBRadio.click()
      expect(optionBRadio.checked).toBe(true)

      const composer = container.querySelector(
        'textarea[aria-label="Message composer"]'
      ) as HTMLTextAreaElement
      composer.focus()
      expect(document.activeElement).toBe(composer)

      composer.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }))

      expect(onResolve).not.toHaveBeenCalled()
      expect(document.activeElement).toBe(composer)

      const dock = container.querySelector('[data-testid="question-dock"]')
      expect(dock).not.toBeNull()
      expect(optionBRadio.checked).toBe(true)
    })

    it('does NOT submit when option is selected but focus is outside the dock', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      // Create an external focusable element outside the dock
      const externalInput = document.createElement('input')
      externalInput.type = 'text'
      externalInput.id = 'external-input'
      document.body.appendChild(externalInput)

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      // Select an option first (click on radio)
      const radioInputs = container.querySelectorAll('input[type="radio"]')
      const optionARadio = radioInputs[0] as HTMLInputElement
      optionARadio.click()
      optionARadio.focus()

      // Verify option is selected
      expect(optionARadio.checked).toBe(true)

      // Move focus to the external element (outside the dock)
      externalInput.focus()

      // Verify focus is outside the dock
      const dock = container.querySelector('[data-testid="question-dock"]')
      expect(dock?.contains(document.activeElement)).toBe(false)
      expect(document.activeElement).toBe(externalInput)

      // Dispatch Enter on document - should NOT submit because dock doesn't own focus
      document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }))

      // Should NOT have been called - focus is outside the dock
      expect(onResolve).not.toHaveBeenCalled()

      // Cleanup
      document.body.removeChild(externalInput)
    })

    it('does not submit when no option is selected and Enter is pressed', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      // Do not select any option - press Enter directly on document
      document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter' }))

      // Should NOT have been called
      expect(onResolve).not.toHaveBeenCalled()
    })

    it('does not submit on Enter when request is null', () => {
      const onResolve = vi.fn()

      dispose = render(() => <QuestionDock request={null} onResolve={onResolve} />, container)

      document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter' }))

      expect(onResolve).not.toHaveBeenCalled()
    })

    it('preserves native button activation when Enter is pressed on focused Answer button', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      // Select an option first
      const radioInputs = container.querySelectorAll('input[type="radio"]')
      const optionARadio = radioInputs[0] as HTMLInputElement
      optionARadio.click()

      // Find and focus the Answer button
      const buttons = container.querySelectorAll('button')
      const answerBtn = Array.from(buttons).find((b) => b.textContent?.includes('Answer'))
      expect(answerBtn).toBeDefined()

      answerBtn!.focus()
      expect(document.activeElement).toBe(answerBtn)

      // Press Enter while button is focused - should NOT trigger document handler
      answerBtn!.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }))

      // Document-level handler should not have been called because button is focused
      // (native button onClick would fire instead)
      expect(onResolve).not.toHaveBeenCalled()
    })

    it('preserves native button activation when Enter is pressed on focused Skip button', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      // Find and focus the Skip button
      const buttons = container.querySelectorAll('button')
      const skipBtn = Array.from(buttons).find((b) => b.textContent?.includes('Skip'))
      expect(skipBtn).toBeDefined()

      skipBtn!.focus()
      expect(document.activeElement).toBe(skipBtn)

      // Press Enter while button is focused - should NOT trigger document handler
      skipBtn!.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }))

      // Document-level handler should not have been called
      expect(onResolve).not.toHaveBeenCalled()
    })
  })

  describe('Keyboard: Escape dismissal', () => {
    it('dismisses with empty answer when Escape is pressed', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      // Press Escape on document
      document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }))

      // Should resolve with empty string
      expect(onResolve).toHaveBeenCalledTimes(1)
      expect(onResolve).toHaveBeenCalledWith('')
    })

    it('dismisses even when an option is selected', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      // Select an option first
      const radioInputs = container.querySelectorAll('input[type="radio"]')
      const optionARadio = radioInputs[0] as HTMLInputElement
      optionARadio.click()

      // Then press Escape
      document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }))

      // Should dismiss with empty string, not the selected option
      expect(onResolve).toHaveBeenCalledTimes(1)
      expect(onResolve).toHaveBeenCalledWith('')
    })

    it('does not respond to Escape when request is null', () => {
      const onResolve = vi.fn()

      dispose = render(() => <QuestionDock request={null} onResolve={onResolve} />, container)

      document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }))

      expect(onResolve).not.toHaveBeenCalled()
    })

    it('Escape takes priority over Enter when both would apply', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      // Select an option
      const radioInputs = container.querySelectorAll('input[type="radio"]')
      const optionARadio = radioInputs[0] as HTMLInputElement
      optionARadio.click()

      // Press Escape
      document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }))

      // Should dismiss with empty string
      expect(onResolve).toHaveBeenCalledTimes(1)
      expect(onResolve).toHaveBeenCalledWith('')
    })
  })

  describe('Keyboard: Freeform option submission', () => {
    it('submits trimmed custom answer when freeform is active and Enter pressed in input', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      // Click the freeform option (last radio input)
      const radioInputs = container.querySelectorAll('input[type="radio"]')
      const freeformRadio = radioInputs[radioInputs.length - 1] as HTMLInputElement
      freeformRadio.click()

      // The freeform input should now appear - find it and type
      const freeformInput = container.querySelector('input[type="text"]') as HTMLInputElement
      expect(freeformInput).not.toBeNull()

      // Type a custom answer with whitespace
      freeformInput.value = '  My custom answer  '
      freeformInput.dispatchEvent(new Event('input', { bubbles: true }))

      // Press Enter while focused in the freeform input
      freeformInput.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }))

      // Should submit with trimmed value
      expect(onResolve).toHaveBeenCalledTimes(1)
      expect(onResolve).toHaveBeenCalledWith('My custom answer')
    })

    it('does not submit empty freeform answer on Enter', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      // Activate freeform
      const radioInputs = container.querySelectorAll('input[type="radio"]')
      const freeformRadio = radioInputs[radioInputs.length - 1] as HTMLInputElement
      freeformRadio.click()

      // Leave input empty
      const freeformInput = container.querySelector('input[type="text"]') as HTMLInputElement
      expect(freeformInput).not.toBeNull()

      // Press Enter with empty value
      freeformInput.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }))

      // Should not submit (empty trimmed value)
      expect(onResolve).not.toHaveBeenCalled()
    })

    it('does not submit whitespace-only freeform answer on Enter', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      // Activate freeform
      const radioInputs = container.querySelectorAll('input[type="radio"]')
      const freeformRadio = radioInputs[radioInputs.length - 1] as HTMLInputElement
      freeformRadio.click()

      // Enter only whitespace
      const freeformInput = container.querySelector('input[type="text"]') as HTMLInputElement
      expect(freeformInput).not.toBeNull()
      freeformInput.value = '   \t\n   '
      freeformInput.dispatchEvent(new Event('input', { bubbles: true }))

      // Press Enter
      freeformInput.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }))

      // Should not submit (whitespace-only trimmed to empty)
      expect(onResolve).not.toHaveBeenCalled()
    })

    it('dismisses via Escape even when freeform has text', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      // Activate freeform with text
      const radioInputs = container.querySelectorAll('input[type="radio"]')
      const freeformRadio = radioInputs[radioInputs.length - 1] as HTMLInputElement
      freeformRadio.click()

      const freeformInput = container.querySelector('input[type="text"]') as HTMLInputElement
      freeformInput.value = 'Some text that should be ignored'
      freeformInput.dispatchEvent(new Event('input', { bubbles: true }))

      // Press Escape - should dismiss, not submit
      document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }))

      expect(onResolve).toHaveBeenCalledTimes(1)
      expect(onResolve).toHaveBeenCalledWith('')
    })
  })

  describe('Semantic structure', () => {
    it('renders as a labeled section landmark', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      const region = container.querySelector('section[data-testid="question-dock"]')
      expect(region).not.toBeNull()

      const ariaLabelledBy = region?.getAttribute('aria-labelledby')
      expect(ariaLabelledBy).toBeTruthy()

      // The label should exist
      const label = container.querySelector(`#${ariaLabelledBy}`)
      expect(label).not.toBeNull()
      expect(label?.textContent).toContain('Which option do you prefer?')
    })

    it('renders radio options in a radiogroup', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      const radiogroup = container.querySelector('[role="radiogroup"]')
      expect(radiogroup).not.toBeNull()

      // Should have radio inputs
      const radios = container.querySelectorAll('input[type="radio"]')
      expect(radios.length).toBeGreaterThanOrEqual(3)
    })

    it('each option has a label associated with its radio input', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      // Get the first visible option label
      const labels = container.querySelectorAll('label')
      expect(labels.length).toBeGreaterThanOrEqual(1)

      // Each label should have a corresponding input
      const firstLabel = labels[0]
      const forAttr = firstLabel.getAttribute('for')
      expect(forAttr).toBeTruthy()

      const associatedInput = container.querySelector(`#${forAttr}`)
      expect(associatedInput).not.toBeNull()
      expect(associatedInput?.getAttribute('type')).toBe('radio')
    })

    it('action buttons have descriptive titles', () => {
      const onResolve = vi.fn()
      const request = makeRequest()

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      const buttons = container.querySelectorAll('button')
      const skipBtn = Array.from(buttons).find((b) => b.textContent?.includes('Skip'))
      const answerBtn = Array.from(buttons).find((b) => b.textContent?.includes('Answer'))

      expect(skipBtn?.getAttribute('title')).toContain('Escape')
      expect(answerBtn?.getAttribute('title')).toContain('Enter')
    })
  })

  describe('Free-text mode (no options)', () => {
    it('renders textarea when options array is empty', () => {
      const onResolve = vi.fn()
      const request = makeRequest({ options: [] })

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      // Should render a textarea instead of radio buttons
      const textarea = container.querySelector('textarea')
      expect(textarea).not.toBeNull()
      expect(textarea?.getAttribute('placeholder')).toBe('Type your answer...')
    })

    it('submits free-text answer on Enter in textarea', () => {
      const onResolve = vi.fn()
      const request = makeRequest({ options: [] })

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      const textarea = container.querySelector('textarea') as HTMLTextAreaElement
      textarea.value = 'Free text answer'
      textarea.dispatchEvent(new Event('input', { bubbles: true }))

      // Enter without shift should submit
      textarea.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }))

      expect(onResolve).toHaveBeenCalledTimes(1)
      expect(onResolve).toHaveBeenCalledWith('Free text answer')
    })

    it('does not submit free-text mode on Shift+Enter (allows multiline)', () => {
      const onResolve = vi.fn()
      const request = makeRequest({ options: [] })

      dispose = render(() => <QuestionDock request={request} onResolve={onResolve} />, container)

      const textarea = container.querySelector('textarea') as HTMLTextAreaElement
      textarea.value = 'Line 1'
      textarea.dispatchEvent(new Event('input', { bubbles: true }))

      // Shift+Enter should NOT submit in textarea (allows newlines)
      textarea.dispatchEvent(
        new KeyboardEvent('keydown', { key: 'Enter', shiftKey: true, bubbles: true })
      )

      expect(onResolve).not.toHaveBeenCalled()
    })
  })
})
