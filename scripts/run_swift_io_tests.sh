#!/bin/bash
# Run all swift_io tests

set -e

echo "Running swift_io test suite..."
echo "================================"

# Activate virtual environment if it exists (local development)
if [ -f ".venv/bin/activate" ]; then
    source .venv/bin/activate
fi

# Work around pytest-twister-harness plugin issue
alias pytest='python -m pytest'

# Set environment variables
export ZEPHYR_EXTRA_MODULES=$PWD/swift_io
export PYTHON_PREFER=$PWD/.venv/bin/python3
export CMAKE_PREFIX_PATH=$PWD/.venv

# Run tests with Twister
python3 zephyr/scripts/twister \
    -T swift_io \
    -p native_sim \
    -v \
    -O twister-out \
    --no-clean \
    --verbose

echo ""
echo "Test suite complete!"