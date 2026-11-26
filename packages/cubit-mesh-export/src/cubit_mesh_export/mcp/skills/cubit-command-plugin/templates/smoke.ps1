# Load the freshly built plugin into a disposable headless Cubit and check
# that both commands run.  Never copies into the Cubit install, so it is
# safe while another user has the deployed plugins open.
#   pwsh -File smoke.ps1 [-Plugin build\my_cubit_plugin.dll]
param(
    [string]$CubitInstallDir = $(if ($env:CUBIT_INSTALL_DIR) { $env:CUBIT_INSTALL_DIR } else { 'C:\Program Files\Coreform Cubit 2026.8' }),
    [string]$Plugin = (Join-Path $PSScriptRoot 'build\my_cubit_plugin.dll'),
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Plugin)) { throw "Plugin not found: $Plugin" }

$bin = Join-Path $CubitInstallDir 'bin'
$console = Join-Path $bin 'cubitx.exe'          # 2026.8 console launcher
if (-not (Test-Path -LiteralPath $console)) {
    $console = Join-Path $bin 'coreform_cubit.com' # older releases
}
if (-not (Test-Path -LiteralPath $console)) { throw "No headless Cubit launcher in $bin" }

$work = Join-Path ([IO.Path]::GetTempPath()) ("cubit_plugin_smoke_" + [guid]::NewGuid().ToString('N'))
$plugins = Join-Path $work 'plugins'
New-Item -ItemType Directory -Force -Path $plugins | Out-Null
try {
    # The 2026.8 loader scans *.dll in CUBIT_PLUGIN_DIR; older releases take
    # -commandplugindir <dir>.  Stage a copy under the loader's name.
    Copy-Item -LiteralPath $Plugin -Destination (Join-Path $plugins ([IO.Path]::GetFileNameWithoutExtension($Plugin) + '.dll'))
    # A non-ASCII directory proves the UTF-8 -> UTF-16 path conversion.
    $outDir = Join-Path $work ([string][char]0x65E5 + [char]0x672C + [char]0x8A9E)   # 日本語
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $out = Join-Path $outDir 'counts.txt'
    $jou = Join-Path $work 'driver.jou'
    @(
        'reset'
        'brick x 1'
        'volume 1 size 0.25'
        'mesh volume 1'
        'block 1 volume 1'
        'myplugin hello "smoke" count 2'
        ('export nodecount "{0}" overwrite' -f $out)
        'exit 0'
    ) | Set-Content -LiteralPath $jou -Encoding utf8

    $psi = [Diagnostics.ProcessStartInfo]::new($console)
    foreach ($a in '-nographics', '-batch', '-nojournal', '-noinitfile') { $psi.ArgumentList.Add($a) }
    if ($console.EndsWith('.com')) { $psi.ArgumentList.Add('-commandplugindir'); $psi.ArgumentList.Add($plugins) }
    $psi.ArgumentList.Add($jou)
    $psi.WorkingDirectory = $work
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardInput = $true   # closed below: Cubit must not wait on stdin
    $psi.Environment['CUBIT_PLUGIN_DIR'] = $plugins
    $acis = Join-Path $CubitInstallDir 'acis\code\bin'
    if (Test-Path $acis) { $psi.Environment['PATH'] = "$acis;$($psi.Environment['PATH'])" }

    $p = [Diagnostics.Process]::Start($psi)
    $p.StandardInput.Close()
    $stdout = $p.StandardOutput.ReadToEndAsync()
    $stderr = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($TimeoutSeconds * 1000)) { $p.Kill($true); throw 'Cubit timed out' }
    $log = $stdout.Result + $stderr.Result
    Set-Content -LiteralPath (Join-Path $work 'cubit.log') -Value $log -Encoding utf8

    $hello = ([regex]::Matches($log, 'MYPLUGIN_HELLO smoke')).Count
    $export = [regex]::Match($log, 'MYPLUGIN_EXPORT nodes=(\d+) blocks=(\d+) elements=(\d+)')
    $ok = ($hello -eq 2) -and $export.Success -and (Test-Path -LiteralPath $out) -and [int]$export.Groups[1].Value -gt 0
    if (-not $ok) {
        Write-Host $log
        throw "Smoke failed (exit=$($p.ExitCode), hello=$hello, export=$($export.Success)); log kept in $work"
    }
    Write-Host "SMOKE OK exit=$($p.ExitCode) hello=$hello $($export.Value)"
    Get-Content -LiteralPath $out
    Remove-Item -LiteralPath $work -Recurse -Force
} catch {
    Write-Host "Work directory kept: $work"
    throw
}
