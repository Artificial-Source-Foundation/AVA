import type { ChatMessage } from '@ava/core-v2/llm'

import { createCacheControlProcessor } from './cache-control.js'
import { createLastNObservationsProcessor } from './last-n-observations.js'
import { createTagToolCallsProcessor } from './tag-tool-calls.js'
import type { HistoryProcessor } from './types.js'

export interface ProcessorFactoryOptions {
  provider: string
}

export function createHistoryProcessorByName(
  name: string,
  options: ProcessorFactoryOptions
): HistoryProcessor | null {
  if (name === 'last-n-observations') return createLastNObservationsProcessor()
  if (name === 'cache-control') return createCacheControlProcessor(options.provider)
  if (name === 'tag-tool-calls') return createTagToolCallsProcessor()
  return null
}

export function runHistoryProcessors(
  messages: ChatMessage[],
  processors: readonly HistoryProcessor[]
): ChatMessage[] {
  let current = messages
  for (const processor of processors) {
    current = processor(current)
  }
  return current
}

export type { HistoryProcessor }
