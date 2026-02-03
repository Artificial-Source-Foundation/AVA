/**
 * File Ranking
 * PageRank-based file importance ranking
 *
 * Based on Aider's approach to ranking files by importance
 * in the dependency graph.
 */

import type { DependencyNode } from './types.js'

// ============================================================================
// PageRank Algorithm
// ============================================================================

/**
 * Calculate PageRank scores for files in the dependency graph
 *
 * PageRank assigns higher scores to files that are:
 * - Imported by many other files (high in-degree)
 * - Imported by other important files
 *
 * @param graph - Dependency graph
 * @param options - PageRank options
 * @returns Map of file path to rank score
 */
export function calculatePageRank(
  graph: Map<string, DependencyNode>,
  options: PageRankOptions = {}
): Map<string, number> {
  const { damping = 0.85, iterations = 20, tolerance = 1e-6 } = options

  const files = Array.from(graph.keys())
  const n = files.length

  if (n === 0) {
    return new Map()
  }

  // Initialize ranks equally
  const ranks = new Map<string, number>()
  for (const file of files) {
    ranks.set(file, 1 / n)
  }

  // Iterative calculation
  for (let i = 0; i < iterations; i++) {
    const newRanks = new Map<string, number>()
    let maxDiff = 0

    for (const file of files) {
      const node = graph.get(file)!

      // Base rank from damping factor
      let rank = (1 - damping) / n

      // Add contributions from incoming edges (files that import this one)
      for (const importer of node.importedBy) {
        const importerNode = graph.get(importer)
        if (importerNode && importerNode.imports.length > 0) {
          // Divide importer's rank equally among its imports
          rank += (damping * (ranks.get(importer) || 0)) / importerNode.imports.length
        }
      }

      newRanks.set(file, rank)

      // Track convergence
      const diff = Math.abs(rank - (ranks.get(file) || 0))
      maxDiff = Math.max(maxDiff, diff)
    }

    // Update ranks
    for (const [file, rank] of newRanks) {
      ranks.set(file, rank)
    }

    // Check convergence
    if (maxDiff < tolerance) {
      break
    }
  }

  // Update nodes with their ranks
  for (const [file, rank] of ranks) {
    const node = graph.get(file)
    if (node) {
      node.rank = rank
    }
  }

  return ranks
}

/**
 * PageRank options
 */
export interface PageRankOptions {
  /** Damping factor (default: 0.85) */
  damping?: number
  /** Maximum iterations (default: 20) */
  iterations?: number
  /** Convergence tolerance (default: 1e-6) */
  tolerance?: number
}

// ============================================================================
// File Scoring
// ============================================================================

/**
 * Calculate a composite relevance score for a file
 *
 * Combines:
 * - PageRank (structural importance)
 * - Keyword matches (relevance to task)
 * - Recency (recently modified files)
 *
 * @param file - File to score
 * @param pageRank - PageRank score from graph
 * @param keywords - Keywords to match against
 * @param options - Scoring options
 * @returns Composite score (0-1)
 */
export function calculateRelevanceScore(
  file: {
    path: string
    relativePath: string
    mtime: number
    symbols: Array<{ name: string }>
  },
  pageRank: number,
  keywords: string[],
  options: ScoringOptions = {}
): { score: number; reasons: string[] } {
  const {
    pageRankWeight = 0.3,
    keywordWeight = 0.5,
    recencyWeight = 0.2,
    maxAge = 7 * 24 * 60 * 60 * 1000, // 7 days
  } = options

  const reasons: string[] = []
  let score = 0

  // PageRank contribution
  const normalizedRank = Math.min(pageRank * 100, 1) // Normalize to 0-1
  score += normalizedRank * pageRankWeight
  if (normalizedRank > 0.5) {
    reasons.push('High structural importance')
  }

  // Keyword contribution
  const keywordScore = calculateKeywordScore(file, keywords)
  score += keywordScore * keywordWeight
  if (keywordScore > 0.5) {
    reasons.push('Strong keyword match')
  } else if (keywordScore > 0) {
    reasons.push('Partial keyword match')
  }

  // Recency contribution
  const now = Date.now()
  const age = now - file.mtime
  const recencyScore = Math.max(0, 1 - age / maxAge)
  score += recencyScore * recencyWeight
  if (recencyScore > 0.8) {
    reasons.push('Recently modified')
  }

  return { score: Math.min(score, 1), reasons }
}

