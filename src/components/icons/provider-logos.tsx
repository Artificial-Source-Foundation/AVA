/**
 * Provider Brand Logos
 *
 * Inline SVG logo components for all 14 LLM providers.
 * All use fill="currentColor" for theme compatibility.
 * SVG paths sourced from official brand assets / simple-icons.
 */

import type { Component } from 'solid-js'

interface LogoProps {
  class?: string
}

/** Anthropic — stylized "A" mark */
export const AnthropicLogo: Component<LogoProps> = (props) => (
  <svg viewBox="0 0 24 24" fill="currentColor" class={props.class} aria-hidden="true">
    <path d="M17.304 3.541h-3.483l6.15 16.918h3.483zm-10.608 0L.546 20.459H4.15l1.262-3.473h6.478l1.262 3.473h3.604L10.608 3.541zm.675 10.482 2.066-5.676 2.065 5.676z" />
  </svg>
)

/** OpenAI — hexagonal flower mark */
export const OpenAILogo: Component<LogoProps> = (props) => (
  <svg viewBox="0 0 24 24" fill="currentColor" class={props.class} aria-hidden="true">
    <path d="M22.282 9.821a5.985 5.985 0 0 0-.516-4.91 6.046 6.046 0 0 0-6.51-2.9A6.065 6.065 0 0 0 4.981 4.18a5.985 5.985 0 0 0-3.998 2.9 6.046 6.046 0 0 0 .743 7.097 5.98 5.98 0 0 0 .51 4.911 6.051 6.051 0 0 0 6.515 2.9A5.985 5.985 0 0 0 13.26 24a6.056 6.056 0 0 0 5.772-4.206 5.99 5.99 0 0 0 3.997-2.9 6.056 6.056 0 0 0-.747-7.073zM13.26 22.43a4.476 4.476 0 0 1-2.876-1.04l.141-.081 4.779-2.758a.795.795 0 0 0 .392-.681v-6.737l2.02 1.168a.071.071 0 0 1 .038.052v5.583a4.504 4.504 0 0 1-4.494 4.494zM3.6 18.304a4.47 4.47 0 0 1-.535-3.014l.142.085 4.783 2.759a.771.771 0 0 0 .78 0l5.843-3.369v2.332a.08.08 0 0 1-.033.062L9.74 19.95a4.5 4.5 0 0 1-6.14-1.646zM2.34 7.896a4.485 4.485 0 0 1 2.366-1.973V11.6a.766.766 0 0 0 .388.676l5.815 3.355-2.02 1.168a.076.076 0 0 1-.071 0l-4.83-2.786A4.504 4.504 0 0 1 2.34 7.872zm16.597 3.855-5.833-3.387L15.119 7.2a.076.076 0 0 1 .071 0l4.83 2.791a4.494 4.494 0 0 1-.676 8.105v-5.678a.79.79 0 0 0-.407-.667zm2.01-3.023-.141-.085-4.774-2.782a.776.776 0 0 0-.785 0L9.409 9.23V6.897a.066.066 0 0 1 .028-.061l4.83-2.787a4.5 4.5 0 0 1 6.68 4.66zm-12.64 4.135-2.02-1.164a.08.08 0 0 1-.038-.057V6.075a4.5 4.5 0 0 1 7.375-3.453l-.142.08L8.704 5.46a.795.795 0 0 0-.393.681zm1.097-2.365 2.602-1.5 2.607 1.5v2.999l-2.597 1.5-2.607-1.5z" />
  </svg>
)

/** Google — four-color "G" simplified to single-color */
export const GoogleLogo: Component<LogoProps> = (props) => (
  <svg viewBox="0 0 24 24" fill="currentColor" class={props.class} aria-hidden="true">
    <path d="M12.48 10.92v3.28h7.84c-.24 1.84-.853 3.187-1.787 4.133-1.147 1.147-2.933 2.4-6.053 2.4-4.827 0-8.6-3.893-8.6-8.72s3.773-8.72 8.6-8.72c2.6 0 4.507 1.027 5.907 2.347l2.307-2.307C18.747 1.44 16.133 0 12.48 0 5.867 0 .307 5.387.307 12s5.56 12 12.173 12c3.573 0 6.267-1.173 8.373-3.36 2.16-2.16 2.84-5.213 2.84-7.667 0-.76-.053-1.467-.173-2.053z" />
  </svg>
)

