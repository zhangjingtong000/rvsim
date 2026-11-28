# 界面 e2e（Windows 侧）。WSL 里没有 Playwright 和 Chrome，所以单独一条。
#
#   pwsh -File scripts/ui/ci-e2e.ps1
#
# 依赖：Node ≥ 20 + Playwright（NODE_PATH 指向安装位置）+ Chrome。
# 会自动起前端服务（WSL 里的 python3 http.server）、跑完关掉；失败退出码非零。

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$port = if ($env:E2E_PORT) { $env:E2E_PORT } else { 8799 }

# Playwright 找法：先看 NODE_PATH 是否已给，其次全局 npm 根下的 @playwright/mcp。
if (-not $env:NODE_PATH) {
    $roots = @()
    if ($env:APPDATA) { $roots += (Join-Path $env:APPDATA "npm\node_modules") }
    try {
        $gr = & npm root -g 2>$null
        if ($gr) { $roots += ("$($gr | Select-Object -First 1)").Trim() }
    } catch { }
    foreach ($r in $roots) {
        if (-not $r) { continue }
        $mcp = Join-Path $r "@playwright\mcp\node_modules"
        if (Test-Path (Join-Path $mcp "playwright")) { $env:NODE_PATH = $mcp; break }
        if (Test-Path (Join-Path $r "playwright")) { $env:NODE_PATH = $r; break }
    }
    if (-not $env:NODE_PATH) {
        Write-Host "找不到 playwright：先装（npm i -D playwright 或 npm i -g @playwright/mcp），或手动设置 NODE_PATH" -ForegroundColor Red
        exit 2
    }
}

Write-Host "== 起前端服务 :$port ==" -ForegroundColor Cyan
$srv = $null
try {
    $probe = Test-NetConnection -ComputerName 127.0.0.1 -Port $port -InformationLevel Quiet -WarningAction SilentlyContinue
} catch { $probe = $false }

if (-not $probe) {
    # 前端服务跑在 WSL 里，路径要从本脚本的仓库根推出来，
    # 不能写死 /home/rvsim/web（那是开发机上的另一份 clone，跑的是旧前端）。
    $winWeb = Join-Path $root "web"
    $linuxWeb = $null
    if ($winWeb -match '^([A-Za-z]):\\(.*)$') {
        $linuxWeb = "/mnt/" + $Matches[1].ToLower() + "/" + ($Matches[2] -replace '\\', '/')
    } else {
        try { $linuxWeb = (& wsl -u root -- wslpath -a "$winWeb").Trim() } catch { }
    }
    if (-not $linuxWeb) {
        Write-Host "无法把 $winWeb 转成 WSL 路径" -ForegroundColor Red
        exit 2
    }
    # Start-Process 不会给含空格的参数自动加引号，bash -lc 的整串命令要自己包一层，
    # 否则 wsl 收到的是被拆开的词，服务起不来。
    $bashCmd = "cd '$linuxWeb' && exec python3 -m http.server $port"
    $srv = Start-Process -FilePath "wsl" -ArgumentList @("-u","root","--","bash","-lc","`"$bashCmd`"") `
        -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 3
    try {
        if (-not (Test-NetConnection -ComputerName 127.0.0.1 -Port $port -InformationLevel Quiet -WarningAction SilentlyContinue)) {
            Write-Host "前端服务没起来：$linuxWeb（端口 $port）" -ForegroundColor Red
            exit 2
        }
    } catch { }
}

Write-Host "== 界面 e2e ==" -ForegroundColor Cyan
Push-Location $root
try {
    node scripts/ui/e2e.js
    $code = $LASTEXITCODE
} finally {
    Pop-Location
    if ($srv) {
        Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue
        # wsl.exe 被杀不代表里面那个 python 也退出，按端口兜底清一次
        & wsl -u root -- bash -lc "fuser -k $port/tcp 2>/dev/null; true" | Out-Null
    }
}

if ($code -ne 0) {
    Write-Host "界面 e2e 失败（退出码 $code）" -ForegroundColor Red
    exit $code
}
Write-Host "界面 e2e 通过" -ForegroundColor Green
