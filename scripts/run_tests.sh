#!/bin/bash
# Unified test runner for all modules
# Usage: ./run_tests.sh <module_name> [twister_options]
# Example: ./run_tests.sh flow
#          ./run_tests.sh weave -v
#          ./run_tests.sh flow --coverage

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

# Parse arguments
MODULE="${1}"
shift || true  # Remove module name from arguments, remaining are twister options
TWISTER_ARGS="$@"

# Function to print usage
usage() {
    echo "Usage: $0 <module_name> [twister_options]"
    echo ""
    echo "Arguments:"
    echo "  module_name    - Name of the module directory to test"
    echo "  twister_options - Any options to pass to Twister"
    echo ""
    echo "Examples:"
    echo "  $0 flow"
    echo "  $0 weave -v"
    echo "  $0 flow --coverage"
    echo "  $0 mymodule -v --inline-logs"
    exit 1
}

# Function to run tests for a specific module
run_module_tests() {
    local module_name=$1

    echo -e "${YELLOW}=== Running ${module_name} Tests ===${NC}"

    # Check if module directory exists
    if [ ! -d "$PROJECT_ROOT/$module_name" ]; then
        echo -e "${RED}Error: Module directory '$module_name' not found${NC}"
        return 1
    fi

    # Set up environment variables
    export ZEPHYR_EXTRA_MODULES="$PROJECT_ROOT/$module_name"
    export PYTHON_PREFER="$PROJECT_ROOT/.venv/bin/python3"
    export CMAKE_PREFIX_PATH="$PROJECT_ROOT/.venv"

    # Run tests with Twister
    echo -e "${BLUE}Running all $module_name tests and samples...${NC}"

    west twister \
        -T "$module_name" \
        -p native_sim \
        -O twister-out \
        --no-clean \
        $TWISTER_ARGS

    local result=$?

    if [ $result -eq 0 ]; then
        echo -e "${GREEN}✓ All $module_name tests passed${NC}"
    else
        echo -e "${RED}✗ Some $module_name tests failed${NC}"
        return $result
    fi

    return 0
}

# Main execution
if [ -z "$MODULE" ] || [ "$MODULE" = "-h" ] || [ "$MODULE" = "--help" ] || [ "$MODULE" = "help" ]; then
    usage
fi

# Check if module directory exists
if [ ! -d "$PROJECT_ROOT/$MODULE" ]; then
    echo -e "${RED}Error: Module directory '$MODULE' not found${NC}"
    echo ""
    echo "Available modules:"
    for dir in */; do
        if [ -f "${dir}zephyr/module.yml" ]; then
            echo "  ${dir%/}"
        fi
    done
    exit 1
fi

# Run the tests
run_module_tests "$MODULE"
exit $?