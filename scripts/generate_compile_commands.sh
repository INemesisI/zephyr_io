#!/bin/bash
# Generate a merged compile_commands.json for all weave samples
# This enables clangd to work correctly across all samples

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="$PROJECT_ROOT/twister-out"
MERGED_OUTPUT="$PROJECT_ROOT/compile_commands.json"

# Module path for weave (now at project root)
WEAVE_MODULE="$PROJECT_ROOT"

echo "=== Building all weave samples ==="

# Source venv if it exists
[ -f "$PROJECT_ROOT/.venv/bin/activate" ] && source "$PROJECT_ROOT/.venv/bin/activate"

# Ensure west is available
if [ -x "$PROJECT_ROOT/.venv/bin/west" ]; then
    export PATH="$PROJECT_ROOT/.venv/bin:$PATH"
elif ! command -v west &> /dev/null; then
    echo "Error: west is not installed"
    exit 1
fi

# Set up environment
export ZEPHYR_EXTRA_MODULES="$WEAVE_MODULE"
export PYTHON_PREFER="$PROJECT_ROOT/.venv/bin/python3"
export CMAKE_PREFIX_PATH="$PROJECT_ROOT/.venv"

# Build all samples with twister (generates compile_commands.json for each)
west twister \
  -T samples \
  -p native_sim \
  -O "$OUTPUT_DIR" \
  --no-clean \
  --build-only \
  -j $(nproc)

echo ""
echo "=== Merging compile_commands.json files ==="

# Find all compile_commands.json files and merge them
"$PROJECT_ROOT/.venv/bin/python3" << 'PYTHON_SCRIPT'
import json
import sys
from pathlib import Path

project_root = Path(".")
output_dir = project_root / "twister-out"
merged_output = project_root / "compile_commands.json"

# Find all compile_commands.json files
compile_dbs = list(output_dir.rglob("compile_commands.json"))

if not compile_dbs:
    print("Error: No compile_commands.json files found in twister-out/", file=sys.stderr)
    sys.exit(1)

print(f"Found {len(compile_dbs)} compile_commands.json files")

# Merge all entries, using file path as key to avoid duplicates
merged = {}
for db_path in compile_dbs:
    print(f"  - {db_path.relative_to(project_root)}")
    with open(db_path) as f:
        entries = json.load(f)
        for entry in entries:
            # Use file path as key (last entry wins for duplicates)
            merged[entry["file"]] = entry

# Write merged output
with open(merged_output, "w") as f:
    json.dump(list(merged.values()), f, indent=2)

print(f"\nMerged {len(merged)} entries into {merged_output}")
PYTHON_SCRIPT

echo ""
echo "=== Done ==="
echo "compile_commands.json generated at: $MERGED_OUTPUT"
echo "Restart clangd or reload your editor to pick up the changes."
