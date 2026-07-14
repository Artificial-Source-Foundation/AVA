#pragma once

namespace ava::agent {

// Lifecycle boundaries emitted by AgentLoop.  The app controller consumes this
// contract; keeping it in agent avoids making the reusable loop depend on app.
enum class RunPhase
{
  Admitted,
  BuildingContext,
  AwaitingProvider,
  PersistingAssistant,
  PreparingTools,
  ExecutingTools,
  SettlingTools,
  Compacting,
  Completing,
  Canceling,
  Failed,
};

}  // namespace ava::agent
