export type ModelFamily = 'claude' | 'gpt' | 'gemini' | 'llama' | 'mistral' | 'other'

export interface ModelVariantPrompt {
  family: ModelFamily
  toolCallGuidance: string
  formattingHints: string
  thinkingMode?: string
  structuredOutput?: string
}

const VARIANTS: Record<ModelFamily, ModelVariantPrompt> = {
  claude: {
    family: 'claude',
    toolCallGuidance:
      'Prefer XML-like tool result framing and keep tool calls explicit, ordered, and grounded in observed outputs.',
    formattingHints:
      'Use concise sections and preserve context-rich reasoning summaries for long-horizon tasks.',
    thinkingMode:
      'For hard tasks, use extended thinking before tool calls, then commit to one concrete next action.',
  },
  gpt: {
    family: 'gpt',
    toolCallGuidance:
      'Use strict JSON-compatible tool call arguments with exact keys and no trailing commentary.',
    formattingHints:
      'Prefer deterministic response structure when asked for machine-readable output.',
    structuredOutput:
      'If structured output is requested, explicitly respond with valid JSON and no markdown wrapper.',
  },
  gemini: {
    family: 'gemini',
    toolCallGuidance:
      'Leverage broad context window while keeping tool calls grounded in source evidence and search-backed facts.',
    formattingHints:
      'Use clear headings and short examples when describing plans or transformations.',
  },
  llama: {
    family: 'llama',
    toolCallGuidance:
      'Keep tool calls simple and explicit; avoid over-nesting arguments and prefer short, direct operations.',
    formattingHints: 'Keep responses concise with minimal prose between actions.',
  },
  mistral: {
    family: 'mistral',
    toolCallGuidance:
      'Use straightforward tool schemas, explicit argument names, and short iterative tool loops.',
    formattingHints:
      'Prefer compact outputs and concrete next steps over long narrative explanations.',
  },
  other: {
    family: 'other',
    toolCallGuidance:
      'Use conservative tool-call formatting, explicit arguments, and verify outputs before proceeding.',
    formattingHints: 'Prefer clear, short sections with stable wording.',
  },
}

const FAMILY_PATTERNS: Array<{ family: ModelFamily; pattern: RegExp }> = [
  { family: 'claude', pattern: /claude|sonnet|haiku|opus/i },
  { family: 'gpt', pattern: /gpt|openai|chatgpt|o1|o3|o4|codex/i },
  { family: 'gemini', pattern: /gemini|gemma/i },
  { family: 'llama', pattern: /llama|codellama/i },
  { family: 'mistral', pattern: /mistral|mixtral|codestral|magistral|ministral|devstral/i },
]

/** Detect model family from model ID string */
export function detectModelFamily(modelId: string): ModelFamily {
  for (const { family, pattern } of FAMILY_PATTERNS) {
    if (pattern.test(modelId)) {
      return family
    }
  }
  return 'other'
}

/** Get variant-specific prompt additions */
export function getModelVariantPrompt(modelId: string): ModelVariantPrompt {
  return VARIANTS[detectModelFamily(modelId)]
}

export function getModelVariantPromptSection(modelId: string): string {
  const variant = getModelVariantPrompt(modelId)
  const lines = [
    '## Model Variant Guidance',
    `Family: ${variant.family}`,
    `Tool calls: ${variant.toolCallGuidance}`,
    `Formatting: ${variant.formattingHints}`,
  ]

  if (variant.thinkingMode) {
    lines.push(`Thinking: ${variant.thinkingMode}`)
  }
  if (variant.structuredOutput) {
    lines.push(`Structured output: ${variant.structuredOutput}`)
  }

  return lines.join('\n')
}
