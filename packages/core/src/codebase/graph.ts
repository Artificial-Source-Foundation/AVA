/**
 * Dependency Graph
 * Build and analyze dependency relationships between files
 *
 * Creates a directed graph of file dependencies based on imports.
 */

import { resolveImportPath } from './imports.js'
import type { DependencyEdge, DependencyNode, FileIndex } from './types.js'

// ============================================================================
// Dependency Graph Builder
// ============================================================================

/**
 * Build a dependency graph from indexed files
 *
 * @param files - Array of file indexes with parsed imports
 * @param rootPath - Project root path
 * @param pathAliases - TypeScript path aliases (from tsconfig)
 * @returns Map of file path to dependency node
 */
export function buildDependencyGraph(
  files: FileIndex[],
  rootPath: string,
  pathAliases?: Record<string, string[]>
): Map<string, DependencyNode> {
  const graph = new Map<string, DependencyNode>()

  // Initialize nodes for all files
  for (const file of files) {
    graph.set(file.relativePath, {
      file: file.relativePath,
      imports: [],
      importedBy: [],
      rank: 0,
    })
  }

  // Build edges from imports
  for (const file of files) {
    const node = graph.get(file.relativePath)!

    for (const imp of file.imports) {
      // Skip external packages
      if (
        !imp.source.startsWith('.') &&
        !imp.source.startsWith('@/') &&
        !imp.source.startsWith('~/')
      ) {
        continue
      }

      // Resolve import to actual file
      const resolvedPath = resolveImportPath(imp.source, file.path, {
        rootPath,
        paths: pathAliases,
      })

      if (!resolvedPath) continue

      // Convert to relative path
      const relativePath = getRelativePath(resolvedPath, rootPath)

      // Find the target file (try with different extensions)
      const targetPath = findTargetFile(relativePath, graph)
      if (!targetPath) continue

      // Add edge
      if (!node.imports.includes(targetPath)) {
        node.imports.push(targetPath)
      }

      // Add reverse edge
      const targetNode = graph.get(targetPath)
      if (targetNode && !targetNode.importedBy.includes(file.relativePath)) {
        targetNode.importedBy.push(file.relativePath)
      }
    }
  }

  return graph
}

/**
 * Find target file in graph (handling extension variations)
 */
function findTargetFile(relativePath: string, graph: Map<string, DependencyNode>): string | null {
  // Try exact match
  if (graph.has(relativePath)) {
    return relativePath
  }

  // Try without extension
  const withoutExt = relativePath.replace(/\.[^.]+$/, '')
  const extensions = ['.ts', '.tsx', '.js', '.jsx', '.mjs', '.cjs']

  for (const ext of extensions) {
    const withExt = withoutExt + ext
    if (graph.has(withExt)) {
      return withExt
    }
  }

  // Try as index file
  for (const ext of extensions) {
    const indexPath = `${withoutExt}/index${ext}`
    if (graph.has(indexPath)) {
      return indexPath
    }
  }

  return null
}

/**
 * Get relative path from absolute
 */
function getRelativePath(absolutePath: string, rootPath: string): string {
  if (absolutePath.startsWith(rootPath)) {
    const relative = absolutePath.slice(rootPath.length)
    return relative.startsWith('/') ? relative.slice(1) : relative
  }
  return absolutePath
}

// ============================================================================
// Graph Analysis
// ============================================================================

/**
 * Get all edges in the dependency graph
 */
export function getEdges(graph: Map<string, DependencyNode>): DependencyEdge[] {
  const edges: DependencyEdge[] = []

  for (const [file, node] of graph) {
    for (const imp of node.imports) {
      edges.push({
        from: file,
        to: imp,
        importType: 'regular', // Would need to track this during parsing
      })
    }
  }

  return edges
}

/**
 * Find files with no dependents (potential entry points or dead code)
 */
export function findRoots(graph: Map<string, DependencyNode>): string[] {
  return Array.from(graph.entries())
    .filter(([_, node]) => node.importedBy.length === 0)
    .map(([file]) => file)
}

/**
 * Find files with no dependencies (leaf files)
 */
