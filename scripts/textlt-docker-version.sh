#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_file="$script_dir/../CMakeLists.txt"
version=$(sed -nE 's/^project\(textlt VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$project_file")

if [ -z "$version" ] || [ "$(printf '%s\n' "$version" | wc -l | tr -d ' ')" -ne 1 ]; then
    echo "Could not read one semantic version from $project_file" >&2
    exit 1
fi

printf 'v%s\n' "$version"
