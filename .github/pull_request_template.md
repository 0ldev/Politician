## What does this PR do?

<!-- A clear, one-paragraph description of the change and why it's needed. -->

## Type of change

- [ ] Bug fix
- [ ] New feature / capture primitive
- [ ] New example
- [ ] Documentation / translation
- [ ] CI / workflow change
- [ ] Performance improvement
- [ ] Other (describe below)

## Checklist

- [ ] I've read [`CONTRIBUTING.md`](../CONTRIBUTING.md)
- [ ] My branch is based off `develop`, not `main`
- [ ] All existing examples still compile (`pio run` in each example directory)
- [ ] If I added a new example, I added it to the CI matrix in `build-examples.yml`
- [ ] If I added a new example, I added it to the examples table in `README.md`
- [ ] If I changed the public API, I updated the relevant docstrings/documentation

## CI / workflow changes (fill in if applicable)

- [ ] All new/modified `uses:` actions are pinned to an immutable **commit SHA** (not a floating tag like `@v4`)
- [ ] Artifact upload steps include `if: github.event_name == 'push'`
- [ ] No new `pull_request_target:` triggers

## Testing

<!-- How did you test this? What hardware/environment? -->

## Related issues

<!-- Closes #XX -->
