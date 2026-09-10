<#
  Recopie depuis le projet UE5 les fichiers de la liste blanche ci-dessous.

  Le projet complet pèse plusieurs gigaoctets (contenu Epic, .uasset, caches).
  Ce dépôt ne contient que du texte : sources C++, Python, harnais d'éval, docs.

  Usage, depuis ce dossier :
      .\sync.ps1                     # source déduite : le dossier parent
      .\sync.ps1 -DryRun             # liste ce qui serait copié, n'écrit rien
      .\sync.ps1 -Source "D:\Travail\ProjectUnreal\GameAnimationSample"

  Après un sync, relire `git status` avant de committer : la liste blanche est
  volontairement stricte, un fichier nouveau n'y entre pas tout seul.
#>

param(
    [string]$Source,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

# $PSScriptRoot n'est pas fiable dans une valeur par défaut de param() selon la
# façon dont le script est lancé : on le résout ici, dans le corps.
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if ([string]::IsNullOrWhiteSpace($ScriptDir)) { $ScriptDir = (Get-Location).Path }
$Dest = $ScriptDir

if ([string]::IsNullOrWhiteSpace($Source)) {
    $Source = Split-Path -Parent $ScriptDir
}

# Fichiers du dépôt qui n'ont pas de source dans le projet : jamais effacés.
$Keep = @('.git', '.github', '.gitignore', 'LICENSE', 'README.md', 'sync.ps1', 'scripts')

# Dossiers recopiés entièrement, moins les exclusions.
$Trees = @(
    'Plugins\BlockoutTools\Source',
    'Plugins\BlockoutTools\Tools',
    'Plugins\BTAuthoringKit\Source',
    'Plugins\BTAuthoringKit\Content\Python',
    'Plugins\RoomGenerator\Source',
    'Plugins\BlueprintPythonUtils\Source',
    'EvalHarness'
)

# Fichiers isolés.
$Files = @(
    'CLAUDE.md',
    'Plugins\BlockoutTools\BlockoutTools.uplugin',
    'Plugins\BlockoutTools\LISEZ-MOI.txt',
    'Plugins\BlockoutTools\Guide_Outil_Blockout.md',
    'Plugins\BTAuthoringKit\BTAuthoringKit.uplugin',
    'Plugins\BTAuthoringKit\README.md',
    'Plugins\RoomGenerator\RoomGenerator.uplugin',
    'Plugins\BlueprintPythonUtils\BlueprintPythonUtils.uplugin',
    'Docs\ASSISE_CONTEXTUELLE.md',
    'Docs\CAS_ETUDE_Eval_Harness_Observabilite.md',
    'Docs\OBSERVABILITE_TRACING.md',
    'Docs\AUDIT_DETTE_TECHNIQUE_BlockoutTools.md',
    'Docs\PROTOCOLE_TEST_MANUEL_BlockoutTools.md',
    'Docs\Guide_Dessin_Blockout.md',
    'Docs\Guide_Outil_Blockout.md',
    'Docs\StairsRamps_Guide.md',
    'Tools\analyze_screenshot.py',
    'Tools\check_content_python_integrity.py',
    'Tools\qc_gate.py',
    'Tools\trust_gate.py',
    'Tools\visual_diff.py',
    'Tools\Package_BlockoutTools.ps1',
    'Content\Python\init_unreal.py',
    'Content\Python\playtest_agent.py',
    'Content\Python\rebuild.py',
    'Content\Python\test_blockout_panels.py',
    'Content\Python\test_npc_behavior.py',
    'Content\Python\test_sit_action.py',
    'Content\Python\test_sit_detection.py',
    'Content\Python\tracing.py',
    'Content\Python\ue5_utils.py'
)

# Jamais recopié, même à l'intérieur d'un dossier de la liste.
$ExcludeDirs  = @('__pycache__', 'Binaries', 'Intermediate', 'Build', 'results', '_to_delete', '.vs')
$ExcludeFiles = @('*.pyc', '*.bak*', '*.b64', '*_reference.py', '*.avant_fusion')

if (-not (Test-Path -LiteralPath $Source)) {
    throw "Projet source introuvable : $Source"
}

Write-Host "Source      : $Source"
Write-Host "Destination : $Dest"
if ($DryRun) { Write-Host "Mode        : simulation, rien n'est ecrit" -ForegroundColor Cyan }
Write-Host ""

function Test-Excluded([string]$RelPath) {
    foreach ($p in ($RelPath -split '[\\/]')) {
        if ($ExcludeDirs -contains $p) { return $true }
    }
    $leaf = Split-Path -Leaf $RelPath
    foreach ($pat in $ExcludeFiles) {
        if ($leaf -like $pat) { return $true }
    }
    return $false
}

# --- 1. vider la destination, sauf ce qui appartient au dépôt lui-même ---
foreach ($item in (Get-ChildItem -LiteralPath $Dest -Force)) {
    if ($Keep -contains $item.Name) { continue }
    if ($DryRun) {
        Write-Host "  supprimerait  $($item.Name)"
    } else {
        Remove-Item -LiteralPath $item.FullName -Recurse -Force
    }
}

# --- 2. recopier ---
$copied  = 0
$missing = New-Object System.Collections.ArrayList

function Copy-One([string]$Rel) {
    $src = Join-Path $Source $Rel
    if (-not (Test-Path -LiteralPath $src)) {
        [void]$missing.Add($Rel)
        return
    }
    $dst = Join-Path $Dest $Rel
    if ($DryRun) {
        Write-Host "  copierait     $Rel"
    } else {
        $dir = Split-Path -Parent $dst
        if (-not (Test-Path -LiteralPath $dir)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
        Copy-Item -LiteralPath $src -Destination $dst -Force
    }
    $script:copied++
}

foreach ($tree in $Trees) {
    $src = Join-Path $Source $tree
    if (-not (Test-Path -LiteralPath $src)) {
        [void]$missing.Add($tree)
        continue
    }
    foreach ($f in (Get-ChildItem -LiteralPath $src -Recurse -File)) {
        $rel = $f.FullName.Substring($Source.Length).TrimStart('\', '/')
        if (-not (Test-Excluded $rel)) { Copy-One $rel }
    }
}

foreach ($f in $Files) { Copy-One $f }

# --- 3. compte rendu ---
Write-Host ""
Write-Host "$copied fichier(s) $(if ($DryRun) { 'a copier' } else { 'copie(s)' })."

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "Absents de la source (deplaces, ou a retirer de la liste) :" -ForegroundColor Yellow
    foreach ($m in $missing) { Write-Host "  $m" -ForegroundColor Yellow }
}

if (-not $DryRun) {
    $files = Get-ChildItem -LiteralPath $Dest -Recurse -File -Force |
             Where-Object { $_.FullName -notlike "*$([IO.Path]::DirectorySeparatorChar).git$([IO.Path]::DirectorySeparatorChar)*" }
    $bytes = ($files | Measure-Object -Property Length -Sum).Sum
    if (-not $bytes) { $bytes = 0 }
    Write-Host ""
    Write-Host ("Taille du depot : {0:N1} Mo  ({1} fichiers)" -f ($bytes / 1MB), $files.Count)
}

Write-Host ""
Write-Host "Relire 'git status' avant de committer."
