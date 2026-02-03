/**
 * Repo Map Generation
 * Create compact codebase summaries for LLM context
 *
 * Based on Aider's repo-map approach for efficient codebase representation.
 */

import { calculatePageRank, calculateRelevanceScore, extractKeywords } from './ranking.js'
import type {
  DependencyNode,
  FileIndex,
  FileSelectionOptions,
  RepoMap,
  RepoMapOptions,
  ScoredFile,
} from './types.js'
import { DEFAULT_REPO_MAP_OPTIONS } from './types.js'

// ============================================================================
// Repo Map Generation
// ============================================================================

/**
 * Generate a repo map from indexed files
 *
 * @param files - Indexed files
 * @param graph - Dependency graph
 * @param rootPath - Project root path
 * @param options - Generation options
 * @returns Complete repo map
 */
export function generateRepoMap(
  files: FileIndex[],
  graph: Map<string, DependencyNode>,
  rootPath: string,
  options: Partial<RepoMapOptions> = {}
): RepoMap {
  const opts: RepoMapOptions = {
    ...DEFAULT_REPO_MAP_OPTIONS,
    ...options,
  }

  // Calculate PageRank for all files
  calculatePageRank(graph)

  // Sort files by rank
  const rankedFiles = [...files].sort((a, b) => {
    const rankA = graph.get(a.relativePath)?.rank || 0
    const rankB = graph.get(b.relativePath)?.rank || 0
    return rankB - rankA
  })

  // Generate summary within token budget
  const summary = generateSummary(rankedFiles, graph, opts)

  // Calculate total tokens
  const totalTokens = files.reduce((sum, f) => sum + f.tokens, 0)

  return {
    files,
    graph,
    summary,
    totalTokens,
    generatedAt: Date.now(),
    rootPath,
  }
}

/**
 * Generate a text summary of the repository
 */
function generateSummary(
  files: FileIndex[],
  graph: Map<string, DependencyNode>,
  options: RepoMapOptions
): string {
  const lines: string[] = []
  let currentTokens = 0
  const maxTokens = options.maxTokens

  lines.push('# Repository Structure')
  lines.push('')
  currentTokens += 10 // Approximate header tokens

  // Group files by directory
  const byDir = groupByDirectory(files)

  for (const [dir, dirFiles] of Object.entries(byDir)) {
    // Check token budget
    const dirHeader = `## ${dir}/`
    const dirTokens = estimateTokens(dirHeader)

    if (currentTokens + dirTokens > maxTokens) break

    lines.push(dirHeader)
    currentTokens += dirTokens

    // Sort files in directory by rank
    const sortedFiles = dirFiles.sort((a, b) => {
      const rankA = graph.get(a.relativePath)?.rank || 0
      const rankB = graph.get(b.relativePath)?.rank || 0
      return rankB - rankA
    })

    for (const file of sortedFiles) {
      const fileEntry = formatFileEntry(file, graph, options)
      const entryTokens = estimateTokens(fileEntry)

      if (currentTokens + entryTokens > maxTokens) break

      lines.push(fileEntry)
      currentTokens += entryTokens
    }

    lines.push('')
  }

  return lines.join('\n')
}

/**
 * Format a single file entry for the summary
 */
function formatFileEntry(
  file: FileIndex,
  graph: Map<string, DependencyNode>,
  options: RepoMapOptions
): string {
  const lines: string[] = []
  const fileName = file.relativePath.split('/').pop() || file.relativePath

  // File header with rank indicator
  const node = graph.get(file.relativePath)
  const rank = node?.rank || 0
  const rankIndicator = rank > 0.01 ? ' *' : '' // Mark important files
  lines.push(`### ${fileName}${rankIndicator}`)

  // Include symbols if requested
  if (options.includeSymbols && file.symbols.length > 0) {
    const exports = file.symbols.filter((s) => s.exported)
    if (exports.length > 0) {
      lines.push('Exports:')
      for (const symbol of exports.slice(0, 10)) {
        // Limit to 10 symbols
        lines.push(`- ${symbol.type} ${symbol.name}`)
      }
      if (exports.length > 10) {
        lines.push(`- ... (${exports.length - 10} more)`)
      }
    }
  }

  // Include dependencies if requested
  if (options.includeDependencies && node) {
    if (node.imports.length > 0) {
      const localImports = node.imports.slice(0, 5)
      lines.push(`Imports: ${localImports.map((i) => i.split('/').pop()).join(', ')}`)
      if (node.imports.length > 5) {
        lines.push(`  ... (${node.imports.length - 5} more)`)
      }
    }
  }

  return lines.join('\n')
}

/**
 * Group files by their parent directory
 */
