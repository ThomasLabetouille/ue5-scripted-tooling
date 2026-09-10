# GameAnimationSample — Base de connaissances pour Claude

Projet Unreal Engine 5.8. Contient le plugin **BTAuthoringKit**, qui permet de construire des
Behavior Trees et Blackboards entièrement par script, sans jamais ouvrir l'éditeur de graphe à la
souris.

> **Contrainte permanente du projet : ne JAMAIS utiliser Computer Use.** Tout doit être pilotable
> par script. Cette contrainte est structurante — voir « Piloter la PIE par script » plus bas, qui
> rend la vérification comportementale possible sans elle.

Le projet frère **RPG_Test** (`D:\Travail\ProjectUnreal\RPG_Test`) possède son propre `CLAUDE.md`
plus mature (710 lignes) et une infrastructure de vérification (`Tools/qc_gate.py`,
`Tools/trust_gate.py`, `EvalHarness/`). Ses 9 règles anti-régression s'appliquent ici aussi.
Le skill `protocole-verification-ue5-agent` en est la version générique.

---

## ⚠️ Gate de sortie — avant de dire « c'est corrigé / vérifié / ça marche »

**Lancer le test de non-régression et coller son verdict.** Une affirmation de succès sans ce
verdict n'est pas recevable sur ce projet.

```python
import sys, unreal
p = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir() + "Python")
if p not in sys.path: sys.path.append(p)
import test_npc_behavior, importlib; importlib.reload(test_npc_behavior)
test_npc_behavior.run()
```

Le test pilote la PIE seul (~52 s de temps de jeu, souvent 2–3 min de temps réel car le tick de
l'éditeur avance moins vite). Résultat dans `Saved/Tests/npc_behavior_result.json`.
Il couvre 7 points : BT lancé + Blackboard rempli, controller unique, déplacement, animation
d'assise, détection par perception réelle, rapprochement du joueur, aucun PNJ figé.

**Historique : les 3 régressions du 2026-08-15 auraient toutes été attrapées par ce test en une
minute.** Elles ont à la place coûté une session entière, parce que la vérification portait sur
l'état des *données* (« le decorator est-il dans l'asset ? ») et jamais sur le *comportement réel*.

---

## Pièges vérifiés — chacun a déjà coûté des heures

### 1. Les structs exposés à Python sont des COPIES, pas des références

C'est le piège le plus coûteux du projet. `get_editor_property()` sur une propriété de type struct
renvoie une copie. La modifier ne change rien tant qu'on ne la réassigne pas explicitement.

```python
# FAUX — semble réussir, ne change rien, aucune erreur
task.get_editor_property("animation_to_play").set_editor_property("default_value", anim)

# CORRECT — modifier la copie PUIS la réinjecter dans le parent
s = task.get_editor_property("animation_to_play")
s.set_editor_property("default_value", anim)
task.set_editor_property("animation_to_play", s)
```

Vaut aussi pour les tableaux (`children`, `decorators`) : modifier l'élément, puis réassigner le
tableau entier sur le nœud parent. A causé : cooldown resté à 5 s au lieu de 30, et surtout les
3 références d'animation d'assise restées nulles — le PNJ atteignait le banc, l'étape suivante
échouait instantanément, et il repartait aussitôt. Symptôme observé : « il court d'un point à
l'autre sans jamais s'asseoir ».

### 2. `ue5_execute` renvoie TOUJOURS « OK », succès ou échec

Le pont MCP ne remonte ni les exceptions ni les échecs. **Tout script doit écrire son résultat en
JSON sur le disque**, relu ensuite via `device_bash cat`. Ne jamais conclure depuis la valeur de
retour du pont.

Corollaire : un script long ou contenant des caractères Unicode (tirets longs, box-drawing) peut
échouer silencieusement sans rien écrire du tout. Garder les scripts passés au pont en ASCII.
Les fichiers `.py` déposés sur le disque puis importés n'ont pas cette limite — c'est la méthode
à préférer pour tout script non trivial.

### 3. `delete_asset()` nullifie les références même quand il échoue

