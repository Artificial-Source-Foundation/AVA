/**
 * Delta9 Agent Definitions
 *
 * Agents for the Delta9 multi-agent system:
 * - Commander: Strategic planning and orchestration (primary)
 * - Operator: Task execution (subagent)
 * - Validator: Quality verification (subagent)
 */

import type { AgentConfig } from '@opencode-ai/sdk'

// =============================================================================
// Legacy Agent Exports (for backward compatibility)
// =============================================================================

export {
  commanderAgent,
  commanderPlanningAgent,
  commanderExecutionAgent,
} from './commander.js'

export {
  operatorAgent,
  operatorComplexAgent,
} from './operator.js'

export {
  validatorAgent,
  validatorStrictAgent,
} from './validator.js'

// Agent router
export {
  routeTask,
  getAgentModel,
  isSupportAgent,
  getSupportAgents,
  suggestSupportAgents,
  type AgentType,
  type RoutingDecision,
} from './router.js'

// Support agents - Delta Team
export {
  // RECON (reconnaissance)
  createReconAgent,
  reconConfig,
  RECON_PROFILE,
  // SIGINT (intelligence research)
  createSigintAgent,
  sigintConfig,
  SIGINT_PROFILE,
  // TACCOM (tactical command)
  createTaccomAgent,
  taccomConfig,
  TACCOM_PROFILE,
  // SURGEON (surgical fixes)
  createSurgeonAgent,
  surgeonConfig,
  SURGEON_PROFILE,
  // SENTINEL (quality assurance)
  createSentinelAgent,
  sentinelConfig,
  SENTINEL_PROFILE,
  // SCRIBE (documentation)
  createScribeAgent,
  scribeConfig,
  SCRIBE_PROFILE,
  // FACADE (frontend operations)
  createFacadeAgent,
  facadeConfig,
  FACADE_PROFILE,
  // SPECTRE (visual intelligence)
  createSpectreAgent,
  spectreConfig,
  SPECTRE_PROFILE,
  // Registry
  supportAgentFactories,
  supportProfiles,
  supportConfigs,
  codenameToConfigKey,
  configKeyToCodename,
  createSupportAgent,
  createSupportAgentByConfigKey,
  listSupportAgents,
  isSupportAgentAvailable,
  getSupportAgentProfile,
  type SupportAgentName,
  type SupportAgentConfigKey,
} from './support/index.js'

// Council agents - The Delta Team
export {
  // Individual Oracle agents
  cipherAgent,
  cipherConfig,
  CIPHER_PROFILE,
  vectorAgent,
  vectorConfig,
  VECTOR_PROFILE,
  prismAgent,
  prismConfig,
  PRISM_PROFILE,
  apexAgent,
  apexConfig,
  APEX_PROFILE,
  // Registry and helpers
  councilAgents,
  oracleConfigs,
  oracleProfiles,
  getOracleAgent,
  getOracleConfig,
  getOracleProfile,
  listOracleCodenames,
  getOraclesBySpecialty,
  defaultOracleConfigs,
  getQuickModeOracles,
  getStandardModeOracles,
  getOracleDescription,
  // Types
  type OracleCodename,
  type OracleSpecialty,
  type OracleProfile,
} from './council/index.js'

// Re-export the AgentConfig type
export type { AgentConfig }

// =============================================================================
// Agent System Prompts
// =============================================================================

const COMMANDER_PROMPT = `You are Commander, the strategic planning and orchestration agent for Delta9.

## Your Role

You orchestrate mission-based development. Your job is to:
1. Analyze user requests and determine complexity
2. Create missions with objectives and tasks using Delta9 tools
3. Dispatch tasks to Operators via delegate_task
4. Monitor progress and coordinate execution
5. Ensure quality through validation

## Critical Rules

- Use mission_create to start new missions
- Use mission_add_objective and mission_add_task to build the plan
- Use dispatch_task to delegate work to Operators
- Use delegate_task for parallel background execution
- NEVER write code yourself - only plan and delegate

## Available Tools

### Mission Management
- mission_create: Create a new mission with goals
- mission_status: Check current mission state
- mission_add_objective: Add objectives to mission
- mission_add_task: Add tasks to objectives

### Task Execution
- dispatch_task: Send task to an Operator for execution
- delegate_task: Spawn background tasks (parallel execution)
- task_complete: Mark a task as complete
- request_validation: Request Validator to verify work

### Validation
- validation_result: Record validation outcome
- run_tests: Execute test suite
- check_lint: Run linter
- check_types: Run type checker

### Council (for complex decisions)
- consult_council: Get multi-model perspective on architecture decisions
- quick_consult: Fast single-oracle consultation

### Memory
- memory_get/set/list: Persistent cross-session memory

## Workflow

1. **New Request** → Analyze complexity, create mission with mission_create
2. **Plan** → Add objectives (mission_add_objective) and tasks (mission_add_task)
3. **Execute** → Dispatch tasks to Operators (dispatch_task or delegate_task for parallel)
4. **Validate** → Each completed task goes through validation
5. **Iterate** → Handle failures, adjust plan as needed

## Communication Style

- Be concise and direct
- Focus on WHAT and WHY, not HOW
- Use bullet points for acceptance criteria
- Let Operators figure out implementation details
`

const OPERATOR_PROMPT = `You are an Operator agent for Delta9.

## Your Role

You execute tasks dispatched by Commander. You:
1. Receive specific tasks with acceptance criteria
2. Implement the required changes
3. Report completion or issues

## Rules

- Focus on the specific task assigned
- Follow acceptance criteria exactly
- Report when done or if blocked
- Don't expand scope beyond the task

## Tools Available

You have access to standard coding tools (Read, Write, Edit, Bash, Glob, Grep, etc.).
Use them to implement the requested changes efficiently.
`

const VALIDATOR_PROMPT = `You are a Validator agent for Delta9.

## Your Role

You verify that completed tasks meet their acceptance criteria. You:
1. Review the work done
2. Run tests, linter, type checks
3. Verify acceptance criteria are met
4. Report pass/fail with details

## Available Tools

- run_tests: Execute test suite
- check_lint: Run linter
- check_types: Run type checker
- validation_result: Record the validation outcome

## Rules

- Be thorough but fair
- Check all acceptance criteria
- Run automated checks (tests, lint, types)
- Provide specific feedback on failures
`

// =============================================================================
// Agent Configurations (OpenCode SDK Format)
// =============================================================================

/**
 * Get agent configurations for OpenCode registration.
 * These agents will appear in the agent selector (Tab menu).
 */
export function getAgentConfigs(): Record<string, AgentConfig> {
  return {
    // Primary agent - appears in agent list
    commander: {
      model: 'anthropic/claude-sonnet-4',
      temperature: 0.7,
      prompt: COMMANDER_PROMPT,
      mode: 'primary',
      description: 'Strategic planning and orchestration for mission-based development',
    },

    // Subagents - invoked via delegate_task
    operator: {
      model: 'anthropic/claude-sonnet-4',
      temperature: 0.3,
      prompt: OPERATOR_PROMPT,
      mode: 'subagent',
      description: 'Task execution specialist for implementing changes',
    },

    validator: {
      model: 'anthropic/claude-haiku-4',
      temperature: 0.1,
      prompt: VALIDATOR_PROMPT,
      mode: 'subagent',
      description: 'Quality verification and automated testing',
    },
  }
}
