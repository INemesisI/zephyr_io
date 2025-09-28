# Coverage Setup

## GitHub Actions Coverage

The repository includes automated coverage testing via GitHub Actions.

### What it does:
- Runs on every push to main/develop and on all PRs
- Tests only the `flow/subsys/flow/` module source code
- Generates coverage reports (XML and HTML)
- Comments on PRs with coverage metrics
- Enforces 90% minimum line coverage
- Uploads artifacts for detailed analysis

### Add a status badge to your README (optional):

```markdown
[![Coverage](https://github.com/YOUR_USERNAME/zephyr_io/actions/workflows/coverage.yml/badge.svg)](https://github.com/YOUR_USERNAME/zephyr_io/actions/workflows/coverage.yml)
```

### Current Coverage:
- **Lines**: 95.7%
- **Functions**: 100%
- **Branches**: 75.3%

### PR Comments:
The workflow automatically comments on PRs with:
- 🟢 Green indicator for ≥95% coverage
- 🟡 Yellow indicator for 90-94% coverage
- 🔴 Red indicator for <90% coverage

Coverage artifacts (HTML reports) are available for download from the workflow run page.