export function findLeaves(graph: Map<string, DependencyNode>): string[] {
  return Array.from(graph.entries())
    .filter(([_, node]) => node.imports.length === 0)
    .map(([file]) => file)
}

/**
 * Find circular dependencies
 */
export function findCircularDependencies(graph: Map<string, DependencyNode>): string[][] {
  const cycles: string[][] = []
  const visited = new Set<string>()
  const stack = new Set<string>()

  function dfs(file: string, path: string[]): void {
    if (stack.has(file)) {
      // Found a cycle
      const cycleStart = path.indexOf(file)
      if (cycleStart !== -1) {
        cycles.push(path.slice(cycleStart))
      }
      return
    }

    if (visited.has(file)) return

    visited.add(file)
    stack.add(file)

    const node = graph.get(file)
    if (node) {
      for (const imp of node.imports) {
        dfs(imp, [...path, file])
      }
    }

    stack.delete(file)
  }

  for (const file of graph.keys()) {
    dfs(file, [])
  }

  return cycles
}

/**
 * Get the dependency depth of a file (longest path from any root)
 */
export function getDependencyDepth(graph: Map<string, DependencyNode>, file: string): number {
  const node = graph.get(file)
  if (!node || node.importedBy.length === 0) {
    return 0
  }

  let maxDepth = 0
  const visited = new Set<string>()

  function dfs(current: string, depth: number): void {
    if (visited.has(current)) return
    visited.add(current)

    maxDepth = Math.max(maxDepth, depth)

    const currentNode = graph.get(current)
    if (currentNode) {
      for (const importer of currentNode.importedBy) {
        dfs(importer, depth + 1)
      }
    }
  }

  dfs(file, 0)
  return maxDepth
}

/**
 * Get transitive dependencies (all files this file depends on, directly or indirectly)
 */
export function getTransitiveDependencies(
  graph: Map<string, DependencyNode>,
  file: string
): Set<string> {
  const deps = new Set<string>()
  const visited = new Set<string>()

  function dfs(current: string): void {
    if (visited.has(current)) return
    visited.add(current)

    const node = graph.get(current)
    if (node) {
      for (const imp of node.imports) {
        deps.add(imp)
        dfs(imp)
      }
    }
  }

  dfs(file)
  return deps
}

/**
 * Get transitive dependents (all files that depend on this file, directly or indirectly)
 */
export function getTransitiveDependents(
  graph: Map<string, DependencyNode>,
  file: string
): Set<string> {
  const dependents = new Set<string>()
  const visited = new Set<string>()

  function dfs(current: string): void {
    if (visited.has(current)) return
    visited.add(current)

    const node = graph.get(current)
    if (node) {
      for (const importer of node.importedBy) {
        dependents.add(importer)
        dfs(importer)
      }
    }
  }

  dfs(file)
  return dependents
}

// ============================================================================
// Graph Statistics
// ============================================================================

/**
 * Get statistics about the dependency graph
 */
export function getGraphStats(graph: Map<string, DependencyNode>): {
  totalFiles: number
  totalEdges: number
  roots: number
  leaves: number
  avgImports: number
  avgDependents: number
  maxImports: number
  maxDependents: number
  circularCount: number
} {
  const files = Array.from(graph.values())
  const edges = getEdges(graph)
  const roots = findRoots(graph)
  const leaves = findLeaves(graph)
  const circles = findCircularDependencies(graph)

  const imports = files.map((f) => f.imports.length)
  const dependents = files.map((f) => f.importedBy.length)

  return {
    totalFiles: graph.size,
    totalEdges: edges.length,
    roots: roots.length,
    leaves: leaves.length,
    avgImports: imports.length > 0 ? imports.reduce((a, b) => a + b, 0) / imports.length : 0,
    avgDependents:
      dependents.length > 0 ? dependents.reduce((a, b) => a + b, 0) / dependents.length : 0,
    maxImports: imports.length > 0 ? Math.max(...imports) : 0,
    maxDependents: dependents.length > 0 ? Math.max(...dependents) : 0,
    circularCount: circles.length,
  }
}
