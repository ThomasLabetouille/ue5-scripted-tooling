"""
bt_authoring.py — construit un Behavior Tree UE5 complet (Blackboard + graphe) en UN appel,
a partir d'une description Python (dict), au-dessus des fonctions bas niveau exposees par le
plugin BTAuthoringKit (unreal.BTAuthoringLibrary, defini en C++).

Objectif : un agent (ou un humain) decrit le comportement voulu comme des donnees, jamais comme
une suite de clics dans l'editeur. Voir l'exemple `EXAMPLE_SPEC` en bas de fichier.

Utilisation typique (depuis execute_python / ue5-mcp) :

    import bt_authoring
    tree = bt_authoring.build_behavior_tree_from_spec(bt_authoring.EXAMPLE_SPEC)

Format d'une spec :

{
    "blackboard": {
        "path": "/Game/.../BBD_MonIA",
        "keys": [
            {"name": "TargetActor", "type": "Object", "filter_class": "/Script/Engine.Pawn"},
            {"name": "PatrolTarget", "type": "Vector"},
            {"name": "IsStunned", "type": "Bool"},
        ],
    },
    "tree": {
        "path": "/Game/.../BT_MonIA",
        "root": <node>,
    },
}

Un <node> est un dict :
{
    "kind": "composite" | "task",
    "type": "Selector" | "Sequence" | "SimpleParallel"   (si kind == composite)
          | "/Script/AIModule.BTTask_MoveTo" ou classe custom  (si kind == task),
    "label": "Combat",
    "decorators": [ {"class": "...", "label": "...", "properties": {...}, "blackboard_keys": {...}} ],
    "services":   [ {"class": "...", "label": "...", "properties": {...}, "blackboard_keys": {...}} ],
    "properties": {"WaitTime": "2.0"},                # proprietes simples du node lui-meme
    "blackboard_keys": {"BlackboardKey": "TargetActor"},  # proprietes FBlackboardKeySelector du node lui-meme
    "children": [ <node>, ... ]                       # uniquement pour kind == composite
}
"""

import unreal


def _key_type_enum(type_name):
    return getattr(unreal.BTAuthoringKeyType, type_name.upper())


def _composite_type_enum(type_name):
    return getattr(unreal.BTAuthoringCompositeType, type_name.upper())


def _resolve_class(path):
    if not path:
        return None
    return unreal.load_class(None, path)


def build_blackboard_from_spec(spec):
    bb_spec = spec["blackboard"]
    path, name = bb_spec["path"].rsplit("/", 1)
    bb = unreal.BTAuthoringLibrary.create_blackboard(path, name)
    for key in bb_spec.get("keys", []):
        filter_class = _resolve_class(key.get("filter_class"))
        ok = unreal.BTAuthoringLibrary.add_blackboard_key(
            bb, key["name"], _key_type_enum(key["type"]), filter_class
        )
        if not ok:
            print(f"[bt_authoring] ATTENTION: echec ajout cle Blackboard '{key['name']}'")
    return bb


def _apply_node_extras(graph_node, node_spec, blackboard):
    for prop_name, value in node_spec.get("properties", {}).items():
        ok = unreal.BTAuthoringLibrary.set_node_property(graph_node, prop_name, str(value))
        if not ok:
            print(f"[bt_authoring] ATTENTION: echec set_node_property {prop_name}={value}")

    for prop_name, key_name in node_spec.get("blackboard_keys", {}).items():
        ok = unreal.BTAuthoringLibrary.set_node_blackboard_key(graph_node, prop_name, blackboard, key_name)
        if not ok:
            print(f"[bt_authoring] ATTENTION: echec set_node_blackboard_key {prop_name}->{key_name}")

    for deco in node_spec.get("decorators", []):
        ok = unreal.BTAuthoringLibrary.add_decorator_to_node(graph_node, deco["class"], deco.get("label", ""))
        if not ok:
            print(f"[bt_authoring] ATTENTION: echec add_decorator_to_node {deco['class']}")
        # NOTE : appliquer properties/blackboard_keys sur un decorator/service attache demande de
        # recuperer son propre graph node (pas encore retourne par add_decorator_to_node dans
        # cette version) -- limitation connue, voir README section "Limites connues".

    for svc in node_spec.get("services", []):
        ok = unreal.BTAuthoringLibrary.add_service_to_node(graph_node, svc["class"], svc.get("label", ""))
        if not ok:
            print(f"[bt_authoring] ATTENTION: echec add_service_to_node {svc['class']}")


