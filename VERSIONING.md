# Versioning Policy

## Pre-1.0.0 Development Phase

UMD follows a modified versioning scheme during initial development (major version 0). Once version 1.0.0 is reached, we will adopt [Semantic Versioning 2.0.0](https://semver.org/).

### Version Format: `0.a.b.c`

- **`c` (Patch)** - Incremented ad-hoc for:
  - PyPI releases to fix Python client CI tests.
  - Urgent bugfixes.
  - Format: `YYYYMMDD` date integer

- **`b` (Minor)** - Incremented for new features.
  - Requires an open GitHub issue documenting which features are required to be complete for the version to be created.
  - Issues must be linked and closed before creating the version.
  - Issues can be T-shirt size XS to M.

- **`a` (Pre-major)** - Incremented on milestones.
  - Requires a defined milestone GitHub issue.
  - Examples: large API changes, new protocols, new architecture support.

### Important Notes

- All version changes MAY include breaking API changes.
- This scheme is temporary and will be replaced by Semantic Versioning at 1.0.0.
