# RivePeek — Rive (`.riv`) preview handler for Windows Explorer

RivePeek is a native Windows **Shell preview handler** that renders animated
[Rive](https://rive.app) `.riv` files live in the File Explorer preview pane —
the same mechanism Windows uses to preview PDFs, Office documents and images.

Select a `.riv` file with the preview pane open (**Alt + P**) and the default
artboard plays, fit and centered, isolated inside Windows' `prevhost.exe`
surrogate process so a malformed file can never destabilize Explorer.

| Headless render (`rivshot`) | Live preview pane (captured from the real COM handler) |
|---|---|
| ![coin](docs/render-coin.png) | ![preview pane](docs/preview-pane.png) |

> Validated against **887 marketplace `.riv` files: 886 render successfully
> (99.9%)**. The single outlier is rejected as `malformed` by the Rive runtime
> itself during import.

---

## How it works

Windows previews files through COM components hosted out-of-process. When you
select a `.riv` file:

1. **Explorer** reads `HKCR\.riv\ShellEx\{8895b1c6-…}` to find the preview
   handler's CLSID.
2. The handler's `AppID` points at the **`prevhost.exe`** surrogate, so Windows
   loads `RivePeek.dll` into that isolated host — not into `explorer.exe`.
3. Explorer hands the file to the handler as a read-only `IStream`
   (`IInitializeWithStream`), then gives it an `HWND` + bounding rectangle
   (`IPreviewHandler::SetWindow`) and calls `DoPreview()`.
4. RivePeek parses the file with the **Rive C++ runtime**, then draws every
   frame with a **Direct2D** renderer into a child window, advancing the default
   state machine (or first animation) on a ~30 fps timer.

```
explorer.exe                       prevhost.exe (surrogate, isolated)
  │  select foo.riv                  │
  │  registry → CLSID ─────────────► │  CoCreateInstance(RivePeek)
  │  IStream  ──────────────────────►│  IInitializeWithStream::Initialize
  │  HWND + RECT ───────────────────►│  IPreviewHandler::SetWindow / DoPreview
  │                                  │      │
  │                                  │      ▼
  │                                  │  rive::File::import  (Rive C++ runtime)
  │                                  │      │  artboard + state machine
  │                                  │      ▼
  │                                  │  D2DRenderer ──► ID2D1HwndRenderTarget
  │                                  │      (paths, gradients, clips, images)
```

### The Direct2D backend

Rather than compiling Rive's heavyweight GPU "Rive Renderer" (and its shader /
HarfBuzz / Yoga / SheenBidi dependency chain), RivePeek implements Rive's two
abstract embedding interfaces directly on top of Direct2D + WIC:

- **`rive::Factory`** → `D2DFactory`: builds paths (`ID2D1PathGeometry`),
  solid/linear/radial brushes, and decodes images via **WIC**.
- **`rive::Renderer`** → `D2DRenderer`: maps `save`/`restore`, `transform`,
  `clipPath` (geometry-mask layers), `drawPath` (fill/stroke), `drawImage`, and
  `drawImageMesh` (per-triangle affine-textured fills) onto an
  `ID2D1RenderTarget`.

The same backend powers a **WIC bitmap render target** for headless thumbnail
generation (no GPU or window required), which doubles as the test harness.

This means only the **core** Rive runtime is compiled — no external
dependencies. Text and Yoga-based layout are `#ifdef`-guarded in the runtime and
compile to no-ops here (see [Limitations](#limitations)).

---

## Repository layout

```
src/
  guids.hpp           CLSID + registration constants
  rive_d2d.hpp/.cpp   Direct2D implementation of rive::Factory + rive::Renderer
  rive_scene.hpp/.cpp loads a .riv, instantiates artboard + state machine, draws
  preview_handler.*   the COM handler (IPreviewHandler, IInitializeWithStream,
                      IOleWindow, IObjectWithSite, IPreviewHandlerVisuals)
  dllmain.cpp         COM class factory, DLL exports, (un)registration
  rivepeek.def        exported entry points
tools/
  rivshot.cpp         headless .riv → .png renderer / validation harness
  preview_test.cpp    in-process integration test of the full COM pipeline
build/                MSVC build scripts (no CMake required)
install/              register.ps1 / unregister.ps1 / RivePeek.reg
rive-runtime/         Rive C++ runtime (git submodule)
```

---

## Building

Requirements: **Visual Studio 2022 Build Tools** (MSVC v143) and the **Windows
10/11 SDK**. No CMake needed — the build scripts configure the toolchain
themselves.

```bat
git clone --recurse-submodules https://github.com/ajsb85/rive-win-preview.git
cd rive-win-preview
build\build_all.bat
```

This produces, in `build\bin\`:

| Artifact | Purpose |
|---|---|
| `RivePeek.dll` | the preview handler (register to install) |
| `rivshot.exe` | `rivshot in.riv out.png [size] [seconds]` — headless render |
| `preview_test.exe` | drives the COM pipeline in-process and screenshots it |

Individual steps: `build_rive_core.bat` (the `rive_core.lib` static lib),
`build_dll.bat`, `build_rivshot.bat`, `build_test.bat`.

> The build environment is configured by `build\env.bat`, which auto-detects the
> newest MSVC toolset and Windows SDK via `vswhere`. It deliberately avoids
> `vcvars64.bat` because that script aborts when the optional `cmake` developer
> extension is absent.

---

## Installing

Registration writes the COM class, the `.riv` association, and the
approved-preview-handlers entry under `HKEY_LOCAL_MACHINE`, so it needs
administrator rights. The script self-elevates:

```powershell
# install (UAC prompt)
install\register.ps1

# uninstall
install\unregister.ps1
```

Equivalent manual route: `regsvr32 build\bin\RivePeek.dll` from an elevated
prompt, or edit + merge `install\RivePeek.reg`.

After installing, open Explorer, enable the preview pane (**Alt + P**) and click
a `.riv` file.

---

## Testing

```powershell
# Render one file headlessly
build\bin\rivshot.exe sample.riv sample.png 512

# Exercise the entire COM handler in-process and capture the preview window
build\bin\preview_test.exe sample.riv capture.png

# Render every .riv in a folder and tally pass/fail
build\validate.ps1 -Dir "path\to\riv\files"
```

`preview_test.exe` performs the exact sequence the Shell does
(`DllGetClassObject` → `IClassFactory::CreateInstance` → `QueryInterface` for
every required interface → `IInitializeWithStream::Initialize` → `SetWindow` →
`DoPreview`) and verifies it without needing the DLL registered.

---

## Limitations

- **Text** (`WITH_RIVE_TEXT`, HarfBuzz + SheenBidi) and **Yoga layout**
  (`WITH_RIVE_LAYOUT`) are not compiled in, so text runs are not drawn and
  layout-driven components fall back to their authored size. Vector art,
  shapes, gradients, clipping, raster images, image meshes, bones, constraints
  and state machines all render.
- **Blend modes** other than `srcOver` are drawn as `srcOver`.
- Files using a different Rive **major version** than the bundled runtime
  (currently 7) are reported as unsupported.
- Adding text/layout is a forward path: build the relevant runtime
  dependencies, define `WITH_RIVE_TEXT` / `WITH_RIVE_LAYOUT`, and implement
  `Factory::decodeFont`.

---

## Credits & license

- Built on the [Rive C++ runtime](https://github.com/rive-app/rive-runtime)
  (MIT), included as a submodule.
- RivePeek itself is MIT licensed — see [`LICENSE`](LICENSE).