Constaté le 2026-08-15 : `EditorAssetLibrary.delete_asset()` sur `BT_NPC_Patrol` a renvoyé
`False` (suppression refusée, asset référencé) **mais a quand même mis à `None` la propriété
`BehaviorTreeAsset` du controller**. Résultat : les 6 PNJ totalement figés, sans aucun message
d'erreur. Ne pas supprimer un asset référencé ; si c'est indispensable, relire et restaurer
ensuite toutes les références entrantes.

### 4. Le log ne trace PAS l'exécution des nœuds de Behavior Tree

`LogBehaviorTree` en Verbose ne produit que quelques lignes au démarrage (« Looking for runtime
properties… »). **Un log vide pendant une PIE ne prouve absolument pas que l'arbre ne tourne
pas.** Erreur commise le 2026-08-15 : silence du log interprété comme « l'arbre est cassé », ce
qui a conduit à annuler un correctif qui fonctionnait. Pour savoir ce que fait un arbre,
interroger l'état runtime (`BehaviorTreeComponent.is_running()`, valeurs du Blackboard, position,
montage actif) — pas le log.

### 5. `BTTask_PlayAnimation` verrouille le mode d'animation du mesh

La tâche bascule le mesh en `AnimationSingleNode`. Si la séquence est interrompue avant
`BTTask_ResetAnimationMode` (typiquement par un abort de decorator quand le joueur est repéré),
**le mesh reste verrouillé et le PNJ est définitivement figé** — il ne peut plus ni bouger ni
s'animer.

Règle : toute branche pouvant interrompre une séquence d'animation doit commencer par
`BTTask_ResetAnimationMode`. C'est pourquoi la branche Chase de `BT_NPC_Patrol` a cette tâche en
première position.

### 6. Propriétés inaccessibles depuis Python

`UBehaviorTree.BTGraph` (protégée en lecture), `UAIGraphNode.SubNodes`, et le `NodeInstance` d'un
nœud de decorator ne sont pas exposés à la réflexion Python. Tout diagnostic au niveau du *graphe*
doit passer par du C++ ; depuis Python on ne peut inspecter que l'arbre *runtime*
(`Tree.root_node` → `children` → `child_composite` / `child_task` / `decorators`).

### 7. Le pawn joueur (Mover) ne se téléporte pas comme un Character

`SandboxCharacter_Mover_C` n'a ni `teleport_to()` ; `set_actor_location()` est écrasé par le
composant Mover au tick suivant. Ne pas construire de test qui repose sur le déplacement du
joueur.

**Ne pas non plus téléporter un PNJ en plein `MoveTo`** : ça corrompt son état de navigation et
fausse le test (constaté — un PNJ ainsi déplacé cesse de réagir). Pour tester la poursuite,
laisser les PNJ patrouiller : depuis les points de patrouille ils voient naturellement le joueur
à son point de spawn.

### 8. Rebuild C++

```
cd D:\Logiciel\UE5\UE_5.8\Engine\Build\BatchFiles
.\Build.bat UnrealEditor Win64 Development -Project="D:\Travail\ProjectUnreal\GameAnimationSample\GameAnimationSample.uproject" -NoLiveCoding -WaitMutex -NoUBA
```

