param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found.'
}

$installationPath = & $vswhere `
    -latest `
    -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (-not $installationPath) {
    throw 'A Visual Studio installation with the C++ build tools was not found.'
}

$devCmd = Join-Path $installationPath 'Common7\Tools\VsDevCmd.bat'
$cmakeDirectory = Join-Path $installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaDirectory = Join-Path $installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$cmake = Join-Path $cmakeDirectory 'cmake.exe'
$ctest = Join-Path $cmakeDirectory 'ctest.exe'

foreach ($requiredFile in @($devCmd, $cmake, $ctest, (Join-Path $ninjaDirectory 'ninja.exe'))) {
    if (-not (Test-Path -LiteralPath $requiredFile)) {
        throw "Required build tool was not found: $requiredFile"
    }
}

# Import the Visual Studio developer environment into this PowerShell process.
$environmentLines = cmd.exe /d /s /c "`"$devCmd`" -arch=x64 -host_arch=x64 >nul && set"
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
    }
}

$env:Path = "$ninjaDirectory;$env:Path"
$preset = $Configuration.ToLowerInvariant()

& $cmake --preset $preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build --preset $preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $ctest --test-dir "build/$preset" --output-on-failure
exit $LASTEXITCODE
