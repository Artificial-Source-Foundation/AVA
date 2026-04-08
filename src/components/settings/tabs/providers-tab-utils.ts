import { normalizeProviderId } from '../../../types/llm'

export const formatContextWindow = (tokens: number): string => {
  if (tokens >= 1000000) return `${(tokens / 1000000).toFixed(1)}M`
  if (tokens >= 1000) return `${Math.round(tokens / 1000)}K`
  return tokens.toString()
}

export const getProviderDocsUrl = (providerId: string): string => {
  const canonicalProviderId = normalizeProviderId(providerId)
  const urls: Record<string, string> = {
    anthropic: 'https://docs.anthropic.com/en/api',
    openai: 'https://platform.openai.com/docs/api-reference',
    gemini: 'https://ai.google.dev/docs',
    copilot: 'https://docs.github.com/en/copilot',
    openrouter: 'https://openrouter.ai/docs',
    cohere: 'https://docs.cohere.com/',
    together: 'https://docs.together.ai/',
    kimi: 'https://platform.moonshot.cn/docs',
    zai: 'https://open.bigmodel.cn/dev/api',
    ollama: 'https://ollama.ai/docs',
    alibaba: 'https://www.alibabacloud.com/help/en/model-studio/opencode-coding-plan',
  }
  return urls[canonicalProviderId] || '#'
}

export const oauthButtonText = (providerId: string): { label: string; description: string } => {
  switch (normalizeProviderId(providerId)) {
    case 'openai':
      return { label: 'Sign in with ChatGPT', description: 'Use your Plus/Pro subscription' }
    case 'gemini':
      return { label: 'Sign in with Google', description: 'Use your Gemini API access' }
    case 'copilot':
      return { label: 'Sign in with GitHub Copilot', description: 'Use your Copilot subscription' }
    default:
      return { label: `Sign in with ${providerId}`, description: 'OAuth authentication' }
  }
}
