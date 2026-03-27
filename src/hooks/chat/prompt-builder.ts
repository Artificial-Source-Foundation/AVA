/**
 * Prompt Builder
 * Async system prompt builder that waits for instructions to load.
 *
 * Mirrors the CLI pattern: listens for `instructions:loaded` event
 * (which fires when CLAUDE.md/AGENTS.md are loaded by the instructions
 * extension) before building the final prompt. This ensures project
 * instructions are included in the system prompt.
 */

import { installReplaceableWindowListener } from '../../lib/replaceable-window-listener'
import { logInfo } from '../../services/logger'

// Track whether instructions have been loaded for the current session.
// Once loaded, skip the wait on subsequent messages (avoids 1.5s delay).
let instructionsLoaded = false

/** Stub event listener (replaces @ava/core-v2/extensions onEvent) */
function _onInstructionsLoaded(): void {
  // Listen for DOM custom event instead of core-v2 event bus
  installReplaceableWindowListener('prompt-builder:instructions-loaded', (target) => {
    const listener = () => {
      instructionsLoaded = true
    }
    target.addEventListener('ava:instructions-loaded', listener)
    return () => target.removeEventListener('ava:instructions-loaded', listener)
  })
}
_onInstructionsLoaded()

/** Local prompt sections registry (replaces packages/extensions/prompts) */
const _promptSections = new Map<string, { name: string; priority: number; content: string }>()

function addPromptSection(section: {
  name: string
  priority: number
  content: string
}): () => void {
  _promptSections.set(section.name, section)
  return () => {
    _promptSections.delete(section.name)
  }
}

function buildSystemPrompt(_model?: string): string {
  const sorted = [..._promptSections.values()].sort((a, b) => a.priority - b.priority)
  return sorted.map((s) => s.content).join('\n\n')
}

/** Reset when session changes (called from session store or core-bridge). */
export function resetInstructionsLoaded(): void {
  instructionsLoaded = false
}

/**
 * Build a system prompt, waiting for instructions to load first.
 *
 * Adds temporary per-request sections (environment, behavior, custom-instructions),
 * waits up to 1.5s for the instructions extension to load project files, then
 * builds the prompt and cleans up temporary sections.
 *
 * @param model - Model ID for model-family-specific adjustments
 * @param cwd - Working directory for environment section
 * @param customInstructions - User's custom instructions from settings
 */
export async function buildSystemPromptAfterInstructions(
  model?: string,
  cwd?: string,
  customInstructions?: string
): Promise<string> {
  const removers: Array<() => void> = []

  const os = navigator.platform?.includes('Win')
    ? 'Windows'
    : navigator.platform?.includes('Mac')
      ? 'macOS'
      : 'Linux'
  const date = new Date().toLocaleDateString()

  // Add temporary per-request sections
  removers.push(
    addPromptSection({
      name: 'environment',
      priority: 50,
      content: `## Environment\n- Working directory: ${cwd || '.'}\n- OS: ${os}\n- Date: ${date}`,
    })
  )

  removers.push(
    addPromptSection({
      name: 'behavior',
      priority: 20,
      content:
        'Do the work without asking questions. Infer missing details from the codebase.\n' +
        'If a task is ambiguous, pick the most reasonable interpretation and execute.',
    })
  )

  if (customInstructions?.trim()) {
    removers.push(
      addPromptSection({
        name: 'custom-instructions',
        priority: 200,
        content: `## Custom Instructions\n${customInstructions.trim()}`,
      })
    )
  }

  // Only wait for instructions on first message (event already fired on session open).
  // Subsequent messages skip the wait — the prompt section is already registered.
  if (!instructionsLoaded) {
    await waitForInstructions(1500)
  }

  // Build using the shared pipeline (includes identity, tool-guidelines,
  // project instructions, and model-family adjustments)
  const prompt = buildSystemPrompt(model)

  // Clean up temporary sections
  for (const remove of removers) remove()

  return prompt
}

/**
 * Wait for the instructions:loaded event or timeout.
 * If instructions were already loaded (the event already fired), the prompt
 * builder's addPromptSection in core-bridge.ts has already added them — so
 * we just need a brief delay to ensure the async handler has run.
 */
function waitForInstructions(timeoutMs: number): Promise<void> {
  return new Promise<void>((resolve) => {
    if (instructionsLoaded) {
      resolve()
      return
    }

    const timeout = setTimeout(() => {
      logInfo('prompt-builder', 'Instructions timeout — building prompt without waiting further')
      resolve()
    }, timeoutMs)

    if (typeof window !== 'undefined') {
      const handler = () => {
        clearTimeout(timeout)
        instructionsLoaded = true
        window.removeEventListener('ava:instructions-loaded', handler)
        setTimeout(resolve, 50)
      }
      window.addEventListener('ava:instructions-loaded', handler)
    } else {
      // No window — just wait for timeout
    }
  })
}
