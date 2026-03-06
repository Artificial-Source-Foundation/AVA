/**
 * Appearance Settings Tab
 *
 * Thin orchestrator composing ThemeSelector, FontSettings, and LayoutSettings
 * sub-components into SettingsCard wrappers.
 */

import {
  Accessibility,
  Code2,
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
import { SettingsCard } from '../SettingsCard'
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
  ThemePresetsGrid,
} from './appearance-tab'

export const AppearanceTab: Component = () => {
  const { updateSettings, updateAppearance } = useSettings()

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

  return (
    <div class="grid grid-cols-1 gap-4">
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
