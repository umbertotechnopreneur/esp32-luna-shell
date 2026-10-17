# Provision the isolated modern model-conversion environment, never firmware tools.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$artifactRoot = Join-Path $root 'artifacts/python/activity'
$savedInstall = $env:UV_PYTHON_INSTALL_DIR
$savedCache = $env:UV_CACHE_DIR
try {
    $env:UV_PYTHON_INSTALL_DIR = Join-Path $artifactRoot 'interpreters'
    $env:UV_CACHE_DIR = Join-Path $artifactRoot 'cache'
    & uv venv --python 3.12 (Join-Path $artifactRoot 'env')
    if ($LASTEXITCODE -ne 0) { throw "Activity Python provisioning failed: $LASTEXITCODE" }
    & uv pip install --python (Join-Path $artifactRoot 'env/Scripts/python.exe') 'esp-ppq[cpu]==1.3.11'
    if ($LASTEXITCODE -ne 0) { throw "Activity dependency provisioning failed: $LASTEXITCODE" }
} finally {
    $env:UV_PYTHON_INSTALL_DIR = $savedInstall
    $env:UV_CACHE_DIR = $savedCache
}
