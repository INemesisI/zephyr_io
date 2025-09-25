#!/bin/bash
# Run all register_mapper tests

set -e

echo "Running register_mapper test suite..."
echo "======================================"

# Activate virtual environment if it exists (local development)
if [ -f ".venv/bin/activate" ]; then
    source .venv/bin/activate
fi

# Work around pytest-twister-harness plugin issue
alias pytest='python -m pytest'

# Set environment variables
export ZEPHYR_EXTRA_MODULES=$PWD/register_mapper
export PYTHON_PREFER=$PWD/.venv/bin/python3
export CMAKE_PREFIX_PATH=$PWD/.venv

# Run tests with Twister
python3 zephyr/scripts/twister \
    -T register_mapper/tests \
    -p native_sim \
    -v \
    -O twister-out \
    --no-clean \
    --verbose

echo ""
echo "Test suite complete!"