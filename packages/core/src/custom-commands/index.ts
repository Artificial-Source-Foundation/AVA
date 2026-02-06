/**
 * Custom Commands Module
 * TOML-based user-defined slash commands
 *
 * Discover, parse, and execute custom commands from:
 *   - <project>/.estela/commands/*.toml (project-level, higher priority)
 *   - ~/.estela/commands/*.toml (user-level, lower priority)
 *
 * Template placeholders:
 *   @{path}     - File content injection
 *   !{command}  - Shell command execution
 *   {{args}}    - Argument substitution
 */

// Discovery
export {
  createDiscoveryConfig,
  discoverCommands,
  getProjectCommandsDir,
  getUserCommandsDir,
} from './discovery.js'
// Loader
export {
  CustomCommandLoader,
  createCustomCommandLoader,
} from './loader.js'
// Parser
export { parseCommandToml } from './parser.js'
// Template
export { extractPlaceholders, resolveTemplate, type TemplateResolveOptions } from './template.js'
// Types
export type {
  CommandDiscoveryConfig,
  CommandFileInfo,
  CustomCommandDef,
  Placeholder,
  PlaceholderType,
  ShellExecution,
  TemplateResult,
} from './types.js'