def _build_node(tree, node_spec, blackboard, x, y):
    kind = node_spec["kind"]
    label = node_spec.get("label", "")

    if kind == "composite":
        graph_node = unreal.BTAuthoringLibrary.add_composite_node(
            tree, _composite_type_enum(node_spec["type"]), label, x, y
        )
    elif kind == "task":
        graph_node = unreal.BTAuthoringLibrary.add_task_node(
            tree, node_spec["type"], label, x, y
        )
    else:
        raise ValueError(f"kind inconnu: {kind}")

    if not graph_node:
        raise RuntimeError(f"Echec creation du node '{label}' ({kind}/{node_spec['type']})")

    _apply_node_extras(graph_node, node_spec, blackboard)

    if kind == "composite":
        child_x = x
        for i, child_spec in enumerate(node_spec.get("children", [])):
            child_node = _build_node(tree, child_spec, blackboard, child_x + i * 220, y + 180)
            unreal.BTAuthoringLibrary.connect_child(graph_node, child_node)

    return graph_node


def build_behavior_tree_from_spec(spec):
    """Construit (ou reconstruit par-dessus) le Blackboard + Behavior Tree decrits par `spec`,
    compile et sauvegarde. Retourne le UBehaviorTree cree."""
    blackboard = build_blackboard_from_spec(spec)

    tree_spec = spec["tree"]
    path, name = tree_spec["path"].rsplit("/", 1)
    tree = unreal.BTAuthoringLibrary.create_behavior_tree(path, name, blackboard)

    root_child = _build_node(tree, tree_spec["root"], blackboard, 0, 0)
    unreal.BTAuthoringLibrary.set_tree_root(tree, root_child)

    ok = unreal.BTAuthoringLibrary.compile_behavior_tree(tree)
    print(f"[bt_authoring] Compile behavior tree '{name}': {'OK' if ok else 'ECHEC'}")
    return tree


# ──────────────────────────────────────────────────────────────────────────
# Exemple : patrouille -> detection -> poursuite -> attaque, meme structure
# que celle discutee pour RPGEnemy (a adapter : chemins de classes Blueprint,
# noms de cles).
# ──────────────────────────────────────────────────────────────────────────
EXAMPLE_SPEC = {
    "blackboard": {
        "path": "/Game/AI/Examples/BBD_ExampleEnemy",
        "keys": [
            {"name": "TargetActor", "type": "Object", "filter_class": "/Script/Engine.Pawn"},
            {"name": "PatrolTarget", "type": "Vector"},
            {"name": "SpawnLocation", "type": "Vector"},
        ],
    },
    "tree": {
        "path": "/Game/AI/Examples/BT_ExampleEnemy",
        "root": {
            "kind": "composite",
            "type": "Selector",
            "label": "Root",
            "children": [
                {
                    "kind": "task",
                    "type": "/Script/AIModule.BTTask_MoveTo",
                    "label": "Chase",
                    "decorators": [
                        {"class": "/Script/AIModule.BTDecorator_Blackboard", "label": "TargetActor Is Set"},
                    ],
                    "blackboard_keys": {"BlackboardKey": "TargetActor"},
                },
                {
                    "kind": "task",
                    "type": "/Script/AIModule.BTTask_MoveTo",
                    "label": "Patrol",
                    "blackboard_keys": {"BlackboardKey": "PatrolTarget"},
                },
            ],
        },
    },
}
