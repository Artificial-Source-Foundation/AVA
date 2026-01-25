/**
 * Delta9 Skills System
 *
 * Skills are reusable prompt templates that can be loaded from:
 * - Project: .delta9/skills/ (highest priority)
 * - User: ~/.config/delta9/skills/
 * - Global: ~/.delta9/skills/
 * - Builtin: Bundled with the plugin
 *
 * Features:
 * - YAML frontmatter with Zod validation
 * - Model-aware rendering (XML for Claude, JSON for GPT, MD for Gemini)
 * - Script and resource discovery
 * - Skill-specific MCP configuration
 * - Session tracking for active skills
 */

// Types
export type {
  SkillLabel,
  RenderFormat,
  DiscoveryPath,
  SkillFrontmatter,
  SkillScript,
  SkillResource,
  Skill,
  SkillSummary,
  FileDiscoveryResult,
  SkillInjectionOptions,
  SkillInjectionResult,
  SkillStoreState,
} from './types.js'

export { SkillFrontmatterSchema } from './types.js'

// Loader
export {
  DEFAULT_DISCOVERY_PATHS,
  parseFrontmatter,
  discoverSkills,
  loadSkill,
  resolveSkill,
  getSkillSummaries,
  readSkillResource,
  listSkillFiles,
} from './loader.js'

// Injection
export {
  getFormatForModel,
  renderSkill,
  renderSkillsList,
  activateSkillInSession,
  deactivateSkillInSession,
  getActiveSkills,
  isSkillActive,
  clearSessionSkills,
  clearAllSessionSkills,
  injectSkill,
} from './injection.js'
