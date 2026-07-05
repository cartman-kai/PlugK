param(
    [Parameter(Mandatory = $true)]
    [string]$SolutionDir,

    [Parameter(Mandatory = $true)]
    [string]$OutDir
)

$versionHeader = Join-Path $SolutionDir "version_info.h"
if (-not (Test-Path -LiteralPath $versionHeader)) {
    Write-Host "version_info.h not found: $versionHeader"
    exit 0
}

$versionValues = @{}
foreach ($line in Get-Content -LiteralPath $versionHeader) {
    if ($line -match '^\s*#define\s+(VERSION_MAJOR|VERSION_MINOR|VERSION_REVISION|VERSION_BUILD)\s+(\d+)\s*$') {
        $versionValues[$matches[1]] = [int]$matches[2]
    }
}

$requiredKeys = @("VERSION_MAJOR", "VERSION_MINOR", "VERSION_REVISION", "VERSION_BUILD")
foreach ($key in $requiredKeys) {
    if (-not $versionValues.ContainsKey($key)) {
        Write-Host "Missing version macro: $key"
        exit 0
    }
}

$versionName = "{0}.{1}.{2}.{3}" -f $versionValues["VERSION_MAJOR"], $versionValues["VERSION_MINOR"], $versionValues["VERSION_REVISION"], $versionValues["VERSION_BUILD"]

$archiveDir = Join-Path $OutDir $versionName
New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null

$files = @("plugK.dll", "plugKLauncher.exe", "PlugKLauncherHook.dll")
foreach ($file in $files) {
    $sourcePath = Join-Path $OutDir $file
    if (Test-Path -LiteralPath $sourcePath) {
        Copy-Item -LiteralPath $sourcePath -Destination $archiveDir -Force
    } else {
        Write-Host "Build output not found, skip archive copy: $sourcePath"
    }
}

Write-Host "Archived build outputs to $archiveDir"
