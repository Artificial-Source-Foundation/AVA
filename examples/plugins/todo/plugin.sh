#!/bin/sh

json_get_string() {
  key=$1
  line=$2
  rest=${line#*\"${key}\":\"}
  if [ "$rest" = "$line" ]; then
    printf '%s' ''
    return 0
  fi
  printf '%s' "${rest%%\"*}"
}

write_initialized() {
  printf '%s\n' '{"id":"ava_1","type":"initialized","api_version":"ava.plugin.v1","plugin_version":"0.1.0","contributions":{"tools":[],"commands":[],"prompts":[],"skills":[],"event_hooks":[]}}'
}

if ! IFS= read -r init_record; then
  exit 0
fi

if [ "$(json_get_string type "$init_record")" != "initialize" ]; then
  printf '%s\n' 'todo sample plugin expected initialize record first' >&2
  exit 2
fi

write_initialized

while IFS= read -r record; do
  id=$(json_get_string id "$record")
  type=$(json_get_string type "$record")

  case "$type" in
    command.call)
      command=$(json_get_string command "$record")
      if [ "$command" = "status" ]; then
        printf '{"id":"%s","type":"command.result","ok":true,"content":"Todo sample plugin is ready.","metadata":{"open_items":0}}\n' "$id"
      else
        printf '{"id":"%s","type":"command.result","ok":false,"content":"Unknown todo sample command.","metadata":{}}\n' "$id"
      fi
      ;;
    tool.call)
      tool=$(json_get_string tool "$record")
      if [ "$tool" = "todo_add" ]; then
        printf '{"id":"%s","type":"tool.result","ok":true,"content":"Todo item accepted by the sample plugin.","metadata":{"items":1}}\n' "$id"
      else
        printf '{"id":"%s","type":"tool.result","ok":false,"content":"Unknown todo sample tool.","metadata":{}}\n' "$id"
      fi
      ;;
    event.observe)
      printf '{"id":"%s","type":"event.observed","ok":true,"content":"Todo sample observed the event.","metadata":{"events":1}}\n' "$id"
      ;;
    cancel)
      exit 0
      ;;
    *)
      printf '%s\n' "todo sample plugin ignored unsupported record type: $type" >&2
      ;;
  esac
done
