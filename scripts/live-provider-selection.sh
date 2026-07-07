select_first_live_provider()
{
  provider=
  model=
  label=
  if [ -n "${OPENAI_API_KEY:-}" ]; then
    provider=openai
    model="${AVA_LIVE_OPENAI_MODEL:-gpt-4.1-mini}"
    label=OpenAI
  elif [ -n "${ANTHROPIC_OAUTH_TOKEN:-}" ] || [ -n "${ANTHROPIC_AUTH_TOKEN:-}" ] || [ -n "${ANTHROPIC_API_KEY:-}" ]; then
    provider=anthropic
    model="${AVA_LIVE_ANTHROPIC_MODEL:-claude-sonnet-4-5}"
    label=Anthropic
  elif [ -n "${DEEPSEEK_API_KEY:-}" ]; then
    provider=deepseek
    model="${AVA_LIVE_DEEPSEEK_MODEL:-deepseek-v4-flash}"
    label=DeepSeek
  elif [ -n "${GEMINI_API_KEY:-}" ]; then
    provider=gemini
    model="${AVA_LIVE_GEMINI_MODEL:-gemini-2.5-pro}"
    label=Gemini
  elif [ -n "${KIMI_API_KEY:-}" ]; then
    provider=kimi
    model="${AVA_LIVE_KIMI_MODEL:-kimi-k2-thinking}"
    label=Kimi
  elif [ -n "${MOONSHOT_API_KEY:-}" ]; then
    provider=moonshot
    model="${AVA_LIVE_MOONSHOT_MODEL:-kimi-k2.6}"
    label=Moonshot
  elif [ -n "${OPENROUTER_API_KEY:-}" ]; then
    provider=openrouter
    model="${AVA_LIVE_OPENROUTER_MODEL:-moonshotai/kimi-k2.6}"
    label=OpenRouter
  fi
}