/** GitHub Copilot — GitHub octocat silhouette */
export const CopilotLogo: Component<LogoProps> = (props) => (
  <svg viewBox="0 0 24 24" fill="currentColor" class={props.class} aria-hidden="true">
    <path d="M12 .297c-6.63 0-12 5.373-12 12 0 5.303 3.438 9.8 8.205 11.385.6.113.82-.258.82-.577 0-.285-.01-1.04-.015-2.04-3.338.724-4.042-1.61-4.042-1.61C4.422 18.07 3.633 17.7 3.633 17.7c-1.087-.744.084-.729.084-.729 1.205.084 1.838 1.236 1.838 1.236 1.07 1.835 2.809 1.305 3.495.998.108-.776.417-1.305.76-1.605-2.665-.3-5.466-1.332-5.466-5.93 0-1.31.465-2.38 1.235-3.22-.135-.303-.54-1.523.105-3.176 0 0 1.005-.322 3.3 1.23.96-.267 1.98-.399 3-.405 1.02.006 2.04.138 3 .405 2.28-1.552 3.285-1.23 3.285-1.23.645 1.653.24 2.873.12 3.176.765.84 1.23 1.91 1.23 3.22 0 4.61-2.805 5.625-5.475 5.92.42.36.81 1.096.81 2.22 0 1.606-.015 2.896-.015 3.286 0 .315.21.69.825.57C20.565 22.092 24 17.592 24 12.297c0-6.627-5.373-12-12-12" />
  </svg>
)

/** OpenRouter — router/network node mark */
export const OpenRouterLogo: Component<LogoProps> = (props) => (
  <svg viewBox="0 0 24 24" fill="currentColor" class={props.class} aria-hidden="true">
    <path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 3c1.66 0 3 1.34 3 3s-1.34 3-3 3-3-1.34-3-3 1.34-3 3-3zm-7 8.5c0-1.38 1.12-2.5 2.5-2.5S10 12.12 10 13.5 8.88 16 7.5 16 5 14.88 5 13.5zm7 5.5c-1.66 0-3-1.34-3-3h6c0 1.66-1.34 3-3 3zm4.5-3c-1.38 0-2.5-1.12-2.5-2.5s1.12-2.5 2.5-2.5 2.5 1.12 2.5 2.5-1.12 2.5-2.5 2.5z" />
  </svg>
)

/** xAI — stylized X mark */
export const XAILogo: Component<LogoProps> = (props) => (
  <svg viewBox="0 0 24 24" fill="currentColor" class={props.class} aria-hidden="true">
    <path d="M2.87 3h4.46l5.91 8.65L21 3h2.13l-9.2 10.96L24 24h-4.46l-6.28-9.19L5.27 24H3.13l9.56-11.37z" />
  </svg>
)

/** Mistral — Le Chat geometric mark (simplified stacked bars) */
export const MistralLogo: Component<LogoProps> = (props) => (
  <svg viewBox="0 0 24 24" fill="currentColor" class={props.class} aria-hidden="true">
    <path d="M3 3h4v4H3zm14 0h4v4h-4zM3 9h4v4H3zm4 0h4v4H7zm4 0h4v4h-4zm4 0h4v4h-4zm4 0h4v4h-4zM3 15h4v4H3zm14 0h4v4h-4zM7 15h4v4H7zm4 0h4v4h-4z" />
  </svg>
)

/** Groq — stylized lightning bolt */
export const GroqLogo: Component<LogoProps> = (props) => (
  <svg viewBox="0 0 24 24" fill="currentColor" class={props.class} aria-hidden="true">
    <path d="M12 0C5.373 0 0 5.373 0 12s5.373 12 12 12 12-5.373 12-12S18.627 0 12 0zm-.84 5.6h5.04L12.72 12h3.36l-6.72 6.4 1.68-5.6H7.68z" />
  </svg>
)

/** DeepSeek — stylized whale/deep mark */
export const DeepSeekLogo: Component<LogoProps> = (props) => (
  <svg viewBox="0 0 24 24" fill="currentColor" class={props.class} aria-hidden="true">
    <path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm4.64 13.28c-.48.72-1.2 1.28-2.08 1.6-.88.32-1.92.4-3.04.24-1.12-.16-2.08-.56-2.88-1.2-.8-.64-1.36-1.44-1.68-2.4l2.08-.72c.24.64.6 1.12 1.12 1.48.52.36 1.12.56 1.76.6.64.04 1.24-.08 1.76-.32.52-.24.92-.6 1.16-1.08.24-.48.24-1.04 0-1.52-.24-.48-.68-.84-1.28-1.04l-2.16-.72c-1.12-.36-1.92-.96-2.4-1.72-.48-.76-.56-1.6-.24-2.48.24-.64.64-1.16 1.2-1.56.56-.4 1.2-.64 1.92-.72.72-.08 1.44.04 2.12.32.68.28 1.24.72 1.68 1.28l-1.68 1.28c-.28-.36-.64-.64-1.04-.8-.4-.16-.84-.2-1.24-.12-.4.08-.76.28-1 .56-.28.28-.4.64-.36 1.04.04.4.24.72.56.96.32.24.72.44 1.2.6l2.16.72c1.12.4 1.92.96 2.4 1.72.48.76.52 1.6.2 2.48z" />
  </svg>
)