function groupByDirectory(files: FileIndex[]): Record<string, FileIndex[]> {
  const groups: Record<string, FileIndex[]> = {}

  for (const file of files) {
    const parts = file.relativePath.split('/')
    const dir = parts.length > 1 ? parts.slice(0, -1).join('/') : '.'

    if (!groups[dir]) {
      groups[dir] = []
    }
    groups[dir].push(file)
  }

  return groups
}

/**
 * Estimate token count for text (rough: chars / 4)
 */
function estimateTokens(text: string): number {
  return Math.ceil(text.length / 4)
}

// ============================================================================
// Smart File Selection
// ============================================================================

/**
 * Select relevant files for a task
 *
 * Uses PageRank + keyword matching to find the most relevant files
 * within a token budget.
 *
 * @param task - Task description
 * @param repoMap - Repository map
 * @param options - Selection options
 * @returns Array of selected files with scores
 */
export function selectRelevantFiles(
  task: string,
  repoMap: RepoMap,
  options: Partial<FileSelectionOptions> = {}
): ScoredFile[] {
  const { maxTokens = 8000, minRelevance = 0.1, priorityPatterns = [] } = options

  // Extract keywords from task
  const keywords = extractKeywords(task)

  // Score all files
  const scoredFiles: ScoredFile[] = []

  for (const file of repoMap.files) {
    const pageRank = repoMap.graph.get(file.relativePath)?.rank || 0
    const { score, reasons } = calculateRelevanceScore(file, pageRank, keywords)

    // Apply priority pattern boost
    let finalScore = score
    for (const pattern of priorityPatterns) {
      if (file.relativePath.includes(pattern)) {
        finalScore = Math.min(finalScore + 0.2, 1)
        reasons.push(`Matches priority pattern: ${pattern}`)
      }
    }

    if (finalScore >= minRelevance) {
      scoredFiles.push({
        file,
        score: finalScore,
        reason: reasons.join('; ') || 'Baseline relevance',
      })
    }
  }

  // Sort by score
  scoredFiles.sort((a, b) => b.score - a.score)

  // Select files within token budget
  const selected: ScoredFile[] = []
  let totalTokens = 0

  for (const scored of scoredFiles) {
    if (totalTokens + scored.file.tokens > maxTokens) {
      continue // Skip files that don't fit
    }

    selected.push(scored)
    totalTokens += scored.file.tokens
  }

  return selected
}

/**
 * Get file content summary for context
 *
 * Creates a compact representation of selected files for LLM context.
 */
export function createContextSummary(files: ScoredFile[]): string {
  const lines: string[] = []

  lines.push('# Relevant Files')
  lines.push('')

  for (const { file, score, reason } of files) {
    lines.push(`## ${file.relativePath}`)
    lines.push(`Relevance: ${(score * 100).toFixed(0)}% - ${reason}`)

    // List exports
    const exports = file.symbols.filter((s) => s.exported)
    if (exports.length > 0) {
      lines.push('Exports:')
      for (const symbol of exports.slice(0, 15)) {
        const sig = symbol.signature ? `: ${symbol.signature.slice(0, 50)}` : ''
        lines.push(`- ${symbol.type} ${symbol.name}${sig}`)
      }
    }

    // List imports
    if (file.imports.length > 0) {
      const localImports = file.imports.filter(
        (i) => i.source.startsWith('.') || i.source.startsWith('@/')
      )
      if (localImports.length > 0) {
        lines.push(
          `Imports from: ${localImports
            .map((i) => i.source)
            .slice(0, 5)
            .join(', ')}`
        )
      }
    }

    lines.push('')
  }

  return lines.join('\n')
}

// ============================================================================
// Incremental Updates
// ============================================================================

/**
 * Update repo map with changed files
 *
 * @param repoMap - Existing repo map
 * @param changedFiles - Files that have changed
 * @param removedFiles - Files that have been removed
 * @returns Updated repo map
 */
export function updateRepoMap(
  repoMap: RepoMap,
  changedFiles: FileIndex[],
  removedFiles: string[]
): RepoMap {
  // Create a new files array
  const filesMap = new Map(repoMap.files.map((f) => [f.relativePath, f]))

  // Remove deleted files
  for (const removed of removedFiles) {
    filesMap.delete(removed)
    repoMap.graph.delete(removed)
  }

  // Update changed files
  for (const changed of changedFiles) {
    filesMap.set(changed.relativePath, changed)
  }

  const newFiles = Array.from(filesMap.values())

  // Note: Graph would need to be rebuilt for changed imports
  // For a full incremental update, we'd need to track which edges changed

  return {
    ...repoMap,
    files: newFiles,
    totalTokens: newFiles.reduce((sum, f) => sum + f.tokens, 0),
    generatedAt: Date.now(),
  }
}
