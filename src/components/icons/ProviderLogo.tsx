import type { Component } from 'solid-js'
import { getProviderLogo } from './provider-logo-map'

interface ProviderLogoProps {
  providerId: string
  class?: string
}

const LIGHTEN_ON_DARK = new Set([
  'anthropic',
  'openai',
  'chatgpt',
  'copilot',
  'openrouter',
  'ollama',
  'zai',
  'minimax',
])

const LOGO_SCALE: Record<string, number> = {
  anthropic: 1.08,
  openai: 1,
  chatgpt: 1,
  copilot: 1.02,
  openrouter: 1.02,
  ollama: 1.02,
  minimax: 0.98,
  alibaba: 1.04,
  'alibaba-cn': 1.04,
  kimi: 1.02,
  inception: 1.04,
}

export const ProviderLogo: Component<ProviderLogoProps> = (props) => {
  const Icon = getProviderLogo(props.providerId)
  const providerId = props.providerId
  const scale = LOGO_SCALE[providerId] ?? 1
  const filter = LIGHTEN_ON_DARK.has(providerId)
    ? 'brightness(0) saturate(100%) invert(83%)'
    : undefined

  return (
    <span class={`inline-flex items-center justify-center overflow-hidden ${props.class ?? ''}`}>
      <Icon
        class="h-full w-full"
        style={{ transform: `scale(${scale})`, filter, 'transform-origin': 'center' }}
      />
    </span>
  )
}
