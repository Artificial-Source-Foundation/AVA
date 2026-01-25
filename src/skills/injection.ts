/**
 * Delta9 Skills System - Injection
 *
 * Handles:
 * - Model-aware skill rendering (XML for Claude, JSON for GPT, MD for Gemini)
 * - Skill injection into sessions
 * - Active skill tracking per session
 */

import type { Skill, SkillSummary, RenderFormat, SkillInjectionOptions, SkillInjectionResult } from './types.js'

// =============================================================================
// State
// =============================================================================

/** Active skills per session */
const activeSessions = new Map<string, Set<string>>()

// =============================================================================
// Model Format Detection
// =============================================================================

/** Model provider to render format mapping */
const PROVIDER_FORMATS: Record<string, RenderFormat> = {
  anthropic: 'xml',
  openai: 'json',
  google: 'md',
  deepseek: 'json',
  ollama: 'md',
}

/** Specific model overrides */
const MODEL_FORMATS: Record<string, RenderFormat> = {
  'claude-3': 'xml',
  'claude-opus': 'xml',
  'claude-sonnet': 'xml',
  'claude-haiku': 'xml',
  'gpt-4': 'json',
  'gpt-4o': 'json',
  'o1': 'json',
  'gemini': 'md',
  'deepseek': 'json',
}

/**
 * Detect the best render format for a model
 */
export function getFormatForModel(providerId?: string, modelId?: string): RenderFormat {
  // Check specific model first
  if (modelId) {
    for (const [key, format] of Object.entries(MODEL_FORMATS)) {
      if (modelId.toLowerCase().includes(key)) {
        return format
      }
    }
  }

  // Provider-level fallback
  if (providerId && providerId in PROVIDER_FORMATS) {
    return PROVIDER_FORMATS[providerId]
  }

  // Default to XML (most structured)
  return 'xml'
}

// =============================================================================
// Skill Rendering
// =============================================================================

/**
 * Render skill content in the specified format
 */
export function renderSkill(skill: Skill, format: RenderFormat, options?: SkillInjectionOptions): string {
  const includeScripts = options?.includeScripts ?? skill.scripts.length > 0
  const includeResources = options?.includeResources ?? skill.resources.length > 0

  switch (format) {
    case 'xml':
      return renderSkillXML(skill, includeScripts, includeResources)
    case 'json':
      return renderSkillJSON(skill, includeScripts, includeResources)
    case 'md':
    default:
      return renderSkillMarkdown(skill, includeScripts, includeResources)
  }
}

/**
 * Render skill in XML format (preferred for Claude)
 */
function renderSkillXML(skill: Skill, includeScripts: boolean, includeResources: boolean): string {
  const parts: string[] = []

  parts.push(`<skill name="${escapeXML(skill.name)}">`)
  parts.push(`  <metadata>`)
  parts.push(`    <description>${escapeXML(skill.description)}</description>`)
  parts.push(`    <source>${skill.label}</source>`)
  parts.push(`    <directory>${escapeXML(skill.path)}</directory>`)

  if (skill.useWhen) {
    parts.push(`    <use-when>${escapeXML(skill.useWhen)}</use-when>`)
  }

  if (skill.allowedTools && skill.allowedTools.length > 0) {
    parts.push(`    <allowed-tools>${skill.allowedTools.join(', ')}</allowed-tools>`)
  }

  parts.push(`  </metadata>`)

  if (includeScripts && skill.scripts.length > 0) {
    parts.push(`  <scripts>`)
    for (const script of skill.scripts) {
      parts.push(`    <script path="${escapeXML(script.relativePath)}" />`)
    }
    parts.push(`  </scripts>`)
  }

  if (includeResources && skill.resources.length > 0) {
    parts.push(`  <resources>`)
    for (const resource of skill.resources) {
      parts.push(`    <resource path="${escapeXML(resource.relativePath)}" type="${resource.type}" />`)
    }
    parts.push(`  </resources>`)
  }

  parts.push(`  <content>`)
  parts.push(skill.template)
  parts.push(`  </content>`)
  parts.push(`</skill>`)

  return parts.join('\n')
}

/**
 * Render skill in JSON format (preferred for GPT)
 */
function renderSkillJSON(skill: Skill, includeScripts: boolean, includeResources: boolean): string {
  const obj: Record<string, unknown> = {
    name: skill.name,
    description: skill.description,
    source: skill.label,
    directory: skill.path,
    content: skill.template,
  }

  if (skill.useWhen) {
    obj.useWhen = skill.useWhen
  }

  if (skill.allowedTools && skill.allowedTools.length > 0) {
    obj.allowedTools = skill.allowedTools
  }

  if (includeScripts && skill.scripts.length > 0) {
    obj.scripts = skill.scripts.map((s) => s.relativePath)
  }

  if (includeResources && skill.resources.length > 0) {
    obj.resources = skill.resources.map((r) => ({ path: r.relativePath, type: r.type }))
  }

  return JSON.stringify({ skill: obj }, null, 2)
}

/**
 * Render skill in Markdown format (default/fallback)
 */
