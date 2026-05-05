set(AVA_BACKEND_LIBRARIES
  ava_agent
  ava_config
  ava_context
  ava_core
  ava_lsp
  ava_mcp
  ava_permissions
  ava_plugin
  ava_provider
  ava_session
  ava_tools

  CACHE INTERNAL "AVA backend libraries"
)

add_subdirectory(agent)
add_subdirectory(config)
add_subdirectory(context)
add_subdirectory(core)
add_subdirectory(lsp)
add_subdirectory(mcp)
add_subdirectory(permissions)
add_subdirectory(plugin)
add_subdirectory(provider)
add_subdirectory(session)
add_subdirectory(tools)

set(AVA_FRONTEND_LIBRARIES
  ava_app
  ava_tui

  CACHE INTERNAL "AVA frontend libraries"
)

add_subdirectory(app)
add_subdirectory(tui)
