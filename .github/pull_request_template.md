## What this changes

<!-- One or two sentences. What did not work before, and what works now. -->

## Why

<!-- Link the issue, or explain the problem. For a new CSS or DOM feature,
     show the markup you want to be able to write. -->

Closes #

## How it was verified

<!-- Which tests you added, and what you ran. -->

- [ ] `ctest --preset x64-windows-release` passes
- [ ] New tests cover the change (unit, golden, or PlayMode)
- [ ] Golden images, if regenerated, were looked at before committing

## Checklist

- [ ] The code this change touches is `clang-format` clean; comments and identifiers in English
- [ ] [docs/css-support.md](https://github.com/Rovniy/xploit_game_ui/blob/main/docs/css-support.md) updated if this adds a feature or changes a documented deviation
- [ ] [CHANGELOG.md](https://github.com/Rovniy/xploit_game_ui/blob/main/unity/Packages/com.xploit.game_ui/CHANGELOG.md) updated if this is user-visible
- [ ] No new dependency, or it is permissively licensed and added to [THIRD-PARTY-NOTICES](https://github.com/Rovniy/xploit_game_ui/blob/main/docs/THIRD-PARTY-NOTICES.md)
- [ ] Unity PlayMode tests run locally, if this touches C# or the plugin
