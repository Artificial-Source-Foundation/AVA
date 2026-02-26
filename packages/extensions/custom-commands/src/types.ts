/**
 * Custom commands types.
 */

export interface CustomCommand {
  name: string
  description: string
  prompt: string
  allowedTools?: string[]
  mode?: string
  source: string
}

export interface CustomCommandConfig {
  searchPaths: string[]
  fileNames: string[]
}

export const DEFAULT_CUSTOM_COMMAND_CONFIG: CustomCommandConfig = {
  searchPaths: ['.ava/commands', '~/.config/ava/commands'],
  fileNames: ['*.toml'],
}