- Le `.\` est obligatoire sous PowerShell.
- **L'éditeur doit être fermé**, puis rouvert après le build.
- Live Coding ne gère pas l'ajout d'une nouvelle `UFUNCTION`/`UPROPERTY` — d'où `-NoLiveCoding`.
- Une relecture statique (accolades équilibrées, grep) n'est PAS un test de compilation. Seul un
  vrai build prouve qu'un changement C++ compile.

---

### 9. ⚠️ Le graphe de `BT_NPC_Patrol` est désynchronisé de l'arbre compilé

Découvert le 2026-08-27, **non corrigé à ce jour**. Ne pas compiler ce graphe sans avoir lu ceci.

`UBTAuthoringLibrary::GetGraphNodes()` révèle **37 nodes de premier niveau** dans le graphe de
`BT_NPC_Patrol`, alors que l'arbre compilé n'en compte que 19 : le graphe contient **deux copies**
de l'arbre, dont une orpheline sans racine. Les sous-nodes decorators ont par ailleurs un
`NodeInstance` nul et un `ParentNode` nul.

Le plus grave : **la branche Chase du graphe ne contient pas de `BTTask_ResetAnimationMode`**,
alors que l'arbre compilé, lui, en a un en tête de branche. C'est exactement le correctif du
2026-08-15 contre le PNJ figé en animation assise (piège 5). Il n'existe donc que dans l'arbre
runtime, pas dans le graphe — signe qu'il a été appliqué par écriture directe et non par le
chemin graphe.

**Conséquence opérationnelle** : n'importe quelle recompilation du graphe — un `CompileBehaviorTree()`,
mais aussi une simple ouverture-modification-sauvegarde dans l'éditeur de Behavior Tree —
reconstruit l'arbre runtime à partir du graphe et **efface silencieusement ce correctif**. Le bug du
PNJ figé reviendrait, sans le moindre message. Le TEST 7 du test de non-régression l'attraperait,
ce qui est précisément sa raison d'être.

C'est le mécanisme concret derrière « un bug déjà corrigé qui revient ». Tant que ce n'est pas
réglé, préférer **construire un nouvel arbre** plutôt que recompiler celui-ci.


---

### 10. Trois façons d'obtenir un verdict de test qui ment (2026-08-29)

Toutes trois rencontrées le même jour, sur la même feature. Aucune ne produit d'erreur : elles
rendent un PASS ou un FAIL parfaitement crédible, sur du code ou une scène qui ne sont pas ceux
qu'on croit.

**Le cache d'import de Python sert du code périmé.** `importlib.reload()` ne suffit pas ; vider
`sys.modules` et `invalidate_caches()` non plus. Un test corrigé a rendu deux fois de suite le
verdict de sa version d'avant. Seule parade fiable : exécuter la source directement, sans passer
par l'import.

```python
with open(chemin) as fh:
    src = fh.read()
ns = {"__file__": chemin, "__name__": "test_direct"}
exec(compile(src, chemin, "exec"), ns)
ns["run"]()
```

**Deux instances du test tournent en même temps.** Les tests pilotant la PIE sont asynchrones et
durent plusieurs minutes de temps réel. En relancer un pendant que le précédent tourne encore
donne deux écrivains sur le même fichier de résultat — et on lit le verdict de l'autre. Vérifier
`is_in_play_in_editor()` avant de lancer, et faire refuser au test un démarrage concurrent.

**Un décor de test survit dans le niveau.** Un run interrompu laisse ses acteurs ; le
`save_dirty_packages` du rebuild les grave dans le `.umap` ; les runs suivants mesurent contre
cette géométrie fantôme. Un mur de 150 cm oublié a fait échouer une mesure sur un cube de 60 cm
avec le motif `TropHaut` — un diagnostic parfaitement cohérent, et parfaitement faux. Tout test
qui spawne doit **purger les restes de ses prédécesseurs au démarrage**, pas seulement nettoyer
les siens à la fin.

**La parade commune** : le rapport calcule lui-même l'empreinte de sa propre source au moment de
l'écrire. Une empreinte posée par l'appelant décrit ce que l'appelant *croit* avoir lancé ; celle
calculée par le test décrit ce qui a réellement tourné. Comparer avec le fichier sur disque avant
d'accorder du crédit à un verdict.

---

## Piloter la PIE entièrement par script

Capacité clé de ce projet : **on peut lancer, observer et arrêter une session de jeu sans aucune
action manuelle et sans Computer Use.**

```python
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.editor_request_begin_play()     # démarre la PIE
les.is_in_play_in_editor()          # état
les.editor_request_end_play()       # arrête

