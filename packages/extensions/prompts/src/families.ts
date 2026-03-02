/**
 * Model-family detection and family-specific prompt sections.
 * Maps model ID strings to known families and provides tailored instructions.
 *
 * Each family gets directives optimized for that model's behavior patterns.
 * Based on research from OpenCode, Pi, and Gemini CLI prompt engineering.
 */

export type ModelFamily = 'claude' | 'gpt' | 'gemini' | 'llama' | 'deepseek' | 'mistral' | 'unknown'

interface FamilyPattern {
  family: ModelFamily
  patterns: RegExp
}

const FAMILY_PATTERNS: FamilyPattern[] = [
  { family: 'claude', patterns: /claude|sonnet|haiku|opus/i },
  { family: 'gpt', patterns: /gpt|o1|o3|o4|chatgpt|codex/i },
  { family: 'gemini', patterns: /gemini|gemma/i },
  { family: 'llama', patterns: /llama|codellama/i },
  { family: 'deepseek', patterns: /deepseek/i },
  { family: 'mistral', patterns: /mistral|mixtral|codestral|magistral/i },
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
 * Family-specific prompt sections that provide tailored autonomy and
 * tool-use instructions for each model family.
 */
export const FAMILY_PROMPT_SECTIONS: Record<ModelFamily, string> = {
  gpt: `## Model-Specific Directives
Prefer function calling over text-based tool use. When you decide to take an action, ACTUALLY call the tool — do not just describe what you would do.
You are an agent — keep going until the task is completely resolved. Do not end your turn to ask the user what to do next. You have everything you need.
NEVER say "I can't invoke tools" or ask the user to run commands manually. You have direct tool access — use it.`,

  claude: `## Model-Specific Directives
Use thinking blocks for complex reasoning before acting. Prefer structured XML output when returning data.
You are an autonomous agent — solve problems end-to-end without asking for permission or clarification.`,

  gemini: `## Model-Specific Directives
Do NOT ask permission before using a tool. The interface provides confirmation if needed — your job is to act.
You can process large amounts of context. Read multiple files in parallel when exploring a codebase.`,

  llama: `## Model-Specific Directives
Keep responses concise. Focus on using the available tools to complete tasks rather than explaining what you would do.`,

  deepseek: `## Model-Specific Directives
You have strong code understanding. Use tools to read and modify files directly. Do not ask the user to perform actions you can do yourself.`,

  mistral: `## Model-Specific Directives
Use function calling for all file and command operations. Act autonomously — do the work, then report results.`,

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
