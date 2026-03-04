export const formatContextWindow = (tokens: number): string => {
  if (tokens >= 1000000) return `${(tokens / 1000000).toFixed(1)}M`
  if (tokens >= 1000) return `${Math.round(tokens / 1000)}K`
  return tokens.toString()
}

export const getProviderDocsUrl = (providerId: string): string => {
  const urls: Record<string, string> = {
    anthropic: 'https://docs.anthropic.com/en/api',
    openai: 'https://platform.openai.com/docs/api-reference',
    google: 'https://ai.google.dev/docs',
    copilot: 'https://docs.github.com/en/copilot',
    openrouter: 'https://openrouter.ai/docs',
    xai: 'https://docs.x.ai/api',
    mistral: 'https://docs.mistral.ai/api/',
    groq: 'https://console.groq.com/docs',
    deepseek: 'https://platform.deepseek.com/api-docs',
    cohere: 'https://docs.cohere.com/',
    together: 'https://docs.together.ai/',
    kimi: 'https://platform.moonshot.cn/docs',
    glm: 'https://open.bigmodel.cn/dev/api',
    ollama: 'https://ollama.ai/docs',
    alibaba: 'https://www.alibabacloud.com/help/en/model-studio/opencode-coding-plan',
  }
  return urls[providerId] || '#'
}

export const oauthButtonText = (providerId: string): { label: string; description: string } => {
  switch (providerId) {
    case 'openai':
      return { label: 'Sign in with ChatGPT', description: 'Use your Plus/Pro subscription' }
    case 'google':
      return { label: 'Sign in with Google', description: 'Use your Gemini API access' }
    case 'copilot':
      return { label: 'Sign in with GitHub Copilot', description: 'Use your Copilot subscription' }
    default:
      return { label: `Sign in with ${providerId}`, description: 'OAuth authentication' }
  }
}
