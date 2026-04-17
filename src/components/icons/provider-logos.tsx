/**
 * Provider Brand Logos
 *
 * Uses local brand assets for provider marks where available.
 * Fallback-only/internal icons remain inline SVGs.
 */

import type { Component, JSX } from 'solid-js'
import alibabaAsset from './assets/alibaba.svg'
import anthropicAsset from './assets/anthropic.svg'
import copilotAsset from './assets/copilot.svg'
import geminiAsset from './assets/gemini.svg'
import inceptionAsset from './assets/inception.svg'
import kimiAsset from './assets/kimi.svg'
import minimaxAsset from './assets/minimax.svg'
import ollamaAsset from './assets/ollama.svg'
import openaiAsset from './assets/openai.svg'
import openrouterAsset from './assets/openrouter.svg'
import zaiAsset from './assets/zai.svg'

interface LogoProps {
  class?: string
  style?: JSX.CSSProperties
}

const assetLogo =
  (src: string, alt: string): Component<LogoProps> =>
  (props) => (
    <img
      src={src}
      alt={alt}
      class={`block h-full w-full object-contain ${props.class ?? ''}`}
      style={props.style}
      aria-hidden="true"
      draggable={false}
    />
  )

export const AnthropicLogo = assetLogo(anthropicAsset, 'Anthropic')
export const OpenAILogo = assetLogo(openaiAsset, 'OpenAI')
export const GoogleLogo = assetLogo(geminiAsset, 'Google')
export const GeminiLogo = assetLogo(geminiAsset, 'Gemini')
export const CopilotLogo = assetLogo(copilotAsset, 'GitHub Copilot')
export const OpenRouterLogo = assetLogo(openrouterAsset, 'OpenRouter')
export const InceptionLogo = assetLogo(inceptionAsset, 'Inception')
export const ZAILogo = assetLogo(zaiAsset, 'Z.AI')
export const MiniMaxLogo = assetLogo(minimaxAsset, 'MiniMax')
export const KimiLogo = assetLogo(kimiAsset, 'Kimi')
export const OllamaLogo = assetLogo(ollamaAsset, 'Ollama')
export const AlibabaCloudLogo = assetLogo(alibabaAsset, 'Alibaba Cloud')

/** Zhipu / GLM fallback mark for GLM-specific IDs not mapped to z.ai */
export const GLMLogo: Component<LogoProps> = (props) => (
  <svg
    viewBox="0 0 24 24"
    fill="currentColor"
    class={props.class}
    style={props.style}
    aria-hidden="true"
  >
    <path d="M9.671 5.365a1.697 1.697 0 011.099 2.132l-.071.172-.016.04-.018.054c-.07.16-.104.32-.104.498-.035.71.47 1.279 1.186 1.314h.366c1.309.053 2.338 1.173 2.286 2.523-.052 1.332-1.152 2.38-2.478 2.327h-.174c-.715.018-1.274.64-1.239 1.368 0 .124.018.23.053.337.209.373.54.658.96.8.75.23 1.517-.125 1.9-.782l.018-.035c.402-.64 1.17-.96 1.92-.711.854.284 1.378 1.226 1.099 2.167a1.661 1.661 0 01-2.077 1.102 1.711 1.711 0 01-.907-.711l-.017-.035c-.2-.323-.463-.58-.851-.711l-.056-.018a1.646 1.646 0 00-1.954.746 1.66 1.66 0 01-1.065.764 1.677 1.677 0 01-1.989-1.279c-.209-.906.332-1.83 1.257-2.043a1.51 1.51 0 01.296-.035h.018c.68-.071 1.151-.622 1.116-1.333a1.307 1.307 0 00-.227-.693 2.515 2.515 0 01-.366-1.403 2.39 2.39 0 01.366-1.208c.14-.195.21-.444.227-.693.018-.71-.506-1.261-1.186-1.332l-.07-.018a1.43 1.43 0 01-.299-.07l-.05-.019a1.7 1.7 0 01-1.047-2.114 1.68 1.68 0 012.094-1.101zm-5.575 10.11c.26-.264.639-.367.994-.27.355.096.633.379.728.74.095.362-.007.748-.267 1.013-.402.41-1.053.41-1.455 0a1.062 1.062 0 010-1.482zm14.845-.294c.359-.09.738.024.992.297.254.274.344.665.237 1.025-.107.36-.396.634-.756.718-.551.128-1.1-.22-1.23-.781a1.05 1.05 0 01.757-1.26zm-.064-4.39c.314.32.49.753.49 1.206 0 .452-.176.886-.49 1.206-.315.32-.74.5-1.185.5-.444 0-.87-.18-1.184-.5a1.727 1.727 0 010-2.412 1.654 1.654 0 012.369 0zm-11.243.163c.364.484.447 1.128.218 1.691a1.665 1.665 0 01-2.188.923c-.855-.36-1.26-1.358-.907-2.228a1.68 1.68 0 011.33-1.038c.593-.08 1.183.169 1.547.652zm11.545-4.221c.368 0 .708.2.892.524.184.324.184.724 0 1.048a1.026 1.026 0 01-.892.524c-.568 0-1.03-.47-1.03-1.048 0-.579.462-1.048 1.03-1.048zm-14.358 0c.368 0 .707.2.891.524.184.324.184.724 0 1.048a1.026 1.026 0 01-.891.524c-.569 0-1.03-.47-1.03-1.048 0-.579.461-1.048 1.03-1.048zm10.031-1.475c.925 0 1.675.764 1.675 1.706s-.75 1.705-1.675 1.705-1.674-.763-1.674-1.705c0-.942.75-1.706 1.674-1.706zm-2.626-.684c.362-.082.653-.356.761-.718a1.062 1.062 0 00-.238-1.028 1.017 1.017 0 00-.996-.294c-.547.14-.881.701-.75 1.262.13.56.679.91 1.223.778z" />
  </svg>
)

/** CLI Agents — terminal prompt icon */
export const TerminalLogo: Component<LogoProps> = (props) => (
  <svg
    viewBox="0 0 24 24"
    fill="currentColor"
    class={props.class}
    style={props.style}
    aria-hidden="true"
  >
    <path d="M2 4a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h20a2 2 0 0 0 2-2V6a2 2 0 0 0-2-2H2zm.5 2h19a.5.5 0 0 1 .5.5v11a.5.5 0 0 1-.5.5h-19a.5.5 0 0 1-.5-.5v-11a.5.5 0 0 1 .5-.5zM6.146 8.146a.5.5 0 0 0 0 .708L8.793 11.5l-2.647 2.646a.5.5 0 0 0 .708.708l3-3a.5.5 0 0 0 0-.708l-3-3a.5.5 0 0 0-.708 0zM12 14a.5.5 0 0 0 0 1h4a.5.5 0 0 0 0-1h-4z" />
  </svg>
)
