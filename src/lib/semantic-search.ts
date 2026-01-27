/**
 * Delta9 Semantic Search
 *
 * Weighted search for memory blocks with:
 * - Label/tag matching
 * - Content similarity
 * - Keyword boosting
 *
 * Pattern from: opencode-mem weighted search
 *
 * This provides better memory retrieval than simple substring matching
 * by considering multiple relevance signals.
 */

import { getNamedLogger } from './logger.js'

const log = getNamedLogger('semantic-search')

// =============================================================================
// Types
// =============================================================================

/**
 * Search configuration
 */
export interface SemanticSearchConfig {
  /** Weight for label/tag matching (0-1) */
  labelWeight: number
  /** Weight for content similarity (0-1) */
  contentWeight: number
  /** Boost multiplier for exact keyword match */
  keywordBoost: number
  /** Minimum score threshold (0-1) */
  minScore: number
  /** Maximum results to return */
  maxResults: number
  /** Enable fuzzy matching */
  fuzzyMatch: boolean
}

/**
 * Searchable item interface
 */
export interface SearchableItem {
  /** Unique identifier */
  id: string
  /** Label/title (for tag matching) */
  label: string
  /** Description (for tag matching) */
  description?: string
  /** Main content (for content matching) */
  content: string
  /** Optional tags for explicit matching */
  tags?: string[]
  /** Original item reference */
  original?: unknown
}

/**
 * Search result with score
 */
export interface SearchResult<T = unknown> {
  /** The matched item */
  item: SearchableItem
  /** Relevance score (0-1) */
  score: number
  /** Score breakdown */
  breakdown: {
    labelScore: number
    contentScore: number
    keywordBonus: number
    totalRaw: number
  }
  /** Matched terms */
  matchedTerms: string[]
  /** Original item if provided */
  original?: T
}

// =============================================================================
// Default Config
// =============================================================================

export const DEFAULT_SEARCH_CONFIG: SemanticSearchConfig = {
  labelWeight: 0.4,      // 40% weight on label/description
  contentWeight: 0.6,    // 60% weight on content
  keywordBoost: 1.5,     // 1.5x for exact keyword match
  minScore: 0.1,         // Minimum 10% relevance
  maxResults: 10,        // Top 10 results
  fuzzyMatch: true,      // Enable fuzzy matching
}

// =============================================================================
// Core Search Functions
// =============================================================================

/**
 * Perform semantic search on items
 */
export function semanticSearch<T = unknown>(
  items: SearchableItem[],
  query: string,
  config: Partial<SemanticSearchConfig> = {}
): SearchResult<T>[] {
  const cfg: SemanticSearchConfig = { ...DEFAULT_SEARCH_CONFIG, ...config }

  // Normalize query
  const normalizedQuery = normalizeText(query)
  const queryTerms = tokenize(normalizedQuery)

  if (queryTerms.length === 0) {
    return []
  }

  // Score each item
  const results: SearchResult<T>[] = items
    .map((item) => scoreItem<T>(item, normalizedQuery, queryTerms, cfg))
    .filter((result) => result.score >= cfg.minScore)
    .sort((a, b) => b.score - a.score)
    .slice(0, cfg.maxResults)

  log.debug(`Searched ${items.length} items, found ${results.length} matches for "${query}"`)

  return results
}

/**
 * Score a single item against query
 */
