/**
 * Basic file indexer — indexes files by language and size.
 *
 * No symbol extraction yet (needs tree-sitter). Just file discovery
 * with language detection from file extensions.
 */

import type { IFileSystem } from '@ava/core-v2/platform'
import type { FileIndex, RepoMap } from './types.js'

const LANGUAGE_MAP: Record<string, string> = {
  '.ts': 'typescript',
  '.tsx': 'typescript',
  '.js': 'javascript',
  '.jsx': 'javascript',
  '.py': 'python',
  '.rs': 'rust',
  '.go': 'go',
  '.java': 'java',
  '.rb': 'ruby',
  '.php': 'php',
  '.c': 'c',
  '.cpp': 'cpp',
  '.h': 'c',
  '.hpp': 'cpp',
  '.cs': 'csharp',
  '.swift': 'swift',
  '.kt': 'kotlin',
  '.scala': 'scala',
  '.vue': 'vue',
  '.svelte': 'svelte',
  '.html': 'html',
  '.css': 'css',
  '.scss': 'scss',
  '.json': 'json',
  '.yaml': 'yaml',
  '.yml': 'yaml',
  '.toml': 'toml',
  '.md': 'markdown',
  '.sh': 'shell',
  '.bash': 'shell',
  '.zsh': 'shell',
}

export function detectLanguage(filePath: string): string {
  const ext = filePath.slice(filePath.lastIndexOf('.'))
  return LANGUAGE_MAP[ext] ?? 'unknown'
}

/**
 * Index files in a directory using glob.
 * Returns basic file metadata without symbol extraction.
 */
export async function indexFiles(
  cwd: string,
  fs: IFileSystem,
  patterns: string[] = ['**/*.{ts,tsx,js,jsx,py,rs,go,java}']
): Promise<FileIndex[]> {
  const indices: FileIndex[] = []

  for (const pattern of patterns) {
    try {
      const files = await fs.glob(pattern, cwd)
      for (const filePath of files) {
        try {
          const stat = await fs.stat(filePath)
          if (!stat.isFile || stat.size > 1_000_000) continue // Skip large files

          indices.push({
            path: filePath,
            symbols: [], // Symbol extraction requires tree-sitter
            imports: [],
            exports: [],
            language: detectLanguage(filePath),
            size: stat.size,
          })
        } catch {
          // Skip files we can't stat
        }
      }
    } catch {
      // Pattern not supported — skip
    }
  }

  return indices
}

export function createRepoMap(files: FileIndex[]): RepoMap {
  return {
    files,
    totalFiles: files.length,
    totalSymbols: files.reduce((sum, f) => sum + f.symbols.length, 0),
    generatedAt: Date.now(),
  }
}
