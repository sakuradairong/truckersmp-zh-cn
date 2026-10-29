# Third-party notices

The MIT license covers the original localization code and translation data in
this repository. It grants no rights to TruckersMP, ETS2, their client assets,
their trademarks, or user-supplied fonts and SDKs. This is an unofficial project.

- **MinHook v1.3.4**, upstream commit
  `c3fcafdc10146beb5919319d0683e44e3c30d537`, is included as the x64 build subset
  in `vendor/minhook/`. Source: <https://github.com/TsudaKageyu/minhook>.
  Its BSD-style license and the HDE notices are preserved in
  [vendor/minhook/LICENSE.txt](vendor/minhook/LICENSE.txt) and source headers.
  The Settings localization DLL bundles this library; its license is also
  available in the installer's license viewer.
- **SCS Telemetry SDK 1.16** is an external build prerequisite and is not
  redistributed here. Official archive:
  <https://download.eurotrucksimulator2.com/scs_sdk_1_16.zip>.
  Reviewed archive SHA-256:
  `49821e25568f4d7e462135f577f8fef2ab197326404f2c1daef139b8f3dc5e69`.
  Obtain it from SCS and follow its terms. Only the game loader entry point is
  used; this project does not link to the TruckersMP SDK.
- **Pillow**, **Capstone**, and **pefile** are external Python dependencies.
  The graphical installer bundles Python, Tcl/Tk and Pillow, with runtime license
  texts accessible in its license viewer. Capstone and pefile are developer-only
  requirements for the experimental native module.
- **PyInstaller 6.22.3** packages the executable. Its bootloader exception permits
  distribution of bundled applications without imposing GPL on application code.
  Its complete license and exception are included in the installer's license viewer.
- **FontTools 4.68.0** is a build-only dependency under the MIT license.
- **ttfautohint-py 0.6.1** and its ttfautohint executable are build-only tools,
  not bundled in the installer. ttfautohint's generated font hinting instructions
  are covered by its font exception; see <https://freetype.org/ttfautohint/>.
- **Noto Sans SC**, copyright 2014-2021 Adobe, reserved font name `Source`, and
  **Noto Sans Mono**, copyright 2022 The Noto Project Authors, are retrieved at
  Google Fonts commit `a85815a42757630ce188fdad368c2dfc444d4773` during the build.
  Both use **SIL Open Font License 1.1**. Full notices are retained in
  [licenses/NotoSansSC-OFL.txt](licenses/NotoSansSC-OFL.txt) and
  [licenses/NotoSansMono-OFL.txt](licenses/NotoSansMono-OFL.txt).
  `TMPZH Sans` is statically instantiated at 400 and 600. `TMPZH Mono` combines
  monospaced Latin from Noto Sans Mono with disjoint Chinese fallback from Noto
  Sans SC; `OfflineFreightZH` remaps only the freight static captions. Derived
  font family names are changed, and the original copyright/license records and
  full OFL texts accompany the executable and ZIP. These fonts remain under OFL,
  not the project's MIT license. Source inputs, hashes and checks are documented
  in [docs/FONTS.md](docs/FONTS.md).
- **Tcl 8.6.12**'s license is provided at
  [licenses/Tcl-8.6.12.txt](licenses/Tcl-8.6.12.txt) for Python distributions that
  omit this file. Other Tcl versions must provide their own matching notice.

Resource profiles contain translation and style edits. Full client RML/Lua/JSON
resources, official binaries and executable test fixtures are excluded from Git
and installer releases. The graphical installer bundles this project's Settings
localization DLL, compatibility fingerprints and open-license fonts. Compatibility
fingerprints describe only the Settings code and ABI required by the adapter;
they are not a whitelist of complete client files or release versions.
