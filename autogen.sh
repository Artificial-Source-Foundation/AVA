#! /bin/bash

# Optional helper for initializing pinned submodules and printing build guidance.

if test -f .git; then
  echo "Error: don't run $0 inside a submodule. Run it from the parent project's directory."
  exit 1
fi

if test "$(realpath $0)" != "$(realpath $(pwd)/autogen.sh)"; then
  echo "Error: run autogen.sh from the directory it resides in."
  exit 1
fi

if test -d .git; then
  # Take care of git submodule related stuff.
  # If this was a clone without --recursive, fix that fact.
  if test ! -e cmake/aicxx/scripts/real_maintainer.sh; then
    if ! git submodule update --init --checkout --recursive; then
      echo "Error: could not initialize AVA's pinned submodules."
      exit 1
    fi
  fi
  # If new git submodules were added by someone else, get them.
  if git submodule status --recursive | grep '^-' >/dev/null; then
    if ! git submodule update --init --checkout --recursive; then
      echo "Error: could not initialize AVA's pinned submodules."
      exit 1
    fi
  fi
  if git submodule status --recursive | grep -E '^[+U]' >/dev/null; then
    echo "Error: a submodule is checked out at a revision other than AVA's pinned commit."
    echo "Run: git submodule update --init --checkout --recursive"
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
  PUSH_RECURSESUBMODULES="$(git config push.recurseSubmodules)"
  if test -z "$PUSH_RECURSESUBMODULES"; then
    # Use this as default for now.
    git config push.recurseSubmodules check
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
