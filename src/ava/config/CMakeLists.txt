add_library(ava_config
  "auth.cpp"
  "auth_file_store.cpp"
  "auth_record.cpp"
  "model_config.cpp"
  "model_profiles.cpp"
  "openai_oauth.cpp"
  "provider_profiles.cpp"
  "prompt_config.cpp"
  "reasoning_profiles.cpp"
  "xdg_paths.cpp"
)

target_include_directories(ava_config
  PUBLIC "${CMAKE_SOURCE_DIR}/src"
  PRIVATE "${AVA_GENERATED_INCLUDE_DIR}"
)
ava_apply_warnings(ava_config)
ava_apply_sanitizers(ava_config)
