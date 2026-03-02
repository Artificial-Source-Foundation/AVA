/**
 * Explore worker — read-only subagent for codebase exploration.
 *
 * Hard constraint: only read-only tools allowed, all write/exec tools denied.
 * Use this for safe, non-destructive codebase analysis and research.
 */

import type { AgentDefinition } from './agent-definition.js'

/** Read-only tools the explorer can use. */
export const EXPLORE_ALLOWED_TOOLS = [
  'read_file',
  'glob',
  'grep',
  'ls',
  'repo_map',
  'websearch',
  'webfetch',
] as const

/** Write/exec tools explicitly denied for the explorer. */
export const EXPLORE_DENIED_TOOLS = [
  'write_file',
  'create_file',
  'delete_file',
  'edit',
  'bash',
  'bash_background',
  'bash_output',
  'bash_kill',
  'apply_patch',
  'multiedit',
  'task',
  'delegate_coder',
  'delegate_tester',
  'delegate_reviewer',
  'delegate_researcher',
  'delegate_debugger',
] as const

export const EXPLORE_WORKER: AgentDefinition = {
  id: 'explorer',
  name: 'explorer',
  displayName: 'Explorer',
  description: 'Explores the codebase in read-only mode — cannot modify any files or run commands',
  tier: 'worker',
  systemPrompt: `You are a codebase explorer. Your job is to understand code structure, find patterns, trace dependencies, and gather information.

You have ONLY read-only access. You cannot modify files, run shell commands, or make any changes.

Focus on:
- Searching for patterns across the codebase
- Reading and understanding file contents
- Mapping project structure and dependencies
- Answering questions about the codebase
- Researching external documentation via web search

Always provide thorough, well-organized findings.`,
  tools: [...EXPLORE_ALLOWED_TOOLS],
  deniedTools: [...EXPLORE_DENIED_TOOLS],
  maxTurns: 15,
  maxTimeMinutes: 5,
  domain: 'fullstack',
  icon: 'Compass',
  capabilities: ['codebase-exploration', 'read-only-analysis', 'dependency-tracing'],
  isBuiltIn: true,
}
