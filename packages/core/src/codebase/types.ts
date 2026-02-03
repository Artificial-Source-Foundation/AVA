/**
 * Codebase Types
 * Core types for codebase understanding and repo mapping
 *
 * Provides types for:
 * - File indexing
 * - Symbol extraction
 * - Dependency graphs
 * - Repo map generation
 */

// ============================================================================
// Language Detection
// ============================================================================

/**
 * Supported programming languages
 */
export type Language =
  | 'typescript'
  | 'javascript'
  | 'python'
  | 'rust'
  | 'go'
  | 'java'
  | 'c'
  | 'cpp'
  | 'csharp'
  | 'ruby'
  | 'php'
  | 'swift'
  | 'kotlin'
  | 'scala'
  | 'markdown'
  | 'json'
  | 'yaml'
  | 'html'
  | 'css'
  | 'unknown'

/**
 * File extension to language mapping
 */
export const EXTENSION_TO_LANGUAGE: Record<string, Language> = {
  '.ts': 'typescript',
  '.tsx': 'typescript',
  '.mts': 'typescript',
  '.cts': 'typescript',
  '.js': 'javascript',
  '.jsx': 'javascript',
  '.mjs': 'javascript',
  '.cjs': 'javascript',
  '.py': 'python',
  '.rs': 'rust',
  '.go': 'go',
  '.java': 'java',
  '.c': 'c',
  '.h': 'c',
  '.cpp': 'cpp',
  '.hpp': 'cpp',
  '.cc': 'cpp',
  '.cxx': 'cpp',
  '.cs': 'csharp',
  '.rb': 'ruby',
  '.php': 'php',
  '.swift': 'swift',
  '.kt': 'kotlin',
  '.kts': 'kotlin',
  '.scala': 'scala',
  '.md': 'markdown',
  '.json': 'json',
  '.yaml': 'yaml',
  '.yml': 'yaml',
  '.html': 'html',
  '.htm': 'html',
  '.css': 'css',
  '.scss': 'css',
  '.sass': 'css',
  '.less': 'css',
}

// ============================================================================
// File Entry
// ============================================================================

/**
 * Basic file entry with metadata
 */
export interface FileEntry {
  /** Absolute file path */
  path: string
  /** Path relative to project root */
  relativePath: string
  /** File size in bytes */
  size: number
  /** Last modification time (ms since epoch) */
  mtime: number
  /** Detected programming language */
  language: Language
  /** Estimated token count (chars / 4) */
  tokens: number
}

// ============================================================================
// Symbols
// ============================================================================

/**
 * Type of symbol
 */
export type SymbolType =
  | 'function'
  | 'class'
  | 'interface'
  | 'type'
  | 'variable'
  | 'constant'
  | 'enum'
  | 'namespace'
  | 'method'
  | 'property'

/**
 * A symbol (function, class, type, etc.) extracted from source code
 */
export interface CodeSymbol {
  /** Symbol name */
  name: string
  /** Symbol type */
  type: SymbolType
  /** Line number (1-based) */
  line: number
  /** End line number (1-based) */
  endLine: number
  /** Whether this symbol is exported */
  exported: boolean
  /** Function signature or type definition (if applicable) */
  signature?: string
  /** Parent symbol name (for methods/properties) */
  parent?: string
  /** JSDoc/docstring content */
  documentation?: string
}

// ============================================================================
// Imports/Exports
// ============================================================================

/**
 * Import information
 */
export interface ImportInfo {
  /** Import source path (e.g., './utils', 'lodash') */
  source: string
  /** Named imports (e.g., ['foo', 'bar']) */
  specifiers: string[]
  /** Default import name (if any) */
  defaultImport?: string
  /** Whether it's a type-only import */
  isType: boolean
  /** Whether it's a namespace import (import * as X) */
  isNamespace: boolean
  /** Line number */
  line: number
}

/**
 * Export information
 */
export interface ExportInfo {
  /** Exported name */
  name: string
  /** Whether it's the default export */
  isDefault: boolean
  /** Whether it's a type export */
  isType: boolean
  /** Whether it's a re-export from another module */
  reExportFrom?: string
  /** Line number */
  line: number
}