function scoreItem<T>(
  item: SearchableItem,
  normalizedQuery: string,
  queryTerms: string[],
  config: SemanticSearchConfig
): SearchResult<T> {
  // Calculate label score (label + description + tags)
  const labelText = normalizeText(
    [item.label, item.description, ...(item.tags || [])].filter(Boolean).join(' ')
  )
  const labelScore = calculateTextSimilarity(labelText, normalizedQuery, queryTerms, config.fuzzyMatch)

  // Calculate content score
  const contentText = normalizeText(item.content)
  const contentScore = calculateTextSimilarity(contentText, normalizedQuery, queryTerms, config.fuzzyMatch)

  // Check for exact keyword match (boost)
  const hasExactKeyword = hasExactMatch(item.content, queryTerms)
  const keywordBonus = hasExactKeyword ? config.keywordBoost : 1

  // Calculate weighted total
  const totalRaw = (labelScore * config.labelWeight) + (contentScore * config.contentWeight)
  const finalScore = Math.min(1, totalRaw * keywordBonus)

  // Find matched terms
  const matchedTerms = findMatchedTerms(
    [labelText, contentText].join(' '),
    queryTerms,
    config.fuzzyMatch
  )

  return {
    item,
    score: finalScore,
    breakdown: {
      labelScore,
      contentScore,
      keywordBonus,
      totalRaw,
    },
    matchedTerms,
    original: item.original as T | undefined,
  }
}

// =============================================================================
// Text Processing
// =============================================================================

/**
 * Normalize text for comparison
 */
function normalizeText(text: string): string {
  return text
    .toLowerCase()
    .replace(/[^\w\s]/g, ' ')  // Remove punctuation
    .replace(/\s+/g, ' ')       // Collapse whitespace
    .trim()
}

/**
 * Tokenize text into terms
 */
function tokenize(text: string): string[] {
  // Split on whitespace and filter short terms
  return text
    .split(/\s+/)
    .filter((term) => term.length >= 2)
    // Remove common stop words
    .filter((term) => !STOP_WORDS.has(term))
}

/**
 * Common stop words to ignore
 */
const STOP_WORDS = new Set([
  'the', 'a', 'an', 'and', 'or', 'but', 'in', 'on', 'at', 'to', 'for',
  'of', 'with', 'by', 'from', 'is', 'are', 'was', 'were', 'be', 'been',
  'being', 'have', 'has', 'had', 'do', 'does', 'did', 'will', 'would',
  'could', 'should', 'may', 'might', 'must', 'can', 'this', 'that',
  'these', 'those', 'it', 'its', 'as', 'if', 'when', 'than', 'so',
])

// =============================================================================
// Similarity Calculation
// =============================================================================

/**
 * Calculate text similarity using term frequency
 */
function calculateTextSimilarity(
  text: string,
  query: string,
  queryTerms: string[],
  fuzzy: boolean
): number {
  if (!text || !query) return 0

  const textTerms = tokenize(text)
  if (textTerms.length === 0) return 0

  // Count matched terms
  let matchCount = 0

  for (const queryTerm of queryTerms) {
    if (fuzzy) {
      // Fuzzy match: check if any text term contains query term or vice versa
      const hasMatch = textTerms.some((textTerm) =>
        textTerm.includes(queryTerm) || queryTerm.includes(textTerm)
      )
      if (hasMatch) matchCount++
    } else {
      // Exact match
      if (textTerms.includes(queryTerm)) matchCount++
    }
  }

  // Calculate score as proportion of matched query terms
  const termScore = matchCount / queryTerms.length

  // Bonus for exact phrase match
  const phraseBonus = text.includes(query) ? 0.2 : 0

  return Math.min(1, termScore + phraseBonus)
}

/**
 * Check if text contains exact keyword match (case-insensitive)
 */
function hasExactMatch(text: string, queryTerms: string[]): boolean {
  const normalizedText = normalizeText(text)
  return queryTerms.some((term) => {
    // Check for word boundary match
    const wordBoundaryRegex = new RegExp(`\\b${escapeRegex(term)}\\b`, 'i')
    return wordBoundaryRegex.test(normalizedText)
  })
}

/**
 * Find which query terms matched
 */
function findMatchedTerms(text: string, queryTerms: string[], fuzzy: boolean): string[] {
  const textTerms = tokenize(text)
  const matched: string[] = []

  for (const queryTerm of queryTerms) {
    if (fuzzy) {
      const hasMatch = textTerms.some((textTerm) =>
        textTerm.includes(queryTerm) || queryTerm.includes(textTerm)
      )
      if (hasMatch) matched.push(queryTerm)
    } else {
      if (textTerms.includes(queryTerm)) matched.push(queryTerm)
    }
  }

  return matched
}

