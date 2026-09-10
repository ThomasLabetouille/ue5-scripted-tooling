# Audit de dette technique — Panneau "Outil Blockout"

**Date** : 2026-08-10
**Périmètre** : plugin `BlockoutTools` (panneau `Tools → Outil Blockout`) + scripts Python
associés (`Content/Python/stairs_ramps.py`, `blockout_tool_generate.py`,
`test_blockout_tools.py`).
**Méthode** : audit formel (framework `engineering:tech-debt`), état vérifié directement sur
le disque et dans le code au moment de l'audit — pas une recopie de CLAUDE.md/GAME_MEMORY.md.

Priorité = (Impact + Risque) × (6 − Effort), chaque facteur noté 1-5. Score le plus haut =
à traiter en premier.

---

## Constat général

Sur 16 outils du panneau (voir note sous le tableau sur l'écart avec le chiffre "17" utilisé
ailleurs dans le projet), 5 ont une logique testée automatiquement (`test_blockout_tools.py`,
64e test de `test_suite.run_all()`) : Salle, Couloir, Dalle, Escalier, Rampe. Les 11 autres
n'ont aucun test automatisé — leur seule validation possible aujourd'hui est un test à l'écran
manuel (cohérent avec la règle n°9 du panneau dans CLAUDE.md, mais ça laisse une vraie zone
aveugle pour tout ce qui n'a jamais été testé à l'écran non plus).

En creusant, deux affirmations reprises telles quelles dans la demande initiale se sont
révélées légèrement fausses en vérifiant le code réel — corrigées ci-dessous plutôt que
recopiées :

- **`stairs_ramps.py` n'est PAS en statut ambigu.** Le fichier porte déjà un bandeau `STATUT`
  explicite (ajouté le 2026-07-30) : conservé volontairement comme utilitaire portable
  console, le panneau C++ reste le chemin recommandé. Ce point est déjà réglé — retiré du
  backlog ci-dessous.
- **`EUW_LevelBlockoutTool.uasset` existe toujours** (`Content/RPGTest/UI/`, vérifié sur
  disque à l'instant), avec `blockout_tool_generate.py` déjà marqué obsolète en tête de
  fichier et pointant explicitement vers cet asset orphelin.

Deux trouvailles supplémentaires, non signalées jusqu'ici :

- Le compte "17 outils" utilisé dans CLAUDE.md/GAME_MEMORY.md ne correspond pas au tableau
  qu'il décrit (16 en comptant chaque outil individuellement dans les lignes groupées
  "Salle/Couloir/Dalle" etc.) — écart mineur mais qui vaut la peine d'être corrigé pour ne
  pas fausser un futur audit.
- `test_blockout_tools.py` (ligne 19) renvoie vers "GAME_MEMORY.md session 21" pour la liste
  de ce qui reste à migrer — **cette section n'existe pas** dans GAME_MEMORY.md (vérifié :
  aucune entrée "Session 21" dans le fichier, seulement une "Session 22" qui *mentionne*
  la session 21 sans la détailler). Référence cassée — un futur agent qui la suit perd du
  temps à chercher un contenu qui n'a jamais été écrit.

---

## Backlog priorisé

| # | Item | Catégorie | Impact | Risque | Effort | Priorité |
|---|------|-----------|:-:|:-:|:-:|:-:|
| 1 | Migrer Cône de vision IA / Arche de pont / Tunnel courbé vers `UBlockoutGeometrySubsystem` (UFUNCTION testable) | Test debt | 3 | 3 | 2 | **24** |
| 2 | Tester Swap intelligent (action destructive, déjà signalée "NON VÉRIFIÉ en conditions réelles" dans une session précédente) | Test debt | 3 | 5 | 3 | **24** |
| 3 | Supprimer `EUW_LevelBlockoutTool.uasset` + `blockout_tool_generate.py` | Code debt | 2 | 2 | 1 | **20** |
| 4 | Migrer Alignement sur grille (`SnapPositionIfEnabled`) vers logique testable | Test debt | 2 | 2 | 1 | **20** |
| 5 | Corriger la référence cassée + l'écart de comptage (16 vs "17") dans CLAUDE.md/GAME_MEMORY.md/test_blockout_tools.py | Documentation debt | 1 | 2 | 1 | **15** |
| 6 | Migrer Duplication en série vers logique testable | Test debt | 2 | 2 | 2 | **16** |
| 7 | Migrer Gabarit de référence vers logique testable | Test debt | 2 | 2 | 2 | **16** |
| 8 | Valider (au minimum visuellement, puis tests) le Générateur depuis un plan 2D — jamais confirmé fonctionnel depuis sa création (session 14) | Test debt | 3 | 4 | 4 | **14** |
| 9 | FOV/Frustum, Vérificateur de pente, Vérificateur de hauteur libre — accepter comme dette non testée | Test debt (accepté) | 2 | 2 | 5 | **4** |