// ============================================================================
// File Index
// ============================================================================

/**
 * Complete file index with symbols and dependencies
 */
export interface FileIndex extends FileEntry {
  /** Symbols defined in this file */
  symbols: CodeSymbol[]
  /** Imports from other files */
  imports: ImportInfo[]
  /** Exports from this file */
  exports: ExportInfo[]
  /** Hash of file content (for incremental updates) */
  contentHash?: string
}

// ============================================================================
// Dependency Graph
// ============================================================================

/**
 * Node in the dependency graph
 */
export interface DependencyNode {
  /** File path */
  file: string
  /** Files this file imports from */
  imports: string[]
  /** Files that import from this file */
  importedBy: string[]
  /** PageRank score (higher = more important) */
  rank: number
}

/**
 * Edge in the dependency graph
 */
export interface DependencyEdge {
  /** Source file (importer) */
  from: string
  /** Target file (imported) */
  to: string
  /** Import type (regular, type-only, namespace) */
  importType: 'regular' | 'type' | 'namespace'
}

// ============================================================================
// Repo Map
// ============================================================================

/**
 * Complete repository map
 */
export interface RepoMap {
  /** All indexed files */
  files: FileIndex[]
  /** Dependency graph */
  graph: Map<string, DependencyNode>
  /** Text summary of the repository */
  summary: string
  /** Total estimated tokens */
  totalTokens: number
  /** Index generation timestamp */
  generatedAt: number
  /** Project root path */
  rootPath: string
}

/**
 * Options for repo map generation
 */
export interface RepoMapOptions {
  /** Maximum tokens for the summary */
  maxTokens: number
  /** Include file content in summary */
  includeContent?: boolean
  /** Include symbols in summary */
  includeSymbols?: boolean
  /** Include import/export relationships */
  includeDependencies?: boolean
  /** File patterns to include */
  include?: string[]
  /** File patterns to exclude */
  exclude?: string[]
}

/**
 * Default repo map options
 */
export const DEFAULT_REPO_MAP_OPTIONS: RepoMapOptions = {
  maxTokens: 8000,
  includeContent: false,
  includeSymbols: true,
  includeDependencies: true,
  exclude: [
    '**/node_modules/**',
    '**/dist/**',
    '**/build/**',
    '**/.git/**',
    '**/coverage/**',
    '**/*.min.js',
    '**/*.map',
  ],
}

// ============================================================================
// File Selection
// ============================================================================

/**
 * Options for selecting relevant files
 */
export interface FileSelectionOptions {
  /** Task description to match against */
  task: string
  /** Maximum tokens to include */
  maxTokens: number
  /** Minimum relevance score (0-1) */
  minRelevance?: number
  /** Boost for files matching these patterns */
  priorityPatterns?: string[]
}

/**
 * File with relevance score
 */
export interface ScoredFile {
  /** File index */
  file: FileIndex
  /** Relevance score (0-1) */
  score: number
  /** Why this file was selected */
  reason: string
}

// ============================================================================
// Indexer Configuration
// ============================================================================

/**
 * Configuration for the file indexer
 */
export interface IndexerConfig {
  /** Project root directory */
  rootPath: string
  /** File patterns to include */
  include?: string[]
  /** File patterns to exclude */
  exclude?: string[]
  /** Whether to extract symbols */
  extractSymbols?: boolean
  /** Whether to parse imports/exports */
  parseImports?: boolean
  /** Maximum file size to index (bytes) */
  maxFileSize?: number
  /** Incremental update (skip unchanged files) */
  incremental?: boolean
}

/**
 * Default indexer configuration
 */
export const DEFAULT_INDEXER_CONFIG: Partial<IndexerConfig> = {
  extractSymbols: true,
  parseImports: true,
  maxFileSize: 1024 * 1024, // 1MB
  incremental: true,
  exclude: [
    '**/node_modules/**',
    '**/dist/**',
    '**/build/**',
    '**/.git/**',
    '**/coverage/**',
    '**/*.min.js',
    '**/*.map',
    '**/package-lock.json',
    '**/yarn.lock',
    '**/pnpm-lock.yaml',
  ],
}
