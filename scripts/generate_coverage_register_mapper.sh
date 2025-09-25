#!/bin/bash
# Generate coverage report for register_mapper tests

set -e

# Activate virtual environment if it exists (local development)
if [ -f ".venv/bin/activate" ]; then
    source .venv/bin/activate
fi

# Check if we're in the correct directory
if [[ ! -f "register_mapper/zephyr/module.yml" ]]; then
    echo "Error: Please run this script from the zephyr_io project root"
    exit 1
fi

echo "Generating register_mapper coverage report..."
echo "============================================="

# Work around pytest-twister-harness plugin issue
alias pytest='python -m pytest'

# Set environment variables
export ZEPHYR_EXTRA_MODULES=$PWD/register_mapper
export PYTHON_PREFER=$PWD/.venv/bin/python3
export CMAKE_PREFIX_PATH=$PWD/.venv

# Clean up old coverage data
rm -rf twister-register-mapper-coverage coverage-work-register-mapper

# Create work directory
mkdir -p coverage-work-register-mapper

# Run tests with coverage
python3 zephyr/scripts/twister \
    --coverage \
    -p native_sim \
    -T register_mapper/tests \
    -v \
    -O twister-register-mapper-coverage \
    --no-clean

# Check if coverage data was generated
if [[ ! -d "twister-register-mapper-coverage" ]]; then
    echo "Error: No coverage data found"
    exit 1
fi

# Find and copy all .gcda and .gcno files to work directory
find twister-register-mapper-coverage -name "*.gcda" -o -name "*.gcno" | while read file; do
    # Get the base filename
    base=$(basename "$file")
    # Copy to work directory
    cp "$file" "coverage-work-register-mapper/${base}" 2>/dev/null || true
done

# Generate coverage report
echo ""
echo "Coverage Report"
echo "---------------"

# Run gcovr directly on the twister output directory
cd twister-register-mapper-coverage
gcovr --filter=".*register_mapper/subsys/register_mapper/.*" --print-summary \
      --txt-metric branch --gcov-ignore-errors=no_working_dir_found \
      . 2>/dev/null || echo "Error generating coverage report"
cd - >/dev/null


# Show test contributions
echo ""
echo "Test contributions:"
for test_dir in twister-register-mapper-coverage/native_sim/*/; do
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
echo "  • Basic functionality tests"
echo "  • Concurrency tests"
echo "  • Advanced edge case tests"

# Clean up work directory
rm -rf coverage-work-register-mapper

echo ""
echo "Coverage report complete!"