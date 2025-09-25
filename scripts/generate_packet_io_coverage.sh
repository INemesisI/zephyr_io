#!/bin/bash
# Generate coverage report from all packet_io tests
# Combines coverage from unit tests, integration tests, and runtime tests

set -e

# Activate virtual environment if it exists (local development)
if [ -f ".venv/bin/activate" ]; then
    source .venv/bin/activate
fi

# Work around pytest-twister-harness plugin issue
alias pytest='python -m pytest'

# Set environment variables
export ZEPHYR_EXTRA_MODULES=$PWD/packet_io
export PYTHON_PREFER=$PWD/.venv/bin/python3
export CMAKE_PREFIX_PATH=$PWD/.venv

# Clean up old coverage data
rm -rf twister-combined-coverage coverage-work

# Create work directory
mkdir -p coverage-work

echo "Generating packet_io coverage report..."
echo "========================================"

python3 zephyr/scripts/twister \
    --coverage \
    -p native_sim \
    -T packet_io/tests \
    -v \
    -O twister-combined-coverage \
    --no-clean

# Check if coverage data was generated
if [[ ! -d "twister-combined-coverage" ]]; then
    echo "Error: No coverage data found"
    exit 1
fi

# Find and copy all .gcda and .gcno files to work directory
find twister-combined-coverage -name "*.gcda" -o -name "*.gcno" | while read file; do
    # Get the base filename
    base=$(basename "$file")
    # Copy to work directory with test suite prefix to avoid overwrites
    testname=$(echo "$file" | sed 's|.*/\(packet_io\.[^/]*\)/.*|\1|')
    if [[ "$file" == *.gcda ]]; then
        cp "$file" "coverage-work/${testname}_${base}" 2>/dev/null || true
    else
        # For .gcno files, we only need one copy
        cp "$file" "coverage-work/${base}" 2>/dev/null || true
    fi
done

# Generate combined coverage report
echo ""
echo "Coverage Report"
echo "---------------"

# Run gcovr directly on the twister output directory
cd twister-combined-coverage
gcovr --filter=".*packet_io/subsys/packet_io/.*" --print-summary \
      --txt-metric branch --gcov-ignore-errors=no_working_dir_found \
      . 2>/dev/null || echo "Error generating coverage report"
cd - >/dev/null


# Show individual test contributions
echo ""
echo "Test contributions:"
for test_dir in twister-combined-coverage/native_sim/*/; do
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
echo "Coverage includes:"
echo "  • Unit tests"
echo "  • Integration tests"
echo "  • Runtime tests"

# Clean up work directory
rm -rf coverage-work

echo ""
echo "Coverage report complete!"