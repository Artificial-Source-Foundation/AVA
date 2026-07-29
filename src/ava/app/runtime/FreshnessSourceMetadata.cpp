#include "sys.h"
#include "FreshnessSourceMetadata.h"
#include "debug.h"

namespace ava::app::runtime {

std::string to_string(FreshnessSourceKind kind)
{
  using enum FreshnessSourceKind;
  switch (kind)
  {
    case SystemPrompt:
      return "SystemPrompt";
    case AppendSystemPrompt:
      return "AppendSystemPrompt";
    case PromptCommand:
      return "PromptCommand";
    case Skill:
      return "Skill";
    case PluginManifest:
      return "PluginManifest";
    case PluginPrompt:
      return "PluginPrompt";
    case PluginSkill:
      return "PluginSkill";
  }
  AI_NEVER_REACHED
}

} // namespace ava::app::runtime
