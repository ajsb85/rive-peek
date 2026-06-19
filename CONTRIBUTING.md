# Contributing to RivePeek

Thanks for your interest in improving RivePeek! This is a small, focused native
Windows project (a COM Shell preview + thumbnail handler for `.riv` files), so
the contribution process is lightweight.

## Ways to contribute

- **Bugs / rendering glitches** — open an issue with a sample `.riv`, your
  Windows version, and (ideally) a `RIVEPEEK_LOG=1` trace (see below).
- **Features** — the [Roadmap](README.md#roadmap) items are all self-contained
  starting points (text, layout, blend modes, focus-aware animation, installer).
- **Docs** — clarifications and fixes to the README/this file are very welcome.

## Prerequisites

- Windows 10 or 11 (x64)
- **Visual Studio 2022 Build Tools** with the *Desktop development with C++*
  workload (MSVC v143) and a **Windows 10/11 SDK**
- `git` (with submodule support)

No CMake is required — `build\*.bat` configure the MSVC toolchain themselves via
`vswhere` (see `build\env.bat`).

## Getting the source

```bat
git clone --recurse-submodules https://github.com/ajsb85/rive-win-preview.git
cd rive-win-preview
```

If you forgot `--recurse-submodules`:

```bat
git submodule update --init --recursive
```

The `rive-runtime/` directory is a submodule pinned to a specific upstream
commit; don't commit changes inside it.

## Building

```bat
build\build_all.bat
```

Outputs land in `build\bin\`. Build artifacts (`build\bin`, `build\obj`,
`*.lib`, logs, generated PNGs) are git-ignored — never commit them.

## Testing your change

Always verify rendering before and after a change:

```powershell
# Render a single file headlessly
build\bin\rivshot.exe sample.riv before.png 512

# Validate the whole example corpus (pass/fail tally)
build\validate.ps1 -Dir "path\to\riv\files"

# Exercise the COM handler in-process (creates + drives the handler)
build\bin\preview_test.exe sample.riv capture.png

# Replicate Explorer's out-of-process activation (prevhost.exe)
build\bin\surrogate_test.exe sample.riv

# Check the thumbnail path
build\bin\thumb_test.exe sample.riv thumb.png 256
```

For handler/registration changes, install and try a real preview:

```powershell
install\register.ps1      # per-user, no admin
# ... test in Explorer ...
install\unregister.ps1
```

Set `RIVEPEEK_LOG=1` to trace every COM call to `%TEMP%\RivePeek.log` — include
this in bug reports.

## Coding conventions

- **C++17**, 4-space indentation, ~90-column soft wrap — match the surrounding
  code's style and comment density.
- Keep the **COM boundary clean**: handler methods return `HRESULT`, never let a
  C++ exception escape across a COM call, and keep `QueryInterface` driven by the
  `QISearch`/`QITAB` table.
- The COM/registration structure intentionally mirrors Microsoft's
  [`RecipePreviewHandler`](https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winui/shell/appshellintegration/RecipePreviewHandler)
  sample — stay close to it unless there's a good reason not to.
- Rendering code lives in `rive_d2d.*` (the `rive::Factory`/`rive::Renderer`
  implementation) and `rive_scene.*`. Prefer extending those over touching the
  COM handler.
- Don't introduce third-party dependencies lightly — a key property of this
  project is that `RivePeek.dll` is self-contained (static CRT, no runtime DLLs).

## Pull requests

1. Branch off `main` (`git switch -c my-change`).
2. Keep PRs focused; describe **what** and **why**, and note how you tested
   (which files you rendered, screenshots help).
3. Make sure `build\build_all.bat` succeeds and `validate.ps1` still passes on a
   sample corpus.
4. Use clear, imperative commit messages.

## License

By contributing, you agree that your contributions are licensed under the
project's [MIT License](LICENSE).
