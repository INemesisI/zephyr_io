#!/bin/bash
# Run all weave tests and samples

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}=== Running Weave Tests ===${NC}"

# Get the absolute path of the project root (parent of scripts directory)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

# Set up environment variables
export ZEPHYR_EXTRA_MODULES="$PROJECT_ROOT/weave"
export PYTHON_PREFER="$PROJECT_ROOT/.venv/bin/python3"
export CMAKE_PREFIX_PATH="$PROJECT_ROOT/.venv"

# Run all weave tests and samples
echo -e "${YELLOW}Running all weave tests and samples...${NC}"
.venv/bin/python zephyr/scripts/twister \
  -T weave -p native_sim -v \
  -O twister-out --no-clean

# Check the results
if [ $? -eq 0 ]; then
    echo -e "${GREEN}=== All Weave Tests Passed ===${NC}"
    exit 0
else
    echo -e "${RED}=== Some Weave Tests Failed ===${NC}"
    exit 1
fi