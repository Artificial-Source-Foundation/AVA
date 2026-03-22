/**
 * Appearance Settings Tab
 *
 * Thin orchestrator composing ThemeSelector, FontSettings, and LayoutSettings
 * sub-components into SettingsCard wrappers.
 */

import {
  Accessibility,
  Code2,
  Layers,
  Lightbulb,
  Maximize2,
  Moon,
  Palette,
  PanelLeft,
  Radius,
  SlidersHorizontal,
  Type,
} from 'lucide-solid'
import type { Component } from 'solid-js'
import type { ThemePreset } from '../../../config/theme-presets'
import { useSettings } from '../../../stores/settings'
import type { ActivityDisplay, ThinkingDisplay } from '../../../stores/settings/settings-types'
import { SettingsCard } from '../SettingsCard'
import { SETTINGS_CARD_GAP } from '../settings-constants'
import {
  AccentSection,
  AccessibilitySection,
  BorderRadiusSection,
  CodeThemeSection,
  ColorModeSection,
  DensitySection,
  FontSection,
  InterfaceScaleSection,
  SidebarOrderSection,
  segmentedBtn,
  ThemePresetsGrid,
} from './appearance-tab'

export const AppearanceTab: Component = () => {
  const { settings, updateSettings, updateAppearance } = useSettings()

  const applyPreset = (preset: ThemePreset): void => {
    updateSettings({ mode: preset.mode })
    const appearance: Record<string, unknown> = {
      accentColor: preset.accentColor,
      codeTheme: preset.codeTheme,
      borderRadius: preset.borderRadius,
    }
    if (preset.customAccentColor) {
      appearance.customAccentColor = preset.customAccentColor
    }
    if (preset.darkStyle) {
      appearance.darkStyle = preset.darkStyle
    }
    updateAppearance(appearance)
  }

  const thinkingOptions: { value: ThinkingDisplay; label: string }[] = [
    { value: 'bubble', label: 'Bubble' },
    { value: 'preview', label: 'Preview' },
    { value: 'hidden', label: 'Hidden' },
  ]

  const activityOptions: { value: ActivityDisplay; label: string }[] = [
    { value: 'collapsed', label: 'Collapsed' },
    { value: 'expanded', label: 'Expanded' },
    { value: 'hidden', label: 'Hidden' },
  ]

  return (
    <div class="grid grid-cols-1" style={{ gap: SETTINGS_CARD_GAP }}>
      <SettingsCard icon={Moon} title="Color Mode" description="Theme and dark style variant">
        <ColorModeSection />
      </SettingsCard>

      <SettingsCard
        icon={Palette}
        title="Theme Presets"
        description="One-click theme configurations"
      >
        <ThemePresetsGrid onApply={applyPreset} />
      </SettingsCard>

      <SettingsCard
        icon={Palette}
        title="Accent Color"
        description="Primary accent color throughout the UI"
      >
        <AccentSection />
      </SettingsCard>

      <SettingsCard
        icon={Lightbulb}
        title="Thinking Display"
        description="How AI thinking/reasoning blocks appear in chat"
      >
        <div class="flex items-center justify-between py-2">
          <span class="text-[var(--settings-text-label)] text-[var(--text-secondary)]">
            Display mode
          </span>
          <div class="flex gap-1">
            {thinkingOptions.map((opt) => (
              <button
                type="button"
                onClick={() => updateAppearance({ thinkingDisplay: opt.value })}
                class={segmentedBtn(settings().appearance.thinkingDisplay === opt.value)}
              >
                {opt.label}
              </button>
            ))}
          </div>
        </div>
        <p class="text-[var(--settings-text-description)] text-[var(--gray-8)]">
          {settings().appearance.thinkingDisplay === 'bubble'
            ? 'Show thinking in a collapsible bubble'
            : settings().appearance.thinkingDisplay === 'preview'
              ? 'Show a short preview of the thinking content inline'
              : 'Hide thinking blocks entirely'}
        </p>
      </SettingsCard>

      <SettingsCard
        icon={Layers}
        title="Agent Activity"
        description="How tool calls and thinking are grouped in chat"
      >
        <div class="flex items-center justify-between py-2">
          <span class="text-[var(--settings-text-label)] text-[var(--text-secondary)]">
            Display mode
          </span>
          <div class="flex gap-1">
            {activityOptions.map((opt) => (
              <button
                type="button"
                onClick={() => updateAppearance({ activityDisplay: opt.value })}
                class={segmentedBtn(settings().appearance.activityDisplay === opt.value)}
              >
                {opt.label}
              </button>
            ))}
          </div>
        </div>
        <p class="text-[var(--settings-text-description)] text-[var(--gray-8)]">
          {settings().appearance.activityDisplay === 'collapsed'
            ? 'Collapse tool calls and thinking into an expandable card'
            : settings().appearance.activityDisplay === 'expanded'
              ? 'Always show all tool calls and thinking inline'
              : 'Hide tool calls and thinking entirely'}
        </p>
      </SettingsCard>

      <SettingsCard
        icon={Maximize2}
        title="Interface Scale"
        description="Adjust overall UI zoom level"
      >
        <InterfaceScaleSection />
      </SettingsCard>

      <SettingsCard icon={Radius} title="Border Radius" description="Corner rounding style">
        <BorderRadiusSection />
      </SettingsCard>

      <SettingsCard
        icon={SlidersHorizontal}
        title="UI Density"
        description="Spacing between elements"
      >
        <DensitySection />
      </SettingsCard>

      <SettingsCard icon={Type} title="Font" description="UI and monospace font settings">
        <FontSection />
      </SettingsCard>

      <SettingsCard icon={Code2} title="Code Theme" description="Syntax highlighting theme">
        <CodeThemeSection />
      </SettingsCard>

      <SettingsCard
        icon={Accessibility}
        title="Accessibility"
        description="High contrast and motion preferences"
      >
        <AccessibilitySection />
      </SettingsCard>

      <SettingsCard icon={PanelLeft} title="Sidebar Order" description="Reorder sidebar sections">
        <SidebarOrderSection />
      </SettingsCard>
    </div>
  )
}