/**
 * Calculate keyword match score
 */
function calculateKeywordScore(
  file: {
    path: string
    relativePath: string
    symbols: Array<{ name: string }>
  },
  keywords: string[]
): number {
  if (keywords.length === 0) return 0

  const pathLower = file.relativePath.toLowerCase()
  const symbolNames = file.symbols.map((s) => s.name.toLowerCase())

  let matches = 0

  for (const keyword of keywords) {
    const keywordLower = keyword.toLowerCase()

    // Check path match
    if (pathLower.includes(keywordLower)) {
      matches += 1
    }

    // Check symbol match
    for (const symbolName of symbolNames) {
      if (symbolName.includes(keywordLower)) {
        matches += 0.5
        break // Only count once per keyword
      }
    }
  }

  return Math.min(matches / keywords.length, 1)
}

/**
 * Scoring options
 */
export interface ScoringOptions {
  /** Weight for PageRank score (default: 0.3) */
  pageRankWeight?: number
  /** Weight for keyword matches (default: 0.5) */
  keywordWeight?: number
  /** Weight for recency (default: 0.2) */
  recencyWeight?: number
  /** Max age in ms for recency scoring (default: 7 days) */
  maxAge?: number
}

// ============================================================================
// Keyword Extraction
// ============================================================================

/**
 * Extract keywords from a task description
 *
 * @param task - Task description
 * @returns Array of keywords
 */
export function extractKeywords(task: string): string[] {
  // Common stop words to ignore
  const stopWords = new Set([
    'a',
    'an',
    'the',
    'and',
    'or',
    'but',
    'in',
    'on',
    'at',
    'to',
    'for',
    'of',
    'with',
    'by',
    'from',
    'as',
    'is',
    'was',
    'are',
    'were',
    'been',
    'be',
    'have',
    'has',
    'had',
    'do',
    'does',
    'did',
    'will',
    'would',
    'could',
    'should',
    'may',
    'might',
    'must',
    'shall',
    'can',
    'need',
    'want',
    'this',
    'that',
    'these',
    'those',
    'it',
    'its',
    'i',
    'me',
    'my',
    'we',
    'us',
    'our',
    'you',
    'your',
    'they',
    'them',
    'their',
    'what',
    'which',
    'who',
    'whom',
    'when',
    'where',
    'why',
    'how',
    'all',
    'each',
    'every',
    'both',
    'few',
    'more',
    'most',
    'other',
    'some',
    'such',
    'no',
    'not',
    'only',
    'same',
    'so',
    'than',
    'too',
    'very',
    'just',
    'also',
  ])

  // Technical terms to preserve (even if short)
  const techTerms = new Set([
    'api',
    'ui',
    'db',
    'id',
    'io',
    'fs',
    'os',
    'cli',
    'jwt',
    'css',
    'html',
    'sql',
    'url',
    'uri',
  ])

  // Extract words
  const words = task
    .toLowerCase()
    .replace(/[^\w\s-]/g, ' ')
    .split(/\s+/)
    .filter((word) => {
      // Keep technical terms
      if (techTerms.has(word)) return true
      // Skip stop words
      if (stopWords.has(word)) return false
      // Skip very short words
      if (word.length < 3) return false
      // Skip pure numbers
      if (/^\d+$/.test(word)) return false
      return true
    })

  // Also extract camelCase/PascalCase terms
  const camelCaseMatches = task.match(/[a-z][A-Z][a-zA-Z]+/g) || []
  const pascalCaseMatches = task.match(/[A-Z][a-z]+[A-Z][a-zA-Z]+/g) || []

  const additionalTerms = [...camelCaseMatches, ...pascalCaseMatches].map((t) => t.toLowerCase())

  // Combine and deduplicate
  const keywords = [...new Set([...words, ...additionalTerms])]

  return keywords
}

// ============================================================================
// Sorting Utilities
// ============================================================================

/**
 * Sort files by rank (highest first)
 */
export function sortByRank(
  files: Array<{ path: string }>,
  ranks: Map<string, number>
): Array<{ path: string }> {
  return [...files].sort((a, b) => {
    const rankA = ranks.get(a.path) || 0
    const rankB = ranks.get(b.path) || 0
    return rankB - rankA
  })
}

/**
 * Sort files by composite score
 */
export function sortByScore(
  files: Array<{ path: string; score: number }>
): Array<{ path: string; score: number }> {
  return [...files].sort((a, b) => b.score - a.score)
}
