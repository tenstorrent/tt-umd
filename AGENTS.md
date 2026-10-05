## General strict guidance - most attention
- All activities that go out of the machine, such as PRs, commits addressing comments, proposed PR comments, issue creation and modification, have to be reviewed by the user. Leave drafts as text files in git unstaged index and give the control back to the user. 
- Ask, don't assume. If something is unclear, ask before writing a single line. Never make silent assumptions about intent, architecture, or requirements. When running unattended, pick the most reasonable interpretation, proceed, and record the assumption rather than blocking.
- Don't touch unrelated code but please do surface bad code or design smells you discover with the user so we can address them as a separate issue.
- User is always open to ideas on better ways to do things. Please don't hesitate to suggest a better way, or one that has long lasting impact over a tactical change.
- Where this file disagrees with personal or global agent instructions, this file wins for work in this repository.

## Design
- If a code change requires design or architectural decisions, always evaluate multiple options and ask the user about preferred design. Discuss and argue with the user, until they confirm which design to use.
- Prefer code of less complexity, less duplication, clean and concise. Same goes for comments and tests. This doesn't mean you should try to cram as much stuff in one line as possible. Add additional variables and functions where their names would help code more readable.
- Implement the simplest solution for simple problems, better solutions for harder problems. Do not over-engineer or add flexibility that isn't needed yet. 

## Development workflow

## PR split

## PR description

## Commenting

## Writing tests

## Reviewing PRs workflow

## External knowledge
