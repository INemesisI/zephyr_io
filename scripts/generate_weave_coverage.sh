#!/bin/bash
# Generate coverage report for weave module

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${YELLOW}=== Generating Weave Coverage Report ===${NC}"

# Get the absolute path of the project root (parent of scripts directory)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

# Set up environment variables
export ZEPHYR_EXTRA_MODULES="$PROJECT_ROOT/weave"
export PYTHON_PREFER="$PROJECT_ROOT/.venv/bin/python3"
export CMAKE_PREFIX_PATH="$PROJECT_ROOT/.venv"

# Check if gcovr is installed
if ! command -v gcovr &> /dev/null; then
    echo -e "${RED}Error: gcovr is not installed. Install it with: pip install gcovr${NC}"
    exit 1
fi

# Clean previous coverage data
echo -e "${YELLOW}Cleaning previous coverage data...${NC}"
rm -rf twister-coverage-weave

# Run tests with coverage enabled
echo -e "${YELLOW}Running weave tests with coverage...${NC}"
.venv/bin/python zephyr/scripts/twister \
  --coverage -p native_sim -T weave -v \
  -O twister-coverage-weave --no-clean

# Check if coverage data was generated
if [ ! -d "twister-coverage-weave" ]; then
    echo -e "${RED}Error: Coverage data not generated${NC}"
    exit 1
fi

# Generate coverage report using gcovr
echo -e "${YELLOW}Generating coverage report...${NC}"
cd twister-coverage-weave

# Generate both terminal and HTML reports
gcovr \
  --root ../ \
  --filter '../weave/subsys/.*\.c$' \
  --exclude-directories '../weave/tests' \
  --exclude-directories '../weave/samples' \
  --print-summary \
  --html-details coverage.html \
  . 2>/dev/null || true

# Also generate a simple text summary
echo -e "${BLUE}=== Weave Coverage Report ===${NC}"
gcovr \
  --root ../ \
  --filter '../weave/subsys/.*\.c$' \
  --exclude-directories '../weave/tests' \
  --exclude-directories '../weave/samples' \
  . 2>/dev/null | tail -3

cd ..

echo -e "${GREEN}Coverage report generated at: twister-coverage-weave/coverage.html${NC}"

# Display summary
echo -e "${YELLOW}=== Coverage Summary ===${NC}"
echo "HTML Report: file://$PROJECT_ROOT/twister-coverage-weave/coverage.html"
echo "To view in browser: firefox twister-coverage-weave/coverage.html"