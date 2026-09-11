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
- PRs should be as small as possible to help reviewers keep the focus while reviewing scoped-down changes. Small doesn't necessarily mean low number of lines of code. Contextually small PRs can have a huge diff but with the same change (for example renaming a class). Do not combine multiple functional changes into a single PR, even if the total line count is small, as it is harder for a reviewer to take a grasp on the full combination of functional changes.
- But be careful not to overdo the PR split. Making 10 PRs for 10 related function changes with 5 lines each is making things less readable rather than just keeping the context per PR small. Use your judgement, and if unsure ask the user.
- To help with a meaningful PR split, PRs should match only one of these categories instead of mixing them: Feature, Performance, Bug fix, Cleanup, Test Only. Tests can be mixed with other categories if they are explicitly used to prove the changes in the same PR
- For changes which have a large scope, try to separate into multiple changes where each of those has a smaller scope
- The point of splitting changes into multiple PRs is to make them easier to review, if you estimate splitting will make overall review harder, don't split.
- Don't implement first then integrate into existing code. Rather, try to do integration first with empty implementation, then follow with implementation. That way we don't have intermediate dead code.
- If a cleanup is moving some implementation, make sure to remove old and add new implementation in same PR. That way it's clear from the diff implementation shouldn't change.
- When each of the PRs is prepared for the user to review, re-review your own diff and evaluate whether it makes sense to split the current diff in multiple PRs, and how would the split look like. Inform the user of your opinion when giving the control to them for review.
- If, when working on a PR change, there is some or several minor cleanup/rename to do in the same context, try to separate those in a different PR which would land right before or right after the current PR.

## PR description
- PR Description should follow PR template defined in this repo.
- Keep the description very brief and up to the point. For smaller PRs they can be one sentence. For larger ones keep them up to 15 lines.
- In both description and list of changes, avoid mentioning many items (files or function names) if there's more than 3 or 4 to be named, rather use a description for that whole category.
- If you need to explain the current state of code, those comments have no place in PR description, those should be commented directly in source files.

## Commenting

## Writing tests

## Reviewing PRs workflow

## External knowledge