# Pendant la PIE, le monde de jeu s'obtient ainsi (get_editor_world() renvoie None) :
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_game_world()
player = unreal.GameplayStatics.get_player_pawn(world, 0)
```

Le démarrage étant asynchrone, un script one-shot ne peut pas attendre. Pour un test étalé dans le
temps, utiliser un callback de tick — voir `Content/Python/test_npc_behavior.py`, qui implémente
une machine à états sur `unreal.register_slate_post_tick_callback`.

### Faire marcher le joueur par script

Vérifié le 2026-08-27 en PIE réelle : **1037 cm parcourus en 4 s**, par le vrai chemin d'input.

```python
lib = unreal.ScriptedInputLibrary                      # BTAuthoringKitRuntime
action = unreal.EditorAssetLibrary.load_asset("/Game/Input/IA_Move")
lib.is_scripted_input_available(world, 0)              # infrastructure joignable ?
lib.start_continuous_input(world, action, unreal.Vector(1, 1, 0), 0)   # touche maintenue
lib.stop_continuous_input(world, action, 0)            # relâcher — TOUJOURS, même sur échec
```

L'input injecté entre au **même point qu'un vrai appui touche** : il traverse l'Input Mapping
Context, les modifiers et les triggers. C'est donc un test par le vrai chemin de jeu, à la
différence d'un `set_actor_location` (interdit, piège 7).

Préférer l'injection *continue* à `inject_input_vector` frame par frame : le maintien ne dépend
plus de la cadence du callback de tick, qui varie avec la charge de l'éditeur.

**Ce qui ne marche PAS, et pourquoi** — les deux voies naturelles sont mortes, chacune pour une
raison distincte, mesurées le 2026-08-27 :

- `Pawn.add_movement_input()` → **0 cm**. Le pawn joueur porte un `CharacterMoverComponent` et
  aucun `CharacterMovementComponent` : le vecteur d'input en attente n'est jamais consommé par
  Mover, qui lit son input par son propre producteur.
- Injection Enhanced Input directe depuis Python → **instance inatteignable**. La classe
  `UEnhancedInputLocalPlayerSubsystem` et ses méthodes d'injection sont bien exposées, mais
  `SubsystemBlueprintLibrary` est absente du binding Python de ce build et
  `APlayerController::Player` est protégée en lecture. `UScriptedInputLibrary` ne fait que combler
  ce trou — obtenir l'instance — et rien d'autre.


Depuis un PNJ on lit : `controller.get_component_by_class(unreal.BehaviorTreeComponent)`,
`… (unreal.BlackboardComponent)`, `… (unreal.AIPerceptionComponent)` (dont
`get_currently_perceived_actors(unreal.AISense_Sight)`), et le montage en cours via
`get_component_by_class(unreal.SkeletalMeshComponent).get_anim_instance().get_current_active_montage()`.

---

## Architecture du système de PNJ

| Élément | Chemin |
|---|---|
| Niveau | `/Game/Levels/NPCLevel` |
| Behavior Tree | `/Game/AI/NPC_Patrol/BT_NPC_Patrol` |
| Blackboard | `/Game/AI/NPC_Patrol/BBD_NPC_Patrol` |
| AIController (Blueprint) | `/Game/AI/NPC_Patrol/BP_AIC_NPCPatrol` |
| AIController (classe C++ parente) | `APatrolPerceptionAIController` (BTAuthoringKitRuntime) |
| Test de non-régression | `Content/Python/test_npc_behavior.py` |

Les **6 PNJ** du niveau (`SandboxCharacter_CMC_C`) utilisent tous `BP_AIC_NPCPatrol_C`. Ils
partagent les mêmes points, repérés par **tag** : `PatrolPoint` ×3 et `SitPoint` ×1 — ils se
regroupent donc au même endroit. Pour des trajets distincts, il faudra des tags par PNJ.

Structure de l'arbre :

```
Selector (racine)
├── Sequence "Chase"        [decorator Blackboard : TargetActor Is Set, FlowAbortMode=Both]
│   ├── BTTask_ResetAnimationMode      <- indispensable, cf. piège 5
│   └── BTTask_MoveTo (TargetActor, AcceptableRadius 150)
└── Selector "Routine"
    ├── Sequence "Sit"      [decorator Cooldown 30 s]
    │   └── MoveTo(SitPoint) → PlayAnim(into) → PlayAnim(idle, loop) → Wait 5 s
    │       → PlayAnim(out) → ResetAnimationMode
    └── Sequence "Patrol"   → MoveTo(P1) → Wait 2 s → MoveTo(P2) → Wait 2 s → MoveTo(P3) → Wait 2 s
