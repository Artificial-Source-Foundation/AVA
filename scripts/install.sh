#!/bin/sh
# Redirect to the canonical install script at the repo root.
# Usage: curl -fsSL https://raw.githubusercontent.com/ASF-GROUP/AVA/master/install.sh | sh
exec sh "$(dirname "$0")/../install.sh" "$@"
