/**
 * Delta9 Skills Tools
 *
 * Tools for working with skills:
 * - list_skills: List all available skills
 * - use_skill: Load a skill into the current context
 * - read_skill_file: Read a resource file from a skill
 * - run_skill_script: Execute a script from a skill
 * - get_skill: Get detailed information about a skill
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import { exec } from 'node:child_process'
import { promisify } from 'node:util'
import {
  discoverSkills,
  resolveSkill,
  readSkillResource,
  listSkillFiles,
  injectSkill,
  activateSkillInSession,
  getActiveSkills,
  isSkillActive,
  getFormatForModel,
  renderSkillsList,
  type SkillSummary,
} from '../skills/index.js'

const execAsync = promisify(exec)

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Tool Factory
// =============================================================================

export interface SkillToolsConfig {
  cwd: string
  sessionId?: string
  providerId?: string
  modelId?: string
  log?: (level: string, message: string, data?: Record<string, unknown>) => void
}

/**
 * Create skill tools with bound context
 */
export function createSkillTools(config: SkillToolsConfig): Record<string, ToolDefinition> {
  const { cwd, sessionId = 'default', providerId, modelId, log } = config

  /**
   * List all available skills
   */
  const list_skills = tool({
    description:
      'List all available skills. Skills are reusable prompt templates that provide specialized instructions.',
    args: {
      showActive: s.boolean().optional().describe('If true, only show skills currently active in the session'),
    },

    async execute(args, _ctx) {
      log?.('debug', 'Listing skills', { showActive: args.showActive })

      const skills = await discoverSkills(cwd)

      let skillList: SkillSummary[]

      if (args.showActive) {
        const activeNames = getActiveSkills(sessionId)
        skillList = Array.from(skills.values())
          .filter((skill) => activeNames.includes(skill.name))
          .map((skill) => ({
            name: skill.name,
            description: skill.description,
            label: skill.label,
            useWhen: skill.useWhen,
          }))
      } else {
        skillList = Array.from(skills.values()).map((skill) => ({
          name: skill.name,
          description: skill.description,
          label: skill.label,
          useWhen: skill.useWhen,
        }))
      }

      if (skillList.length === 0) {
        return args.showActive
          ? 'No active skills in this session.'
          : 'No skills found. Create skills in .delta9/skills/ or ~/.config/delta9/skills/'
      }

      // Render in XML format (most structured)
      const format = getFormatForModel('anthropic')
      return renderSkillsList(skillList, format)
    },
  })

  /**
   * Load a skill into the current context
   */
  const use_skill = tool({
    description:
      'Load a skill into the current context. The skill instructions will be injected and available for the rest of the session.',
    args: {
      skill: s.string().describe('Name of the skill to load (e.g., "my-skill" or "project:my-skill")'),
    },

    async execute(args, _ctx) {
      log?.('info', 'Loading skill', { skill: args.skill })

      const skills = await discoverSkills(cwd)
      const skill = resolveSkill(args.skill, skills)

      if (!skill) {
        const available = Array.from(skills.keys()).join(', ')
        return `Skill "${args.skill}" not found. Available skills: ${available || 'none'}`
      }

      // Check if already active
      if (isSkillActive(sessionId, skill.name)) {
        return `Skill "${skill.name}" is already active in this session.`
      }

      // Inject the skill
      const result = injectSkill(skill, providerId, modelId)

      if (!result.success) {
        return `Failed to load skill "${skill.name}": ${result.error}`
      }

      // Mark as active
      activateSkillInSession(sessionId, skill.name)

      // Return the formatted skill content
      return `Skill "${skill.name}" loaded successfully.\n\n${result.content}`
    },
  })

  /**
   * Read a resource file from a skill
   */
  const read_skill_file = tool({
    description: 'Read a resource file from a skill directory (documentation, templates, guides).',
    args: {
      skill: s.string().describe('Name of the skill'),
      file: s.string().describe('Relative path to the file within the skill directory'),
    },

    async execute(args, _ctx) {
      log?.('debug', 'Reading skill file', { skill: args.skill, file: args.file })

      const skills = await discoverSkills(cwd)
      const skill = resolveSkill(args.skill, skills)

      if (!skill) {
        return `Skill "${args.skill}" not found.`
      }

      const content = await readSkillResource(skill, args.file)

      if (content === null) {
        const availableFiles = await listSkillFiles(skill)
        return `File "${args.file}" not found in skill "${skill.name}". Available files: ${availableFiles.join(', ') || 'none'}`
      }

      return `<skill-file skill="${skill.name}" file="${args.file}">\n${content}\n</skill-file>`
    },
  })

  /**
   * Run a script from a skill
   */
  const run_skill_script = tool({
    description: 'Execute a script from a skill directory. Scripts must be executable.',
    args: {
      skill: s.string().describe('Name of the skill'),
      script: s.string().describe('Relative path to the script within the skill directory'),
      scriptArgs: s.string().optional().describe('Arguments to pass to the script (space-separated)'),
      timeout: s.number().optional().describe('Timeout in milliseconds (default: 30000)'),
    },

    async execute(args, _ctx) {
      log?.('info', 'Running skill script', { skill: args.skill, script: args.script })

      const skills = await discoverSkills(cwd)
      const skill = resolveSkill(args.skill, skills)

      if (!skill) {
        return `Skill "${args.skill}" not found.`
      }

      const scriptInfo = skill.scripts.find((sc) => sc.relativePath === args.script)

      if (!scriptInfo) {
        const availableScripts = skill.scripts.map((sc) => sc.relativePath)
        return `Script "${args.script}" not found in skill "${skill.name}". Available scripts: ${availableScripts.join(', ') || 'none'}`
      }

      try {
        const scriptArgs = args.scriptArgs ?? ''
        const command = `"${scriptInfo.absolutePath}" ${scriptArgs}`
        const timeout = args.timeout ?? 30000

        const { stdout, stderr } = await execAsync(command, {
          cwd: skill.path,
          timeout,
        })

        const output: string[] = []
        if (stdout.trim()) {
          output.push(`stdout:\n${stdout.trim()}`)
        }
        if (stderr.trim()) {
          output.push(`stderr:\n${stderr.trim()}`)
        }

        return `<skill-script skill="${skill.name}" script="${args.script}">\n${output.join('\n\n') || '(no output)'}\n</skill-script>`
      } catch (error) {
        if (error instanceof Error) {
          return `Error running script "${args.script}": ${error.message}`
        }
        return `Error running script "${args.script}": Unknown error`
      }
    },
  })

  /**
   * Get skill details
   */
  const get_skill = tool({
    description: 'Get detailed information about a skill including its scripts, resources, and MCP configuration.',
    args: {
      skill: s.string().describe('Name of the skill to get details for'),
    },

    async execute(args, _ctx) {
      log?.('debug', 'Getting skill details', { skill: args.skill })

      const skills = await discoverSkills(cwd)
      const skill = resolveSkill(args.skill, skills)

      if (!skill) {
        return `Skill "${args.skill}" not found.`
      }

      const details: string[] = []
      details.push(`<skill-details name="${skill.name}">`)
      details.push(`  <description>${skill.description}</description>`)
      details.push(`  <source>${skill.label}</source>`)
      details.push(`  <path>${skill.path}</path>`)

      if (skill.useWhen) {
        details.push(`  <use-when>${skill.useWhen}</use-when>`)
      }

      if (skill.allowedTools && skill.allowedTools.length > 0) {
        details.push(`  <allowed-tools>${skill.allowedTools.join(', ')}</allowed-tools>`)
      }

      if (skill.mcp) {
        details.push(`  <mcp>`)
        details.push(`    <command>${skill.mcp.command}</command>`)
        if (skill.mcp.args) {
          details.push(`    <args>${skill.mcp.args.join(' ')}</args>`)
        }
        details.push(`  </mcp>`)
      }

      if (skill.scripts.length > 0) {
        details.push(`  <scripts>`)
        for (const script of skill.scripts) {
          details.push(`    <script>${script.relativePath}</script>`)
        }
        details.push(`  </scripts>`)
      }

      if (skill.resources.length > 0) {
        details.push(`  <resources>`)
        for (const resource of skill.resources) {
          details.push(`    <resource type="${resource.type}">${resource.relativePath}</resource>`)
        }
        details.push(`  </resources>`)
      }

      details.push(`</skill-details>`)

      return details.join('\n')
    },
  })

  return {
    list_skills,
    use_skill,
    read_skill_file,
    run_skill_script,
    get_skill,
  }
}
