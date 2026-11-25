param([string]$Out = (Join-Path $PSScriptRoot '..\..\build\prompt-font-helper'))
$ErrorActionPreference = 'Stop'
$Out = [IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force $Out | Out-Null
dotnet publish (Join-Path $PSScriptRoot 'PromptFontHelper.csproj') -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -o $Out
if ($LASTEXITCODE -ne 0) { throw 'Font helper build failed.' }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ build tools are required for the archive decoder.' }
& (Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$source = Join-Path $PSScriptRoot 'ooz'
Push-Location $Out
try {
    & clang-cl /nologo /O2 /EHsc /MT /FI sys/stat.h '/Fe:ooz.exe' `
        (Join-Path $source 'kraken.cpp') (Join-Path $source 'bitknit.cpp') (Join-Path $source 'lzna.cpp')
    if ($LASTEXITCODE -ne 0) { throw 'Archive decoder build failed.' }
} finally { Pop-Location }
$sourceOut = Join-Path $Out 'ooz-source'
New-Item -ItemType Directory -Force $sourceOut | Out-Null
Get-ChildItem -LiteralPath $source -File | Copy-Item -Destination $sourceOut -Force
