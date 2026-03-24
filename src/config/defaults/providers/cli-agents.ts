/**
 * CLI Agents Provider — External coding agents installed on the system
 * Agents like Claude Code, Gemini CLI, Codex, Aider, etc.
 * These use their own authentication — no API key needed in AVA.
 */

import { TerminalLogo } from '../../../components/icons/provider-logos'
import type { LLMProviderConfig } from '../provider-defaults'

export const cliAgents: LLMProviderConfig = {
  id: 'cli-agents',
  name: 'CLI Agents',
  icon: TerminalLogo,
  description: 'External coding agents (Claude Code, Gemini CLI, etc.)',
  enabled: true,
  status: 'disconnected',
  // Models are populated dynamically via Tauri discovery command.
  // These are static fallbacks for offline display.
  models: [
    {
      id: 'claude-code',
      name: 'Claude Code (CLI)',
      contextWindow: 200000,
      capabilities: ['tools', 'reasoning'],
    },
    {
      id: 'gemini-cli',
      name: 'Gemini CLI',
      contextWindow: 1000000,
      capabilities: ['tools', 'reasoning'],
    },
    {
      id: 'codex',
      name: 'Codex (CLI)',
      contextWindow: 200000,
      capabilities: ['tools'],
    },
    {
      id: 'aider',
      name: 'Aider (CLI)',
      contextWindow: 128000,
      capabilities: ['tools'],
    },
  ],
}