/**
 * Escape special regex characters
 */
function escapeRegex(str: string): string {
  return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
}

// =============================================================================
// Memory-Specific Helpers
// =============================================================================

/**
 * Search memory blocks with semantic search
 *
 * Convenience function for searching MemoryBlock arrays.
 */
export function searchMemoryBlocks<T extends { label: string; description: string; content: string }>(
  blocks: T[],
  query: string,
  config: Partial<SemanticSearchConfig> = {}
): SearchResult<T>[] {
  const searchableItems: SearchableItem[] = blocks.map((block) => ({
    id: block.label,
    label: block.label,
    description: block.description,
    content: block.content,
    tags: extractTags(block.content),
    original: block,
  }))

  return semanticSearch<T>(searchableItems, query, config)
}

/**
 * Extract tags/keywords from content
 *
 * Looks for markdown headers and emphasized text as implicit tags.
 */
function extractTags(content: string): string[] {
  const tags: string[] = []

  // Extract markdown headers (## Header)
  const headerMatches = content.match(/^#{1,3}\s+(.+)$/gm)
  if (headerMatches) {
    for (const match of headerMatches) {
      const header = match.replace(/^#+\s*/, '').trim()
      tags.push(header.toLowerCase())
    }
  }

  // Extract bold text (**bold**)
  const boldMatches = content.match(/\*\*([^*]+)\*\*/g)
  if (boldMatches) {
    for (const match of boldMatches) {
      const bold = match.replace(/\*\*/g, '').trim()
      if (bold.length <= 30) { // Reasonable tag length
        tags.push(bold.toLowerCase())
      }
    }
  }

  return [...new Set(tags)] // Dedupe
}

// =============================================================================
// Ranking & Filtering
// =============================================================================

/**
 * Re-rank results with custom scoring function
 */
export function rerankResults<T>(
  results: SearchResult<T>[],
  customScorer: (result: SearchResult<T>) => number
): SearchResult<T>[] {
  return results
    .map((result) => ({
      ...result,
      score: customScorer(result),
    }))
    .sort((a, b) => b.score - a.score)
}

/**
 * Filter results by minimum score
 */
export function filterByScore<T>(
  results: SearchResult<T>[],
  minScore: number
): SearchResult<T>[] {
  return results.filter((r) => r.score >= minScore)
}

/**
 * Get top N results
 */
export function topResults<T>(
  results: SearchResult<T>[],
  n: number
): SearchResult<T>[] {
  return results.slice(0, n)
}

// =============================================================================
// Query Parsing
// =============================================================================

/**
 * Parse query with optional filters
 *
 * Supports:
 * - "scope:project pattern matching" -> filters + query
 * - "label:failures bug fixes" -> filters + query
 */
export interface ParsedQuery {
  /** Main search query */
  query: string
  /** Scope filter */
  scope?: 'global' | 'project'
  /** Label filter */
  label?: string
  /** Raw original query */
  raw: string
}

export function parseQuery(input: string): ParsedQuery {
  const result: ParsedQuery = {
    query: input,
    raw: input,
  }

  // Extract scope filter
  const scopeMatch = input.match(/\bscope:(global|project)\b/i)
  if (scopeMatch) {
    result.scope = scopeMatch[1].toLowerCase() as 'global' | 'project'
    result.query = result.query.replace(scopeMatch[0], '').trim()
  }

  // Extract label filter
  const labelMatch = input.match(/\blabel:(\S+)/i)
  if (labelMatch) {
    result.label = labelMatch[1].toLowerCase()
    result.query = result.query.replace(labelMatch[0], '').trim()
  }

  return result
}

// =============================================================================
// Exports
// =============================================================================

export {
  normalizeText,
  tokenize,
  calculateTextSimilarity,
  extractTags,
}
