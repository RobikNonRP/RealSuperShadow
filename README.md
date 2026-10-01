# Real Super Shadow

Real Super Shadow is a Sonic X Shadow Generations mod that adds a Super Shadow
visual implementation over Doom Wings, with custom Super Shadow visual effects.

- **Game:** Shadow Generations (Sonic X Shadow Generations)
- **Author:** RobikRP
- **GameBanana release:** Super Shadow over Doom Wings

This source repository exists for transparency and development/build purposes.
It contains the mod's DLL source and configuration, not a complete playable mod
package.

## Current Status

**Beta v0.7** — the project is still in development.

This is the Beta v0.7 source snapshot. It does not include later experimental
development builds.

## Features

- Super Shadow transformation over Doom Wings.
- Doom Wings automatically transition into the Super Shadow appearance.
- Super Shadow aura with a lightning effect.
- Super Boost and Super Air Boost.
- Super JumpBall / Homing.
- Super Stomp.

The included Hedge Mod Manager configuration exposes **Super Shadow on Doom
Surf**. Restart the game after changing this option.

## Build

The project uses Windows x64, Visual Studio 2022/MSVC with C++ and Windows SDK
tools, CMake 3.28 or newer, Git, miller-sdk, and Microsoft Detours. The SDK also
requires its own dependencies. See [DEVELOPMENT.md](DEVELOPMENT.md) for the exact
dependency revisions, build commands, output, and installation instructions.

## Game Assets

The resource portion of the mod is distributed separately with the GameBanana
release. Extracted game assets, PAC archives, textures, models, and effect
resources are not included in this source repository. Building the DLL does not
reconstruct that resource package; the corresponding Beta v0.7 resources are
required at runtime.

## GameBanana

Mod page: https://gamebanana.com/wips/104034

## Dependencies and Licensing

Third-party SDK and library source is not vendored here. Obtain it separately
from the upstream projects listed in [DEVELOPMENT.md](DEVELOPMENT.md).

No license has been selected for this project's source code. No LICENSE file is
included. Third-party dependencies remain subject to their own licenses.
