<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Contributing to Duranta

We want to make contributing to this project as easy and transparent as possible.

1. Create an account on [GitHub](https://github.com/). Only contributions
   against [`duranta-project/openairinterface5g/`](https://github.com/duranta-project/openairinterface5g/)
   are accepted.
2. Fork the repository, and open pull requests for your contributions from your
   fork.
3. The contributing policies are described in the [corresponding documentation
   page](doc/code-style-contrib.md).
4. [Sign the CLA](https://github.com/duranta-project/governance/blob/main/docs/easy_cla_process.md)
   either before doing making your first pull request or after submitting the
   pull request.
5. Mandatory signing of all the commits using the email address used for CLA.

## Commit Guidelines

Every pull request must pass two CI checks before it can be merged:

1. **[Developer Certificate of Origin (DCO)](https://developercertificate.org/)**:
   Each commit must include a `Signed-off-by:` trailer in the commit message.
   Use `git commit -s` (or `--signoff`).

2. **[Verified commits](https://docs.github.com/en/authentication/managing-commit-signature-verification/about-commit-signature-verification)**:
   Each commit must be cryptographically signed using SSH or GPG keys to confirm
   its origin.

### Signing Commits

GitHub supports commit signing using either SSH keys or GPG keys. For the
step-by-step setup (key generation, Git configuration, registering the key on
GitHub, and verifying signatures locally), see the
[commit signing section of the Git guide](doc/git-guide.md#setting-up-commit-signing).

> **NOTE:** If your commits are not signed, the CI framework will not accept the PR.
For more information regarding contribution guidelines
please check [this document](doc/code-style-contrib.md)

## License

By contributing to Duranta, you agree that your contributions will be
licensed under

1. [CSSL v1.0 license](LICENSES/preferred/CSSL-v1.0.txt): for RAN and UE
   related source code and test scripts
2. [CC-BY-4.0](LICENSES/preferred/CC-BY-4.0.txt): All the documentation
3. [MIT](LICENSES/preferred/MIT.txt): Orchestration (helm-charts, docker
   compose)

Certain files are using different licenses; you can read about them in
[NOTICE](NOTICE).
