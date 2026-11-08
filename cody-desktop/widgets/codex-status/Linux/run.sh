#!/bin/sh
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ ! -x "$script_dir/../backend/OpenAIUsage.Backend" ]; then
    command -v dotnet >/dev/null 2>&1 || { echo '.NET 10 runtime is required for this source build.' >&2; exit 1; }
fi
command -v codex >/dev/null 2>&1 || echo 'Codex is not on PATH; set its executable in Settings.' >&2
exec python3 "$script_dir/openai_usage.py" "$@"
