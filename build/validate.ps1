# Render every .riv in a folder with rivshot and tally pass/fail.
param(
  [string]$Dir = "C:\Users\gbast\espressif-jtag\rive-py\examples\marketplace",
  [int]$Size = 256
)
$exe = "C:\Users\gbast\espressif-jtag\rive-win-preview\build\bin\rivshot.exe"
$outDir = "C:\Users\gbast\espressif-jtag\rive-win-preview\build\thumbs"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$files = Get-ChildItem -Path $Dir -Filter *.riv
$ok = 0; $fail = 0; $failed = @()
foreach ($f in $files) {
  $out = Join-Path $outDir ($f.BaseName + ".png")
  $null = & $exe $f.FullName $out $Size 0 2>&1
  if ($LASTEXITCODE -eq 0) { $ok++ } else { $fail++; $failed += $f.Name }
}
Write-Output "TOTAL=$($files.Count) OK=$ok FAIL=$fail"
if ($failed.Count -gt 0) {
  Write-Output "FAILED FILES:"
  $failed | ForEach-Object { Write-Output "  $_" }
}
