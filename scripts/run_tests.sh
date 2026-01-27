#!/bin/bash
# Test runner for weave module
# Usage: ./run_tests.sh [twister_options]
# Example: ./run_tests.sh
#          ./run_tests.sh -v
#          ./run_tests.sh --coverage

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get the absolute path of the project root
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

# Source venv if it exists
[ -f ".venv/bin/activate" ] && source .venv/bin/activate

# Ensure west is available - prefer venv version
if [ -x ".venv/bin/west" ]; then
    export PATH="$PWD/.venv/bin:$PATH"
elif ! command -v west &> /dev/null; then
    echo -e "${RED}Error: west is not installed${NC}"
    echo "Install it with: pip install west"
    exit 1
fi

# Parse arguments - all args are twister options now
TWISTER_ARGS="$@"

# Function to print usage
usage() {
    echo "Usage: $0 [twister_options]"
    echo ""
    echo "Arguments:"
    echo "  twister_options - Any options to pass to Twister"
    echo ""
    echo "Examples:"
    echo "  $0"
    echo "  $0 -v"
    echo "  $0 --coverage"
    echo "  $0 -v --inline-logs"
    exit 1
}

# Check for help
if [ "$1" = "-h" ] || [ "$1" = "--help" ] || [ "$1" = "help" ]; then
    usage
fi

echo -e "${YELLOW}=== Running Weave Tests ===${NC}"

# Set up environment variables
export ZEPHYR_EXTRA_MODULES="$PROJECT_ROOT"
export PYTHON_PREFER="$PROJECT_ROOT/.venv/bin/python3"
export CMAKE_PREFIX_PATH="$PROJECT_ROOT/.venv"

# Run tests with Twister
echo -e "${BLUE}Running all weave tests and samples...${NC}"

west twister \
    -T tests \
    -T samples \
    -p native_sim \
    -O twister-out \
    --no-clean \
    $TWISTER_ARGS

result=$?

if [ $result -eq 0 ]; then
    echo -e "${GREEN}✓ All weave tests passed${NC}"
else
    echo -e "${RED}✗ Some weave tests failed${NC}"
fi

exit $result