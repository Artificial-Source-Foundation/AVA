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
  { family: 'mistral', patterns: /mistral|mixtral|codestral|magistral|devstral|ministral/i },
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
You are an autonomous coding agent with full tool access. ALWAYS use your tools to complete tasks.
When a user asks you to do something — DO IT using your tools. Do not describe what you would do. Do not ask permission. Do not say "I can help with that" and then stop. CALL THE TOOLS.
- User says "read this file" → call read_file immediately
- User says "fix the bug" → call read_file, then edit, then verify
- User says "run the tests" → call bash with the test command
You MUST iterate and keep going until the problem is solved. Only terminate your turn when you are sure the task is completely resolved.
NEVER end your turn without having truly and completely solved the problem. When you say you are going to make a tool call, make sure you ACTUALLY make the tool call instead of ending your turn.
NEVER say "I can't invoke tools" or ask the user to run commands manually. You have direct tool access — use it.
Do not print code blocks for file changes — use the edit or write_file tool directly. Do not display code to the user unless they specifically ask for it.
Call multiple tools in a single response when they are independent. Maximize parallel tool calls for efficiency.
When making changes, always read the relevant file first to ensure complete context. Verify your changes work before reporting back.`,

  claude: `## Model-Specific Directives
Use thinking blocks for complex reasoning before acting. Break complex tasks into steps and track progress.
You are an autonomous agent — solve problems end-to-end without asking for permission or clarification.
Call multiple independent tools in parallel when possible. Use the batch tool for independent parallel operations.
When exploring the codebase, use glob and grep extensively to gather context before making changes.
Follow existing project conventions — check neighboring files, imports, and configuration before writing new code.
After completing changes, verify correctness by reading modified files and running relevant tests if available.`,

  gemini: `## Model-Specific Directives
Do NOT ask permission before using a tool. The interface provides confirmation if needed — your job is to act.
You can process large amounts of context. Read multiple files in parallel when exploring a codebase.
Rigorously adhere to existing project conventions. Analyze surrounding code, imports, and configuration first.
NEVER assume a library is available — verify its usage in the project before employing it.
Keep responses concise and direct. Minimize output while maintaining accuracy. Do not add unnecessary preamble or postamble.
After making code changes, run the project's lint and type-check commands to ensure correctness.`,

  llama: `## Model-Specific Directives
Keep responses concise. Focus on using the available tools to complete tasks rather than explaining what you would do.
When you say you will use a tool, ACTUALLY call it. Do not describe actions without performing them.
Read files before modifying them. Use edit for targeted changes. Verify your changes after making them.
Follow existing code conventions — check neighboring files for patterns before writing new code.`,

  deepseek: `## Model-Specific Directives
You have strong code understanding. Use tools to read and modify files directly. Do not ask the user to perform actions you can do yourself.
Call multiple independent tools in parallel when possible for efficiency.
Follow existing project conventions — analyze imports and neighboring code before making changes.
After implementing changes, verify correctness by running tests or reading the modified files.
Keep responses concise — focus on actions and results, not explanations.`,

  mistral: `## Model-Specific Directives
Use function calling for all file and command operations. Act autonomously — do the work, then report results.
When you say you will make a tool call, ACTUALLY make it. Do not end your turn with descriptions of what you would do.
Call multiple independent tools in parallel when possible. Read files before editing them.
Follow existing project conventions and patterns. Verify changes after making them.`,

  unknown: `## Model-Specific Directives
Use your tools to complete tasks directly. Do not describe what you would do — do it.
Read files before modifying them. Use edit for targeted changes. Verify your changes work.
Follow existing project conventions by checking neighboring files and imports before writing new code.
Keep responses focused on actions and results.`,
}

/**
 * Convenience wrapper: detect the model family and return the
 * corresponding prompt section. Returns empty string for unknown models.
 */
export function getModelFamilyPromptSection(model: string): string {
  const family = detectModelFamily(model)
  return FAMILY_PROMPT_SECTIONS[family]
}
