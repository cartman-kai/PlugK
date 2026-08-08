param(
  [string]$TagName = "",
  [string]$Configuration = "Release",
  [string]$PlatformOutput = "Win32",
  [string]$PackageDir = "release_zip",
  [string]$ReleaseNotesPath = "release_notes.md",
  [string]$ArchivePath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-PackageVersion {
  param([string]$VersionHeader)

  $versionValues = @{}
  foreach ($line in Get-Content -LiteralPath $VersionHeader -Encoding UTF8) {
    if ($line -match '^\s*#define\s+(VERSION_MAJOR|VERSION_MINOR|VERSION_REVISION|VERSION_BUILD)\s+(\d+)\s*$') {
      $versionValues[$matches[1]] = [int]$matches[2]
    }
  }

  $requiredKeys = @("VERSION_MAJOR", "VERSION_MINOR", "VERSION_REVISION", "VERSION_BUILD")
  foreach ($key in $requiredKeys) {
    if (-not $versionValues.ContainsKey($key)) {
      throw "Missing version macro: $key"
    }
  }

  return "{0}.{1}.{2}.{3}" -f `
    $versionValues["VERSION_MAJOR"], `
    $versionValues["VERSION_MINOR"], `
    $versionValues["VERSION_REVISION"], `
    $versionValues["VERSION_BUILD"]
}

function Get-ChangelogSection {
  param(
    [string]$ChangelogPath,
    [string]$TagName
  )

  if (-not (Test-Path -LiteralPath $ChangelogPath)) {
    throw "Missing changelog: $ChangelogPath"
  }

  if ([string]::IsNullOrWhiteSpace($TagName)) {
    throw "TagName is required to extract release notes"
  }

  $lines = Get-Content -LiteralPath $ChangelogPath -Encoding UTF8
  $headingPattern = '^(#{1,6})\s+' + [regex]::Escape($TagName) + '\s*$'
  $startIndex = -1
  $headingLevel = 0
  for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match $headingPattern) {
      $startIndex = $i
      $headingLevel = $matches[1].Length
      break
    }
  }

  if ($startIndex -lt 0) {
    throw "Missing changelog section for tag: $TagName"
  }

  $endIndex = $lines.Count
  for ($i = $startIndex + 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^(#{1,' + $headingLevel + '})\s+') {
      $endIndex = $i
      break
    }
  }

  return @($lines[$startIndex..($endIndex - 1)])
}

function New-UsageGuide {
  param(
    [string]$DestinationPath,
    [string]$PackageVersion
  )

  $content = @(
    "# plugK 使用说明",
    "",
    "版本：$PackageVersion",
    "",
    "## 兼容性",
    "",
    "- 支持《刀剑封魔录》v1.05",
    "- 支持《上古传说》v2.01",
    "- v0.7.2 开始支持 Steam 版本",
    "",
    "## 安装",
    "",
    "1. 将压缩包内的 `plugK.dll`、`plugKLauncher.exe` 和 `PlugKLauncherHook.dll` 解压到游戏根目录，也就是 `ComeOn.exe` 所在目录。",
    "2. 运行 `plugKLauncher.exe`。",
    "3. 首次启动时，启动器会在游戏根目录自动生成默认配置文件 `PlugK.ini`。",
    "",
    "## 启动器功能",
    "",
    "- 自动检测游戏文件、补丁 DLL、配置文件是否齐全",
    "- 一键启动原版游戏或 MOD 模式",
    "- 图形化修改补丁配置",
    "- 创建原版和 MOD 模式桌面快捷方式",
    "",
    "## 常用快捷键",
    "",
    "- `Ctrl+Z`：开关自动拾取",
    "- `Shift+Z`：切换自动拾取范围",
    "- `Alt+1` 到 `Alt+4`：快捷释放已学习的四个必杀技",
    "- `` ` ``：长按显示地面物品名称",
    "- `Ctrl+X`：拆分背包中第一个可叠加物品",
    "",
    "更多配置说明见 `README.md`，本次版本更新见 `更新日志.md`，完整历史见 `CHANGELOG.md`。"
  )

  $content | Set-Content -LiteralPath $DestinationPath -Encoding UTF8
}

$packageVersion = Get-PackageVersion -VersionHeader "version_info.h"
if ([string]::IsNullOrWhiteSpace($ArchivePath)) {
  $ArchivePath = "plugK_${packageVersion}_build.zip"
}

if (-not [string]::IsNullOrWhiteSpace($TagName)) {
  $expectedTag = "v" + ($packageVersion -replace '\.0$','')
  if ($TagName -ne $expectedTag) {
    Write-Warning "Tag '$TagName' does not match package version '$packageVersion' expected tag '$expectedTag'."
  }
}

$releaseNoteLines = Get-ChangelogSection -ChangelogPath "CHANGELOG.md" -TagName $TagName
$releaseNoteLines | Set-Content -LiteralPath $ReleaseNotesPath -Encoding UTF8

if (Test-Path -LiteralPath $PackageDir) {
  Remove-Item -LiteralPath $PackageDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null

$binaryDir = Join-Path -Path "bin/$PlatformOutput" -ChildPath $Configuration
$requiredFiles = @(
  (Join-Path -Path $binaryDir -ChildPath "plugK.dll"),
  (Join-Path -Path $binaryDir -ChildPath "plugKLauncher.exe"),
  (Join-Path -Path $binaryDir -ChildPath "PlugKLauncherHook.dll"),
  "README.md",
  "CHANGELOG.md",
  "LICENSE",
  "THIRD_PARTY_NOTICES.md"
)

foreach ($file in $requiredFiles) {
  if (-not (Test-Path -LiteralPath $file)) {
    throw "Missing package input: $file"
  }
  Copy-Item -LiteralPath $file -Destination $PackageDir
}

New-UsageGuide -DestinationPath (Join-Path -Path $PackageDir -ChildPath "使用说明.md") -PackageVersion $packageVersion
Copy-Item -LiteralPath $ReleaseNotesPath -Destination (Join-Path -Path $PackageDir -ChildPath "更新日志.md")

if (Test-Path -LiteralPath $ArchivePath) {
  Remove-Item -LiteralPath $ArchivePath -Force
}
Compress-Archive -Path (Join-Path -Path $PackageDir -ChildPath "*") -DestinationPath $ArchivePath

Write-Output "PACKAGE_VERSION=$packageVersion"
Write-Output "ARCHIVE_PATH=$ArchivePath"
Write-Output "RELEASE_NOTES_PATH=$ReleaseNotesPath"
