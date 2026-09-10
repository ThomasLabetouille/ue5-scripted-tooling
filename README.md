# ue5-scripted-tooling

Outillage Unreal Engine 5.8 écrit sous une contrainte tenue du début à la fin : **rien ne se
pilote à l'écran**. Construire une salle, câbler un Behavior Tree, jouer une session pour vérifier
qu'un PNJ réagit — tout passe par du script. C'est cette contrainte qui a décidé de ce qu'il
fallait écrire.

Quatre plugins, un toolchain Python, et un harnais qui évalue le comportement de l'agent IA qui
pilote l'ensemble.

Le code C++ et Python est écrit en grande partie par un agent Claude, branché sur l'éditeur via
MCP. La partie qui m'a demandé le plus de travail n'est pas la génération : c'est ce qui vérifie
que le résultat fait ce qu'il prétend.

---

## Ce qu'il y a dedans

### BlockoutTools — level design dans le viewport

Un panneau Slate (`Tools > Outil Blockout`) avec trois façons de poser de la géométrie : une salle
paramétrique, un contour dessiné à la souris puis extrudé en direct (Ctrl contraint aux angles
droits, Ctrl+F ferme d'équerre), et une découpe qui perce portes et fenêtres sur la surface
survolée.

La découpe ne retire pas de matière. Le panneau garde sa description logique — contour, épaisseur,
liste des trous — sur un composant sérialisé, et régénère le maillage entier à chaque fois. C'est
ce qui permet de percer vingt ouvertures sans dégradation, d'en retirer une, ou d'annuler.

Le panneau est utilisé par quelqu'un qui ne programme pas. D'où `LISEZ-MOI.txt` (installation en
trois minutes, sans jargon) et une ligne de statut en bas du panneau qui donne toujours la raison
quand un outil refuse d'agir.

### BTAuthoringKit — Behavior Trees par script

Un plugin C++ qui expose une API pour construire des Behavior Trees complets : Blackboard,
composites, tasks, decorators, services, graphe visuel inclus. Toutes les fonctions sont
`BlueprintCallable`, donc appelables depuis Python.

Il existe parce qu'il manquait. Ni l'API Python d'Unreal ni l'outillage Blueprint habituel ne
savent éditer le graphe d'un Behavior Tree — seulement des Blueprints classiques. Sans ça,
construire un comportement demandait de cliquer dans l'éditeur.

`ScriptedInputLibrary` (dans `BTAuthoringKitRuntime`) comble un trou du même genre côté input :
elle obtient l'instance du subsystem Enhanced Input, inatteignable depuis Python sur ce build.
L'input injecté entre ensuite au même point qu'une vraie touche — il traverse l'Input Mapping
Context, les modifiers et les triggers. Mesuré en session réelle : 1037 cm parcourus en 4 s.

`PatrolPerceptionAIController` écrit trois niveaux d'attention dans le Blackboard à 10 Hz, avec une
habituation qui resserre les seuils tant que le joueur garde ses distances et se perd si on brusque
le sujet.

### RoomGenerator, BlueprintPythonUtils

Génération de géométrie, édition de graphes Blueprint en un appel (`BatchWireGraph`), pilotage de
la PIE, panneau de chat in-editor. Ces deux plugins viennent d'un projet précédent et continuent de
servir ici.

### EvalHarness — noter l'agent, pas seulement le code

`CLAUDE.md` documente les pièges rencontrés en vrai : un screenshot périmé pris pour une
vérification fraîche, un `save()` qui retourne `True` sans écrire un seul package d'acteur, une
valeur de tuning calibrée en live et perdue au rebuild suivant. Documenter une règle après
l'incident répare le passé ; elle n'empêche pas d'y retomber la fois suivante, avec une tâche
formulée autrement.

`EvalHarness/agentic/` rejoue dix de ces pièges avec des outils factices. L'agent reçoit une tâche
ambiguë, un outil piégé et un outil fiable, et sa trajectoire est notée : a-t-il pris le bon outil,
et sa conclusion tient-elle à ce qu'il a vérifié plutôt qu'à ce qu'il a supposé. Le système de
règles injecté dans l'éval est extrait de `CLAUDE.md` — une règle qui change là-bas est suivie sans
recopier de texte.

`Content/Python/tracing.py` pose des spans OpenTelemetry sur les points d'entrée qui comptent
(`save()`, `safe_modify_plugin()`, `test_suite.run_all()`, une session de playtest) : durée,
verdict, compteurs de régression en attributs, plutôt que des lignes de log à recompter à la main.
Le backend se choisit avec deux variables d'environnement, sans branche de code séparée.

---

## Lancer les vérifications

La partie géométrie et la partie éval tournent sans Unreal.

```bash
# Géométrie du blockout, hors moteur — ~1 s, 40 000 contours
cd Plugins/BlockoutTools/Tools/GeometryTests
./run.bat            # ou : g++ -O2 -I shims Tests.cpp shims/Shims.cpp -o tests && ./tests

# Harnais d'évaluation
cd EvalHarness
pip install -r requirements.txt
python run_eval.py
python agentic/run_agentic_eval.py
```

Le reste (`Content/Python/test_*.py`) demande l'éditeur ouvert : ces tests pilotent une vraie
session de jeu.

## Comment la géométrie est vérifiée

`BlockoutPanelGeometry.cpp` ne dépend d'aucune API Unreal — uniquement `FVector2D`, `FVector3f` et
`FMath`. Une centaine de lignes de doublures (`shims/CoreMinimal.h`) suffisent à le compiler hors
moteur. Le harnais compile **les fichiers du projet**, pas des copies : si la géométrie se met à
utiliser un type Unreal absent des doublures, il ne compile plus et on le sait tout de suite.

Ce qui est vérifié n'est pas une liste de résultats attendus mais des propriétés qui doivent tenir
pour n'importe quelle entrée : l'aire triangulée égale l'aire du contour moins celle des trous, un
point dans la matière est couvert par exactement un triangle et un point dans un trou par aucun, le
maillage est un prisme recto-verso d'épaisseur exacte, aucun triangle sous le seuil de
dégénérescence — qui est relatif à la taille du panneau —, aucune T-jonction, et deux appels rendent
la même surface au bit près.

La calibration est la partie qui coûte. Deux seuils ont dû être recalés sur le contrat réel du code
après avoir crié au loup : le code écarte un triangle sous `bbox × 1e-9`, et un test plus strict que
ça reproche au code de faire ce qu'il annonce ; sous l'epsilon du triangulateur, deux lignes de
tranche fusionnent volontairement, et les panneaux concernés sont écartés et comptés (7 sur 12 000)
plutôt que comptés en échec.

Et un bug du harnais lui-même, trouvé avant qu'il ne serve : le générateur de contours tirait N
angles uniformes puis les triait, ce qui ne donne un polygone simple que si les sommets font le tour
du centre. 57 contours auto-intersectants sur 2 000 étaient rapportés comme des erreurs d'aire du
code testé.

## Vérifier le comportement plutôt que l'état des données

Un log vide ne prouve pas qu'un arbre ne tourne pas. Un decorator présent dans un asset ne prouve
pas qu'il agit. Trois régressions d'une même journée l'ont montré : la vérification portait sur
l'état des données, jamais sur le comportement, et elles ont coûté une session entière alors que le
test de non-régression les aurait attrapées en une minute.

`Content/Python/test_npc_behavior.py` pilote donc seul une session de jeu (~52 s de temps de jeu) et
interroge l'état runtime : arbre en cours, valeurs du Blackboard, position, montage d'animation
actif. Sept points, résultat écrit en JSON.

Le mode d'abort du decorator de fuite a été tranché de la même façon, par la mesure : 1308 cm
d'éloignement avec `Both`, 2060 cm avec `LowerPriority`, pour un critère qui en demande 1500.

## Organisation

```
Plugins/BlockoutTools/       outil de blockout + harnais géométrique hors moteur
Plugins/BTAuthoringKit/      Behavior Trees par script, input scripté, IA de patrouille
Plugins/RoomGenerator/       génération de géométrie, édition de Blueprint, pilotage PIE
Plugins/BlueprintPythonUtils/  helper de graphe Blueprint
Content/Python/              toolchain, tests pilotant l'éditeur, tracing
EvalHarness/                 évaluation du comportement de l'agent
Tools/                       QC hors éditeur (analyse de screenshot, diff visuel, garde-fous)
Docs/                        spécifications et cas d'étude
CLAUDE.md                    les règles anti-régression, et la source des scénarios d'éval
```

## Écarts connus

- `Plugins/BlockoutTools/Tools/GeometryTests/run.bat` (MSVC) n'a pas encore tourné sous Windows ; le
  harnais est développé et lancé sous g++. Si `cl.exe` bronche, regarder du côté des doublures.
- Le graphe de `BT_NPC_Patrol` est désynchronisé de son arbre compilé : le graphe contient deux
  copies de l'arbre, dont une orpheline, et il lui manque le correctif d'animation présent dans
  l'arbre runtime. Le recompiler effacerait ce correctif sans message. `BT_NPC_Naturalist`,
  construit entièrement par script, l'a remplacé ; `BT_NPC_Patrol` reste sur disque comme repli.
- Le panneau de chat in-editor (`ClaudeEditorSubsystem`) compile et gère l'absence de clé API, mais
  l'aller-retour HTTP réel n'a jamais été fait — il demande une clé Anthropic payante.
- Ce dépôt est une copie filtrée : les `.uasset`, les niveaux et le contenu Epic d'origine ne sont
  pas ici. `sync.ps1` recopie la liste blanche depuis le projet complet.

## Licence

MIT, voir `LICENSE`.
