#!/bin/sh
# AVA Uninstaller
# Removes the AVA binary and PATH entries. Config/data at ~/.ava/ is preserved.
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

printf "Removing AVA...\n"

# Remove binary
rm -f "$HOME/.ava/bin/ava"
printf "${GREEN}✓${NC} Removed binary\n"

# Remove PATH entries from shell profiles
for rcfile in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.bash_profile" "$HOME/.profile"; do
    if [ -f "$rcfile" ]; then
        if grep -q '# AVA' "$rcfile" 2>/dev/null || grep -q '\.ava/bin' "$rcfile" 2>/dev/null; then
            # macOS sed requires -i '' while GNU sed uses -i alone; use temp file for portability
            grep -v '# AVA' "$rcfile" | grep -v '\.ava/bin' > "${rcfile}.ava_tmp" || true
            mv "${rcfile}.ava_tmp" "$rcfile"
        fi
    fi
done

# Fish shell
fish_config="$HOME/.config/fish/config.fish"
if [ -f "$fish_config" ]; then
    if grep -q 'ava/bin' "$fish_config" 2>/dev/null; then
        grep -v '# AVA' "$fish_config" | grep -v 'ava/bin' > "${fish_config}.ava_tmp" || true
        mv "${fish_config}.ava_tmp" "$fish_config"
    fi
fi

printf "${GREEN}✓${NC} Removed PATH entries\n"

# Remove bin directory if empty
if [ -d "$HOME/.ava/bin" ]; then
    rmdir "$HOME/.ava/bin" 2>/dev/null || true
fi

printf "\nAVA has been uninstalled.\n"
printf "Config and data remain at ~/.ava/ — delete manually if desired.\n"
