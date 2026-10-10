# Contributing

Thanks for your interest in AdMutify.

## Bugs and ideas

Open an issue with the bug report or feature request form. For security problems, follow [SECURITY.md](SECURITY.md) instead.

## Pull requests

1. Open an issue first for anything larger than a small fix, so we can agree on the approach.
2. Build with `build.bat` (Visual Studio 2022 or newer, CMake 3.21+). It also runs the tests; all of them must pass.
3. Keep the code style of the surrounding code: C++20, plain Win32, no third-party libraries.
4. User-facing text goes through the string tables in `src/strings.cpp`, with an entry for every language.
5. Describe what changed and how you tested it.

By contributing you agree that your work is licensed under the [MIT License](LICENSE).
