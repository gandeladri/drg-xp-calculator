$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$gcc = (Get-Command gcc -ErrorAction Stop).Source
$windres = (Get-Command windres -ErrorAction Stop).Source

$sourceFile = Join-Path $repoRoot 'drg-xp-calculator.c'
$iconFile = Join-Path $repoRoot 'drg-xp-calculator.ico'
$resourceFile = Join-Path $repoRoot 'drg-xp-calculator.rc'
$resourceObj = Join-Path $repoRoot 'drg-xp-calculator.res'
$outputExe = Join-Path $repoRoot 'drg-xp-calculator.exe'

foreach ($path in @($sourceFile, $iconFile, $resourceFile)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing required file: $path"
    }
}

try {
    & $windres $resourceFile -O coff -o $resourceObj
    if ($LASTEXITCODE -ne 0) {
        throw "windres failed with exit code $LASTEXITCODE"
    }

    & $gcc `
        '-municode' `
        '-Wl,--subsystem,windows' `
        '-o' $outputExe `
        $sourceFile `
        $resourceObj `
        '-luser32' '-lgdi32' '-lkernel32' '-lcomdlg32' '-lcomctl32' '-ldwmapi' '-luxtheme'
    if ($LASTEXITCODE -ne 0) {
        throw "gcc failed with exit code $LASTEXITCODE"
    }
}
finally {
    if (Test-Path -LiteralPath $resourceObj) {
        Remove-Item -LiteralPath $resourceObj -Force
    }
}

Write-Host "Built $outputExe"
