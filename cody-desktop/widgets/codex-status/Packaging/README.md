# Cody Companion packages

The source version is 0.4.2. The Windows MSI has its own upgrade code, install directory and Start menu shortcut. The MSIX has a separate package identity and declares the Windows notification listener capability. The Debian package, launcher and installation paths are named `cody-companion`. These identities are separate from the original OpenAI Usage application.

Packages are built from source. Dependency preparation uses official NuGet sources and retains the staged inputs; no antivirus scan is initiated. Build and packaging preserve generated material and do not run behavioral test suites. Do not run scans, builds, tests or installation without the authorization required by the repository working agreements. Cleanup is manual on explicit request.

Package creation, Windows notification listening and Linux native execution require independent validation. The source changes alone do not establish that any installer has been generated or installed.
