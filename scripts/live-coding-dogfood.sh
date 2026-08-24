#!/bin/sh
set -u
umask 077

enabled="${AVA_LIVE_PROVIDER_SMOKE:-}"
if [ "$enabled" != "1" ] && [ "$enabled" != "true" ] && [ "$enabled" != "TRUE" ] && [ "$enabled" != "yes" ] && [ "$enabled" != "on" ]; then
  echo "classification=skipped/not opted in"
  echo "set AVA_LIVE_PROVIDER_SMOKE=1 and one provider credential to run the live coding dogfood"
  exit 77
fi

script_dir=$(CDPATH= cd "$(dirname "$0")" 2>/dev/null && pwd)
. "$script_dir/live-provider-selection.sh"
select_first_live_provider

if [ -z "$provider" ]; then
  echo "classification=skipped/no credential"
  echo "no supported provider credential environment variables are set"
  exit 77
fi

AVA_EXE="${AVA_EXE:-./build/ava}"
case "$AVA_EXE" in
  /*) ;;
  *) AVA_EXE="$PWD/$AVA_EXE" ;;
esac
if [ ! -x "$AVA_EXE" ]; then
  echo "classification=AVA regression"
  echo "ava executable is not runnable: $AVA_EXE"
  exit 1
fi

json_escape()
{
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

classify_failure()
{
  text=$(tr '[:upper:]' '[:lower:]' < "$1")
  case "$text" in
    *401*|*403*|*auth*|*credential*|*api_key*|*api-key*|*unauthorized*|*forbidden*)
      echo "classification=credential/auth-blocked"
      ;;
    *429*|*rate*limit*|*quota*)
      echo "classification=provider/rate-limited"
      ;;
    *timeout*|*timed\ out*|*could\ not\ resolve*|*connection*|*network*|*curl*)
      echo "classification=network-blocked"
      ;;
    *)
      echo "classification=AVA regression"
      ;;
  esac
}

private_parent_error()
{
  echo "live coding dogfood private parent rejected: $1" >&2
  exit 1
}

private_parent_metadata()
{
  if metadata=$(stat -c '%u %a' "$1" 2>/dev/null); then
    printf '%s\n' "$metadata"
    return 0
  fi
  if metadata=$(stat -f '%u %Lp' "$1" 2>/dev/null); then
    printf '%s\n' "$metadata"
    return 0
  fi
  return 1
}

validate_private_parent()
{
  parent=$1
  case "$parent" in
    /*) ;;
    *) private_parent_error "path must be absolute" ;;
  esac
  while [ "$parent" != "/" ] && [ "${parent%/}" != "$parent" ]; do
    parent=${parent%/}
  done
  [ -d "$parent" ] || private_parent_error "path must be an existing directory"
  [ ! -L "$parent" ] || private_parent_error "path must not be a symlink"

  parent_physical=$(CDPATH= cd -P "$parent" 2>/dev/null && pwd -P) ||
    private_parent_error "directory cannot be resolved"
  [ "$parent_physical" != "/" ] || private_parent_error "filesystem root is not allowed"

  if [ -n "${HOME:-}" ] && [ -d "$HOME" ]; then
    home_physical=$(CDPATH= cd -P "$HOME" 2>/dev/null && pwd -P) || home_physical=
    [ "$parent_physical" != "$home_physical" ] || private_parent_error "HOME itself is not allowed"
  fi

  checkout_physical=$(CDPATH= cd -P "$script_dir/.." 2>/dev/null && pwd -P) ||
    private_parent_error "checkout cannot be resolved"
  case "$parent_physical" in
    "$checkout_physical"|"$checkout_physical"/*)
      private_parent_error "the checkout and directories inside it are not allowed"
      ;;
  esac

  metadata=$(private_parent_metadata "$parent_physical") ||
    private_parent_error "directory ownership and mode cannot be inspected"
  set -- $metadata
  [ "$#" -eq 2 ] || private_parent_error "directory metadata is invalid"
  [ "$1" = "$(id -u)" ] || private_parent_error "directory must be owned by the current effective user"
  case "$2" in
    700|0700) ;;
    *) private_parent_error "directory mode must be exactly 0700" ;;
  esac

  printf '%s\n' "$parent_physical"
}

if [ -n "${AVA_LIVE_CODING_DOGFOOD_ROOT:-}" ]; then
  private_parent=$(validate_private_parent "$AVA_LIVE_CODING_DOGFOOD_ROOT") || exit 1
  root=$(mktemp -d "$private_parent/ava-live-coding-dogfood.XXXXXX") ||
    private_parent_error "could not allocate an evidence directory"
else
  root=$(mktemp -d "${TMPDIR:-/tmp}/ava-live-coding-dogfood.XXXXXX") ||
    private_parent_error "could not allocate an evidence directory"
fi
if ! chmod 700 "$root"; then
  rm -rf "$root"
  private_parent_error "could not make the evidence directory private"
fi

workspace="$root/workspace"
home_dir="$root/home"
config_dir="$root/config"
state_dir="$root/state"
data_dir="$root/data"
rpc_in="$root/rpc-input.fifo"
rpc_out="$root/rpc-output.jsonl"
rpc_err="$root/rpc-error.log"
replied_ids="$root/replied-permissions.txt"
combined_error="$root/combined-error.log"
target_file="$workspace/src/task.txt"
ava_pid=

cleanup()
{
  if [ -n "$ava_pid" ]; then kill "$ava_pid" 2>/dev/null || true; fi
  if [ -z "${AVA_LIVE_CODING_DOGFOOD_KEEP:-}" ]; then rm -rf "$root"; fi
}
trap cleanup EXIT INT TERM

evidence_note_printed=
print_evidence_note()
{
  if [ -n "${AVA_LIVE_CODING_DOGFOOD_KEEP:-}" ]; then
    if [ -z "$evidence_note_printed" ]; then
      echo "evidence_root=$root"
      evidence_note_printed=1
    fi
  else
    echo "set AVA_LIVE_CODING_DOGFOOD_KEEP=1 to retain RPC logs"
  fi
}
if [ -n "${AVA_LIVE_CODING_DOGFOOD_KEEP:-}" ]; then print_evidence_note; fi

mkdir -p "$workspace/src" "$workspace/.ava/skills/coding-smoke" "$home_dir" "$config_dir/ava" "$state_dir/ava" "$data_dir"
marker="AVA_LIVE_CODING_DONE_4172"
printf '%s\n' "# Live Coding Dogfood" "" "status: TODO" > "$target_file"
printf '%s\n' "live coding dogfood context" > "$workspace/AGENTS.md"
cat > "$workspace/.ava/skills/coding-smoke/SKILL.md" <<'EOF'
---
name: coding-smoke
description: Verify a coding edit by reading context, changing the requested file, and reporting only the marker.
---
Use this skill for live coding dogfood. Read the target file first, make the smallest exact edit requested by the user, and report only the requested marker.
EOF

workspace_json=$(json_escape "$workspace")
cat > "$state_dir/ava/project-trust.json" <<EOF
{"schema_version":1,"decisions":[{"path":"$workspace_json","trusted":true}]}
EOF

provider_json=$(json_escape "$provider")
model_json=$(json_escape "$model")
cat > "$config_dir/ava/models.json" <<EOF
{"default_provider":"$provider_json","default_model":"$model_json","models":[{"provider":"$provider_json","id":"$model_json","family":"live-coding-dogfood","context_window_tokens":8192,"max_output_tokens":2048,"supports_tools":true,"supports_streaming":false,"supports_reasoning":false,"reports_usage":true}]}
EOF

rm -f "$rpc_in" "$rpc_out" "$rpc_err" "$replied_ids" "$combined_error"
touch "$replied_ids"
mkfifo "$rpc_in"
(cd "$workspace" && HOME="$home_dir" XDG_CONFIG_HOME="$config_dir" XDG_STATE_HOME="$state_dir" XDG_DATA_HOME="$data_dir" NO_COLOR=1 "$AVA_EXE" --rpc --allow read-only) < "$rpc_in" > "$rpc_out" 2> "$rpc_err" &
ava_pid=$!
exec 3>"$rpc_in"

reply_permissions()
{
  if [ ! -s "$rpc_out" ]; then return 0; fi
  grep '"name":"permission_requested"' "$rpc_out" 2>/dev/null | while IFS= read -r line; do
    resolver_id=$(printf '%s\n' "$line" | sed -n 's/.*"resolver_request_id":"\([^"]*\)".*/\1/p')
    [ -n "$resolver_id" ] || continue
    if grep -qx "$resolver_id" "$replied_ids" 2>/dev/null; then continue; fi
    case "$line" in
      *'"tool_name":"skill"'*|*'"tool_name":"apply_patch"'*|*'"tool_name":"edit_file"'*|*'"tool_name":"write_file"'*)
        decision=allow
        reason="approved by live coding dogfood"
        ;;
      *)
        decision=deny
        reason="live coding dogfood only permits skill and file mutation tools"
        ;;
    esac
    printf '%s\n' "{\"id\":\"reply-$resolver_id\",\"type\":\"permission_reply\",\"request_id\":\"$resolver_id\",\"correlation_id\":\"prompt\",\"decision\":\"$decision\",\"reason\":\"$reason\"}" >&3
    echo "$resolver_id" >> "$replied_ids"
  done
}

