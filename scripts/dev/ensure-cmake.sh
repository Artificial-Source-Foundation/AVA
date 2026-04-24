#!/usr/bin/env bash
set -euo pipefail

required_version="3.28.0"
pinned_version="3.28.6"

version_ge() {
  python3 - "$1" "$2" <<'PY'
import sys

def parts(value):
    return tuple(int(part) for part in value.split('.') if part.isdigit())

current = parts(sys.argv[1])
required = parts(sys.argv[2])
width = max(len(current), len(required))
current += (0,) * (width - len(current))
required += (0,) * (width - len(required))
sys.exit(0 if current >= required else 1)
PY
}

cmake_version() {
  "$1" --version | python3 -c 'import re, sys; m=re.search(r"version\s+([0-9.]+)", sys.stdin.read()); print(m.group(1) if m else "0")'
}

candidate="${CMAKE:-}"
if [[ -n "${candidate}" && -x "${candidate}" ]]; then
  version="$(cmake_version "${candidate}")"
  if version_ge "${version}" "${required_version}"; then
    printf '%s\n' "${candidate}"
    exit 0
  fi
fi

if command -v cmake >/dev/null 2>&1; then
  candidate="$(command -v cmake)"
  version="$(cmake_version "${candidate}")"
  if version_ge "${version}" "${required_version}"; then
    printf '%s\n' "${candidate}"
    exit 0
  fi
fi

if [[ "${AVA_BOOTSTRAP_CMAKE:-0}" == "1" ]]; then
  python3 -m pip install --user "cmake==${pinned_version}" >/dev/null
  user_bin="$(python3 - <<'PY'
import site
print(site.USER_BASE + '/bin')
PY
)"
  candidate="${user_bin}/cmake"
  if [[ -x "${candidate}" ]]; then
    version="$(cmake_version "${candidate}")"
    if version_ge "${version}" "${required_version}"; then
      printf '%s\n' "${candidate}"
      exit 0
    fi
  fi
fi

cat >&2 <<EOF
CMake ${required_version}+ is required for the C++ milestone workspace.
Install CMake ${pinned_version}, set CMAKE=/path/to/cmake, or rerun with AVA_BOOTSTRAP_CMAKE=1.
EOF
exit 1
