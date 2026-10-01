$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Set-Location $PSScriptRoot

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is not installed or not in PATH."
}

function Get-PinnedDependency {
    param([string]$Url, [string]$Directory, [string]$Revision)
    if (Test-Path -LiteralPath $Directory) {
        $current = git -C $Directory rev-parse HEAD
        if ($LASTEXITCODE -ne 0 -or $current -ne $Revision) {
            throw "Existing dependency $Directory does not match $Revision. It was not modified."
        }
        $changes = git -C $Directory status --porcelain
        if ($LASTEXITCODE -ne 0 -or $changes) {
            throw "Existing dependency $Directory has local changes. It was not modified."
        }
        Write-Host "$Directory already matches $Revision."
        return
    }
    git clone --no-checkout $Url $Directory
    if ($LASTEXITCODE -ne 0) { throw "Failed to clone $Directory." }
    git -C $Directory checkout --detach $Revision
    if ($LASTEXITCODE -ne 0) { throw "Failed to select $Revision for $Directory." }
    git -C $Directory submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw "Failed to initialize submodules for $Directory." }
}

Get-PinnedDependency -Url "https://github.com/HE2-SDK/miller-sdk.git" `
    -Directory "miller-sdk" -Revision "2e4d74b818d1444225a568f2ef60982fcca92d8f"
Get-PinnedDependency -Url "https://github.com/microsoft/Detours.git" `
    -Directory "vendor/detours/Detours" -Revision "adb07604aa56508448b95bf037c2a6d0d3b6831a"

Write-Host "Dependencies are ready. They are excluded from this source repository."
