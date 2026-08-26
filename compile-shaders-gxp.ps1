# compile rsc-c's Cg shaders to GXP binaries for the Vita hardware renderer
$cgc = "C:\Vita\sdk\host_tools\bin\psp2cgc.exe"
$env:Path = "C:\Vita\sdk\host_tools\bin;C:\Vita\sdk\host_tools\lib;" + $env:Path
$cache = Join-Path $PSScriptRoot "cache"

$vs = @("flat","pick","game-model")
$ok = $true
foreach ($n in $vs) {
    $src = Join-Path $cache "$n.vs.cg"; $out = Join-Path $cache "$n.vs.gxp"
    & $cgc -profile sce_vp_psp2 -o $out $src 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $out)) { Write-Host "FAILED: $n.vs" -ForegroundColor Red; $ok = $false }
    else { Write-Host ("OK  {0,-16} vs.gxp = {1} bytes" -f $n, (Get-Item $out).Length) -ForegroundColor Green }

    $src = Join-Path $cache "$n.fs.cg"; $out = Join-Path $cache "$n.fs.gxp"
    & $cgc -profile sce_fp_psp2 -o $out $src 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $out)) { Write-Host "FAILED: $n.fs" -ForegroundColor Red; $ok = $false }
    else { Write-Host ("OK  {0,-16} fs.gxp = {1} bytes" -f $n, (Get-Item $out).Length) -ForegroundColor Green }
}

# fragment-only no-discard variant of game-model
& $cgc -profile sce_fp_psp2 -o (Join-Path $cache "game-model-noclip.fs.gxp") (Join-Path $cache "game-model-noclip.fs.cg")
if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $cache "game-model-noclip.fs.gxp"))) { Write-Host "FAILED: game-model-noclip.fs" -ForegroundColor Red; $ok = $false }
else { Write-Host ("OK  {0,-16} fs.gxp = {1} bytes" -f "game-model-noclip", (Get-Item (Join-Path $cache "game-model-noclip.fs.gxp")).Length) -ForegroundColor Green }

if ($ok) { "`nAll shaders compiled to GXP." } else { "`nSome shaders FAILED, see errors above." }