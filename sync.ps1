<#
  Recopie depuis le projet UE5 les fichiers de la liste blanche ci-dessous.

  Le projet complet pèse plusieurs gigaoctets (contenu Epic, .uasset, caches).
  Ce dépôt ne contient que du texte : sources C++, Python, harnais d'éval, docs.

  Usage, depuis ce dossier :
      .\sync.ps1                     # source déduite : le dossier parent
      .\sync.ps1 -Source "D:\...\GameAnimationSample"
      .\sync.ps1 -WhatIf             # liste ce qui serait copié, n'écrit rien

  Après un sync, relire `git status` avant de committer : la liste blanche est
  volontairement stricte, un fichier nouveau n'y entre pas tout seul.
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$Source = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$Dest = $PSScriptRoot

# Fichiers du dépôt qui n'ont pas de source dans le projet : jamais effacés.
$Keep = @('.git', '.github', '.gitignore', 'LICENSE', 'README.md', 'sync.ps1', 'scripts')

# Dossiers recopiés entièrement, moins les exclusions.
$Trees = @(
    'Plugins/BlockoutTools/Source',
    'Plugins/BlockoutTools/Tools',
    'Plugins/BTAuthoringKit/Source',
    'Plugins/BTAuthoringKit/Content/Python',
    'Plugins/RoomGenerator/Source',
    'Plugins/BlueprintPythonUtils/Source',
    'EvalHarness'
)

# Fichiers isolés.
$Files = @(
    'CLAUDE.md',
    'Plugins/BlockoutTools/BlockoutTools.uplugin',
    'Plugins/BlockoutTools/LISEZ-MOI.txt',
    'Plugins/BlockoutTools/Guide_Outil_Blockout.md',
    'Plugins/BTAuthoringKit/BTAuthoringKit.uplugin',
    'Plugins/BTAuthoringKit/README.md',
    'Plugins/RoomGenerator/RoomGenerator.uplugin',
    'Plugins/BlueprintPythonUtils/BlueprintPythonUtils.uplugin',
    'Docs/ASSISE_CONTEXTUELLE.md',
    'Docs/CAS_ETUDE_Eval_Harness_Observabilite.md',
    'Docs/OBSERVABILITE_TRACING.md',
    'Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md',
    'Docs/PROTOCOLE_TEST_MANUEL_BlockoutTools.md',
    'Docs/Guide_Dessin_Blockout.md',
    'Docs/Guide_Outil_Blockout.md',
    'Docs/StairsRamps_Guide.md',
    'Tools/analyze_screenshot.py',
    'Tools/check_content_python_integrity.py',
    'Tools/qc_gate.py',
    'Tools/trust_gate.py',
    'Tools/visual_diff.py',
    'Tools/Package_BlockoutTools.ps1',
    'Content/Python/init_unreal.py',
    'Content/Python/playtest_agent.py',
    'Content/Python/rebuild.py',
    'Content/Python/test_blockout_panels.py',
    'Content/Python/test_npc_behavior.py',
    'Content/Python/test_sit_action.py',
    'Content/Python/test_sit_detection.py',
    'Content/Python/tracing.py',
    'Content/Python/ue5_utils.py'
)

# Jamais recopié, même à l'intérieur d'un dossier de la liste.
$ExcludeDirs  = @('__pycache__', 'Binaries', 'Intermediate', 'Build', 'results', '_to_delete', '.vs')
$ExcludeFiles = @('*.pyc', '*.bak*', '*.b64', '*_reference.py', '*.avant_fusion')

if (-not (Test-Path -LiteralPath $Source)) {
    throw "Projet source introuvable : $Source"
}
Write-Host "Source      : $Source"
Write-Host "Destination : $Dest`n"

function Test-Excluded {
    param([string]$RelPath)
    $parts = $RelPath -split '[\\/]'
    foreach ($p in $parts) { if ($ExcludeDirs -contains $p) { return $true } }
    $leaf = $parts[-1]
    foreach ($pat in $ExcludeFiles) { if ($leaf -like $pat) { return $true } }
    return $false
}

# --- 1. vider la destination, sauf ce qui appartient au dépôt lui-même ---
Get-ChildItem -LiteralPath $Dest -Force |
    Where-Object { $Keep -notcontains $_.Name } |
    ForEach-Object {
        if ($PSCmdlet.ShouldProcess($_.FullName, 'Supprimer')) {
            Remove-Item -LiteralPath $_.FullName -Recurse -Force
        }
    }

# --- 2. recopier ---
$copied = 0
$missing = @()

function Copy-One {
    param([string]$Rel)
    $src = Join-Path $Source $Rel
    if (-not (Test-Path -LiteralPath $src)) { $script:missing += $Rel; return }
    $dst = Join-Path $Dest $Rel
    $dir = Split-Path -Parent $dst
    if ($PSCmdlet.ShouldProcess($Rel, 'Copier')) {
        if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
        Copy-Item -LiteralPath $src -Destination $dst -Force
    }
    $script:copied++
}

foreach ($tree in $Trees) {
    $src = Join-Path $Source $tree
    if (-not (Test-Path -LiteralPath $src)) { $missing += $tree; continue }
    Get-ChildItem -LiteralPath $src -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($Source.Length).TrimStart('\', '/')
        if (-not (Test-Excluded $rel)) { Copy-One $rel }
    }
}
foreach ($f in $Files) { Copy-One $f }

# --- 3. compte rendu ---
Write-Host "$copied fichier(s) copié(s)."
if ($missing.Count) {
    Write-Host "`nAbsents de la source (à retirer de la liste, ou déplacés) :" -ForegroundColor Yellow
    $missing | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
}

$bytes = (Get-ChildItem -LiteralPath $Dest -Recurse -File -Force |
          Where-Object { $_.FullName -notmatch '\\\.git\\' } |
          Measure-Object -Property Length -Sum).Sum
Write-Host ("`nTaille du dépôt : {0:N1} Mo" -f ($bytes / 1MB))
Write-Host "Relire 'git status' avant de committer."
