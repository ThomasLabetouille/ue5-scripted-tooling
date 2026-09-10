# =============================================================================
#  Fabrique le zip du plugin "Outil Blockout" a envoyer au level designer.
#
#  A lancer dans PowerShell, EDITEUR UE5 FERME, apres avoir compile le projet
#  au moins une fois (les binaires compiles sont ce qui evite au designer
#  d'avoir besoin de Visual Studio).
#
#      cd D:\Travail\ProjectUnreal\GameAnimationSample
#      powershell -ExecutionPolicy Bypass -File Tools\Package_BlockoutTools.ps1
#
#  Produit : D:\Travail\ProjectUnreal\GameAnimationSample\Dist\BlockoutTools_UE5.8.zip
# =============================================================================

$ErrorActionPreference = "Stop"

$ProjectRoot = "D:\Travail\ProjectUnreal\GameAnimationSample"
$PluginSrc   = Join-Path $ProjectRoot "Plugins\BlockoutTools"
$DistDir     = Join-Path $ProjectRoot "Dist"
$StageDir    = Join-Path $DistDir "BlockoutTools"
$ZipPath     = Join-Path $DistDir "BlockoutTools_UE5.8.zip"

if (-not (Test-Path $PluginSrc)) {
    throw "Plugin introuvable : $PluginSrc"
}

# --- Verification : les binaires compiles doivent exister -------------------
# Sans eux, le designer se verrait demander de recompiler (donc Visual Studio).
$BinDir = Join-Path $PluginSrc "Binaries\Win64"
$Dlls = @()
if (Test-Path $BinDir) {
    $Dlls = Get-ChildItem -Path $BinDir -Filter "*BlockoutTools*.dll" -ErrorAction SilentlyContinue
}
if ($Dlls.Count -eq 0) {
    Write-Host ""
    Write-Host "  ATTENTION : aucun binaire compile trouve dans" -ForegroundColor Yellow
    Write-Host "  $BinDir" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  Compile d'abord le projet (editeur ferme) :" -ForegroundColor Yellow
    Write-Host '  & "D:\Logiciel\UE5\UE_5.8\Engine\Build\BatchFiles\Build.bat" GameAnimationSampleEditor Win64 Development "D:\Travail\ProjectUnreal\GameAnimationSample\GameAnimationSample.uproject" -waitmutex -NoUBA' -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  Le zip serait quand meme produit, mais le designer devrait" -ForegroundColor Yellow
    Write-Host "  recompiler lui-meme (Visual Studio requis) - ce qu'on veut eviter." -ForegroundColor Yellow
    Write-Host ""
    $reponse = Read-Host "  Continuer quand meme ? (o/N)"
    if ($reponse -ne "o") { Write-Host "Annule."; exit 1 }
}

# --- Preparation d'une copie propre ----------------------------------------
if (Test-Path $StageDir) { Remove-Item $StageDir -Recurse -Force }
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

Copy-Item -Path (Join-Path $PluginSrc "*") -Destination $StageDir -Recurse -Force

# Intermediate = fichiers temporaires de compilation, inutiles et volumineux.
$Inter = Join-Path $StageDir "Intermediate"
if (Test-Path $Inter) { Remove-Item $Inter -Recurse -Force }

# Les .pdb (symboles de debug) pesent tres lourd et ne servent pas au designer.
Get-ChildItem -Path $StageDir -Filter "*.pdb" -Recurse -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue

# --- Creation du zip --------------------------------------------------------
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
Compress-Archive -Path $StageDir -DestinationPath $ZipPath -CompressionLevel Optimal

Remove-Item $StageDir -Recurse -Force

$SizeMo = [math]::Round((Get-Item $ZipPath).Length / 1MB, 1)

Write-Host ""
Write-Host "  Zip cree : $ZipPath  ($SizeMo Mo)" -ForegroundColor Green
Write-Host ""
Write-Host "  A envoyer tel quel au level designer." -ForegroundColor Green
Write-Host "  Il decompresse, et met le dossier BlockoutTools dans le dossier" -ForegroundColor Green
Write-Host "  Plugins/ de son projet (tout est explique dans LISEZ-MOI.txt)." -ForegroundColor Green
Write-Host ""
