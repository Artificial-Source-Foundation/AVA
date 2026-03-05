/**
 * Codebase intelligence types.
 */

export interface FileSymbol {
  name: string
  kind:
    | 'function'
    | 'class'
    | 'interface'
    | 'type'
    | 'variable'
    | 'enum'
    | 'method'
    | 'import'
    | 'export'
  line: number
  endLine?: number
  filePath: string
}

export interface FileIndex {
  path: string
  symbols: FileSymbol[]
  imports: string[]
  exports: string[]
  language: string
  size: number
}

export interface RepoMap {
  files: FileIndex[]
  totalFiles: number
  totalSymbols: number
  generatedAt: number
}
