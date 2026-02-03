/**
 * Codebase Module
 * Understanding and mapping codebases for intelligent code modifications
 *
 * Provides:
 * - File indexing with language detection
 * - Symbol extraction (functions, classes, types)
 * - Dependency graph analysis
 * - Repo map generation with PageRank ranking
 * - Smart file selection for tasks
 */

// Dependency Graph
export {
  buildDependencyGraph,
  findCircularDependencies,
  findLeaves,
  findRoots,
  getDependencyDepth,
  getEdges,
  getGraphStats,
  getTransitiveDependencies,
  getTransitiveDependents,
} from './graph.js'
// Import Parsing
export { parseExports, parseImports, resolveImportPath } from './imports.js'
// File Indexer
export { createIndexer, FileIndexer, getLanguageStats, quickScan } from './indexer.js'
// Ranking (PageRank)
export {
  calculatePageRank,
  calculateRelevanceScore,
  extractKeywords,
  sortByRank,
  sortByScore,
} from './ranking.js'
// Repo Map Generation
export {
  createContextSummary,
  generateRepoMap,
  selectRelevantFiles,
  updateRepoMap,
} from './repomap.js'
// Symbol Extraction
export { extractSymbols } from './symbols.js'
// Types
export * from './types.js'
