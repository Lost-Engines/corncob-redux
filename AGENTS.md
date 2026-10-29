# Corncob development workflow

This instruction file is shared by `corncob-cpp` and `corncob-redux`.

## Keep Redux current with CPP

- `corncob-cpp/master` is the faithful upstream. `corncob-redux/master` must contain
  all upstream commits, with its deliberate enhancements maintained on top.
- When working in either repository, inspect both sibling repositories and their
  worktree status. Preserve unrelated local changes.
- Fetch current branches before starting work. In Redux, use an `upstream` remote
  pointing to `https://github.com/Lost-Engines/corncob-cpp.git`, alongside its own
  `origin`. Add that remote if the clone does not have it yet.
- Merge any missing CPP commits into Redux. Whenever new changes are committed to
  CPP, bring those commits into Redux during the same task, including changes
  whose equivalent fixes have already been applied there. Copying or cherry-picking
  fixes alone does not maintain the required ancestry.
- Prefer ordinary merge commits to preserve published history. Resolve conflicts
  deliberately, retaining Redux features and its distinct packaging, then run
  the checks appropriate to the combined change.
- Before finishing or pushing Redux, verify that the current CPP master is an
  ancestor of Redux HEAD, for example after fetching upstream:

  ```sh
  git merge-base --is-ancestor upstream/master HEAD
  git rev-list --count HEAD..upstream/master
  ```

  The first command must succeed and the second must print `0`. Also account for
  any CPP commits made locally during the task before they are pushed.
- Do not merge Redux enhancements back into the faithful CPP branch. Apply shared
  bug fixes in CPP and merge them forward into Redux.
- If synchronization cannot be completed, report the outstanding commits and
  concrete blocker rather than leaving the repositories silently out of sync.
