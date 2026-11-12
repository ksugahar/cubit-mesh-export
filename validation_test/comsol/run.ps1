[CmdletBinding()]
param([switch]$ExportOnly)

$ErrorActionPreference = 'Stop'
$required = @('COMSOL_CI_CUBIT_ROOT', 'COMSOL_CI_PYTHON_SITE', 'COMSOL_CI_EVIDENCE_ROOT')
if (-not $ExportOnly) {
    $required += @('COMSOL_CI_MATLAB_EXE', 'COMSOL_CI_MLI', 'COMSOL_CI_PORT')
}
foreach ($name in $required) {
    $value = [Environment]::GetEnvironmentVariable($name)
    if ([string]::IsNullOrWhiteSpace($value)) { throw "Missing runner configuration: $name" }
    if ($name -ne 'COMSOL_CI_PORT' -and -not (Test-Path -LiteralPath $value)) {
        throw "Configured path for $name does not exist"
    }
}
if (-not $ExportOnly) {
    $port = 0
    if (-not [int]::TryParse($env:COMSOL_CI_PORT, [ref]$port) -or $port -lt 1 -or $port -gt 65535) {
        throw 'COMSOL_CI_PORT must specify the dedicated local listener'
    }
    $listeners = @(Get-NetTCPConnection -LocalPort $port -State Listen |
        Select-Object -ExpandProperty OwningProcess -Unique)
    if ($listeners.Count -ne 1 -or (Get-Process -Id $listeners[0]).ProcessName -ne 'comsolmphserver') {
        throw 'Expected one dedicated COMSOL server process on the configured port'
    }
    $server = Get-Process -Id $listeners[0]
    $clients = @(Get-NetTCPConnection -LocalPort $port -State Established -ErrorAction SilentlyContinue)
    if ($clients.Count -ne 0) { throw 'The dedicated COMSOL listener already has a client' }
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$cubitRoot = $env:COMSOL_CI_CUBIT_ROOT
if (-not (Select-String -LiteralPath (Join-Path $cubitRoot 'BUILD_INFO') -Pattern '^short\s*=\s*2026\.8\s*$')) {
    throw 'This validation requires Cubit 2026.8'
}
$cubit = Join-Path $cubitRoot 'bin/cubitx.exe'
$env:COMSOL_CI_EXPORTER_SRC = Join-Path $repo 'packages/cubit-mesh-export/src'
$env:PATH = (Join-Path $cubitRoot 'acis/code/bin') + ';' + $env:PATH
$env:OMP_NUM_THREADS = '1'
$env:OPENBLAS_NUM_THREADS = '1'
$env:MKL_NUM_THREADS = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'
foreach ($name in @('PYTHONHOME', 'PYTHONPATH', 'VIRTUAL_ENV')) {
    [Environment]::SetEnvironmentVariable($name, $null, 'Process')
}

$start = [DateTimeOffset]::UtcNow
$root = Join-Path $env:COMSOL_CI_EVIDENCE_ROOT ('webcut-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$env:COMSOL_CI_JOB_ROOT = $root
$env:COMSOL_CI_SCRIPT_ROOT = $PSScriptRoot
$owner = [ordered]@{
    source = (git -C $repo rev-parse HEAD)
    exporter_sha256 = (Get-FileHash (Join-Path $env:COMSOL_CI_EXPORTER_SRC 'cubit_mesh_export/comsol.py')).Hash
    journal_sha256 = (Get-FileHash (Join-Path $PSScriptRoot 'webcut.jou')).Hash
    verifier_sha256 = (Get-FileHash (Join-Path $PSScriptRoot 'verify_direct.m')).Hash
    owner = 'Cubit COMSOL validation'
    start_utc = $start.ToString('o')
    deadline_utc = $start.AddMinutes(12).ToString('o')
    cubit_threads = 1
    dedicated_comsol_pid = if ($ExportOnly) { $null } else { $server.Id }
    dedicated_comsol_start = if ($ExportOnly) { $null } else { $server.StartTime.ToUniversalTime().ToString('o') }
}
$owner | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'owner.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'webcut.jou'), (Join-Path $PSScriptRoot 'verify_direct.m') -Destination $root
Write-Host "Evidence run: $(Split-Path -Leaf $root)"

Push-Location -LiteralPath $root
try {
    & $cubit -batch -nographics -nojournal -noinitfile -workingdir $root -input (Join-Path $PSScriptRoot 'webcut.jou') *> cubit.log
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath 'webcut-direct.mphtxt.json')) {
        throw 'Cubit export failed; see cubit.log'
    }
    $meta = Get-Content -LiteralPath 'webcut-direct.mphtxt.json' -Raw | ConvertFrom-Json
    if ((Get-FileHash 'webcut-direct.mphtxt').Hash -ne $meta.mphtxt_sha256) { throw 'MPHTXT hash mismatch' }
    if ($ExportOnly) { return }

    $command = "addpath(getenv('COMSOL_CI_MLI')); addpath(getenv('COMSOL_CI_SCRIPT_ROOT')); mphstart('127.0.0.1',str2double(getenv('COMSOL_CI_PORT'))); verify_direct(getenv('COMSOL_CI_JOB_ROOT'));"
    & $env:COMSOL_CI_MATLAB_EXE -batch $command *> matlab.log
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath 'comsol-result.json')) {
        throw 'COMSOL validation failed; see matlab.log and any result JSON'
    }
    $result = Get-Content -LiteralPath 'comsol-result.json' -Raw | ConvertFrom-Json
    if ($result.status -ne 'MEASURED' -or -not $result.prior_models_preserved) { throw 'Invalid COMSOL result' }
    if ($result.displacement_error_m -gt 1e-12 -or $result.temperature_error_K -gt 1e-9) {
        throw 'COMSOL result does not agree with the analytic fixture within the CI bounds'
    }
    Write-Host "Displacement error (m): $($result.displacement_error_m); temperature error (K): $($result.temperature_error_K)"
}
finally {
    Get-ChildItem -File | ForEach-Object { '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName).Hash, $_.Name } |
        Set-Content -LiteralPath 'SHA256SUMS' -Encoding utf8
    Pop-Location
}
