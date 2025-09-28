#!/bin/bash
# Run all flow tests

set -e

echo "Running flow test suite..."
echo "================================"

# Activate virtual environment if it exists (local development)
if [ -f ".venv/bin/activate" ]; then
    source .venv/bin/activate
fi

# Work around pytest-twister-harness plugin issue
alias pytest='python -m pytest'

# Set environment variables
export ZEPHYR_EXTRA_MODULES=$PWD/flow
export PYTHON_PREFER=$PWD/.venv/bin/python3
export CMAKE_PREFIX_PATH=$PWD/.venv

# Run tests with Twister
python3 zephyr/scripts/twister \
    -T flow \
    -p native_sim \
    -v \
    -O twister-out \
    --no-clean \
    --verbose

echo ""
echo "Test suite complete!"