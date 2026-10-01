# Development

## Prerequisites

- Windows x64.
- Visual Studio 2022 with Desktop development with C++, the MSVC x64 compiler,
  MASM, and a Windows SDK/resource compiler. The frozen Beta build used the
  `Visual Studio 17 2022` CMake generator; an exact minimum Windows SDK version
  has not been established.
- CMake 3.28 or newer, Git, and PowerShell.
- A legitimate installation of Sonic X Shadow Generations and Hedge Mod Manager
  for runtime testing. Neither is needed to compile the DLL.

## External Dependencies

The following revisions are recorded from the Beta v0.7 source dependencies:

| Dependency | Revision | Source |
| --- | --- | --- |
| miller-sdk | `2e4d74b818d1444225a568f2ef60982fcca92d8f` | [HE2-SDK/miller-sdk](https://github.com/HE2-SDK/miller-sdk) |
| Microsoft Detours | `adb07604aa56508448b95bf037c2a6d0d3b6831a` | [microsoft/Detours](https://github.com/microsoft/Detours) |

The pinned SDK declares [universal-cslib](https://github.com/angryzor/universal-cslib)
`v1.0.52` through CMake FetchContent and includes a
[Bullet](https://github.com/bulletphysics/bullet3) submodule at
`2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5`. The inspected universal-cslib revision
declares Eigen at `0fb2ed140d4fc0108553ecfb25f2d7fc1a9319a1` and
[simple-reflection](https://github.com/angryzor/simple-reflection) `v1.0.3` through
FetchContent. Network access is needed for the first
setup/configure. These libraries and their licenses are obtained from upstream;
none of their implementation files are part of this repository.

From the repository root:

```powershell
.\setup_dependencies.ps1
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The setup script checks out the recorded revisions in `miller-sdk/` and
`vendor/detours/Detours/`. It refuses to overwrite a pre-existing checkout with
another revision or local changes. Initialize any missing submodules in an
existing clean checkout with `git -C miller-sdk submodule update --init --recursive`.

Existing dependency checkouts can instead be selected with the CMake cache
variables `MILLER_SDK_DIR` and `DETOURS_SOURCE_DIR`. The latter points to the
Detours repository root, containing `src/detours.cpp`.

## Output and Local Staging

The Release DLL is `build/Release/RealSuperShadowPOC.dll`. The generated Hedge
Mod Manager metadata is `build/mod.ini`.

Optionally assemble the code/configuration portion in a local directory:

```powershell
cmake --install build --config Release --prefix ./build/stage
```

This stages only the DLL, `mod.ini`, `SuperVisuals.json`, and
`RealSuperShadow.ini`. It does not copy game assets, locate the game, or install
anything into a game directory. The default install prefix is also `build/stage`.

## Hedge Mod Manager Installation

1. Obtain the matching Beta v0.7 resource release from the official mod page.
2. Make a separate development copy of that mod package, preserving its resource
   files. Keep your working release backed up.
3. With the game closed, copy the staged DLL and metadata/configuration files
   into the development copy. Preserve any personal INI settings as needed.
4. Add the complete development mod folder to Hedge Mod Manager for Shadow
   Generations, and enable only one copy of this mod at a time.

The source repository alone is not an installable visual mod. The corresponding
resource package supplies `chr_supershadow` and the custom effect resources.
The original resource names `chr_shadow` and `chr_supershadow` are intentional.

## Source Layout

- `src/DllMain.cpp`: entry point, player capture hook, and configuration loading.
- `src/Mod.cpp`: player lifecycle, separate Super visual, Doom Wings visibility,
  Doom Surf selection, and movement effect redirection.
- `src/BetaAura.cpp`: lightning aura resource checks and effect lifecycle.
- `src/Runtime.h`: shared state and declarations.
- `src/BetaVersion.rc`: Beta v0.7 DLL version metadata.
- `cmake/Detours.cmake`: this project's CMake adapter for external Detours.
- `SuperVisuals.json`, `RealSuperShadow.ini`, `mod.ini.in`: mod configuration and
  Hedge Mod Manager metadata.

## Provenance and Compatibility

The five files in `src/` preserve the frozen Beta v0.7 runtime implementation;
extra blank lines at the ends of three files were removed.
Public-source changes concern portable dependency paths, local staging, and
documentation. No later experimental hotkey/probe builds are included.

The initial player-state hook pattern and StateSquat entry address were informed
by the public [Functional Slide mod](https://github.com/stis1/shadow-functional-slide).
The project uses the external miller-sdk API declarations and Microsoft Detours
for hooks; it does not claim ownership of these third-party projects.

The runtime contains game-version-specific addresses and byte signatures.
Changing the SDK or game version may require an audit of those hooks. Passing a
build is not proof of in-game compatibility. Keep the frozen runtime logic intact
when reproducing this source snapshot; compatibility with other game versions is
not established here.

## Source Release Validation

The public CMake configuration and Release build were checked using MSVC
19.36.32534.0 and Windows SDK 10.0.22621.0 with the dependency revisions above.
The build succeeded. The SDK emitted C4624 warnings about implicitly deleted
destructors; Eigen emitted a CMake policy deprecation warning. The resulting DLL
was not installed or tested in-game as part of the source publication.
