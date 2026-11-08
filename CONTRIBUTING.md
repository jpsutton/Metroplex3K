# Contributing

- Branch from `staging` (`feat/<topic>` or `fix/<topic>`) and open a pull request into `staging`. CI runs the fast checks.
  Releases are cut from `staging` into `release`; see [docs/RELEASING.md](docs/RELEASING.md).
- Run `tools/ci/static-checks.sh` and the unit tests (`ctest`) before pushing.
- Every `.cpp` and `.h` file starts with the SPDX header used throughout the tree. Contributions are accepted under
  GPL-3.0-or-later.
- This is a clean-room project. Do not paste code, addresses, internal names or text from the original program into
  source, comments, the spec or issues. The rules are in [spec/00-README.md](spec/00-README.md) and
  [docs/IP-REVIEW.md](docs/IP-REVIEW.md); the static checks enforce the mechanical ones.
- Known gaps are tracked as GitHub issues (see STATUS.md for the labels).
