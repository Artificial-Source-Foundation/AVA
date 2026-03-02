/**
 * Prompts extension.
 * Provides system prompt building and management.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { buildSystemPrompt } from './builder.js'

export function activate(api: ExtensionAPI): Disposable {
  // Expose prompt builder via events
  api.on('prompt:build', (data) => {
    const req = data as { model?: string }
    const prompt = buildSystemPrompt(req.model)
    api.emit('prompt:built', { prompt })
  })

  return { dispose() {} }
}

export type { PromptSection } from './builder.js'
export {
  addPromptSection,
  buildSystemPrompt,
  getPromptSections,
  resetPromptSections,
} from './builder.js'
export type { ModelFamily } from './families.js'
export {
  detectModelFamily,
  FAMILY_PROMPT_SECTIONS,
  getModelFamilyPromptSection,
} from './families.js'
