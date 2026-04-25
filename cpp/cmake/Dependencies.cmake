include(FetchContent)

# Keep fetched third-party builds from enabling strict warning failure modes.
set(FMT_WERROR OFF CACHE BOOL "Disable fmt warnings-as-errors in bootstrap" FORCE)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "Use external fmt for spdlog in bootstrap" FORCE)
set(SPDLOG_WERROR OFF CACHE BOOL "Disable spdlog warnings-as-errors in bootstrap" FORCE)
set(CLI11_WARNINGS_AS_ERRORS OFF CACHE BOOL "Disable CLI11 warnings-as-errors in bootstrap" FORCE)
set(CATCH_ENABLE_WERROR OFF CACHE BOOL "Disable Catch2 warnings-as-errors in bootstrap" FORCE)

find_package(fmt CONFIG QUIET)
if(NOT fmt_FOUND)
  FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 11.0.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(fmt)
endif()

find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
  FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(spdlog)
endif()

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(CLI11 CONFIG QUIET)
if(NOT CLI11_FOUND)
  FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(CLI11)
endif()

find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
  message(FATAL_ERROR "SQLite3 development files are required for ava_session. Install libsqlite3-dev/sqlite-devel or provide SQLite3_ROOT.")
endif()
if(TARGET SQLite3::SQLite3 AND NOT TARGET SQLite::SQLite3)
  add_library(SQLite::SQLite3 ALIAS SQLite3::SQLite3)
endif()

if(AVA_BUILD_TESTS)
  find_package(Catch2 3 CONFIG QUIET)
  if(NOT Catch2_FOUND)
    FetchContent_Declare(
      Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG v3.7.0
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
  endif()
endif()

add_library(ava_optional_ftxui INTERFACE)
set(AVA_RESOLVED_WITH_FTXUI_VALUE 0)
if(AVA_WITH_FTXUI)
  find_package(ftxui CONFIG QUIET)
  if(NOT ftxui_FOUND)
    FetchContent_Declare(
      ftxui
      GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
      GIT_TAG v5.0.0
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(ftxui)
    find_package(ftxui CONFIG QUIET)
  endif()
  if(TARGET ftxui::screen AND TARGET ftxui::dom AND TARGET ftxui::component)
    target_link_libraries(ava_optional_ftxui INTERFACE ftxui::screen ftxui::dom ftxui::component)
    target_compile_definitions(ava_optional_ftxui INTERFACE AVA_WITH_FTXUI=1)
    set(AVA_RESOLVED_WITH_FTXUI_VALUE 1)
  else()
    message(WARNING "AVA_WITH_FTXUI is ON but ftxui was not found/fetched. Building without FTXUI linkage.")
    target_compile_definitions(ava_optional_ftxui INTERFACE AVA_WITH_FTXUI=0)
  endif()
else()
  target_compile_definitions(ava_optional_ftxui INTERFACE AVA_WITH_FTXUI=0)
endif()

add_library(ava_optional_cpr INTERFACE)
set(AVA_RESOLVED_WITH_CPR_VALUE 0)
if(AVA_WITH_CPR)
  find_package(cpr CONFIG QUIET)
  find_package(CPR CONFIG QUIET)
  if(NOT cpr_FOUND AND NOT CPR_FOUND)
    FetchContent_Declare(
      cpr
      GIT_REPOSITORY https://github.com/libcpr/cpr.git
      GIT_TAG 1.11.0
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(cpr)
    find_package(cpr CONFIG QUIET)
    find_package(CPR CONFIG QUIET)
  endif()
  if(TARGET cpr::cpr)
    target_link_libraries(ava_optional_cpr INTERFACE cpr::cpr)
    target_compile_definitions(ava_optional_cpr INTERFACE AVA_WITH_CPR=1)
    set(AVA_RESOLVED_WITH_CPR_VALUE 1)
  elseif(TARGET CPR::cpr)
    target_link_libraries(ava_optional_cpr INTERFACE CPR::cpr)
    target_compile_definitions(ava_optional_cpr INTERFACE AVA_WITH_CPR=1)
    set(AVA_RESOLVED_WITH_CPR_VALUE 1)
  else()
    message(WARNING "AVA_WITH_CPR is ON but cpr was not found. Building without CPR linkage.")
    target_compile_definitions(ava_optional_cpr INTERFACE AVA_WITH_CPR=0)
  endif()
else()
  target_compile_definitions(ava_optional_cpr INTERFACE AVA_WITH_CPR=0)
endif()