### Notes sur le scoring

- **#1 et #2 sont ex æquo en tête** malgré des profils différents : #1 est un gain "mécanique"
  (le patron Escalier/Rampe de la session 21 est directement réutilisable, effort faible pour
  3 outils d'un coup) ; #2 a un risque plus élevé (action destructive, jamais vérifiée
  réellement) mais coûte plus cher à tester proprement (transaction éditeur, setup/teardown
  d'acteurs réels).
- **#8 mérite une attention disproportionnée à son score** : c'est le seul outil du panneau
  qui n'a *jamais* été confirmé fonctionnel une seule fois, contrairement aux autres qui ont au
  moins eu un passage visuel manuel. Le score de priorité (14) le place au milieu du classement
  à cause de l'effort de test élevé (lecture de texture, I/O), mais un simple test visuel
  manuel à l'écran (coût quasi nul, pas un vrai chantier) devrait être fait avant les items
  6-7 même si son score formel est plus bas — ça ne coûte rien et ça referme un vrai inconnu.
- **#9 est un "non" assumé, pas un oubli** : ces 3 outils sont des overlays temps réel qui
  sondent la géométrie réelle du niveau depuis la position de la caméra — les rendre testables
  en pur Python demanderait de mocker un niveau + une caméra, effort disproportionné par
  rapport au risque réel (ce sont des indicateurs visuels, pas des générateurs qui modifient
  le niveau). Le test à l'écran existant (règle n°9 du panneau) reste le bon outil pour ceux-là.

---

## Plan de remédiation par phases

Pensé pour être fait EN PARALLÈLE du travail feature, par petits blocs — pas une session
dédiée de plusieurs heures.

**Phase 1 — gains rapides (< 1h cumulé, aucun rebuild C++ requis)**
- #5 : corriger la référence cassée + le comptage (édition de texte pure).
- #3 : supprimer `EUW_LevelBlockoutTool.uasset` + `blockout_tool_generate.py` — **éditeur
  fermé, suppression manuelle via l'Explorateur Windows** (règle du projet : jamais de
  suppression d'asset en live depuis Python/Content Browser, voir CLAUDE.md "Assets Puzzle
  orphelins"). À faire par Thomas, pas par l'agent.
- Test visuel manuel du Générateur de plan 2D (partie basse-effort de #8) : juste confirmer
  qu'il produit quelque chose de cohérent une fois, sans investir dans des tests automatisés
  tout de suite.

**Phase 2 — migration géométrie (1 session dédiée, 1 rebuild)**
- #1 : Cône / Arche / Tunnel → `UBlockoutGeometrySubsystem`, tests ajoutés à
  `test_blockout_tools.py` en suivant le patron Escalier/Rampe.
- #4 : Alignement sur grille → testable (probablement quasi gratuit à ajouter en même temps
  que #1, c'est une fonction pure déjà isolée).

**Phase 3 — actions et repères (1 session, 1 rebuild)**
- #2 : Swap intelligent — test d'intégration isolé (zone de test dédiée, comme les autres),
  avec une attention particulière au `FScopedTransaction`/undo vu le caractère destructif.
- #6, #7 : Duplication en série, Gabarit de référence.

**Phase 4 — accepté tel quel**
- #9 : documenté comme dette assumée dans CLAUDE.md plutôt que laissé en note éparse — pas de
  travail prévu sauf changement de risque perçu.

---

## Ce qui a déjà été fait pendant cet audit

Sur l'item #5, une seule des deux corrections a été appliquée avec certitude : la référence
cassée dans `test_blockout_tools.py` (pointait vers une section "GAME_MEMORY.md session 21"
qui n'existe pas) a été corrigée pour renvoyer vers ce document. L'écart de comptage
"16 vs 17" n'a PAS été corrigé partout dans CLAUDE.md/GAME_MEMORY.md (7 occurrences trouvées
dans les deux fichiers) : l'historique du panneau montre plusieurs ajouts/retraits d'outils
au fil des sessions (ex. "Presets" ajouté puis retiré le même jour en session 19), donc il
n'est pas certain que "17" ait toujours été faux au moment où chaque ligne a été écrite —
corriger en masse sans vérifier chaque occurrence risquerait d'introduire une nouvelle
inexactitude plutôt que d'en retirer une. Laissé comme note documentée ici plutôt que comme
un edit non vérifié.

Le reste du backlog (#2, #3, #4, #6, #7, #8) reste à faire.

---

## Item #1 — traité le 2026-08-10 (code écrit, REBUILD NON CONFIRMÉ)

Cône de vision / Arche de pont / Tunnel courbé migrés vers `UBlockoutGeometrySubsystem`
(`GenerateVisionCone`, `GenerateBridgeArch`, `ComputeCurvedTunnelPoints` + `GenerateCurvedTunnel`),
même démarche qu'Escalier/Rampe en session 21. Les handlers Slate correspondants dans
`SBlockoutToolPanel_Generators.cpp` ne font plus que lire l'UI et déléguer au sous-système.

Changement annexe nécessaire : `GenerateCorridor` retournait `void` (les appelants
récupéraient les acteurs créés via un diff avant/après de tous les acteurs du niveau) — il
retourne maintenant `TArray<AActor*>`, obligatoire pour que `GenerateCurvedTunnel` puisse
collecter les acteurs de chaque segment sans repasser par un diff de niveau entier. Vérifié
qu'aucun appelant existant ne dépendait du type `void` avant de faire ce changement.

Point notable : `ComputeCurvedTunnelPoints` couvre maintenant automatiquement le sens du
virage (gauche/droite selon le signe de l'angle), qui était explicitement marqué "NON VÉRIFIÉ
VISUELLEMENT" dans le code avant cette migration — c'était la raison principale de traiter cet
item en premier.

5 nouveaux tests ajoutés à `test_blockout_tools.py` (cône, arche, arche sans pilier, points du
tunnel, tunnel complet).

**REBUILD CONFIRMÉ le 2026-08-10** : `Build.bat ... -NoUBA` → `Result: Succeeded`. Puis, éditeur
rouvert, `test_suite.run_all()` exécuté réellement via `ue5_execute` (pas un "OK" silencieux —
sortie contrôlée explicitement) : **64/64 tests passés, 0 skip**, dont **51/51 sous-tests**
pour `blockout_tools` (33 avant cette migration, +18 nouveaux venant des 5 tests ajoutés —
tous passés du premier coup, y compris le test du sens du virage gauche/droite du tunnel).
Aucune régression. Item #1 du backlog considéré fait et vérifié.

---

## Item #2 (Swap intelligent) — traité le 2026-08-10

Sur les 8 items restants du backlog, un examen plus poussé a montré que 7 d'entre eux
(Alignement sur grille, Duplication en série, Gabarit de référence, Générateur de plan 2D,
FOV/Frustum, Vérificateur de pente, Vérificateur de hauteur libre) ne justifient pas
l'investissement d'un test automatisé — soit parce qu'un bug y serait immédiatement visible à
l'écran (fonctions simples), soit parce que le vrai besoin est un test visuel ponctuel plutôt
qu'une suite de tests (Plan2D, jamais confirmé fonctionnel), soit parce que le coût de mock
d'une scène/caméra dépasserait la valeur réelle (les 3 overlays temps réel — dette déjà
assumée). Seul Swap intelligent avait une justification réelle : action **destructive**, déjà
signalée "NON VÉRIFIÉ en conditions réelles" dans le code avant cette session.

Migré vers `UBlockoutGeometrySubsystem::FindActorsByLabelContains` +
`UBlockoutGeometrySubsystem::SwapActorsToMesh`, même démarche que les items précédents. La
transaction éditeur (`FScopedTransaction`, undo Ctrl+Z) reste dans le handler Slate — le
sous-système lui-même ne fait aucune hypothèse sur un contexte d'undo actif, pour rester
appelable proprement depuis un test qui nettoie ses propres acteurs.

3 nouveaux tests ajoutés à `test_blockout_tools.py` (recherche par nom, remplacement complet
avec vérification transform/tag/dossier Outliner/label/échelle native/destruction de
l'original, cas liste vide).

**REBUILD CONFIRMÉ le 2026-08-10** : `Build.bat ... -NoUBA` → `Result: Succeeded`. Éditeur
ouvert, `test_suite.run_all()` exécuté réellement via `ue5_execute` : **64/64 tests passés,
0 skip**, dont **62/62 sous-tests** pour `blockout_tools` (51 avant cette migration, +11
nouveaux — tous passés du premier coup). Aucune régression. Item #2 du backlog considéré fait
et vérifié.

---

## Bilan à ce stade (2026-08-10, avant le chantier ci-dessous)

Items traités et vérifiés en conditions réelles : #1 (Cône/Arche/Tunnel) et #2 (Swap
intelligent) — les deux seuls items du backlog jugés réellement utiles après examen. Items
restants (#3 EUW_LevelBlockoutTool, #6 Duplication en série, #7 Gabarit de référence, #8
Plan2D) laissés délibérément de côté : rapport effort/bénéfice jugé trop faible pour justifier
un nouveau chantier de test, sauf si un bug réel y apparaît un jour (voir raisonnement détaillé
plus haut dans ce document). #9 (overlays FOV/pente/hauteur libre) reste une dette assumée.

---

## Reprise du chantier le 2026-08-10 — vérification du code réel, 3 items révisés

Thomas a redemandé de traiter "les 10 outils Slate-only restants" (décompte informel, périmé
suite aux migrations #1/#2 ci-dessus — en réalité 7 restaient à cette date : FOV, Pente, Hauteur
libre, Gabarit, Plan2D, Alignement sur grille, Duplication en série). Plutôt que de partir du
jugement "pas utile" formulé plus haut (bilan précédent, basé sur une estimation sans relire le
code), un agent a été envoyé lire le code réel des 7 handlers pour vérifier si un cœur de calcul
PUR (sans dépendance à une scène/caméra live) en était extractible.

**Résultat, contraire à l'estimation initiale sur 3 des 7** :
- **FOV/Frustum** : un noyau pur existe (calcul des 4 coins du frustum depuis Apex/Yaw/Pitch/
  FOV/Distance) mais jugé de valeur/effort insuffisant pour ce passage — laissé Slate-only.
- **Vérificateur de pente, Vérificateur de hauteur libre, Gabarit de référence** : confirmés
  **inhérence impurs** (sweeps contre la géométrie réelle du niveau, ou métriques joueur
  live/CDO) — aucun noyau utile à isoler. Restent Slate-only, couverts par
  `Docs/PROTOCOLE_TEST_MANUEL_BlockoutTools.md` (checklist manuelle formalisée, nouveau).
- **Alignement sur grille** (`SnapPositionIfEnabled`) : déjà quasi-pur (ne lit l'état des
  widgets que pour `bEnabled`/`GridSize`, le calcul lui-même est un simple arrondi) — extrait en
  `UBlockoutGeometrySubsystem::SnapToGrid`.
- **Duplication en série** : le calcul des offsets (direction × espacement × index) est pur, la
  duplication elle-même (`EAS->DuplicateActor`) ne l'est pas — offsets extraits en
  `ComputeSeriesOffsets`, le spawn reste dans le handler.
- **Générateur depuis un plan 2D** : le plus gros morceau extractible — tout ce qui suit le
  décodage de l'image (sous-échantillonnage en grille, fusion gloutonne en rectangles maximaux,
  placement monde) est pur. Seule la lecture de la texture/du fichier (`LoadPlanPixels`) reste
  dépendante de l'éditeur. Extrait en `ComputePlanWallRects`. **C'était le seul outil du panneau
  jamais confirmé fonctionnel ne serait-ce qu'une fois (item #8 du backlog original)** — priorité
  la plus justifiée des 3.

**Portée retenue avec Thomas** (question posée avant d'attaquer le C++, vu le coût d'un nouveau
rebuild) : les 2 candidats quasi-gratuits (Alignement sur grille, Duplication en série) + le
Générateur de plan 2D (seul vrai inconnu). FOV explicitement laissé de côté (effort moyen, risque
faible — un frustum mal dessiné se voit immédiatement à l'écran).

**Fait (édition de source uniquement — REBUILD NON CONFIRMÉ à ce stade)** :
- `BlockoutGeometrySubsystem.h/.cpp` : 3 nouvelles `UFUNCTION` (`SnapToGrid`,
  `ComputeSeriesOffsets`, `ComputePlanWallRects`) + nouveau `USTRUCT(BlueprintType)
  FBlockoutPlanWallRect` (Location + Size).
- `SBlockoutToolPanel_Actions.cpp` : `SnapPositionIfEnabled` et `OnSnapSelectionToGridClicked`
  délèguent à `SnapToGrid` (repli sur l'ancien calcul si le sous-système est indisponible) ;
  `OnDuplicateInSeriesClicked` délègue le calcul des offsets à `ComputeSeriesOffsets`, garde le
  `DuplicateActor` local.
- `SBlockoutToolPanel_Plan2D.cpp` : `OnGenerateFromPlanClicked` délègue tout le calcul de
  rectangles à `ComputePlanWallRects`, ne garde que `LoadPlanPixels` + le spawn.
- `test_blockout_tools.py` : 3 nouvelles fonctions de test (`_test_snap_to_grid`,
  `_test_compute_series_offsets`, `_test_compute_plan_wall_rects`), formules vérifiées à la main
  avant d'écrire les assertions (voir commentaires inline pour le détail des calculs attendus).
- `Docs/PROTOCOLE_TEST_MANUEL_BlockoutTools.md` (nouveau) : checklist à l'écran pour FOV/Pente/
  Hauteur libre/Gabarit, avec un journal d'exécution à tenir à jour — remplace le "test à l'œil
  improvisé" par une procédure répétable, sans prétendre automatiser ce qui ne devrait pas
  l'être.

**REBUILD CONFIRMÉ le 2026-08-10** : `Build.bat ... -waitmutex -NoUBA` → `Result: Succeeded`.
Éditeur rouvert, `test_suite.run_all()` exécuté réellement via `ue5_execute` (sanity `print()` de
contrôle avant de faire confiance à la sortie, voir CLAUDE.md règle "silence ≠ succès") :
**64/64 tests passés, 0 skip**, dont **76/76 sous-tests** `blockout_tools` (62 avant cette
migration, +14 nouveaux — tous passés du premier coup, y compris les cas limites GridSize≤0,
Count négatif, masque incohérent). Aucune régression.

**PAS ENCORE FAIT** : un passage réel du Générateur de plan 2D à l'écran (jamais fait depuis sa
création, session 14) pour confirmer que le comportement visuel correspond à ce que les tests
Python vérifient numériquement — `test_suite.run_all()` couvre la LOGIQUE (formules de placement,
fusion de rectangles), pas le rendu final dans le viewport. Idem pour le protocole de test manuel
(`Docs/PROTOCOLE_TEST_MANUEL_BlockoutTools.md`) sur les 4 outils restants : rédigé mais jamais
encore exécuté une seule fois.

### Bilan mis à jour

Items du backlog original désormais traités et vérifiés par rebuild réel : #1, #2, #4
(Alignement sur grille), #6 (Duplication en série), #8 (Générateur de plan 2D, partiellement —
le noyau de calcul est testé et rebuild confirmé, la lecture de texture/fichier et le rendu
visuel final ne le sont toujours pas). #3 (EUW_LevelBlockoutTool, suppression manuelle par
Thomas) et #7 (Gabarit de référence, confirmé non extractible) restent en l'état. #9 (FOV/Pente/
Hauteur libre/Gabarit) reste une dette assumée, désormais couverte par un protocole de test
manuel plutôt que laissée sans procédure du tout — protocole écrit mais pas encore exécuté.
