/**
 * Thinking Block Component (Legacy)
 *
 * Re-exports ThinkingRow for backward compatibility.
 * All thinking display now uses the unified ThinkingRow component.
 */

import type { Component } from 'solid-js'
import { ThinkingRow } from './message-rows/ThinkingRow'

interface ThinkingBlockProps {
  thinking: string
  isStreaming: boolean
}

export const ThinkingBlock: Component<ThinkingBlockProps> = (props) => {
  return <ThinkingRow thinking={props.thinking} isStreaming={props.isStreaming} />
}
