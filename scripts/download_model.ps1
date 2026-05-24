param(
    [string]$Model = "base",
    [string]$OutputDir = "models"
)

$ErrorActionPreference = "Stop"

$models = @{
    "base"    = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin"
    "base.en" = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin"
    "tiny"    = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin"
    "small"   = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin"
}

if (-not $models.ContainsKey($Model)) {
    Write-Error "未知模型 '$Model'。可选：$($models.Keys -join ', ')"
}

$fileName = "ggml-$Model.bin"
$url = $models[$Model]
$destDir = Join-Path (Split-Path $PSScriptRoot -Parent) $OutputDir
$destPath = Join-Path $destDir $fileName

New-Item -ItemType Directory -Force -Path $destDir | Out-Null

if (Test-Path $destPath) {
    Write-Host "模型已存在：$destPath"
    exit 0
}

Write-Host "正在下载 $fileName ..."
Invoke-WebRequest -Uri $url -OutFile $destPath -UseBasicParsing
Write-Host "已保存至 $destPath"
