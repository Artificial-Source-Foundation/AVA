/**
 * Provider Logo Map
 *
 * Maps provider IDs to their brand logo components.
 * Falls back to a generic Bot icon for unknown providers.
 */

import { Bot } from 'lucide-solid'
import type { Component, JSX } from 'solid-js'
import {
  AlibabaCloudLogo,
  AnthropicLogo,
  CopilotLogo,
  GeminiLogo,
  GLMLogo,
  InceptionLogo,
  KimiLogo,
  MiniMaxLogo,
  OllamaLogo,
  OpenAILogo,
  OpenRouterLogo,
  ZAILogo,
} from './provider-logos'

type IconComponent = Component<{ class?: string; style?: JSX.CSSProperties }>

export const PROVIDER_LOGOS: Record<string, IconComponent> = {
  anthropic: AnthropicLogo,
  openai: OpenAILogo,
  chatgpt: OpenAILogo,
  gemini: GeminiLogo,
  google: GeminiLogo,
  copilot: CopilotLogo,
  openrouter: OpenRouterLogo,
  inception: InceptionLogo,
  alibaba: AlibabaCloudLogo,
  'alibaba-cn': AlibabaCloudLogo,
  zai: ZAILogo,
  'zai-coding-plan': ZAILogo,
  'zhipuai-coding-plan': GLMLogo,
  kimi: KimiLogo,
  'kimi-for-coding': KimiLogo,
  glm: GLMLogo,
  minimax: MiniMaxLogo,
  ollama: OllamaLogo,
}

/** Get the logo component for a provider, falling back to Bot icon */
export function getProviderLogo(id: string): IconComponent {
  return PROVIDER_LOGOS[id] ?? (Bot as IconComponent)
}
