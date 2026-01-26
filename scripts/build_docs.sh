#!/bin/bash
# Build Weave documentation (HTML and PDF)
#
# Usage: ./scripts/build_docs.sh [html|pdf|all]
#        Default: all (both HTML and PDF)
#
# Requirements:
#   pip install sphinx sphinx-rtd-theme
#   apt install latexmk texlive-latex-extra

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DOCS_DIR="$PROJECT_ROOT/libs/weave/docs"
BUILD_DIR="$PROJECT_ROOT/build/docs"

FORMAT="${1:-all}"

cd "$PROJECT_ROOT"

build_html() {
    echo "Building HTML..."
    .venv/bin/sphinx-build -b html "$DOCS_DIR" "$BUILD_DIR/html" -q
    echo "  $BUILD_DIR/html/index.html"
}

build_pdf() {
    echo "Building PDF..."
    .venv/bin/sphinx-build -b latex "$DOCS_DIR" "$BUILD_DIR/latex" -q
    make -C "$BUILD_DIR/latex" -s
    echo "  $BUILD_DIR/latex/weave.pdf"
}

case "$FORMAT" in
    html)
        build_html
        ;;
    pdf)
        build_pdf
        ;;
    all)
        build_html
        build_pdf
        ;;
    *)
        echo "Usage: $0 [html|pdf|all]"
        exit 1
        ;;
esac
