const BEGIN_MARKER = '*** Begin Patch'
const END_MARKER = '*** End Patch'

const OPERATION_HEADER = /^\*\*\*\s+(Add|Update|Delete|Move)\s+File:/i

export interface ChunkParseState {
  sawBegin: boolean
  sawEnd: boolean
  currentOperation: string[]
}

export interface ChunkParseResult {
  operations: string[][]
  state: ChunkParseState
}

export function createChunkParseState(): ChunkParseState {
  return { sawBegin: false, sawEnd: false, currentOperation: [] }
}

export function parseStreamingChunk(
  incomingLines: string[],
  previous: ChunkParseState
): ChunkParseResult {
  const next: ChunkParseState = {
    sawBegin: previous.sawBegin,
    sawEnd: previous.sawEnd,
    currentOperation: [...previous.currentOperation],
  }
  const operations: string[][] = []

  for (const rawLine of incomingLines) {
    const line = rawLine.trimEnd()

    if (!next.sawBegin) {
      if (line === BEGIN_MARKER) next.sawBegin = true
      continue
    }

    if (line === END_MARKER) {
      if (next.currentOperation.length > 0) {
        operations.push(next.currentOperation)
        next.currentOperation = []
      }
      next.sawEnd = true
      continue
    }

    if (OPERATION_HEADER.test(line)) {
      if (next.currentOperation.length > 0) {
        operations.push(next.currentOperation)
      }
      next.currentOperation = [line]
      continue
    }

    if (next.currentOperation.length > 0) {
      next.currentOperation.push(rawLine)
    }
  }

  return { operations, state: next }
}
