/**
 * Built-in worker definitions.
 */

import type { WorkerDefinition } from './types.js'

export const CODER_WORKER: WorkerDefinition = {
  name: 'coder',
  displayName: 'Coder',
  description: 'Writes and modifies code files',
  systemPrompt:
    'You are a senior developer. Write clean, well-structured code. Focus on the task, make minimal changes, and follow existing patterns.',
  tools: ['read_file', 'write_file', 'create_file', 'delete_file', 'edit', 'grep', 'glob'],
  maxTurns: 15,
  maxTimeMinutes: 5,
}

export const TESTER_WORKER: WorkerDefinition = {
  name: 'tester',
  displayName: 'Tester',
  description: 'Writes and runs tests',
  systemPrompt:
    'You are a QA engineer. Write comprehensive tests covering happy paths, edge cases, and error cases. Run tests to verify they pass.',
  tools: ['read_file', 'write_file', 'create_file', 'bash', 'grep', 'glob'],
  maxTurns: 10,
  maxTimeMinutes: 5,
}

export const REVIEWER_WORKER: WorkerDefinition = {
  name: 'reviewer',
  displayName: 'Reviewer',
  description: 'Reviews code for quality, bugs, and security',
  systemPrompt:
    'You are a code reviewer. Analyze code for bugs, security issues, and quality. You have read-only access.',
  tools: ['read_file', 'grep', 'glob'],
  maxTurns: 10,
  maxTimeMinutes: 5,
}

export const RESEARCHER_WORKER: WorkerDefinition = {
  name: 'researcher',
  displayName: 'Researcher',
  description: 'Explores the codebase and gathers context',
  systemPrompt:
    'You are a codebase researcher. Explore the codebase to gather context and understand architecture. You have read-only access.',
  tools: ['read_file', 'grep', 'glob', 'ls'],
  maxTurns: 15,
  maxTimeMinutes: 5,
}

export const DEBUGGER_WORKER: WorkerDefinition = {
  name: 'debugger',
  displayName: 'Debugger',
  description: 'Debugs and fixes errors',
  systemPrompt:
    'You are a debugging specialist. Diagnose issues, trace errors, and apply fixes. Be methodical and verify fixes work.',
  tools: ['read_file', 'write_file', 'edit', 'bash', 'grep', 'glob'],
  maxTurns: 15,
  maxTimeMinutes: 5,
}

export const BUILTIN_WORKERS: WorkerDefinition[] = [
  CODER_WORKER,
  TESTER_WORKER,
  REVIEWER_WORKER,
  RESEARCHER_WORKER,
  DEBUGGER_WORKER,
]
