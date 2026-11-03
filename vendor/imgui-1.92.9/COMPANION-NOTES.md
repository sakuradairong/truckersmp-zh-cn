# Vendored Dear ImGui

Version 1.92.9, copied from the existing local ETS2Trainer vendor directory on 2026-11-03. Upstream: https://github.com/ocornut/imgui . MIT license retained in LICENSE.txt.

Only core files and the DX11 backend are included. No Win32 WndProc backend or injection code is used; the overlay feeds input from the official TruckersMP SDK.

Local changes in imgui_impl_dx11.cpp: save/restore Hull, Domain and Compute shaders and their class instances; release Geometry Shader class-instance references after restoration. These changes prevent pipeline state loss and a reference leak when used inside the host game's official rendering callback.
