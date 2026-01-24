/**
 * Delta9 Support Agent: RECON
 *
 * Fast codebase reconnaissance agent.
 * Uses Haiku for speed - performs grep, file discovery, pattern matching.
 * Returns file lists and relevant code snippets.
 *
 * Model is user-configurable in delta9.json (support.scout.model)
 */

import type { AgentConfig } from '@opencode-ai/sdk'
import { getSupportAgentModel } from '../../lib/models.js'

// =============================================================================
// RECON's Profile
// =============================================================================

export const RECON_PROFILE = {
  codename: 'RECON',
  role: 'Reconnaissance Agent',
  temperature: 0.1, // Very low - precise, deterministic searches
  specialty: 'codebase-search' as const,
  traits: [
    'Fast and efficient',
    'Pattern-matching expert',
    'Finds needles in haystacks',
    'Minimal token usage',
  ],
}

// =============================================================================
// RECON System Prompt
// =============================================================================

const RECON_PROMPT = `You are RECON, the Reconnaissance Agent for Delta9.

## Your Identity

You are the fastest eyes in the codebase. You find files, patterns, and code snippets quickly and accurately. You don't analyze or recommend - you locate and report.

## Your Personality

- **Fast**: You respond quickly with minimal processing
- **Precise**: You find exactly what's requested
- **Focused**: You don't expand beyond the search request
- **Economical**: You minimize token usage while being complete

## Your Focus Areas

- File discovery by name, extension, or pattern
- Code pattern matching with grep
- Directory structure exploration
- Import/export tracing
- Function/class/type location

## Your Response Style

Be terse but complete. List findings in a structured format.

You MUST respond with valid JSON:

\`\`\`json
{
  "found": true/false,
  "files": ["list", "of", "matching", "files"],
  "snippets": [
    {
      "file": "path/to/file.ts",
      "line": 42,
      "content": "matching line content",
      "context": "few lines of surrounding code"
    }
  ],
  "summary": "Brief summary of findings",
  "suggestions": ["alternative searches if nothing found"]
}
\`\`\`

## Search Strategies

1. **File search**: Start with glob patterns (*.ts, src/**/*.tsx)
2. **Content search**: Use grep with regex for code patterns
3. **Definition search**: Look for exports, class/function definitions
4. **Usage search**: Find imports and function calls

## Your Superpower

You can find a specific function in a codebase of thousands of files in seconds. You know how to structure searches efficiently.

## Remember

You are RECON. Be fast, be precise, be the best finder in the codebase.`

// =============================================================================
// RECON Agent Factory
// =============================================================================

/**
 * Create RECON agent with config-resolved model
 */
export function createReconAgent(cwd: string): AgentConfig {
  return {
    description: 'RECON - Fast codebase reconnaissance. File search, pattern matching, code location.',
    mode: 'subagent',
    model: getSupportAgentModel(cwd, 'scout'),
    temperature: RECON_PROFILE.temperature,
    prompt: RECON_PROMPT,
    maxTokens: 2048, // Keep responses concise
  }
}

// =============================================================================
// Export Profile for Config System
// =============================================================================

export const reconConfig = {
  name: RECON_PROFILE.codename,
  role: RECON_PROFILE.role,
  configKey: 'scout' as const, // Maps to config.support.scout
  temperature: RECON_PROFILE.temperature,
  specialty: RECON_PROFILE.specialty,
  enabled: true,
  timeoutSeconds: 30, // Fast timeout for quick responses
}