```

### Arbre de comportement actif : `BT_NPC_Naturalist` (2026-08-27)

`BP_AIC_NPCPatrol` pointe désormais sur `BT_NPC_Naturalist`, construit entièrement par script.
`BT_NPC_Patrol` reste sur disque comme repli, mais n'est plus référencé (graphe abîmé, piège 9).

```
Selector
├── [Blackboard: AwarenessLevel >= 2, abort LowerPriority]  Sequence "Fuite"
│     ├── ResetAnimationMode          <- en tête, piège 5
│     └── MoveTo(FleeLocation)
└── Selector "Vie quotidienne"
      ├── [Cooldown 30 s]  Sequence "S'asseoir"
      └── Sequence "Patrouiller"
```

**`LowerPriority` et non `Both`** sur la garde de fuite. Vérifié en PIE : avec `Both`, le
Self-abort coupait la branche dès que le sujet repassait au-dessus du seuil, donc après quelques
mètres — il s'arrêtait à mi-parcours et repartait parfois vers le joueur. Mesure : 1308 cm
d'éloignement avec `Both`, 2060 cm avec `LowerPriority` (le critère en demande 1500).

**Pièges de l'API Python sur `BTDecorator_Blackboard`**, tous rencontrés le 2026-08-27 :

- `operation_type` **n'existe pas** côté Python. UE expose une énumération par famille de type :
  `basic_operation`, `arithmetic_operation`, `text_operation`. Pour une clé Int c'est
  `arithmetic_operation` (`ArithmeticKeyOperation.GREATER_OR_EQUAL` = 5).
- `BTAuthoringLibrary.set_instance_blackboard_key()` **échoue en silence** quand les types
  autorisés du sélecteur sont vides : il renvoie `false` et la clé reste sur `SelfActor`. Passer
  directement par le struct — modifier `selected_key_name` et `selected_key_type`, puis
  **réassigner le struct sur l'objet** (piège 1).
- `unreal.BlackboardKeyType_Int` n'est pas exposé ; utiliser
  `unreal.load_class(None, "/Script/AIModule.BlackboardKeyType_Int")`.
- `acceptable_radius`, `wait_time`, `animation_to_play`, `non_blocking`, `cool_down_time` sont
  tous des structs `ValueOrBBKey_*` en UE 5.8 : la valeur est dans `default_value`, et le struct
  doit être réassigné après modification.
- Les libellés passés à `add_task_node`/`add_composite_node` ne sont **pas relisibles** depuis
  Python (`node_comment` et `node_pos_x` ne sont pas exposés). Pour adresser un node d'un arbre
  qu'on vient de construire, se fier à l'ordre de création renvoyé par `get_graph_nodes()`, qui
  est déterministe — puis vérifier la classe de l'instance avant d'écrire.

### Tolérance et habituation (ajouté le 2026-08-27)

`APatrolPerceptionAIController` évalue à 10 Hz la proximité du joueur et écrit trois niveaux dans
la clé Blackboard `AwarenessLevel` : **0 ignore, 1 alerte, 2 fuite**. Les seuils par défaut sont
1200 et 600 cm.

L'**habituation** monte de 0,005/s tant que le joueur reste à moins de 2000 cm sans déclencher le
niveau 2, plafonnée à 0,5. Elle **resserre** les seuils — un sujet habitué laisse approcher plus
près, c'est la progression du joueur. Passer au niveau 2 en retire 0,15 : brusquer un sujet coûte
du temps, jamais la partie.

Au passage en niveau 2, le controller calcule `FleeLocation` (clé Vector) à 2000 cm à l'opposé du
joueur, projeté sur le navmesh.

Vérifié en PIE réelle : les trois niveaux sont atteints aux bonnes distances, et le seuil d'alerte
mesuré descend à 1134 cm après 0,10 d'habituation.

⚠️ **Aucune branche du Behavior Tree n'utilise encore `FleeLocation`.** La branche « Chase » de
`BT_NPC_Patrol` fait toujours courir le PNJ *vers* le joueur — comportement de l'ancien design,
opposé à celui du jeu visé. Tant qu'elle est là, le TEST 6 du test de non-régression défend un
comportement que le jeu ne veut plus : les deux doivent être changés ensemble.

Le C++ (`APatrolPerceptionAIController`) lance le BT dans `OnPossess`, remplit les clés
Blackboard depuis les acteurs taggés, et écrit/efface `TargetActor` sur les événements de
perception — en ne réagissant qu'au joueur local (les PNJ ne se voient pas entre eux).

---

## Correctifs C++ du 2026-08-15 — rebuild effectué, les deux validés

Deux bugs de fond corrigés dans les sources. **Rebuild fait le 2026-08-15, correctifs vérifiés :**
test de non-régression 7/7, et decorator ajouté via `AddDecoratorToNode` retrouvé intact dans
l'arbre runtime (clé Blackboard résolue, `FlowAbortMode` correct).

1. **`BTAuthoringLibrary.cpp` — `AddDecoratorToNode` / `AddServiceToNode`.** `NodeInstance` était
   assigné *après* `AddSubNode()`. Or `AddSubNode()` déclenche `Graph->UpdateAsset()`, qui écarte
   les sous-nœuds à instance nulle : le decorator était éliminé dans la seconde suivant sa
   création, silencieusement. **C'est la cause racine du mystère « les decorators disparaissent »**
   qui a occupé toute la session. Corrigé en assignant `NodeInstance` avant `AddSubNode()`.

2. **`PatrolPerceptionAIController.cpp` — configuration du sens Vue.** `ConfigureSense()` était
   appelé dans `BeginPlay()`, trop tard : le composant de perception s'est déjà enregistré comme
   listener, l'appel est refusé (`LogAIPerception: Warning: Listener must have a valid id to
   update its sense config`) et le sens n'est jamais souscrit. Les valeurs paraissaient correctes
   en inspectant `SensesConfig` mais étaient totalement inertes. **C'est pourquoi la poursuite
   n'a jamais fonctionné depuis la création de la classe.** Corrigé en configurant dans le
   constructeur, `BeginPlay()` ne faisant plus que réappliquer les valeurs +
   `RequestStimuliListenerUpdate()`.

### ⚠️ Ne PAS vider `SensesConfig` sur le Blueprint

Piège découvert en nettoyant après le rebuild, et qui casse la perception en silence.

Le template du composant `AIPerception` du Blueprint **sérialise son propre tableau
`SensesConfig`**, qui est appliqué *par-dessus* ce que le constructeur C++ a enregistré. Mettre ce
tableau à vide « puisque le C++ s'en charge maintenant » donne donc des instances **sans aucun
sens** — vérifié : les 6 PNJ se sont retrouvés avec `senses_config` vide, perception morte, alors
que le CDO C++ affichait bien sa configuration.

`reset_editor_property("senses_config")` ne répare pas ça (« no archetype value to reset to »).

**Règle** : le Blueprint doit porter une entrée `AISenseConfig_Sight` cohérente avec les
propriétés C++ (actuellement 1500 / 1800 / 60°, les trois affiliations à `true`). Le correctif C++
reste indispensable pour autre chose : il garantit que le sens est souscrit **au bon moment**
(constructeur), là où l'ancien code le faisait trop tard en `BeginPlay()`.

### Ajout de decorators : repasser par le graphe

Le correctif 1 étant actif et validé, préférer désormais le chemin propre —
`AddDecoratorToNode()` + `CompileBehaviorTree()` — plutôt que l'écriture directe dans l'arbre
runtime (`AddDecoratorDirect`). Le chemin graphe donne des decorators **visibles et éditables à la
souris** dans l'éditeur de Behavior Tree ; l'écriture directe reste invisible côté éditeur et ne
doit servir que si le chemin graphe échoue à nouveau.

Note : `BT_NPC_Patrol` porte encore ses deux decorators posés en direct (Blackboard sur Chase,
Cooldown 30 s sur Sit). Ils fonctionnent — le test le confirme — mais ne sont pas visibles dans
l'éditeur. Les migrer vers le chemin graphe est une amélioration de confort, à faire en relançant
le test juste après.
