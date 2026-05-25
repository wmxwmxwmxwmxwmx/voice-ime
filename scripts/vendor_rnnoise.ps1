# 将 simple-rnnoise-wasm 预编译产物复制到 frontend/vendor/rnnoise（无需 npm 全局安装）
param(
    [string]$Version = "1.1.0"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Dest = Join-Path $Root "frontend\vendor\rnnoise"
New-Item -ItemType Directory -Force -Path $Dest | Out-Null

$Base = "https://unpkg.com/simple-rnnoise-wasm@$Version/dist"
$Files = @(
    @{ Url = "$Base/rnnoise.worklet.js"; Out = "rnnoise.worklet.js" },
    @{ Url = "$Base/rnnoise.wasm"; Out = "simple-rnnoise.wasm" }
)

foreach ($f in $Files) {
    Write-Host "Downloading $($f.Out) ..."
    Invoke-WebRequest -Uri $f.Url -OutFile (Join-Path $Dest $f.Out) -UseBasicParsing
}

Write-Host "Done. Files in $Dest"