printf '%s\n' "{\"id\":\"prompt\",\"type\":\"prompt\",\"protocol_version\":1,\"message\":\"Use the skill tool to load coding-smoke. Then use read_file to inspect src/task.txt. Then use apply_patch or edit_file to replace the line status: TODO with status: $marker. Do not use bash or network tools. Reply with exactly $marker and no other text.\"}" >&3

i=0
while ! grep -q '"final_text":"' "$rpc_out" 2>/dev/null; do
  reply_permissions
  if ! kill -0 "$ava_pid" 2>/dev/null; then
    cat "$rpc_out" "$rpc_err" > "$combined_error" 2>/dev/null || true
    classify_failure "$combined_error"
    echo "$label live coding dogfood exited before final assistant response"
    cat "$rpc_err" >&2 2>/dev/null || true
    print_evidence_note
    exit 1
  fi
  i=$((i + 1))
  if [ "$i" -gt 3600 ]; then
    cat "$rpc_out" "$rpc_err" > "$combined_error" 2>/dev/null || true
    classify_failure "$combined_error"
    echo "$label live coding dogfood timed out waiting for final assistant response"
    print_evidence_note
    exit 1
  fi
  sleep 0.05
done

reply_permissions
printf '%s\n' '{"id":"stats-after","type":"get_session_stats"}' >&3
printf '%s\n' '{"id":"validate-after","type":"validate_session"}' >&3
printf '%s\n' '{"id":"messages-after","type":"get_messages"}' >&3
exec 3>&-
wait "$ava_pid"
ava_status=$?
ava_pid=

