/**
 * Tool Namespacing
 *
 * Prevents name collisions between built-in tools, MCP tools, and extension tools.
 * Inspired by Goose's `ext__tool` pattern.
 *
 * Naming convention:
 * - Built-in tools: no prefix (e.g., `read_file`, `bash`)
 * - MCP tools: `mcp__<server>__<tool>` (e.g., `mcp__github__create_issue`)
 * - Extension tools: `ext__<plugin>__<tool>` (e.g., `ext__docker__build`)
 */

// ============================================================================
// Constants
// ============================================================================

/** Prefix for MCP server tools */
export const MCP_TOOL_PREFIX = 'mcp__'

/** Prefix for extension/plugin tools */
export const EXT_TOOL_PREFIX = 'ext__'

/** Separator between namespace components */
export const NAMESPACE_SEPARATOR = '__'

// ============================================================================
// Core Functions
// ============================================================================

/**
 * Create a namespaced tool name
 *
 * @param prefix - Namespace prefix (e.g., 'mcp', 'ext')
 * @param source - Source name (e.g., server name, plugin name)
 * @param name - Tool name
 * @returns Namespaced tool name
 *
 * @example
 * namespaceTool('mcp', 'github', 'create_issue')
 * // → 'mcp__github__create_issue'
 */
export function namespaceTool(prefix: string, source: string, name: string): string {
  return `${prefix}${NAMESPACE_SEPARATOR}${source}${NAMESPACE_SEPARATOR}${name}`
}

/**
 * Strip namespace from a tool name
 *
 * @returns Object with prefix, source, and bare name
 *
 * @example
 * stripNamespace('mcp__github__create_issue')
 * // → { prefix: 'mcp', source: 'github', name: 'create_issue' }
 *
 * stripNamespace('read_file')
 * // → { prefix: null, source: null, name: 'read_file' }
 */
export function stripNamespace(fullName: string): {
  prefix: string | null
  source: string | null
  name: string
} {
  const parts = fullName.split(NAMESPACE_SEPARATOR)

  if (parts.length >= 3) {
    return {
      prefix: parts[0],
      source: parts[1],
      name: parts.slice(2).join(NAMESPACE_SEPARATOR),
    }
  }

  return { prefix: null, source: null, name: fullName }
}

/**
 * Check if a tool name is namespaced
 */
export function isNamespaced(name: string): boolean {
  return name.startsWith(MCP_TOOL_PREFIX) || name.startsWith(EXT_TOOL_PREFIX)
}

/**
 * Get the namespace prefix from a tool name, or null if not namespaced
 */
export function getNamespace(name: string): string | null {
  if (name.startsWith(MCP_TOOL_PREFIX)) return 'mcp'
  if (name.startsWith(EXT_TOOL_PREFIX)) return 'ext'
  return null
}

/**
 * Get the source (server/plugin name) from a namespaced tool, or null
 */
export function getSource(name: string): string | null {
  const { source } = stripNamespace(name)
  return source
}

/**
 * Get the bare tool name (without namespace)
 */
export function getBareName(name: string): string {
  return stripNamespace(name).name
}

// ============================================================================
// MCP Helpers
// ============================================================================

/**
 * Create a namespaced MCP tool name
 */
export function mcpToolName(serverName: string, toolName: string): string {
  return `${MCP_TOOL_PREFIX}${serverName}${NAMESPACE_SEPARATOR}${toolName}`
}

/**
 * Check if a tool is an MCP tool
 */
export function isMcpTool(name: string): boolean {
  return name.startsWith(MCP_TOOL_PREFIX)
}

// ============================================================================
// Extension Helpers
// ============================================================================

/**
 * Create a namespaced extension tool name
 */
export function extToolName(pluginName: string, toolName: string): string {
  return `${EXT_TOOL_PREFIX}${pluginName}${NAMESPACE_SEPARATOR}${toolName}`
}

/**
 * Check if a tool is an extension tool
 */
export function isExtTool(name: string): boolean {
  return name.startsWith(EXT_TOOL_PREFIX)
}

// ============================================================================
// Lookup Helpers
// ============================================================================

/**
 * Find a tool by name in a registry, supporting both namespaced and bare lookups
 *
 * This allows backward compatibility: looking up 'create_issue' will match
 * 'mcp__github__create_issue' if no exact match exists.
 *
 * @param name - Tool name to look up
 * @param registry - Map of tool names to values
 * @returns The matched value and its full name, or null
 */
export function lookupTool<T>(
  name: string,
  registry: Map<string, T>
): { fullName: string; value: T } | null {
  // Try exact match first
  const exact = registry.get(name)
  if (exact !== undefined) {
    return { fullName: name, value: exact }
  }

  // If the name is already namespaced, no fuzzy matching
  if (isNamespaced(name)) {
    return null
  }

  // Try to find a namespaced tool that ends with this name
  for (const [fullName, value] of registry) {
    const { name: bareName } = stripNamespace(fullName)
    if (bareName === name) {
      return { fullName, value }
    }
  }

  return null
}
