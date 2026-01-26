# Weave Documentation - Sphinx Configuration

from pathlib import Path

DOCS_DIR = Path(__file__).parent
ZEPHYR_BASE = DOCS_DIR.parent.parent.parent / "zephyr"  # libs/weave/docs -> project root

project = 'Weave'
copyright = '2024'
author = 'Benedikt Eliasson'

# Extensions
extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.todo',
]

# Theme
html_theme = 'sphinx_rtd_theme'

# Exclude build output
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']

# Source suffix
source_suffix = '.rst'

# Master doc
master_doc = 'index'

# -- Options for LaTeX output (Zephyr style) ---------------------------------

latex_elements = {
    "papersize": "a4paper",
    # Use our own title (Zephyr's has logo dependencies)
    "maketitle": (DOCS_DIR / "_static" / "latex" / "title.tex").read_text(),
    # Zephyr-inspired styling (inline to avoid babel/polyglossia conflict)
    "preamble": r"""
\usepackage{sectsty}
\definecolor{zephyr-blue}{HTML}{333f67}
\setcounter{tocdepth}{2}
\allsectionsfont{\color{zephyr-blue}}
""",
    "makeindex": r"\usepackage[columns=1]{idxlayout}\makeindex",
    "fontpkg": r"""
        \usepackage{fontspec}
        \setmainfont{Roboto}
        \setsansfont{Roboto}
        \setmonofont{JetBrains Mono}
    """,
    "sphinxsetup": ",".join([
        "verbatimwithframe=false",
        "VerbatimColor={HTML}{f0f2f4}",
        "InnerLinkColor={HTML}{2980b9}",
        "warningBgColor={HTML}{e9a499}",
        "warningborder=0pt",
        r"HeaderFamily=\rmfamily\bfseries",
    ]),
}

latex_documents = [
    ("index", "weave.tex", "Weave Documentation", author, "manual"),
]

latex_engine = "xelatex"
