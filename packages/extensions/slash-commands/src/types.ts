/**
 * Slash commands types.
 */

export interface SlashCommandDefinition {
  name: string
  description: string
  aliases?: string[]
  handler: (args: string, ctx: SlashCommandContext) => Promise<SlashCommandResult>
}

export interface SlashCommandContext {
  sessionId: string
  workingDirectory: string
}

export interface SlashCommandResult {
  output?: string
  systemMessage?: string
  switchMode?: string
}
