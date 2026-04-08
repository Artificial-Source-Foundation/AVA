/**
 * Provider Logo Map
 *
 * Maps provider IDs to their brand logo components.
 * Falls back to a generic Bot icon for unknown providers.
 */

import { Bot } from 'lucide-solid'
import type { Component } from 'solid-js'
import {
  AlibabaCloudLogo,
  AnthropicLogo,
  CopilotLogo,
  GLMLogo,
  GoogleLogo,
  KimiLogo,
  OllamaLogo,
  OpenAILogo,
  OpenRouterLogo,
} from './provider-logos'

type IconComponent = Component<{ class?: string }>

export const PROVIDER_LOGOS: Record<string, IconComponent> = {
  anthropic: AnthropicLogo,
  openai: OpenAILogo,
  chatgpt: OpenAILogo,
  gemini: GoogleLogo,
  google: GoogleLogo,
  copilot: CopilotLogo,
  openrouter: OpenRouterLogo,
  inception: OpenAILogo,
  alibaba: AlibabaCloudLogo,
  'alibaba-cn': AlibabaCloudLogo,
  zai: GLMLogo,
  'zai-coding-plan': GLMLogo,
  'zhipuai-coding-plan': GLMLogo,
  kimi: KimiLogo,
  'kimi-for-coding': KimiLogo,
  glm: GLMLogo,
  ollama: OllamaLogo,
}

/** Get the logo component for a provider, falling back to Bot icon */
export function getProviderLogo(id: string): IconComponent {
  return PROVIDER_LOGOS[id] ?? (Bot as IconComponent)
}
