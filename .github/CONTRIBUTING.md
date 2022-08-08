# Contributing

This document describes how you can contribute to Kotatogram Desktop. Please read it carefully.

**Table of Contents**

* [What contributions are accepted](#what-contributions-are-accepted)
* [Contributing translations](#contributing-translations)
* [Build instructions](#build-instructions)
* [Pull upstream changes into your fork regularly](#pull-upstream-changes-into-your-fork-regularly)
* [How to get your pull request accepted](#how-to-get-your-pull-request-accepted)
  * [Keep your pull requests limited to a single issue](#keep-your-pull-requests-limited-to-a-single-issue)
    * [Squash your commits to a single commit](#squash-your-commits-to-a-single-commit)
  * [Don't mix code changes with whitespace cleanup](#dont-mix-code-changes-with-whitespace-cleanup)
  * [Keep your code simple!](#keep-your-code-simple)
  * [Test your changes!](#test-your-changes)
  * [Write a good commit message](#write-a-good-commit-message)

## What contributions are accepted

Before contribute to Kotatogram Desktop, you could try to contribute to Telegram Desktop in following cases:

* Bug fixes of original Telegram Desktop features
* Optimization of Telegram Desktop's source code and documentation

If your PR is merged into the official app, it will help both apps (as well as many other Telegram Desktop forks). Be sure to read its [CONTRIBUTING.md][tdesktop_contributing] before trying.

If your feature couldn't be added to official Telegram Desktop (e.g. new feature), you can try to [submit a pull request here][pr].

Following contributions are accepted to Kotatogram Desktop:

* Bug fixes and improvements
* New features

If you are submitting a new feature, please note that:

* It **must** be tested: we need these features to work, and work correctly.
* It **must not** confilct with exsiting features. If it's an alternative way of making this feature (e.g. showing text in other place), you should improve existing feature, and give user a choice unless there is no logical reasons for making a choice.
* It **must** look as good and refined as possible. I can accept some "dirty" solutions, but in that case they should be optional, and disabled by default. Still, too "dirty" solution won't be accepted.
* It **must not** violate [Telegram API Terms of Service][api_tos], e.g. features like Ghost Mode won't be implemented.

## Contributing translations

Translation contributions are currently accepted on [Crowdin](https://crowdin.com/project/kotatogram-desktop). Even though you can open an issue about translation here (e.g. requesting a new language) or submit a PR, Crowdin is the prefered way.

Please note: if you submit a PR with translation, it won't be merged. Instead it will be updated on Crowdin.

## Build instructions

Build instructions of Kotatogram Desktop are practically same, as Telegram Desktop's. See the [README.md][build_instructions] for details on the various build environments.

Of course, you should clone `https://github.com/kotatogram/kotatogram-desktop.git` instead of `https://github.com/telegramdesktop/tdesktop.git`.

## Pull upstream changes into your fork regularly

Kotatogram Desktop is advancing quickly. It is therefore critical that you pull upstream changes into your fork on a regular basis. Nothing is worse than putting in a days of hard work into a pull request only to have it rejected because it has diverged too far from upstream.

To pull in upstream changes:

    git remote add upstream https://github.com/kotatogram/kotatogram-desktop.git
    git fetch upstream master

Check the log to be sure that you actually want the changes, before merging:

    git log upstream/master

Then rebase your changes on the latest commits in the `master` branch:

    git rebase upstream/master

After that, you have to force push your commits:

    git push --force

For more info, see [GitHub Help][help_fork_repo].

## How to get your pull request accepted

We want to improve Kotatogram Desktop with your contributions. But we also want to provide a stable experience for our users and the community. Follow these rules and you should succeed without a problem!

### Keep your pull requests limited to a single issue

Pull requests should be as small/atomic as possible. Large, wide-sweeping changes in a pull request will be **rejected**, with comments to isolate the specific code in your pull request. Some examples:

* If you are making spelling corrections in the docs, don't modify other files.
* If you are adding new functions don't '*cleanup*' unrelated functions. That cleanup belongs in another pull request.

#### Squash your commits to a single commit

To keep the history of the project clean, you should make one commit per pull request.
If you already have multiple commits, you can add the commits together (squash them) with the following commands in Git Bash:

1. Open `Git Bash` (or `Git Shell`)
2. Enter following command to squash the recent {N} commits: `git reset --soft HEAD~{N} && git commit` (replace `{N}` with the number of commits you want to squash)
3. Press <kbd>i</kbd> to get into Insert-mode
4. Enter the commit message of the new commit
5. After adding the message, press <kbd>ESC</kbd> to get out of the Insert-mode
6. Write `:wq` and press <kbd>Enter</kbd> to save the new message or write `:q!` to discard your changes
7. Enter `git push --force` to push the new commit to the remote repository

For example, if you want to squash the last 5 commits, use `git reset --soft HEAD~5 && git commit`

### Don't mix code changes with whitespace cleanup

If you change two lines of code and correct 200 lines of whitespace issues in a file the diff on that pull request is functionally unreadable and will be **rejected**. Whitespace cleanups need to be in their own pull request.

### Keep your code simple!

Please keep your code as clean and straightforward as possible.
Furthermore, the pixel shortage is over. We want to see:

* `opacity` instead of `o`
* `placeholder` instead of `ph`
* `myFunctionThatDoesThings()` instead of `mftdt()`

### Test your changes!

Before you submit a pull request, please test your changes. Verify that Kotatogram Desktop still works and your changes don't cause other issue or crashes.

### Write a good commit message

* Explain why you make the changes. [More infos about a good commit message.][commit_message]

* If you fix an issue with your commit, please close the issue by [adding one of the keywords and the issue number][closing-issues-via-commit-messages] to your commit message.

  For example: `Fix #545`

[//]: # (LINKS)
[telegram]: https://telegram.org/
[help_fork_repo]: https://help.github.com/articles/fork-a-repo/
[help_change_commit_message]: https://help.github.com/articles/changing-a-commit-message/
[commit_message]: http://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html
[pr]: https://github.com/kotatogram/kotatogram-desktop/compare
[build_instructions]: https://github.com/telegramdesktop/tdesktop/blob/master/README.md#build-instructions
[tdesktop_contributing]: https://github.com/telegramdesktop/tdesktop/blob/master/.github/CONTRIBUTING.md
[closing-issues-via-commit-messages]: https://help.github.com/articles/closing-issues-via-commit-messages/
[api_tos]: https://core.telegram.org/api/terms
