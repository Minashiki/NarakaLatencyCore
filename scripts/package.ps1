param(
    [string]$Configuration = "Release",
    [string]$CMakeExe = "C:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    [string]$DotNetExe = ".tools\dotnet\dotnet.exe",
    [string]$WinDivertRoot = ".tools\windivert\WinDivert-2.2.2-A"
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildRoot = Join-Path $projectRoot "build"
$distributionRoot = Join-Path $projectRoot "dist"
$publishRoot = Join-Path $projectRoot "build\gui-publish"
$resolvedDotNet = (Resolve-Path (Join-Path $projectRoot $DotNetExe)).Path
$resolvedWinDivert = (Resolve-Path (Join-Path $projectRoot $WinDivertRoot)).Path
$env:DOTNET_CLI_HOME = Join-Path $projectRoot ".tools\dotnet-home"

if (-not $distributionRoot.StartsWith($projectRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to package outside the project root."
}
if (Test-Path -LiteralPath $distributionRoot) {
    Remove-Item -LiteralPath $distributionRoot -Recurse -Force
}
if (Test-Path -LiteralPath $publishRoot) {
    Remove-Item -LiteralPath $publishRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $distributionRoot | Out-Null

& $CMakeExe --build $buildRoot --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "Native build failed." }

& $resolvedDotNet restore (Join-Path $projectRoot "src\gui\NarakaLatency.Gui.csproj") `
    --configfile (Join-Path $projectRoot "NuGet.Config") --runtime win-x64
if ($LASTEXITCODE -ne 0) { throw "GUI restore failed." }

& $resolvedDotNet publish (Join-Path $projectRoot "src\gui\NarakaLatency.Gui.csproj") `
    -c $Configuration -r win-x64 --self-contained true --no-restore `
    -p:PublishSingleFile=false -o $publishRoot
if ($LASTEXITCODE -ne 0) { throw "GUI publish failed." }

Copy-Item -Path (Join-Path $publishRoot "*") -Destination $distributionRoot -Recurse
$nativeOutput = Join-Path $buildRoot $Configuration
$runtimeFiles = @(
    "NarakaLatency.Native.dll",
    "nlc_delay_console.exe",
    "nlc_lifecycle_probe.exe",
    "nlc_udp_echo_server.exe",
    "nlc_udp_echo_client.exe",
    "nlc_tcp_echo_server.exe",
    "nlc_tcp_echo_client.exe"
)
foreach ($file in $runtimeFiles) {
    Copy-Item -LiteralPath (Join-Path $nativeOutput $file) -Destination $distributionRoot
}
Copy-Item -LiteralPath (Join-Path $resolvedWinDivert "x64\WinDivert.dll") -Destination $distributionRoot
Copy-Item -LiteralPath (Join-Path $resolvedWinDivert "x64\WinDivert64.sys") -Destination $distributionRoot
Copy-Item -LiteralPath (Join-Path $resolvedWinDivert "LICENSE") `
    -Destination (Join-Path $distributionRoot "WinDivert-LICENSE.txt")
Copy-Item -LiteralPath (Join-Path $projectRoot "README.md") -Destination $distributionRoot
Copy-Item -LiteralPath (Join-Path $projectRoot "THIRD_PARTY_NOTICES.md") -Destination $distributionRoot
Copy-Item -LiteralPath (Join-Path $projectRoot "docs") -Destination $distributionRoot -Recurse

$required = @(
    "NarakaLatencyController.exe",
    "NarakaLatency.Native.dll",
    "WinDivert.dll",
    "WinDivert64.sys",
    "WinDivert-LICENSE.txt"
)
foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $distributionRoot $file))) {
        throw "Missing required distribution file: $file"
    }
}

Write-Host "Self-contained distribution created at $distributionRoot"
