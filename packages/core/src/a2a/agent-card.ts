/**
 * A2A Agent Card
 *
 * Describes AVA's capabilities for agent discovery.
 * Served at /.well-known/agent.json
 */

import type { A2AServerConfig, AgentCard } from './types.js'
import { A2A_PROTOCOL_VERSION, DEFAULT_A2A_PORT, DEFAULT_AGENT_VERSION } from './types.js'

// ============================================================================
// Agent Card Creation
// ============================================================================

/**
 * Create the AVA agent card for A2A discovery.
 *
 * @param config - Server configuration
 * @returns AgentCard describing AVA's capabilities
 */
export function createAgentCard(config: A2AServerConfig = {}): AgentCard {
  const port = config.port ?? DEFAULT_A2A_PORT
  const host = config.host ?? 'localhost'
  const version = config.agentVersion ?? DEFAULT_AGENT_VERSION

  return {
    name: 'AVA',
    description:
      'Multi-agent AI coding assistant with browser automation, fuzzy edits, and parallel execution',
    url: `http://${host}:${port}/`,
    version,
    protocolVersion: A2A_PROTOCOL_VERSION,
    provider: {
      organization: 'AVA',
      url: 'https://github.com/g0dxn4/AVA',
    },
    capabilities: {
      streaming: true,
      pushNotifications: false,
      stateTransitionHistory: true,
    },
    skills: [
      {
        id: 'code-editing',
        name: 'Code Generation & Editing',
        description: 'Generate, edit, and refactor code with fuzzy edit strategies',
        tags: ['code', 'edit', 'refactor'],
        examples: [
          'Add error handling to the login function',
          'Refactor this module to use async/await',
        ],
      },
      {
        id: 'code-search',
        name: 'Codebase Search & Analysis',
        description: 'Search files by pattern, grep content, analyze code structure',
        tags: ['search', 'grep', 'glob', 'analysis'],
        examples: [
          'Find all files that import the database module',
          'Search for TODO comments in the codebase',
        ],
      },
      {
        id: 'shell-execution',
        name: 'Shell Command Execution',
        description:
          'Execute shell commands with PTY support for testing, building, and deployment',
        tags: ['bash', 'shell', 'terminal'],
        examples: [
          'Run the test suite and report failures',
          'Build the project and check for errors',
        ],
      },
      {
        id: 'browser-automation',
        name: 'Browser Automation',
        description: 'Automate web tasks with Puppeteer (click, type, screenshot)',
        tags: ['browser', 'testing', 'web', 'puppeteer'],
        examples: [
          'Take a screenshot of the login page',
          'Fill in the registration form and submit',
        ],
      },
      {
        id: 'task-delegation',
        name: 'Multi-Agent Task Delegation',
        description: 'Delegate subtasks to specialized worker agents for parallel execution',
        tags: ['agents', 'delegation', 'parallel'],
        examples: [
          'Review code quality while running tests in parallel',
          'Analyze multiple files simultaneously',
        ],
      },
    ],
    defaultInputModes: ['text'],
    defaultOutputModes: ['text'],
    authentication: config.authToken
      ? { schemes: [{ scheme: 'bearer', description: 'Bearer token authentication' }] }
      : undefined,
  }
}