/** Cohere — C mark */
export const CohereLogo: Component<LogoProps> = (props) => (
  <svg viewBox="0 0 24 24" fill="currentColor" class={props.class} aria-hidden="true">
    <path d="M12.005 2C6.478 2 2 6.478 2 12.005 2 17.527 6.478 22 12.005 22c1.89 0 3.673-.525 5.193-1.44l-.12-.19c-1.41.78-3.04 1.23-4.773 1.23C7.262 21.6 3 17.335 3 12.293 3 7.05 7.262 2.4 12.305 2.4c3.07 0 5.78 1.6 7.37 4l.17-.1C18.195 3.78 15.295 2 12.005 2zm3.29 6.7c-1.83 0-3.48.75-4.68 1.96a6.67 6.67 0 0 0-1.96 4.74c0 1.06.87 1.92 1.93 1.92h4.84c1.62 0 3.23-.47 4.58-1.4l-.14-.18c-1.26.82-2.73 1.27-4.24 1.27h-4.74c-.88 0-1.59-.71-1.59-1.59 0-1.63.65-3.19 1.79-4.34a6.11 6.11 0 0 1 4.25-1.78c1.15 0 2.3.3 3.35.9l.13-.19c-1.1-.63-2.31-.96-3.52-.96z" />
  </svg>
)

/** Together — interconnected nodes */
export const TogetherLogo: Component<LogoProps> = (props) => (
  <svg viewBox="0 0 24 24" fill="currentColor" class={props.class} aria-hidden="true">
    <path d="M7 4a3 3 0 1 1 0 6 3 3 0 0 1 0-6zm10 0a3 3 0 1 1 0 6 3 3 0 0 1 0-6zM7 14a3 3 0 1 1 0 6 3 3 0 0 1 0-6zm10 0a3 3 0 1 1 0 6 3 3 0 0 1 0-6zM9.5 7h5M9.5 17h5M7 9.5v5m10-5v5" />
  </svg>
)

/** Kimi / Moonshot — crescent moon mark */
export const KimiLogo: Component<LogoProps> = (props) => (
  <svg viewBox="0 0 24 24" fill="currentColor" class={props.class} aria-hidden="true">
    <path d="M12 2A10 10 0 0 0 2 12a10 10 0 0 0 10 10 10 10 0 0 0 10-10A10 10 0 0 0 12 2zm0 2a8 8 0 0 1 4.906 1.684A6.5 6.5 0 0 0 12.5 18.95 8 8 0 0 1 12 4z" />
  </svg>
)

/** Zhipu / GLM — stylized brain/network */
export const GLMLogo: Component<LogoProps> = (props) => (
  <svg viewBox="0 0 24 24" fill="currentColor" class={props.class} aria-hidden="true">
    <path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-1 14H9v-2h2zm0-4H9V7h2zm4 4h-2v-2h2zm0-4h-2V7h2z" />
  </svg>
)

/** Ollama — llama silhouette */
export const OllamaLogo: Component<LogoProps> = (props) => (
  <svg viewBox="0 0 24 24" fill="currentColor" class={props.class} aria-hidden="true">
    <path d="M12 2c-2.5 0-4.5 1.5-5.5 3.5C5 6 4 7.5 4 9.5c0 1.5.5 2.8 1.5 3.8V20c0 .55.45 1 1 1h2c.55 0 1-.45 1-1v-2h7v2c0 .55.45 1 1 1h2c.55 0 1-.45 1-1v-6.7c1-.97 1.5-2.3 1.5-3.8 0-2-1-3.5-2.5-4C18.5 3.5 16.5 2 14 2h-2zm-2 8.5a1.25 1.25 0 1 1 0-2.5 1.25 1.25 0 0 1 0 2.5zm4 0a1.25 1.25 0 1 1 0-2.5 1.25 1.25 0 0 1 0 2.5z" />
  </svg>
)