if [ "$ava_status" -ne 0 ]; then
  cat "$rpc_out" "$rpc_err" > "$combined_error" 2>/dev/null || true
  classify_failure "$combined_error"
  echo "$label live coding dogfood ava --rpc exited with $ava_status"
  cat "$rpc_err" >&2 2>/dev/null || true
  print_evidence_note
  exit "$ava_status"
fi

if ! grep -qx "status: $marker" "$target_file"; then
  echo "classification=provider-behavior/inconclusive"
  echo "$label returned but did not persist the requested edit"
  print_evidence_note
  exit 2
fi

for needle in \
  '"id":"validate-after"' \
  '"ok":true' \
  '"tool":"skill"' \
  '"tool":"read_file"' \
  '"status":"success"' \
  '"name":"permission_requested"' \
  '"name":"permission_replied"' \
  '"decision":"allow"' \
  '"tool_name":"skill"'; do
  if ! grep -q "$needle" "$rpc_out"; then
    echo "classification=provider-behavior/inconclusive"
    echo "$label live coding dogfood missing expected RPC evidence: $needle"
    print_evidence_note
    exit 2
  fi
done

if ! grep -q '"tool":"apply_patch"' "$rpc_out" && ! grep -q '"tool":"edit_file"' "$rpc_out"; then
  echo "classification=provider-behavior/inconclusive"
  echo "$label edited the file without expected apply_patch/edit_file RPC evidence"
  print_evidence_note
  exit 2
fi

if ! grep -q "\"final_text\":\"$marker\"" "$rpc_out"; then
  echo "classification=provider-behavior/inconclusive"
  echo "$label did not return the exact coding marker"
  print_evidence_note
  exit 2
fi

session_file=
for candidate in "$state_dir"/ava/sessions/*/*.jsonl "$state_dir"/ava/sessions/*.jsonl; do
  if [ -f "$candidate" ]; then
    session_file="$candidate"
    break
  fi
done

if [ -z "$session_file" ]; then
  echo "classification=AVA regression"
  echo "live coding dogfood did not persist a session file"
  print_evidence_note
  exit 1
fi

for needle in \
  '"type":"assistant_output_item"' \
  '"type":"assistant_turn_commit"' \
  '"kind":"function_call"' \
  '"type":"tool_result"' \
  '"assistant_output_entry_id"' \
  '"type":"permission_decision"' \
  '"tool_name":"skill"' \
  '"operation":"edit"' \
  '"resolution":"allow"' \
  "$marker"; do
  if ! grep -q "$needle" "$session_file"; then
    echo "classification=AVA regression"
    echo "persisted session missing expected coding evidence: $needle"
    print_evidence_note
    exit 1
  fi
done

if ! grep -q '"tool_name":"apply_patch"' "$session_file" && ! grep -q '"tool_name":"edit_file"' "$session_file"; then
  echo "classification=AVA regression"
  echo "persisted session missing expected edit tool permission evidence"
  print_evidence_note
  exit 1
fi

if grep -q '"type":"assistant_message"' "$session_file" || grep -q '"type":"tool_call"' "$session_file"; then
  echo "classification=AVA regression"
  echo "new live turn unexpectedly used legacy v3 assistant/tool-call records"
  print_evidence_note
  exit 1
fi

echo "classification=passed"
echo "provider=$provider model=$model"
print_evidence_note
