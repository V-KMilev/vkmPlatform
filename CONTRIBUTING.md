# Contributing to vkmPlatform

Thank you for wanting to help. This page is for changing vkmPlatform itself; a bug or a question
goes in an [issue](https://github.com/V-KMilev/vkmPlatform/issues).

## Before you write code

Open an issue for anything bigger than a fix, so the shape is agreed before the work is done. Read
the [README](README.md) and [how it works](docs/how-it-works.md) first: the README's conventions are
promises its consumers rely on. The window and its input stay the main thread's, and the pool keeps
its order, unless a change is meant to move them and says so.

## Build and test

vkmPlatform is developed inside vkmEngine, which pins it at `modules/vkmPlatform` and builds every
library's suite beside its own:

```sh
git clone --recursive https://github.com/V-KMilev/vkmEngine
cd vkmEngine
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build -L vkm_platform --output-on-failure
```

Work in `modules/vkmPlatform`, a checkout of this repository, and run the engine's suites too: they
use the library as a game does. The engine's
[CONTRIBUTING.md](https://github.com/V-KMilev/vkmEngine/blob/master/CONTRIBUTING.md#build-and-test)
has the rest.

## What a pull request needs

A clean build with no warnings in GCC and Clang, passing suites, a test you saw fail without your
fix, and the README and `docs/` updated in the same commit. A commit message is lowercase
`type: summary` under 72 characters, then a few lines on what the change does as a whole and why.
One subject per pull request, in as few commits as tell it.

## How the code looks

As vkmEngine's, listed in
[its CONTRIBUTING.md](https://github.com/V-KMilev/vkmEngine/blob/master/CONTRIBUTING.md#how-the-code-looks).
Match the files around your change.

## The license

vkmPlatform is source-available under the [vkmEngine License](LICENSE), not an open-source license.
By submitting a contribution you grant the licensor the license its Contributions section describes,
and confirm the work is yours to give. Changed code may be shared only as a contribution here or in
a fork within this repository's GitHub fork network.
