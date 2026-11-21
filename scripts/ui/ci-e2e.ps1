# 界面 e2e（Windows 侧）。WSL 里没有 Playwright 和 Chrome，所以单独一条。
#
#   pwsh -File scripts/ui/ci-e2e.ps1
#
# 依赖：Node + Playwright（NODE_PATH 指向全局安装）+ Chrome。
# 会自动起前端服务、跑完关掉；失败退出码非零。

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSCommandPath))
$port = if ($env:E2E_PORT) { $env:E2E_PORT } else { 8799 }

if (-not $env:NODE_PATH) {
    $env:NODE_PATH = "C:\Users\Administrator\AppData\Roaming\npm\node_modules\@playwright\mcp\node_modules"
}

Write-Host "== 起前端服务 :$port ==" -ForegroundColor Cyan
$srv = $null
try {
    $probe = Test-NetConnection -ComputerName 127.0.0.1 -Port $port -InformationLevel Quiet -WarningAction SilentlyContinue
} catch { $probe = $false }

if (-not $probe) {
    $srv = Start-Process -FilePath "wsl" -ArgumentList "-u","root","--","bash","-lc",
        "cd /home/rvsim/web && python3 -m http.server $port" -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 2
}

Write-Host "== 界面 e2e ==" -ForegroundColor Cyan
Push-Location $root
try {
    node scripts/ui/e2e.js
    $code = $LASTEXITCODE
} finally {
    Pop-Location
    if ($srv) { Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue }
}

if ($code -ne 0) {
    Write-Host "界面 e2e 失败（退出码 $code）" -ForegroundColor Red
    exit $code
}
Write-Host "界面 e2e 通过" -ForegroundColor Green
