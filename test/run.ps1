$CC = "cl"
$CFLAGS = @("/std:c17", "/O2")
$PASS = 0; $FAIL = 0

# --- Selftest: internal consistency check via /DSELFTEST ---
Write-Host "[selftest] " -NoNewline
& $CC $CFLAGS /DSELFTEST compress.c /Fetest_selftest 2>$null
if ($LASTEXITCODE -eq 0) {
    & .\test_selftest test/input/compress.c -o test_selftest_out.c 2>$null
    if ($LASTEXITCODE -eq 0) { Write-Host "OK"; $PASS++ }
    else { Write-Host "FAIL"; $FAIL++ }
} else { Write-Host "FAIL (compile)"; $FAIL++ }

# --- Round-trip tests ---
New-Item -ItemType Directory -Force -Path test/work | Out-Null
foreach ($f in Get-ChildItem test/input) {
    $name = $f.Name
    Write-Host ("  {0,-25} " -f $name) -NoNewline
    $orig = $f.Length

    & .\ccompress "test/input/$name" -o "test/work/$name.c" 2>$null
    if ($LASTEXITCODE -ne 0) { Write-Host "FAIL (compress)"; $FAIL++; continue }

    # Debug: show N array from generated C file
    $gen = Get-Content "test/work/$name.c" -Raw
    if ($gen -match 'static const char\*N\[\]=\{') {
        $match = [regex]::Match($gen, 'static const char\*N\[\]=\{(.*?)\};', [System.Text.RegularExpressions.RegexOptions]::Singleline)
        Write-Host "  [dbg N array: $($match.Groups[1].Value)]"
    }

    & $CC $CFLAGS "test/work/$name.c" "/Fetest/work/${name}_extract.exe" 2>$null
    if ($LASTEXITCODE -ne 0) { Write-Host "FAIL (compile)"; $FAIL++; continue }

    New-Item -ItemType Directory -Force -Path "test/work/test/input" | Out-Null
    Push-Location test/work
    $out = & .\${name}_extract.exe 2>&1
    $exitCode = $LASTEXITCODE
    Write-Host "  [dbg extract exit: $exitCode]"
    Write-Host "  [dbg extract out: $out]"
    Pop-Location

    $extracted = "test/work/test/input/$name"
    Write-Host "  [dbg checking $extracted]"
    Write-Host "  [dbg exists: $(Test-Path $extracted)]"
    # Check if file was created in wrong location
    $wrong = "test/work/$name"
    Write-Host "  [dbg wrong loc exists: $(Test-Path $wrong)]"
    if (Test-Path $extracted) {
        $size = (Get-Item $extracted).Length
        git diff --exit-code --no-index --ignore-cr-at-eol "test/input/$name" $extracted 2>$null
        if ($LASTEXITCODE -eq 0) {
            Write-Host ("OK  {0} -> {1}" -f $orig, $size); $PASS++
        } else { Write-Host "FAIL (diff)"; $FAIL++ }
    } else { Write-Host "FAIL (extract)"; $FAIL++ }
}
Remove-Item -Recurse -Force test/work -ErrorAction SilentlyContinue

# --- Argo.c self-extracting archive test ---
Write-Host "[Argo.c self-extract] " -NoNewline
Remove-Item -Recurse -Force test/work_argo -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path test/work_argo | Out-Null
& .\ccompress compress.c Makefile README.md -o test/work_argo/Argo.c 2>$null
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL (compress)"; $FAIL++; exit 1 }

& $CC $CFLAGS test/work_argo/Argo.c "/Fetest/work_argo/extract" 2>$null
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL (compile)"; $FAIL++; exit 1 }

Push-Location test/work_argo
    $out = & .\extract 2>&1
    $exitCode = $LASTEXITCODE
    Write-Host "  [dbg argo extract exit: $exitCode]"
    Write-Host "  [dbg argo extract out: $out]"
    Pop-Location

$ok = $true
foreach ($fn in @("compress.c", "Makefile", "README.md")) {
    if (-not (Test-Path "test/work_argo/$fn")) { $ok = $false }
}
if ($ok) {
    $d1 = git diff --exit-code --no-index --ignore-cr-at-eol compress.c test/work_argo/compress.c 2>$null
    $d2 = git diff --exit-code --no-index --ignore-cr-at-eol Makefile test/work_argo/Makefile 2>$null
    $d3 = git diff --exit-code --no-index --ignore-cr-at-eol README.md test/work_argo/README.md 2>$null
    if ($LASTEXITCODE -eq 0) { Write-Host "OK"; $PASS++ }
    else { Write-Host "FAIL (diff)"; $FAIL++ }
} else { Write-Host "FAIL (extract)"; $FAIL++ }
Remove-Item -Recurse -Force test/work_argo -ErrorAction SilentlyContinue

Write-Host "---"
Write-Host ("Passed: {0}  Failed: {1}" -f $PASS, $FAIL)
if ($FAIL -gt 0) { exit 1 }
