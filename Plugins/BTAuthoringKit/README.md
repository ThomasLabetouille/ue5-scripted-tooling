# BT Authoring Kit

Plugin **Unreal Engine 5** (C++, editor-only) qui expose une API scriptable pour
construire des Behavior Trees complets — Blackboard, composites, tasks,
decorators, services, **graphe visuel inclus** — sans jamais ouvrir l'éditeur
de graphe à la souris. Toutes les fonctions sont `BlueprintCallable`, donc
automatiquement appelables depuis Python (`unreal.BTAuthoringLibrary...`),
Blueprint, ou tout script.

## Ce que c'est — et ce que ce n'est PAS

Ce n'est **pas** le plugin Cowork `ue5-behavior-tree-expert` livré
précédemment (celui-là vit côté Claude, pas dans ton projet Unreal, et ne
s'installe nulle part dans UE5). **Celui-ci est un vrai plugin Unreal** :
il s'installe dans le dossier `Plugins/` de ton projet (ou de ton moteur),
se compile avec le reste du code C++, et s'active/désactive dans
`Édition > Plugins` comme n'importe quel plugin. C'est ce format qui permet
de l'exporter et de le partager avec ton équipe.

## Pourquoi ce plugin existe

En essayant de construire un Behavior Tree par script pour tester le premier
plugin, on a buté sur une vraie limite : ni l'API Python standard d'Unreal,
ni l'outillage Python déjà présent dans RPG_Test (`bpes`/`bgh`), ne savent
éditer le graphe d'un Behavior Tree — seulement des Blueprints classiques.
Résultat : construire un comportement complexe demandait soit de cliquer à
la main dans l'éditeur, soit de piloter l'écran (ce que tu as explicitement
écarté). Ce plugin comble ce trou en ajoutant, en C++, exactement les
fonctions qui manquaient.

## Installation

1. Copier le dossier `BTAuthoringKit/` dans `<TonProjet>/Plugins/`.
2. Régénérer les fichiers projet si besoin, puis compiler (`rebuild.bat` ou
   Live Coding) — le module se construit avec le reste du projet, un plugin
   n'a pas de compilation séparée.
3. Vérifier dans `Édition > Plugins > AI` que "BT Authoring Kit" apparaît et
   est activé (activé par défaut à l'installation).
4. Pour le partager à l'équipe : commiter le dossier `Plugins/BTAuthoringKit/`
   dans le dépôt du projet (comme n'importe quel autre plugin) — chaque
   membre le récupère au prochain `git pull` + rebuild.

## Important — je n'ai pas pu compiler ni tester ce code moi-même

Cette session n'a pas accès à un environnement Windows/UE5 pour compiler du
C++ (seulement au pont `ue5-mcp` qui exécute du Python dans l'éditeur déjà
ouvert). Ce plugin est écrit avec le plus grand soin à partir de ce que je
sais de l'architecture d'édition des Behavior Trees dans UE5, mais **certains
noms de fonctions/propriétés internes à l'éditeur (module `BehaviorTreeEditor`)
peuvent différer légèrement selon la version exacte du moteur** (ce projet
est en UE5.8). Chaque endroit à risque est marqué `VERIFIER` ou `RISQUE` en
commentaire dans le code, avec une piste concrète pour corriger si besoin
(quel fichier moteur regarder, quel autre nom essayer).

Concrètement, si ça ne compile pas du premier coup : copie-moi les erreurs de
compilation (le message complet, pas juste "ça marche pas"), je corrige, tu
recompiles — 1 ou 2 aller-retours de ce type sont probables pour ce genre de
code qui touche à des classes d'éditeur peu documentées. Une fois que ça
compile, la partie Blackboard (`CreateBlackboard`/`AddBlackboardKey`) a une
confiance élevée — c'est la même mécanique que j'ai déjà validée en Python
dans cette session. La partie graphe (composites/tasks/connect/compile) est
la plus susceptible d'avoir besoin d'un ajustement.

## API (résumé)

Voir les commentaires dans `Source/BTAuthoringKitEditor/Public/BTAuthoringLibrary.h`
pour le détail de chaque fonction. En résumé :

| Fonction | Rôle |
|---|---|
| `create_blackboard(path, name)` | Crée/recharge un BlackboardData |
| `add_blackboard_key(bb, name, type, filter_class)` | Ajoute une clé (idempotent) |
| `create_behavior_tree(path, name, blackboard)` | Crée/recharge un BehaviorTree |
| `add_composite_node(tree, type, label, x, y)` | Ajoute Selector/Sequence/SimpleParallel |
| `add_task_node(tree, class_path, label, x, y)` | Ajoute une Task (standard ou Blueprint custom) |
| `add_decorator_to_node(node, class_path, label)` | Attache un Decorator à un node |
| `add_service_to_node(node, class_path, label)` | Attache un Service à un composite |
| `connect_child(parent, child)` | Relie parent → enfant dans le graphe |
| `set_tree_root(tree, child)` | Relie la racine du graphe au premier node |
| `set_node_property(node, prop_name, value_as_string)` | Règle une propriété simple |
| `set_node_blackboard_key(node, prop_name, blackboard, key_name)` | Règle un `FBlackboardKeySelector` |
| `compile_behavior_tree(tree)` | Synchronise graphe → arbre runtime, sauvegarde |

## Construire un arbre en un seul appel (recommandé)

`Content/Python/bt_authoring.py` fournit `build_behavior_tree_from_spec(spec)`
qui prend une description Python (dict) du Blackboard + de l'arbre entier et
fait tous les appels ci-dessus dans le bon ordre. C'est la façon prévue de
s'en servir au quotidien plutôt que d'enchaîner les appels bas niveau à la
main — voir l'exemple `EXAMPLE_SPEC` en bas de ce fichier, et le docstring en
tête pour le format complet.

```python
import bt_authoring
tree = bt_authoring.build_behavior_tree_from_spec(bt_authoring.EXAMPLE_SPEC)
```

## Test après compilation

Une fois le plugin compilé et activé, un test minimal depuis `execute_python`
(ue5-mcp) ou la console Python de l'éditeur :

```python
import unreal
bb = unreal.BTAuthoringLibrary.create_blackboard("/Game/AI/Test", "BBD_SmokeTest")
ok = unreal.BTAuthoringLibrary.add_blackboard_key(bb, "TargetActor", unreal.BTAuthoringKeyType.OBJECT, unreal.Pawn)
print("Blackboard OK:", ok)

tree = unreal.BTAuthoringLibrary.create_behavior_tree("/Game/AI/Test", "BT_SmokeTest", bb)
sel = unreal.BTAuthoringLibrary.add_composite_node(tree, unreal.BTAuthoringCompositeType.SELECTOR, "Root", 0, 0)
wait_task = unreal.BTAuthoringLibrary.add_task_node(tree, "/Script/AIModule.BTTask_Wait", "Wait", 0, 200)
unreal.BTAuthoringLibrary.connect_child(sel, wait_task)
unreal.BTAuthoringLibrary.set_tree_root(tree, sel)
unreal.BTAuthoringLibrary.set_node_property(wait_task, "WaitTime", "3.0")
print("Compile:", unreal.BTAuthoringLibrary.compile_behavior_tree(tree))
```

Puis **ouvrir `BT_SmokeTest` dans l'éditeur** : c'est la vraie vérification —
un Selector relié à une Task "Wait" (3 secondes) doit apparaître dans le
graphe, pas juste "aucune erreur Python". Si le graphe est vide malgré un
`print` qui dit "OK" partout, c'est le signe que `compile_behavior_tree`
(la synchronisation graphe → asset) n'a pas fonctionné comme prévu — le
premier endroit à corriger dans ce cas.

## Limites connues (v0.1.0)

- `add_decorator_to_node`/`add_service_to_node` ne retournent pas encore le
  node du decorator/service lui-même — impossible pour l'instant de régler
  ses propriétés (ex. le rayon d'un decorator de distance) via
  `set_node_property` après coup. Contournement en attendant : régler ces
  propriétés à la main dans l'éditeur après génération, ou demander une
  v0.2 qui retourne aussi ce handle.
- Pas de support des clés Blackboard de type Enum avec un `UEnum` custom
  (seulement le type de base, sans association à un enum précis).
- Pensé pour construire un arbre nouveau ou compléter un arbre existant —
  pas encore de fonction dédiée pour supprimer/réorganiser des nodes déjà
  posés (à la main dans l'éditeur en attendant si besoin).

## Une fois que ça marche

Le vrai bénéfice pour ton équipe : je peux (ou n'importe quel script) décrire
un comportement complexe comme les données `EXAMPLE_SPEC` dans
`bt_authoring.py`, appeler `build_behavior_tree_from_spec()`, et obtenir un
Behavior Tree entièrement câblé et sauvegardé — sans jamais utiliser
Computer Use, et sans qu'un humain ait besoin de reproduire à la main ce
qu'on a déjà conçu ensemble.
