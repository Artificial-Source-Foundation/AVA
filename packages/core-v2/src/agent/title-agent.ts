/**
 * Title Agent — Generates concise chat titles from first user message
 *
 * Follows opencode's pattern: specialized agent with system prompt,
 * no tools, weak model preference for cost efficiency.
 */

import { getSettingsManager } from '../config/manager.js'
import type { LLMProvider } from '../llm/types.js'
import { AgentExecutor } from './loop.js'

const TITLE_SYSTEM_PROMPT = `You are a title generator. You output ONLY a thread title. Nothing else.

<task>
Generate a brief title that would help the user find this conversation later.

Follow all rules in <rules>
Use the <examples> so you know what a good title looks like.
Your output must be:
- A single line
- ≤50 characters
- No explanations
</task>

<rules>
- you MUST use the same language as the user message you are summarizing
- Title must be grammatically correct and read naturally - no word salad
- Never include tool names in the title (e.g. "read tool", "bash tool", "edit tool")
- Focus on the main topic or question the user needs to retrieve
- Vary your phrasing - avoid repetitive patterns like always starting with "Analyzing"
- When a file is mentioned, focus on WHAT the user wants to do WITH the file, not just that they shared it
- Keep exact: technical terms, numbers, filenames, HTTP codes
- Remove: the, this, my, a, an
- Never assume tech stack
- Never use tools
- NEVER respond to questions, just generate a title for the conversation
- The title should NEVER include "summarizing" or "generating" when generating a title
- DO NOT SAY YOU CANNOT GENERATE A TITLE OR COMPLAIN ABOUT THE INPUT
- Always output something meaningful, even if the input is minimal.
- If the user message is short or conversational (e.g. "hello", "lol", "what's up", "hey"):
  → create a title that reflects the user's tone or intent (such as Greeting, Quick check-in, Light chat, Intro message, etc.)
</rules>

<examples>
"debug 500 errors in production" → Debugging production 500 errors
"refactor user service" → Refactoring user service
"why is app.js failing" → app.js failure investigation
"implement rate limiting" → Rate limiting implementation
"how do I connect postgres to my API" → Postgres API connection
"best practices for React hooks" → React hooks best practices
"@src/auth.ts can you add refresh token support" → Auth refresh token support
"@utils/parser.ts this is broken" → Parser bug fix
"look at @config.json" → Config review
"@App.tsx add dark mode toggle" → Dark mode toggle in App
</examples>`

/**
 * Generate a concise title from the first user message.
 *
 * Uses the configured weak model for cost efficiency.
 * Returns null if generation fails (non-critical feature).
 */
export async function generateTitle(firstMessage: string): Promise<string | null> {
  try {
    const settingsMgr = getSettingsManager()
    let providerSettings: {
      defaultProvider: string
      defaultModel: string
      weakModel?: string
      weakModelProvider?: string
    } | null = null

    try {
      providerSettings = settingsMgr.get<{
        defaultProvider: string
        defaultModel: string
        weakModel?: string
        weakModelProvider?: string
      }>('provider')
      console.log('[AutoTitle] Provider settings loaded:', providerSettings)
    } catch (settingsErr) {
      console.error('[AutoTitle] Failed to load provider settings:', settingsErr)
      return null
    }

    // Use weak model if configured, otherwise fall back to default
    const model = providerSettings?.weakModel ?? providerSettings?.defaultModel
    const provider = (providerSettings?.weakModelProvider ??
      providerSettings?.defaultProvider) as LLMProvider

    console.log('[AutoTitle] Provider config:', { model, provider })

    if (!model || !provider) {
      console.error('[AutoTitle] Missing model or provider')
      return null
    }

    const executor = new AgentExecutor({
      id: 'title-agent',
      name: 'Title Generator',
      systemPrompt: TITLE_SYSTEM_PROMPT,
      provider,
      model,
      maxTurns: 1,
      maxTimeMinutes: 1,
      toolMode: 'none', // No tools needed for title generation
    })

    const result = await executor.run(
      {
        goal: firstMessage,
        cwd: '.',
        messages: [{ role: 'user', content: firstMessage }],
      },
      AbortSignal.timeout(10000) // 10s timeout for title generation
    )

    if (!result.success || !result.output) {
      return null
    }

    // Clean up the title
    let title =
      result.output
        .replace(/<think>[\s\S]*?<\/think>\s*/g, '') // Remove thinking tags
        .split('\n')
        .map((line) => line.trim())
        .find((line) => line.length > 0) ?? ''

    // Remove surrounding quotes if present
    title = title.replace(/^["']|["']$/g, '').trim()

    // Limit to 50 characters
    if (title.length > 50) {
      title = `${title.substring(0, 47)}...`
    }

    return title || null
  } catch (err) {
    console.error('[AutoTitle] Title generation failed:', err)
    return null
  }
}