function renderSkillMarkdown(skill: Skill, includeScripts: boolean, includeResources: boolean): string {
  const parts: string[] = []

  parts.push(`## Skill: ${skill.name}`)
  parts.push('')
  parts.push(`**Description:** ${skill.description}`)
  parts.push(`**Source:** ${skill.label}`)
  parts.push(`**Directory:** ${skill.path}`)

  if (skill.useWhen) {
    parts.push(`**Use when:** ${skill.useWhen}`)
  }

  if (skill.allowedTools && skill.allowedTools.length > 0) {
    parts.push(`**Allowed tools:** ${skill.allowedTools.join(', ')}`)
  }

  parts.push('')

  if (includeScripts && skill.scripts.length > 0) {
    parts.push('### Scripts')
    for (const script of skill.scripts) {
      parts.push(`- \`${script.relativePath}\``)
    }
    parts.push('')
  }

  if (includeResources && skill.resources.length > 0) {
    parts.push('### Resources')
    for (const resource of skill.resources) {
      parts.push(`- \`${resource.relativePath}\` (${resource.type})`)
    }
    parts.push('')
  }

  parts.push('### Instructions')
  parts.push('')
  parts.push(skill.template)

  return parts.join('\n')
}

/**
 * Escape XML special characters
 */
function escapeXML(str: string): string {
  return str
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&apos;')
}

// =============================================================================
// Skill List Rendering
// =============================================================================

/**
 * Render available skills list for session injection
 */
export function renderSkillsList(skills: SkillSummary[], format: RenderFormat): string {
  switch (format) {
    case 'xml':
      return renderSkillsListXML(skills)
    case 'json':
      return renderSkillsListJSON(skills)
    case 'md':
    default:
      return renderSkillsListMarkdown(skills)
  }
}

function renderSkillsListXML(skills: SkillSummary[]): string {
  const parts: string[] = []

  parts.push(`<available-skills>`)
  parts.push(`  <instructions>`)
  parts.push(`    Use the use_skill, read_skill_file, run_skill_script, and list_skills tools to work with skills.`)
  parts.push(`  </instructions>`)
  parts.push(`  <skills>`)

  for (const skill of skills) {
    parts.push(`    <skill name="${escapeXML(skill.name)}">`)
    parts.push(`      <description>${escapeXML(skill.description)}</description>`)
    parts.push(`      <source>${skill.label}</source>`)
    if (skill.useWhen) {
      parts.push(`      <use-when>${escapeXML(skill.useWhen)}</use-when>`)
    }
    parts.push(`    </skill>`)
  }

  parts.push(`  </skills>`)
  parts.push(`</available-skills>`)

  return parts.join('\n')
}

function renderSkillsListJSON(skills: SkillSummary[]): string {
  return JSON.stringify(
    {
      availableSkills: {
        instructions:
          'Use the use_skill, read_skill_file, run_skill_script, and list_skills tools to work with skills.',
        skills: skills.map((s) => ({
          name: s.name,
          description: s.description,
          source: s.label,
          useWhen: s.useWhen,
        })),
      },
    },
    null,
    2
  )
}

function renderSkillsListMarkdown(skills: SkillSummary[]): string {
  const parts: string[] = []

  parts.push(`## Available Skills`)
  parts.push('')
  parts.push(`Use the \`use_skill\`, \`read_skill_file\`, \`run_skill_script\`, and \`list_skills\` tools to work with skills.`)
  parts.push('')

  for (const skill of skills) {
    parts.push(`- **${skill.name}** (${skill.label}): ${skill.description}`)
    if (skill.useWhen) {
      parts.push(`  - Use when: ${skill.useWhen}`)
    }
  }

  return parts.join('\n')
}

// =============================================================================
// Session Tracking
// =============================================================================

/**
 * Mark a skill as active in a session
 */
export function activateSkillInSession(sessionId: string, skillName: string): void {
  let sessionSkills = activeSessions.get(sessionId)
  if (!sessionSkills) {
    sessionSkills = new Set()
    activeSessions.set(sessionId, sessionSkills)
  }
  sessionSkills.add(skillName)
}

/**
 * Deactivate a skill in a session
 */
export function deactivateSkillInSession(sessionId: string, skillName: string): void {
  const sessionSkills = activeSessions.get(sessionId)
  if (sessionSkills) {
    sessionSkills.delete(skillName)
  }
}

/**
 * Get active skills for a session
 */
export function getActiveSkills(sessionId: string): string[] {
  const sessionSkills = activeSessions.get(sessionId)
  return sessionSkills ? Array.from(sessionSkills) : []
}

/**
 * Check if a skill is active in a session
 */
export function isSkillActive(sessionId: string, skillName: string): boolean {
  const sessionSkills = activeSessions.get(sessionId)
  return sessionSkills?.has(skillName) ?? false
}

/**
 * Clear all active skills for a session
 */
export function clearSessionSkills(sessionId: string): void {
  activeSessions.delete(sessionId)
}

/**
 * Clear all session tracking (for testing)
 */
export function clearAllSessionSkills(): void {
  activeSessions.clear()
}

// =============================================================================
// Skill Injection
// =============================================================================

/**
 * Create injection result for a skill
 */
export function injectSkill(
  skill: Skill,
  providerId?: string,
  modelId?: string,
  options?: SkillInjectionOptions
): SkillInjectionResult {
  try {
    const format = options?.format ?? getFormatForModel(providerId, modelId)
    const content = renderSkill(skill, format, options)

    return {
      name: skill.name,
      content,
      success: true,
    }
  } catch (error) {
    return {
      name: skill.name,
      content: '',
      success: false,
      error: error instanceof Error ? error.message : 'Unknown error',
    }
  }
}
