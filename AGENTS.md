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
- Always first present a plan to the user, layout the design and architecture and how you plan to separate PRs. Discuss and argue with the user. Continue with execution upon confirmation.
- If the request is very direct, there is not much for discussion, and all fits into single PR, you can skip the planning phase. Use your judgement.
- Once you start execution, you should implement PRs one by one. 
- Changes drafted for a PR should be left in git staged index. Also leave a PR description draft in git unstaged index. Let user review it, once it is confirmed you should continue with committing the change on a new branch, pushing, and creating a PR. Do this for each PR in this stack of PRs.
- Before giving the control back to the user, run pre-commit checks using `pre-commit run --all-files`, and build the whole repo using `cmake -B build -G Ninja -DTT_UMD_BUILD_ALL=ON; cmake --build build`. Don't run any tests.
- Before starting work, make sure you're on main, and fetch the latest one. If there are already some uncommitted changes in the repo, report to the user and ask what to do.
- Once you finish the PR stack of changes, ask the user if they want to link them in a github stack (using gh stack).
- Once you finish the PR stack of changes go through each of the created PRs, and add a new section in the PR description at the end "### Full stack diff" and add a compare link between last branch and main, so full diff can be easily accessed from any PR.

## PR split

## PR description

## Commenting

## Writing tests

## Reviewing PRs workflow

## External knowledge
