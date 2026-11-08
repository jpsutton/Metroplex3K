## Release x.y.z

Single pull request from `staging` to `release`. See docs/RELEASING.md.

- [ ] Version in `CMakeLists.txt` is x.y.z and `CHANGELOG.md` has a dated `## [x.y.z]` section
- [ ] `release-gate/x.y.z.md` is committed and ends with `Result: PASS` (`tools/release-gate.sh`)
- [ ] No code changed after the gate ran (CI checks this)
- [ ] Known limitations in the changelog are current
- [ ] Merge with a merge commit; the push to `release` publishes the tag and the GitHub release
