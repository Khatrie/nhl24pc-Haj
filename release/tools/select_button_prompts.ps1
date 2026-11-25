param(
    [ValidateSet('playstation', 'xbox', 'nintendo')]
    [string]$Layout,
    [string]$InstallRoot = (Split-Path -Parent $PSScriptRoot)
)
$ErrorActionPreference = 'Stop'

if (-not $Layout) {
    Write-Host 'Choose a button prompt layout:'
    Write-Host '1. PlayStation'
    Write-Host '2. Xbox'
    Write-Host '3. Nintendo'
    switch (Read-Host 'Enter 1, 2 or 3') {
        '1' { $Layout = 'playstation' }
        '2' { $Layout = 'xbox' }
        '3' { $Layout = 'nintendo' }
        default { throw 'Choose 1, 2 or 3.' }
    }
}

$root = [IO.Path]::GetFullPath($InstallRoot)
$config = Join-Path $root 'user\config.json'
$game = Join-Path $root 'game'
if (-not (Test-Path -LiteralPath $config -PathType Leaf)) {
    throw "Launcher configuration was not found: $config"
}
$settings = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
$gamePathFile = Join-Path $root 'user\game_path.txt'
if (Test-Path -LiteralPath $gamePathFile -PathType Leaf) {
    $executable = (Get-Content -LiteralPath $gamePathFile -TotalCount 1).Trim()
    if ($executable) { $game = Split-Path -Parent $executable }
}
$helper = Join-Path $root 'tools\font-helper\nhl24-font-patch.exe'
$fontUpdated = $false
if (Test-Path -LiteralPath $helper -PathType Leaf) {
    & $helper --game $game --layout $Layout
    if ($LASTEXITCODE -eq 2) {
        Write-Warning 'This game version is not supported by the font update tool. Texture prompts will still use the selected layout.'
    } elseif ($LASTEXITCODE -ne 0) {
        throw 'The controls reference could not be updated. The launcher selection was not changed.'
    } else {
        $fontUpdated = $true
    }
} elseif ($Layout -ne 'playstation') {
    Write-Warning 'The font update tool is unavailable. Texture prompts will change, but the controls reference will still show its original symbols.'
}
if (-not $settings.Launcher) {
    $settings | Add-Member -MemberType NoteProperty -Name Launcher -Value ([pscustomobject]@{})
}
if ($settings.Launcher.PSObject.Properties['prompt_layout']) {
    $settings.Launcher.prompt_layout = $Layout
} else {
    $settings.Launcher | Add-Member -MemberType NoteProperty -Name prompt_layout -Value $Layout
}
if ($fontUpdated) {
    if ($settings.Launcher.PSObject.Properties['prompt_font_layout']) {
        $settings.Launcher.prompt_font_layout = $Layout
    } else {
        $settings.Launcher | Add-Member -MemberType NoteProperty -Name prompt_font_layout -Value $Layout
    }
}
$json = $settings | ConvertTo-Json -Depth 50
$temp = $config + '.prompt.tmp'
$backup = $config + '.prompt.' + [Guid]::NewGuid().ToString('N') + '.bak'
[IO.File]::WriteAllText($temp, $json, (New-Object Text.UTF8Encoding $false))
[IO.File]::Replace($temp, $config, $backup)
Remove-Item -LiteralPath $backup
Write-Host "Button prompts set to $Layout. Launch the game to apply them."
