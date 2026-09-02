#! /bin/bash

if test -f .git; then
  echo "Error: don't run $0 inside a submodule. Run it from the parent project's directory."
  exit 1
fi

if test "$(realpath $0)" != "$(realpath $(pwd)/autogen.sh)"; then
  echo "Error: run autogen.sh from the directory it resides in."
  exit 1
fi

if test -d .git; then
  # Initialize missing submodules only at the commits pinned by this checkout.
  if ! SUBMODULE_STATUS="$(git submodule status --recursive)"; then
    echo "Error: unable to inspect pinned submodules."
    exit 1
  fi
  if printf '%s\n' "$SUBMODULE_STATUS" | grep -E '^[+U]' >/dev/null; then
    echo "Error: a submodule does not match the commit pinned by this checkout."
    echo "Run: git submodule update --init --checkout --recursive"
    exit 1
  fi
  if printf '%s\n' "$SUBMODULE_STATUS" | grep '^-' >/dev/null; then
    if ! git submodule update --init --checkout --recursive; then
      echo "Error: unable to initialize submodules at their pinned commits."
      exit 1
    fi
    if ! SUBMODULE_STATUS="$(git submodule status --recursive)"; then
      echo "Error: unable to verify initialized submodules."
      exit 1
    fi
  fi
  if printf '%s\n' "$SUBMODULE_STATUS" | grep -E '^[+U]' >/dev/null; then
    echo "Error: a submodule does not match the commit pinned by this checkout."
    exit 1
  fi
  if printf '%s\n' "$SUBMODULE_STATUS" | grep '^-' >/dev/null; then
    echo "Error: one or more pinned submodules remain uninitialized."
    exit 1
  fi
else
  if test ! -e cmake/aicxx/scripts/real_maintainer.sh; then
    echo "Houston, we have a problem: the cmake-aicxx git submodule is missing from your source tree!?"
    echo "I'd suggest to clone the source code of this project from github:"
    echo "git clone --recursive https://github.com/Artificial-Source/AVA.git"
    exit 1
  fi
fi

# Do some git sanity checks.
if test -d .git; then
  PUSH_RECURSESUBMODULES="$(git config --get push.recurseSubmodules)"
  CONFIG_RET=$?
  if test $CONFIG_RET -ne 0 && test $CONFIG_RET -ne 1; then
    echo "Error: unable to inspect push.recurseSubmodules configuration."
    exit 1
  fi
  if test -z "$PUSH_RECURSESUBMODULES"; then
    # Use this as default for now.
    if ! git config push.recurseSubmodules check; then
      echo "Error: unable to set push.recurseSubmodules safety default."
      exit 1
    fi
    echo -e "\n*** WARNING: git config push.recurseSubmodules was not set!"
    echo "***      To prevent pushing a project that references unpushed submodules,"
    echo "***      this config was set to 'check'. Use instead the command"
    echo "***      > git config push.recurseSubmodules on-demand"
    echo "***      to automatically push submodules when pushing a reference to them."
    echo "***      See http://stackoverflow.com/a/10878273/1487069 and"
    echo "***      http://stackoverflow.com/a/34615803/1487069 for more info."
    echo
  fi
fi

if [ -e CMakeLists.txt ]; then
  # Set CMAKE_CONFIG to '$CMAKE_CONFIG' if not already set.
  : "${CMAKE_CONFIG:=\$CMAKE_CONFIG}"
  # Set BUILDDIR to '$BUILDDIR' if not already set.
  : "${BUILDDIR:=\$BUILDDIR}"

  echo -e "\nBuilding with cmake:\n"
  echo "To make a $CMAKE_CONFIG build, run:"
  [ -d "$BUILDDIR" ] || echo "mkdir -p \$BUILDDIR"
  echo -n "cmake -S \"\$REPOROOT\" -B \"\$BUILDDIR\" -DCMAKE_BUILD_TYPE=\"$CMAKE_CONFIG\""
  # Put quotes around options that contain spaces.
  for option in "${CMAKE_CONFIGURE_OPTIONS[@]}"; do
    if [[ $option == *" "* ]]; then
        printf ' "%s"' "$option"
    else
        printf ' %s' "$option"
    fi
  done
  echo
  echo "cmake --build \"\$BUILDDIR\" --config \"$CMAKE_CONFIG\" --parallel $(nproc)"
fi
