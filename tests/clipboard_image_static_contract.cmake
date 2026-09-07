if(NOT DEFINED AVA_CLIPBOARD_IMAGE_SOURCE OR NOT DEFINED AVA_CLIPBOARD_IMAGE_HEADER OR NOT DEFINED AVA_INTERACTIVE_TUI_SOURCE)
  message(FATAL_ERROR "clipboard image static contract requires source, header, and TUI paths")
endif()

file(READ "${AVA_CLIPBOARD_IMAGE_SOURCE}" clipboard_source)
file(READ "${AVA_CLIPBOARD_IMAGE_HEADER}" clipboard_header)
file(READ "${AVA_INTERACTIVE_TUI_SOURCE}" tui_source)

foreach(forbidden IN ITEMS
    "fork[ \t\r\n]*\\("
    "kill[ \t\r\n]*\\("
    "waitpid[ \t\r\n]*\\("
    "waitid[ \t\r\n]*\\("
    "poll[ \t\r\n]*\\("
    "pollfd"
    "<poll.h>"
    "<signal.h>"
    "<csignal>"
    "<sys/wait.h>"
    "<sys/types.h>"
    "<unistd.h>"
    "<fcntl.h>")
  if(clipboard_source MATCHES "${forbidden}")
    message(FATAL_ERROR "clipboard image helper regained legacy process ownership: ${forbidden}")
  endif()
endforeach()

foreach(required IN ITEMS
    "session_process_scope.operation()"
    "make_clipboard_desktop_environment_v1"
    "if (Clock::now() >= helper_deadline)"
    "supervisor.reserve"
    "ProcessRoleV1::ClipboardHelper"
    ".termination_grace = 0ms"
    ".execution_deadline = helper_deadline"
    "supervisor.spawn"
    ".cwd = \"/\""
    "StreamModeV1::Discard"
    "StreamModeV1::Capture"
    "supervisor.request_stop"
    "supervisor.wait("
    "supervisor.try_wait"
    "supervisor.wait_for_activity"
    "supervisor.account_output"
    "status.cleanup != ava::process::CleanupStateV1::Complete"
    "clipboard_helper_lifecycle_error()"
    "AVA_ASSERT_NO_SESSION_LOCK_HELD")
  string(FIND "${clipboard_source}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "clipboard image static contract is missing: ${required}")
  endif()
endforeach()

string(FIND "${clipboard_source}" "if (Clock::now() >= helper_deadline)" deadline_check_index)
string(FIND "${clipboard_source}" "auto reservation = supervisor.reserve" reservation_index)
if(deadline_check_index EQUAL -1 OR reservation_index EQUAL -1 OR deadline_check_index GREATER reservation_index)
  message(FATAL_ERROR "clipboard image helper must reject an exhausted deadline before reservation")
endif()

foreach(required_header IN ITEMS
    "std::optional<ava::process::ProcessScopeV1> const& session_process_scope"
    "import_clipboard_image_attachment")
  string(FIND "${clipboard_header}" "${required_header}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "clipboard image API is not session-process-scoped: ${required_header}")
  endif()
endforeach()

foreach(required_tui IN ITEMS
    "auto const [store, session_process_scope] = [&unlocked_session]"
    "SCOPED_CRITICAL_AREA_R(session_r, unlocked_session)"
    "return std::pair{session_r->store, session_r->session_process_scope()}"
    "import_clipboard_image_attachment(store, session_process_scope)")
  string(FIND "${tui_source}" "${required_tui}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "interactive TUI does not snapshot and pass the current session scope: ${required_tui}")
  endif()
endforeach()
