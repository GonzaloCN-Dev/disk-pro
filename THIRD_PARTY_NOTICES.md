# Third-party notices

Disk Pro's own code is MIT (see `LICENSE`). Third-party components keep their own licenses.

## Slint 1.19.1 (SixtyFPS GmbH) — UI toolkit
- Used under the **Slint Royalty-free Desktop, Mobile, and Web Applications License 2.0** (alternatively GPLv3). The MIT license of Disk Pro does **not** relicense Slint.
- Condition met: the `AboutSlint` widget is shown in the About dialog.
- Binaries redistributed: `slint_cpp.dll` (portable package). Full text: `SLINT-LICENSE.md`, `.tools/slint/licenses/THIRDPARTY.md` lists Slint's own dependencies.
- Source: https://github.com/slint-ui/slint — release asset SHA-256 verified against the GitHub-published digest.

## Microsoft
- Built with MSVC / Windows SDK (Visual Studio 2022 Build Tools). The MSVC runtime is linked dynamically (`/MD`); end users need the Microsoft Visual C++ Redistributable x64.

No other dependencies. No code, assets or trademarks from DiskGenius, MiniTool or other products were used (clean-room).
