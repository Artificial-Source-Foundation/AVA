#!/bin/sh
# Redirect to the canonical install script at the repo root.
# Usage: curl -fsSL https://raw.githubusercontent.com/Artificial-Source/AVA/develop/install.sh | sh
exec sh "$(dirname "$0")/../install.sh" "$@"
