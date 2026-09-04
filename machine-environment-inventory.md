# Workstation environment inventory

> Base inventory verified on 2026-07-26. Blender, Unity, and Git LFS verification added on 2026-09-05. This is a workstation snapshot, not a license or security attestation.

## Confirmed toolchain

| Area | Detected version | Status and use |
| --- | --- | --- |
| Oh My Posh | 29.13.1 | Available for terminal branding |
| PowerShell | 7.6.4 | Default shell for workspace automation |
| Python | 3.13.7 | Available through `python` and `py` |
| .NET SDK | 6.0.428, 10.0.110, 10.0.400 preview | Installed for services, tools, and POCs |
| .NET runtimes | .NET 6, 8, 9, and 10; ASP.NET Core and WindowsDesktop | Available |
| Visual Studio | 18 Insiders | Installed |
| MSVC | 14.44.35207 and 14.51.36231 | Native x64 and ARM64 toolchains available |
| Windows SDK | 10.0.26100.0 | Windows API and native headers available |
| VS Code | 1.130.0 | Available on PATH |
| Git | 2.54.0.windows.1 | Available |
| Git LFS | 3.7.1 | Available for large binary asset versioning when a repository adopts an LFS policy |
| Blender | 5.2.1 | Installed for 3D modelling, source assets, scenes, and rendering; not currently on PATH |
| Unity Hub | 3.21.0 | Installed for Unity editor and module management |
| Unity game engine | 6000.5.10f1 | Installed for C# interactive 3D and game-engine development |
| FFmpeg / FFprobe | 8.1.2 full build | Available for media inspection and conversion |
| Node.js / npm | Node 20.19.0, npm 10.8.2 | Available for frontend tooling |
| pnpm | 11.9.0 | Available |
| SQL command line | SQL Server `sqlcmd` 15.0.1300.359 | Client installed |
| Espressif | ESP-IDF 5.5.4 tree; `esptool` present | Use a repository-provided device wrapper where available |
| Android Debug Bridge | Present | Available for Android and device workflows |
| Winget / Chocolatey | Winget 1.29.280, Chocolatey 2.3.0 | Available for controlled setup |
| CMake / Ninja | Not on PATH | Use a project-provided toolchain or configure explicitly |
| Docker / Java / NuGet CLI | Not on PATH | Do not assume as prerequisites |
| SQL Server engine | No service detected in the original check | Treat as unavailable until a named instance is confirmed |

## Interpretation

The workstation is ready for Windows-based R&D using C#, Unity, and Blender. Blender can be invoked by its installed executable; add it to PATH only if a project needs command-line Blender automation. Unity Hub and Unity 6000.5.10f1 are installed. Verify target-platform modules only after a target is chosen.

## Project decisions still required

- Pin the Unity editor version in each Unity project before adding package dependencies.
- Choose the render pipeline deliberately: Built-in, URP, or HDRP.
- Verify or install only the Unity build-support modules required for the selected target platform.
- Adopt and configure Git LFS patterns before committing large `.blend`, texture, render, audio, or other binary assets.
- Define the repository's remote, CI, asset-provenance, and release policies when they become relevant.

## Recheck guidance

Re-run the applicable repository preflight or inspect the current executable/version directly after a toolchain update. Do not infer that a tool or platform module is available solely from this historical snapshot.
