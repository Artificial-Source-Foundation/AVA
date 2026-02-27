/**
 * Provider Logo Map
 *
 * Maps provider IDs to their brand logo components.
 * Falls back to a generic Bot icon for unknown providers.
 */

import { Bot } from 'lucide-solid'
import type { Component } from 'solid-js'
import {
  AnthropicLogo,
  CohereLogo,
  CopilotLogo,
  DeepSeekLogo,
  GLMLogo,
  GoogleLogo,
  GroqLogo,
  KimiLogo,
  MistralLogo,
  OllamaLogo,
  OpenAILogo,
  OpenRouterLogo,
  TogetherLogo,
  XAILogo,
} from './provider-logos'

type IconComponent = Component<{ class?: string }>

export const PROVIDER_LOGOS: Record<string, IconComponent> = {
  anthropic: AnthropicLogo,
  openai: OpenAILogo,
  google: GoogleLogo,
  copilot: CopilotLogo,
  openrouter: OpenRouterLogo,
  xai: XAILogo,
  mistral: MistralLogo,
  groq: GroqLogo,
  deepseek: DeepSeekLogo,
  cohere: CohereLogo,
  together: TogetherLogo,
  kimi: KimiLogo,
  glm: GLMLogo,
  ollama: OllamaLogo,
}

/** Get the logo component for a provider, falling back to Bot icon */
export function getProviderLogo(id: string): IconComponent {
  return PROVIDER_LOGOS[id] ?? (Bot as IconComponent)
}
