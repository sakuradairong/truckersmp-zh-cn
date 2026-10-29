# Third-party notices

The MIT license covers the original localization code and translation data in
this repository. It grants no rights to TruckersMP, ETS2, their client assets,
their trademarks, or user-supplied fonts and SDKs. This is an unofficial project.

- **MinHook v1.3.4**, upstream commit
  `c3fcafdc10146beb5919319d0683e44e3c30d537`, is included as the x64 build subset
  in `vendor/minhook/`. Source: <https://github.com/TsudaKageyu/minhook>.
  Its BSD-style license and the HDE notices are preserved in
  [vendor/minhook/LICENSE.txt](vendor/minhook/LICENSE.txt) and source headers.
- **SCS Telemetry SDK 1.16** is an external build prerequisite and is not
  redistributed here. Official archive:
  <https://download.eurotrucksimulator2.com/scs_sdk_1_16.zip>.
  Reviewed archive SHA-256:
  `49821e25568f4d7e462135f577f8fef2ab197326404f2c1daef139b8f3dc5e69`.
  Obtain it from SCS and follow its terms. Only the game loader entry point is
  used; this project does not link to the TruckersMP SDK.
- **Pillow**, **Capstone**, and **pefile** are external Python dependencies,
  installed separately under their respective licenses.
- **Fonts** are supplied locally by the user. No font is included or downloaded.
  The freight workaround modifies a local cmap and renames the derived font.
  Use a font whose license permits the intended modification and use. Font
  copyright and license name records are retained. The project's MIT license
  does not apply to that font or to locally generated font files.

The resource profile contains new text/style edits and original-file hashes.
Full RML/Lua/JSON resources, official binaries, extracted machine-code guards,
and test fixtures are generated from local inputs and excluded from Git.
