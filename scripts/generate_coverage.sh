#!/bin/bash
# Unified coverage report generator for all modules
# Usage: ./generate_coverage.sh <module_name> [twister_options]
# Example: ./generate_coverage.sh weave
#          ./generate_coverage.sh weave -v
#          ./generate_coverage.sh weave --filter unit_test

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
    echo "  module_name     - Name of the module directory to generate coverage for"
    echo "  twister_options - Any additional options to pass to Twister"
    echo ""
    echo "Examples:"
    echo "  $0 weave"
    echo "  $0 weave -v"
    echo "  $0 weave --filter unit_test"
    echo ""
    echo "Note: Requires gcovr to be installed (pip install gcovr)"
    exit 1
}

# Main execution
if [ -z "$MODULE" ] || [ "$MODULE" = "-h" ] || [ "$MODULE" = "--help" ] || [ "$MODULE" = "help" ]; then
    usage
fi

# Check if module directory exists (support both root and libs/ locations)
MODULE_PATH=""
if [ -d "$PROJECT_ROOT/$MODULE" ]; then
    MODULE_PATH="$MODULE"
elif [ -d "$PROJECT_ROOT/libs/$MODULE" ]; then
    MODULE_PATH="libs/$MODULE"
else
    echo -e "${RED}Error: Module directory '$MODULE' not found${NC}"
    echo ""
    echo "Available modules:"
    for dir in */; do
        if [ -f "${dir}zephyr/module.yml" ]; then
            echo "  ${dir%/}"
        fi
    done
    for dir in libs/*/; do
        if [ -f "${dir}zephyr/module.yml" ]; then
            echo "  libs/${dir#libs/}"
            echo "  ${dir#libs/}" | sed 's|/$||'  # Also show short name
        fi
    done 2>/dev/null || true
    exit 1
fi

# Check if gcovr is installed
if ! command -v gcovr &> /dev/null; then
    echo -e "${RED}Error: gcovr is not installed${NC}"
    echo "Install it with: pip install gcovr"
    exit 1
fi

echo -e "${YELLOW}=== Generating Coverage Report for $MODULE ===${NC}"

# Set up environment variables (only if not already set by CI)
export ZEPHYR_EXTRA_MODULES="${ZEPHYR_EXTRA_MODULES:-$PROJECT_ROOT/$MODULE_PATH}"
export PYTHON_PREFER="${PYTHON_PREFER:-$PROJECT_ROOT/.venv/bin/python3}"
export CMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-$PROJECT_ROOT/.venv}"

# Define coverage output directory - use consistent twister-out like run_tests.sh
COVERAGE_DIR="twister-out"

# Note: Not cleaning up to preserve build artifacts and speed up incremental builds
# The --no-clean flag will handle this for us

# Run tests with coverage enabled
echo -e "${BLUE}Running $MODULE tests with coverage...${NC}"

west twister \
    --coverage \
    -p native_sim \
    -T "$MODULE_PATH" \
    -O "$COVERAGE_DIR" \
    --no-clean \
    $TWISTER_ARGS

# Check if coverage data was generated
if [ ! -d "$COVERAGE_DIR" ]; then
    echo -e "${RED}Error: Coverage data not generated${NC}"
    exit 1
fi

echo ""
echo -e "${YELLOW}Generating coverage reports for $MODULE...${NC}"

# Generate coverage reports directly from the test output directory
cd "$COVERAGE_DIR"

# Determine source filter pattern - check for both src/ and subsys/ directories
if [ -d "../${MODULE_PATH}/src" ]; then
    SOURCE_FILTER="../${MODULE_PATH}/src/.*\.c$"
elif [ -d "../${MODULE_PATH}/subsys" ]; then
    SOURCE_FILTER="../${MODULE_PATH}/subsys/.*\.c$"
else
    # Fallback to any .c file in the module
    SOURCE_FILTER="../${MODULE_PATH}/.*\.c$"
fi

echo -e "${BLUE}Using source filter: ${SOURCE_FILTER}${NC}"

# Pattern to exclude LOG_* macro branches from coverage
LOG_BRANCH_EXCLUDE='.*LOG_(DBG|INF|WRN|ERR|HEXDUMP_\w+)\s*\(.*'

# Generate text summary
echo ""
echo -e "${BLUE}=== $MODULE Coverage Report (Combined) ===${NC}"
gcovr \
    --root ../ \
    --filter "${SOURCE_FILTER}" \
    --exclude-directories "../${MODULE_PATH}/tests" \
    --exclude-directories "../${MODULE_PATH}/samples" \
    --exclude-branches-by-pattern "${LOG_BRANCH_EXCLUDE}" \
    --print-summary \
    --txt-metric branch \
    --gcov-ignore-errors=no_working_dir_found \
    . 2>/dev/null || echo "Error generating coverage report"

# Generate HTML report
echo ""
echo "Generating HTML report..."
gcovr \
    --root ../ \
    --filter "${SOURCE_FILTER}" \
    --exclude-directories "../${MODULE_PATH}/tests" \
    --exclude-directories "../${MODULE_PATH}/samples" \
    --exclude-branches-by-pattern "${LOG_BRANCH_EXCLUDE}" \
    --html-details coverage-${MODULE}.html \
    --gcov-ignore-errors=no_working_dir_found \
    . 2>/dev/null || true

# Generate XML report for CI
echo "Generating XML report..."
gcovr \
    --root ../ \
    --filter "${SOURCE_FILTER}" \
    --exclude-directories "../${MODULE_PATH}/tests" \
    --exclude-directories "../${MODULE_PATH}/samples" \
    --exclude-branches-by-pattern "${LOG_BRANCH_EXCLUDE}" \
    --xml coverage-${MODULE}.xml \
    --gcov-ignore-errors=no_working_dir_found \
    . 2>/dev/null || true

# Generate summary to a file for CI
echo "Generating coverage summary..."
gcovr \
    --root ../ \
    --filter "${SOURCE_FILTER}" \
    --exclude-directories "../${MODULE_PATH}/tests" \
    --exclude-directories "../${MODULE_PATH}/samples" \
    --exclude-branches-by-pattern "${LOG_BRANCH_EXCLUDE}" \
    --print-summary \
    --txt-metric branch \
    --gcov-ignore-errors=no_working_dir_found \
    . > coverage-summary-${MODULE}.txt 2>/dev/null || true

cd ..

# Show test contributions
echo ""
echo "Test contributions:"
for test_dir in "$COVERAGE_DIR"/native_sim/*/; do
    if [[ -d "$test_dir" ]]; then
        test_name=$(basename "$test_dir")
        echo -n "  - $test_name: "
        # Check if test has coverage data
        if find "$test_dir" -name "*.gcda" 2>/dev/null | grep -q .; then
            echo "✓ (has coverage data)"
        else
            echo "✗ (no coverage data)"
        fi
    fi
done

# Summary
echo ""
echo -e "${GREEN}Coverage report complete!${NC}"
echo "Reports generated in: $COVERAGE_DIR/"
echo "  - HTML: coverage-${MODULE}.html"
echo "  - XML: coverage-${MODULE}.xml"
echo "  - Summary: coverage-summary-${MODULE}.txt"