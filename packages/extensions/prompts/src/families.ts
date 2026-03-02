/**
 * Model-family detection and family-specific prompt sections.
 * Maps model ID strings to known families and provides tailored instructions.
 */

export type ModelFamily = 'claude' | 'gpt' | 'gemini' | 'llama' | 'deepseek' | 'mistral' | 'unknown'

interface FamilyPattern {
  family: ModelFamily
  patterns: RegExp
}

const FAMILY_PATTERNS: FamilyPattern[] = [
  { family: 'claude', patterns: /claude|sonnet|haiku|opus/i },
  { family: 'gpt', patterns: /gpt|o1|o3|o4|chatgpt/i },
  { family: 'gemini', patterns: /gemini|gemma/i },
  { family: 'llama', patterns: /llama|codellama/i },
  { family: 'deepseek', patterns: /deepseek/i },
  { family: 'mistral', patterns: /mistral|mixtral|codestral/i },
]

/**
 * Detect the model family from a model ID string.
 * Matches against known patterns, returns 'unknown' if no match.
 */
export function detectModelFamily(model: string): ModelFamily {
  const lower = model.toLowerCase()
  for (const { family, patterns } of FAMILY_PATTERNS) {
    if (patterns.test(lower)) {
      return family
    }
  }
  return 'unknown'
}

/**
 * Family-specific prompt sections that provide tailored instructions
 * for each model family's strengths and capabilities.
 */
export const FAMILY_PROMPT_SECTIONS: Record<ModelFamily, string> = {
  claude: 'Use XML tags for structured output. Prefer thinking blocks for complex reasoning.',
  gpt: 'Use markdown formatting. Prefer function calling over text-based tool use.',
  gemini: 'Supports large context windows. Use structured output when available.',
  llama: 'Keep responses concise. May not support all tool calling features.',
  deepseek: 'Supports code completion natively. Use fill-in-the-middle when appropriate.',
  mistral: 'Supports function calling. Keep tool schemas simple.',
  unknown: '',
}

/**
 * Convenience wrapper: detect the model family and return the
 * corresponding prompt section. Returns empty string for unknown models.
 */
export function getModelFamilyPromptSection(model: string): string {
  const family = detectModelFamily(model)
  return FAMILY_PROMPT_SECTIONS[family]
}
