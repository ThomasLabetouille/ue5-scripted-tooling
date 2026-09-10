"""ue5_utils.py — Outils Python pour RPG_Test (UE5.8)
Portage depuis HoverGame. Adapté pour RPG_Test (RPG 3e personne, style Witcher).
"""
import unreal, os, math

# Tracing OpenTelemetry (voir tracing.py + Docs/OBSERVABILITE_TRACING.md) — import défensif :
# un déploiement qui n'a pas ce fichier ou pas le SDK opentelemetry installé continue de
# fonctionner à l'identique, juste sans traces.
try:
    from tracing import traced, span, record_verdict
except Exception:
    def traced(name=None, **kw):
        def deco(fn): return fn
        return deco
    class _NoOpSpanCtx:
        def set_attribute(self, *a, **k): pass
        def __enter__(self): return self
        def __exit__(self, *a): return False
    def span(name, **kw): return _NoOpSpanCtx()
    def record_verdict(sp, report_text): pass

# ══════════════════════════════════════════════════════
# SUBSYSTEMS
# ══════════════════════════════════════════════════════

def aeas(): return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# BlueprintEditingSubsystem — disponible seulement si le plugin RoomGenerator est installé
try:
    _bpes_instance = unreal.get_editor_subsystem(unreal.BlueprintEditingSubsystem)
    def bpes(): return unreal.get_editor_subsystem(unreal.BlueprintEditingSubsystem)
    _BPES_AVAILABLE = True
except Exception:
    _bpes_instance = None
    def bpes(): raise RuntimeError("BlueprintEditingSubsystem indisponible — plugin RoomGenerator requis")
    _BPES_AVAILABLE = False

try:
    bgh = unreal.BlueprintGraphHelper
    _BGH_AVAILABLE = True
except Exception:
    bgh = None
    _BGH_AVAILABLE = False

_BP_EDITING_AVAILABLE = _BPES_AVAILABLE

# NiagaraEditingSubsystem — ajouter User Parameters aux assets Niagara depuis Python
try:
    _nes_instance = unreal.get_editor_subsystem(unreal.NiagaraEditingSubsystem)
    def niagara_es(): return unreal.get_editor_subsystem(unreal.NiagaraEditingSubsystem)
    _NIAGARA_EDITING_AVAILABLE = True
except Exception:
    _nes_instance = None
    def niagara_es(): raise RuntimeError("NiagaraEditingSubsystem indisponible — compiler le plugin RoomGenerator")
    _NIAGARA_EDITING_AVAILABLE = False

# RoomGeneratorSubsystem — géométrie procédurale (rooms/corridors/cubes).
# Identique bit pour bit à HorrorGame côté C++ (vérifié le 2026-07-14, voir
# AgentToolkit/README.md "Écarts UE5.8 vs UE5.7") — seuls les raccourcis Python
# manquaient dans ce fichier jusqu'ici.
try:
    _room_instance = unreal.get_editor_subsystem(unreal.RoomGeneratorSubsystem)
    def room(): return unreal.get_editor_subsystem(unreal.RoomGeneratorSubsystem)
    _ROOM_AVAILABLE = True
except Exception:
    _room_instance = None
    def room(): raise RuntimeError("RoomGeneratorSubsystem indisponible — plugin RoomGenerator requis")
    _ROOM_AVAILABLE = False

# ══════════════════════════════════════════════════════
# BLUEPRINT GRAPH HELPERS
# ══════════════════════════════════════════════════════

def load_bp(path):
    """Charge un Blueprint asset depuis son chemin UE."""
    return unreal.load_asset(path)

def load_bp_class(path):
    """Charge une classe Blueprint. Essaie 3 stratégies dans l'ordre."""
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    if cls is None:
        cls = unreal.load_class(None, path)
    if cls is None and "." not in path.rstrip("/").split("/")[-1]:
        asset_name = path.rstrip("/").split("/")[-1]
        cls = unreal.load_class(None, f"{path}.{asset_name}_C")
    return cls

def resolve_class(p):
    """Résout une classe depuis son chemin /Script/..."""
    return bpes().resolve_class(p)

def add_fn_node(bp, graph, fn, cls, x=0, y=0):
    return bpes().add_function_call_node(bp, graph, fn, cls, x, y)

def add_cast(bp, graph, cls, x=0, y=0):
    return bpes().add_cast_node(bp, graph, cls, x, y)

def add_branch(bp, graph, x=0, y=0):
    return bgh.add_branch_node(bp, graph, x, y)

def add_foreach(bp, graph, x=0, y=0):
    return bgh.add_macro_node(bp, graph, "ForEachLoop", x, y)

def add_var_get(bp, graph, var, x=0, y=0):
    return bgh.add_variable_get_node(bp, graph, var, x, y)

def get_node(bp, g, nid):  return bgh.find_node_by_name(bp, g, nid)
def list_nodes(bp, g):      return bgh.list_graph_nodes(bp, g)
def get_pins(n):             return bgh.list_node_pins(n)
def fn_of(n):                return bgh.get_node_function_name(n)

def connect(fn, fp, tn, tp): return bgh.connect_pins(fn, fp, tn, tp)
def break_pin(n, p):         return bgh.break_all_pin_links(n, p)
def delete_node(n):          return bgh.delete_node(n)

def compile_bp(bp):
    """Compile et sauvegarde un Blueprint. Retourne le bool de compile_blueprint().

    BUG REEL trouve le 2026-07-15 en debloquant sys_win_condition : cette fonction n'avait
    JAMAIS de return (donc toujours None -> falsy), invisible jusqu'ici car aucun
    gameplay_system precedent n'etait passe par le chemin verified_build_for_unit() ->
    compile_bp() sur un Blueprint retourne par build_fn (sys_puzzle_1 avait ete construit/
    verifie manuellement, hors de ce chemin). Consequence concrete : verified_build_for_unit()
    ajoutait un faux "compile_bp() a retourné False" a CHAQUE unité gameplay_system future,
    quel que soit l'etat reel du Blueprint -- meme famille de piege que compile_blueprint()
    "qui ne reflete jamais fiablement un echec reel", mais dans l'autre sens ici (faux negatif
    plutot que faux positif)."""
    ok = bgh.compile_blueprint(bp)
    bgh.save_blueprint(bp)
    return ok

def create_blueprint(parent_class, package_path, asset_name):
    """Crée un nouveau Blueprint asset depuis zéro (parent_class = chemin '/Script/...'
    ou classe unreal directe, ex: unreal.Actor). Ajouté 2026-07-15 pour le puzzle
    générique (Axe D) — capacité générique, pas spécifique au puzzle : rien dans le
    projet avant ça ne créait de NOUVELLE classe Blueprint depuis Python, seulement
    du câblage sur des Blueprints déjà existants (BPGraph/BatchWireGraph).
    Retourne le Blueprint créé (déjà sauvegardé), ou None si échec/déjà existant."""
    full_path = package_path.rstrip("/") + "/" + asset_name
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        return unreal.load_asset(full_path)

    cls = parent_class
    if isinstance(parent_class, str):
        cls = resolve_class(parent_class)
    if cls is None:
        raise ValueError(f"create_blueprint: parent_class introuvable: {parent_class}")

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", cls)
    at = unreal.AssetToolsHelpers.get_asset_tools()
    new_bp = at.create_asset(asset_name, package_path, unreal.Blueprint, factory)
    if new_bp:
        unreal.EditorAssetLibrary.save_loaded_asset(new_bp)
    return new_bp

def add_var(bp, var_name, pin_category, default_value=""):
    """Ajoute une variable membre à un Blueprint via BlueprintEditingSubsystem.add_member_variable
    (fonction C++ ajoutée 2026-07-15 — BatchWireGraph/BPGraph ne câblent que des variables
    déjà existantes, rien ne pouvait auparavant en CRÉER une depuis Python).
    pin_category : "int" | "bool" | "float" | "string" | "name"."""
    result = bpes().add_member_variable(bp, var_name, pin_category, str(default_value))
    if not result.startswith("OK"):
        raise RuntimeError(f"add_var({var_name}): {result}")
    return result

# ══════════════════════════════════════════════════════
# ACTORS
# ══════════════════════════════════════════════════════

def spawn(cls_or_path, x=0, y=0, z=100, label=None):
    """Spawn un acteur depuis une classe ou un chemin Blueprint."""
    if isinstance(cls_or_path, str):
        cls_or_path = load_bp_class(cls_or_path)
    a = aeas().spawn_actor_from_class(cls_or_path, unreal.Vector(x,y,z), unreal.Rotator(0,0,0))
    if a and label: a.set_actor_label(label)
    return a

def destroy(a):              aeas().destroy_actor(a)
def all_actors():            return aeas().get_all_level_actors()
def actors_by_type(t):       return [a for a in all_actors() if type(a).__name__ == t]
def actor_by_label(lbl):     return next((a for a in all_actors() if a.get_actor_label()==lbl), None)
def set_loc(a, x, y, z):    a.set_actor_location(unreal.Vector(x,y,z), False, False)

def tag_actor(actor, tag):
    """Ajoute un gameplay tag à un acteur (utile pour GetAllActorsWithTag)."""
    tags = list(actor.tags)
    n = unreal.Name(tag)
    if n not in tags:
        tags.append(n)
        actor.tags = tags
    return actor

# ══════════════════════════════════════════════════════
# LEVEL
# ══════════════════════════════════════════════════════

def _package_disk_path(obj):
    """Chemin disque réel d'un ASSET chargé (ex. pour trust_gate, qui a besoin d'un chemin de
    FICHIER, pas d'un chemin /Game/...). IMPORTANT (trouvé le 2026-08-11 en testant en éditeur
    réel, pas en relecture statique — voir règle #8 de CLAUDE.md) : `get_system_path()` doit
    recevoir l'OBJET ASSET lui-même (le `world`, ou un ACTEUR pour un package OFPA externe), PAS
    le `UPackage` brut renvoyé par `obj.get_package()` — passé un package, l'API renvoie une
    CHAÎNE VIDE (pas une exception, pas `None`), donc un appelant qui teste seulement `if p`
    laisse passer un `None` sans jamais lever d'erreur. Défensif : retourne None plutôt que de
    lever si l'API échoue (ne doit jamais faire planter un appelant comme save())."""
    try:
        p = unreal.SystemLibrary.get_system_path(obj)
        return p if p else None
    except Exception:
        return None

def save():
    # CORRIGE le 2026-07-22 : save_current_level() renvoie True sans jamais
    # réécrire le .umap sur disque si le package du niveau n'a pas été marqué
    # "dirty" par le mécanisme standard (ce que set_actor_location()/modify()
    # via Python ne fait pas de façon fiable dans ce projet) — confirmé cause
    # racine de 3 régressions de position Enemy/Ramp documentées comme
    # "cause non identifiée" les 2026-07-15 et 2026-07-22 (x2) dans GAME_MEMORY.md.
    # save_loaded_asset(..., only_if_is_dirty=False) force réellement l'écriture,
    # vérifié par mtime du fichier avant/après (identique avec l'ancien appel,
    # change réellement avec celui-ci).
    #
    # RE-CORRIGE le 2026-07-23 (cause racine COMPLETE, enfin) : le niveau utilise
    # One File Per Actor (OFPA) — 101 packages d'acteurs externes dans
    # Content/__ExternalActors__/ThirdPerson/Lvl_ThirdPerson/. save_loaded_asset(world)
    # n'écrit QUE le package monde (.umap), jamais les packages d'acteurs externes :
    # toute modification d'ACTEUR (position, tags, matériau d'instance...) était donc
    # toujours perdue au redémarrage suivant, même avec le fix du 2026-07-22 (qui ne
    # corrigeait le problème que pour les données stockées dans le .umap lui-même).
    # C'est l'explication définitive des régressions à répétition Enemy/Ramp et tags
    # "Interactable" — confirmée le 2026-07-23 en comparant l'état avant/après un
    # redémarrage réel de l'éditeur. Fix : sauvegarder AUSSI tous les packages
    # d'acteurs externes du niveau, sans condition de dirty (même famille de méfiance
    # que le reste de cette fonction).
    with span("save_level") as sp:
        world = unreal.EditorLevelLibrary.get_editor_world()
        ok = unreal.EditorAssetLibrary.save_loaded_asset(world, only_if_is_dirty=False)
        sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        pkgs = []
        for a in sub.get_all_level_actors():
            try:
                p = a.get_package()
            except Exception:
                p = None
            if p and "__ExternalActors__" in p.get_name():
                pkgs.append(p)
        if pkgs:
            unreal.EditorLoadingAndSavingUtils.save_packages(pkgs, False)
        # Ce compteur est le point précis qui a fait défaut pendant les 3 régressions
        # Enemy/Ramp documentées : save_loaded_asset() seul renvoyait ok=True en écrivant
        # 0 package OFPA. Une trace qui montre ofpa_packages_saved=0 sur un niveau qui a
        # des acteurs modifiés est le signal direct de cette classe de bug.
        sp.set_attribute("umap_save_ok", bool(ok))
        sp.set_attribute("ofpa_packages_saved", len(pkgs))

        # Enregistrement trust_gate automatique du backend "ue5_native" (2026-08-11, CORRIGÉ le
        # même jour après un test réel en éditeur — voir docstring de _package_disk_path() pour
        # le bug exact trouvé : la 1ère version passait des UPackage bruts, get_system_path()
        # renvoyait '' silencieusement, la liste de chemins restait vide, AUCUNE exception
        # n'était levée -> succès apparent qui n'enregistrait en réalité RIEN. Piège "un silence
        # sans sortie n'est pas un succès" (règle #7), trouvé seulement parce qu'un vrai save()
        # a été exécuté en éditeur connecté, pas par relecture.
        #
        # PORTÉE : seul le package du NIVEAU est auto-enregistré (le fichier historiquement
        # responsable des 3 régressions Enemy/Ramp) — pas chacun des ~108 packages OFPA
        # individuels à chaque save() (mesuré en éditeur réel : 108 candidats sur ce niveau).
        # Un hash SHA256 par acteur externe à CHAQUE sauvegarde serait un coût perf non justifié
        # pour un signal marginal ; l'enregistrement manuel ciblé
        # (`trust_gate_record(chemin_precis)`) reste le bon outil pour un acteur précis suspect.
        #
        # Additif et NON BLOQUANT (try/except large) : une erreur d'enregistrement ne doit
        # jamais faire échouer save() lui-même — trust_gate est un filet de détection de
        # divergence, pas une condition de succès de la sauvegarde. Ça ne ferme QUE le côté
        # "ue5_native" du quorum : le côté "bash_sandbox" reste un appel manuel côté Cowork
        # (`python3 Tools/trust_gate.py --record bash_sandbox <chemin>` puis `--compare`) —
        # sans ce 2e appel, `compare_readings()` reste UNVERIFIED (1 seul backend), pas TRUSTED.
        try:
            _wp = _package_disk_path(world)
            if _wp:
                trust_gate_record(_wp, backend="ue5_native", note="save() auto (niveau)")
                sp.set_attribute("trust_gate_ue5_native_recorded", 1)
            else:
                unreal.log_error(
                    "[trust_gate] get_system_path(world) vide — enregistrement auto saute "
                    "(NON bloquant, save() reste valide)")
                sp.set_attribute("trust_gate_ue5_native_recorded", 0)
        except Exception as _tg_err:
            unreal.log_error(
                "[trust_gate] enregistrement auto post-save echoue (NON bloquant, save() "
                "reste valide) : {}".format(_tg_err))
            sp.set_attribute("trust_gate_auto_record_error", str(_tg_err))

        return ok
def open_level(p): unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(p)

# ══════════════════════════════════════════════════════
# ROOM GENERATION — portage HorrorGame (2026-07-14)
# ══════════════════════════════════════════════════════

def gen_room(cx, cy, sx, sy, h=300, t=20, name="Room"):
    room().generate_room(unreal.Vector(cx, cy, 0), unreal.Vector(sx, sy, 0), h, t, name)

def gen_corridor(x1, x2, w=500, h=300, t=20, name="Corr"):
    room().generate_corridor(unreal.Vector(x1, 0, 0), unreal.Vector(x2, 0, 0), w, h, t, name)

def spawn_cube(x, y, z, sx, sy, sz, label="Cube"):
    room().spawn_scaled_cube(None, unreal.Vector(x, y, z), unreal.Vector(sx, sy, sz), label)

# ══════════════════════════════════════════════════════
# MÉCANIQUES GÉNÉRIQUES — via BPGraph DSL (portage agent_core.py, 2026-07-14)
# ══════════════════════════════════════════════════════
# Version générique paramétrable (pas la version HorrorGame qui hardcode
# BP_LightSwitch) — RPG_Test n'a pas encore de Blueprint "interrupteur", donc
# switch_class doit être fourni explicitement tant qu'un tel Blueprint n'existe
# pas côté jeu (voir jalon "puzzle à 3 interrupteurs" dans ROADMAP_JEU_COMPLET.md
# de HorrorGame).
DEFAULT_SWITCH_CLASS = None  # ex: "/Game/RPGTest/Blueprints/BP_Switch.BP_Switch_C"

def create_trigger_zone(bp_path, trigger_event="ActorBeginOverlap",
                        action_fn="PrintString", action_cls="/Script/Engine.KismetSystemLibrary",
                        action_defaults=None, x=0, y=0):
    """Crée dans un BP existant : Overlap → action.
    Exemple : create_trigger_zone("/Game/MyBP", action_defaults={"InString": "Triggered!"})
    """
    bp = load_bp(bp_path)
    g = BPGraph(bp, "EventGraph")
    ev = g.event(trigger_event, x=x, y=y)
    fn = g.call(action_fn, action_cls, x=x+400, y=y,
                defaults=action_defaults or {})
    ev >> fn
    return g.wire_and_compile()

def create_door_mechanism(bp_path, open_z_offset=300.0):
    """Génère dans bp_path un système d'ouverture de porte :
    E pressé → SetActorLocation vers le haut (simule porte coulissante).
    """
    bp = load_bp(bp_path)
    g = BPGraph(bp, "EventGraph")
    ev     = g.event("InputAction Interact", x=0, y=0)
    vg_loc = g.call("GetActorLocation", "/Script/Engine.Actor", x=250, y=0)
    fn_add = g.call("Add_VectorVector", "/Script/Engine.KismetMathLibrary", x=500, y=0,
                    defaults={"B": f"(X=0.0,Y=0.0,Z={open_z_offset})"})
    fn_set = g.call("SetActorLocation", "/Script/Engine.Actor", x=750, y=0)
    ev >> vg_loc >> fn_add >> fn_set
    return g.wire_and_compile()

def create_sequence_puzzle(zone_center_x, zone_center_y, n_switches=3, switch_class=None):
    """Spawn n interrupteurs dans la zone (scaffolding générique de puzzle
    séquentiel — à relier manuellement à ta condition de victoire).

    switch_class : chemin Blueprint (ex: "/Game/RPGTest/Blueprints/BP_Switch.BP_Switch_C").
                   Si None, utilise DEFAULT_SWITCH_CLASS.
    """
    cls = switch_class or DEFAULT_SWITCH_CLASS
    if not cls:
        raise ValueError(
            "create_sequence_puzzle: aucune classe d'interrupteur fournie. "
            "Définis DEFAULT_SWITCH_CLASS dans ue5_utils.py ou passe switch_class=... explicitement."
        )
    spacing = 300
    spawned = []
    for i in range(n_switches):
        x = zone_center_x + (i - n_switches // 2) * spacing
        a = spawn(cls, x=x, y=zone_center_y, z=50, label=f"Puzzle_Switch_{i+1}")
        spawned.append(a)
        print(f"Switch {i+1} spawné @ ({x:.0f}, {zone_center_y:.0f}, 50)")
    return spawned

# ══════════════════════════════════════════════════════
# LIGHTS
# ══════════════════════════════════════════════════════

def point_light(x, y, z, intensity=3000, rgb=(255,255,255), radius=500, label=None):
    """Spawn une PointLight.
    IMPORTANT: utilise set_editor_property pour attenuation_radius —
    set_attenuation_radius() échoue silencieusement en UE5.x.

    Deux bugs corrigés le 2026-07-14 (trouvés en écrivant test_suite.py — voir
    CLAUDE.md HorrorGame pour l'historique complet des deux) :
    1. mobilité forcée à MOVABLE : ce pipeline agent ne fait jamais de lighting
       build ("Build Lighting"), donc une lumière Stationary (mobilité par
       défaut du moteur) peut se comporter de façon incomplète/incorrecte.
    2. unreal.Color() prend BGRA et NON RGBA — passer rgb dans l'ordre RGB tel
       quel inversait rouge et bleu (rgb=(255,0,0) donnait une lumière BLEUE).
    """
    pl = aeas().spawn_actor_from_class(
        unreal.PointLight.static_class(),
        unreal.Vector(x,y,z), unreal.Rotator(0,0,0)
    )
    lc = pl.point_light_component
    lc.set_mobility(unreal.ComponentMobility.MOVABLE)
    lc.set_editor_property("intensity", float(intensity))
    lc.set_editor_property("light_color", unreal.Color(rgb[2], rgb[1], rgb[0], 255))  # BGRA — r et b inversés
    lc.set_editor_property("attenuation_radius", float(radius))
    if label: pl.set_actor_label(label)
    actual = int(lc.get_editor_property("attenuation_radius"))
    if actual != int(radius):
        unreal.log_warning(f"[point_light] radius non appliqué: target={radius} actual={actual}")
    return pl

# ══════════════════════════════════════════════════════
# STATIC MESH PLACEMENT
# ══════════════════════════════════════════════════════

def place_static_mesh(ue_path, x, y, z, pitch=0, yaw=0, roll=0,
                      sx=1, sy=1, sz=1, label=""):
    """Place un StaticMesh depuis son chemin UE (/Game/...) dans le level."""
    mesh = unreal.load_asset(ue_path)
    if mesh is None:
        unreal.log_warning(f"[place_static_mesh] asset introuvable : {ue_path}")
        return None
    actor = aeas().spawn_actor_from_class(
        unreal.StaticMeshActor.static_class(),
        unreal.Vector(x, y, z),
        unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll)
    )
    if actor is None:
        return None
    comp = actor.get_component_by_class(unreal.StaticMeshComponent)
    if comp:
        comp.set_static_mesh(mesh)
    actor.set_actor_scale3d(unreal.Vector(sx, sy, sz))
    if label:
        actor.set_actor_label(label)
    return actor

def scatter_props(asset_paths, cx, cy, z=0, count=5, spread=300,
                  label_prefix="Prop", seed=None, scale_range=(0.9, 1.1)):
    """Place count props aléatoirement dans un rayon spread autour de (cx, cy, z)."""
    import random
    if seed is not None:
        random.seed(seed)
    placed = []
    for i in range(count):
        path = random.choice(asset_paths)
        ox  = random.uniform(-spread, spread)
        oy  = random.uniform(-spread, spread)
        yaw = random.uniform(0, 360)
        sc  = random.uniform(scale_range[0], scale_range[1])
        lbl = f"{label_prefix}_{i}"
        a = place_static_mesh(path, cx+ox, cy+oy, z, yaw=yaw, sx=sc, sy=sc, sz=sc, label=lbl)
        if a: placed.append(a)
    return placed

def list_assets(ue_folder, recursive=True):
    """Liste tous les assets dans un dossier UE."""
    return unreal.EditorAssetLibrary.list_assets(ue_folder, recursive=recursive)

# ══════════════════════════════════════════════════════
# SCREENSHOT — vérification visuelle obligatoire
# ══════════════════════════════════════════════════════

def take_screenshot(name="viewport"):
    """Prend un screenshot du viewport UE5 et retourne le chemin du fichier PNG.

    WORKFLOW OBLIGATOIRE après chaque étape majeure :
        path = take_screenshot("etape1")
        # puis Read(path) dans l'agent → vérification visuelle AVANT de continuer

    Utilise un timestamp dans le nom pour garantir un fichier FRAIS à chaque appel.
    Si le fichier attendu n'apparaît pas, loggue un avertissement STALE.
    """
    import glob as _glob, time as _time
    ts = int(_time.time())
    unique_name = f"{name}_{ts}"
    # Chemin absolu — project_saved_dir() retourne un chemin relatif moteur
    saved_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
    screenshot_dir = os.path.normpath(os.path.join(saved_dir, "Screenshots", "WindowsEditor"))
    os.makedirs(screenshot_dir, exist_ok=True)
    before = set(_glob.glob(os.path.join(screenshot_dir, "*.png")))
    unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, f"{unique_name}.png")
    # Attente avec polling — jusqu'à 8s
    exact = os.path.join(screenshot_dir, f"{unique_name}.png")
    for _ in range(16):
        _time.sleep(0.5)
        if os.path.exists(exact):
            unreal.log(f"[take_screenshot] ✅ {exact}")
            return exact
        after = set(_glob.glob(os.path.join(screenshot_dir, "*.png")))
        new_files = after - before
        if new_files:
            newest = max(new_files, key=os.path.getmtime)
            unreal.log(f"[take_screenshot] fallback→new: {newest}")
            return newest
    # Dernier recours — peut être stale
    all_shots = sorted(_glob.glob(os.path.join(screenshot_dir, "*.png")),
                       key=os.path.getmtime, reverse=True)
    if all_shots:
        age = int(_time.time() - os.path.getmtime(all_shots[0]))
        unreal.log_warning(f"[take_screenshot] STALE ({age}s) : {all_shots[0]}")
        return all_shots[0]
    return None

# ══════════════════════════════════════════════════════
# LOGS & DEBUG
# ══════════════════════════════════════════════════════

def read_log(n=80, filter_kw=None):
    """Lit les n dernières lignes du log UE5."""
    import glob
    log_dir = os.path.join(unreal.Paths.project_saved_dir(), "Logs")
    logs = sorted(glob.glob(os.path.join(log_dir, "*.log")), key=os.path.getmtime, reverse=True)
    if not logs: return "Aucun fichier log trouvé."
    with open(logs[0], encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    if filter_kw:
        lines = [l for l in lines if filter_kw.lower() in l.lower()]
    return "".join(lines[-n:])

def check_errors():
    """Raccourci : lit les 30 dernières erreurs du log."""
    return read_log(30, "Error") or "Aucune erreur trouvée."

# ══════════════════════════════════════════════════════
# PIE — Play In Editor
# ══════════════════════════════════════════════════════

def start_pie():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()

def stop_pie():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()

def is_pie_running():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()

def test_in_pie(duration=5.0):
    """Lance PIE, attend duration secondes, arrête, retourne erreurs + warnings."""
    import time
    start_pie(); time.sleep(duration); stop_pie(); time.sleep(1.0)
    return f"=== ERREURS ===\n{read_log(100,'Error')}\n=== WARNINGS ===\n{read_log(50,'Warning')}"

# ══════════════════════════════════════════════════════
# FICHIERS SÉCURISÉS
# ══════════════════════════════════════════════════════

# ══════════════════════════════════════════════════════
# GARDE-FOU D'INTÉGRITÉ FICHIER — portage HorrorGame (2026-07-14)
# ══════════════════════════════════════════════════════
# Le pont Windows↔sandbox Linux de Claude Cowork peut voir un fichier périmé ou
# corrompu (mount FUSE en retard, octets NUL de padding après une écriture
# tronquée) SANS lever la moindre erreur. Reconfirmé sur CE projet dès la
# première session (le .uproject juste édité, puis une comparaison de
# fonctions, sont tous les deux apparus périmés côté bash sandbox le
# 2026-07-14) — voir ROADMAP_JEU_COMPLET.md/AgentToolkit/README.md de
# HorrorGame pour l'historique complet de ce bug.

_INTEGRITY_PARSE_EXEMPT = set()  # rien d'exempté connu côté RPG_Test pour l'instant

def verify_file_integrity(path, expected_text=None, check_parse=True):
    """Vérifie qu'un fichier écrit sur disque n'est pas silencieusement
    corrompu : octets NUL, égalité exacte avec expected_text (fins de ligne
    normalisées), et — pour un .py — parse Python valide (ast.parse).
    Ne lève jamais elle-même : retourne (True, "") ou (False, message).
    """
    import os
    if not os.path.isfile(path):
        return False, "fichier introuvable: {}".format(path)

    data = open(path, "rb").read()

    if b"\x00" in data:
        count = data.count(b"\x00")
        first = data.index(b"\x00")
        return False, "{} octet(s) NUL trouve(s) (premier a l'offset {}) — fichier probablement tronque/corrompu".format(count, first)

    try:
        actual_text = data.decode("utf-8")
    except UnicodeDecodeError as e:
        return False, "decode UTF-8 echoue: {}".format(e)

    if expected_text is not None:
        actual_normalized = actual_text.replace("\r\n", "\n")
        expected_normalized = expected_text.replace("\r\n", "\n")
        if actual_normalized != expected_normalized:
            m = min(len(actual_normalized), len(expected_normalized))
            i = 0
            while i < m and actual_normalized[i] == expected_normalized[i]:
                i += 1
            return False, "contenu different de l'attendu a partir du caractere {} (attendu {} chars, obtenu {} chars, fins de ligne normalisees)".format(
                i, len(expected_normalized), len(actual_normalized))

    if path.endswith(".py") and check_parse:
        import ast
        try:
            ast.parse(actual_text, filename=path)
        except (SyntaxError, ValueError) as e:
            return False, "ne parse pas comme Python valide: {}".format(e)

    return True, ""


def scan_content_python_integrity(verbose=True):
    """Audit d'intégrité de TOUS les .py de Content/Python/. Retourne la liste
    des (path, message) en échec bloquant — liste vide = tout sain."""
    import os, glob
    d = os.path.dirname(os.path.abspath(__file__))
    problems = []
    warnings = []
    all_py = sorted(glob.glob(os.path.join(d, "*.py")))
    for f in all_py:
        base = os.path.basename(f)
        exempt = base in _INTEGRITY_PARSE_EXEMPT
        ok, msg = verify_file_integrity(f, check_parse=not exempt)
        if not ok:
            problems.append((f, msg))
        elif exempt:
            parse_ok, parse_msg = verify_file_integrity(f, check_parse=True)
            if not parse_ok:
                warnings.append((f, parse_msg))
    if verbose:
        if problems:
            unreal.log_error("[INTEGRITY] {} probleme(s) trouve(s) dans Content/Python/ :".format(len(problems)))
            for f, msg in problems:
                unreal.log_error("  - {}: {}".format(os.path.basename(f), msg))
        else:
            unreal.log("[INTEGRITY] OK — tous les .py de Content/Python/ sont sains ({} fichiers)".format(len(all_py)))
        for f, msg in warnings:
            unreal.log("[INTEGRITY][warning connu, non bloquant] {}: {}".format(os.path.basename(f), msg))
    return problems


def safe_write(path, content, must_contain=None):
    """Écrit un fichier de façon ATOMIQUE : écrit d'abord dans un fichier
    temporaire, vérifie intégralement (verify_file_integrity), et bascule
    seulement ensuite via os.replace() — le fichier final n'est jamais touché
    par une écriture ratée/corrompue."""
    import os

    tmp_path = "{}.tmp_safewrite_{}".format(path, os.getpid())
    try:
        with open(tmp_path, "w", encoding="utf-8") as f:
            f.write(content)

        ok, msg = verify_file_integrity(tmp_path, expected_text=content)
        if not ok:
            raise Exception("safe_write ECHEC integrite (fichier final NON touche) dans {}: {}".format(path, msg))

        if must_contain:
            for needle in (must_contain if isinstance(must_contain, list) else [must_contain]):
                if needle not in content:
                    raise Exception("safe_write ECHEC (fichier final NON touche): '{}' manquant dans {}".format(needle, path))

        os.replace(tmp_path, path)
    finally:
        if os.path.exists(tmp_path):
            try:
                os.remove(tmp_path)
            except Exception:
                pass

    unreal.log("[safe_write] OK {} ({} chars, ecriture atomique + integrite verifiee)".format(os.path.basename(path), len(content)))
    return len(content)


def safe_append(path, content, must_contain=None):
    """Ajoute du contenu à un fichier de façon ATOMIQUE (même logique que
    safe_write) : construit le contenu final en mémoire, l'écrit dans un
    fichier temporaire, vérifie intégralement, bascule seulement ensuite."""
    import os

    with open(path, encoding="utf-8") as f:
        old_content = f.read()

    new_content = old_content + content

    if not new_content.endswith(content):
        raise Exception("safe_append ECHEC: incoherence interne de concatenation pour {} (ne devrait jamais arriver)".format(path))

    tmp_path = "{}.tmp_safeappend_{}".format(path, os.getpid())
    try:
        with open(tmp_path, "w", encoding="utf-8") as f:
            f.write(new_content)

        ok, msg = verify_file_integrity(tmp_path, expected_text=new_content)
        if not ok:
            raise Exception("safe_append ECHEC integrite (fichier final NON touche) dans {}: {}".format(path, msg))

        if must_contain:
            for needle in (must_contain if isinstance(must_contain, list) else [must_contain]):
                if needle not in new_content:
                    raise Exception("safe_append ECHEC (fichier final NON touche): '{}' manquant dans {}".format(needle, path))

        os.replace(tmp_path, path)
    finally:
        if os.path.exists(tmp_path):
            try:
                os.remove(tmp_path)
            except Exception:
                pass

    unreal.log("[safe_append] OK {} ({} chars total, ecriture atomique + integrite verifiee)".format(os.path.basename(path), len(new_content)))
    return len(new_content)


# ══════════════════════════════════════════════════════
# TRUST GATE — quorum multi-canaux (portage HorrorGame, 2026-07-14)
# Voir Tools/trust_gate.py pour le mécanisme complet (sidecars indépendants
# bash_sandbox / ue5_native, verdict TRUSTED/DIVERGENT/UNVERIFIED).
# ══════════════════════════════════════════════════════

def _load_trust_gate():
    """Importe Tools/trust_gate.py par chemin de fichier direct — accès disque
    Windows natif depuis ce process, canal de confiance pour le quorum."""
    import importlib.util
    # __file__ = <ROOT>/Content/Python/ue5_utils.py -> 3 dirname() pour remonter à ROOT
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    tg_path = os.path.join(root, "Tools", "trust_gate.py")
    spec = importlib.util.spec_from_file_location("trust_gate_native", tg_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def trust_gate_record(path, backend="ue5_native", note=None):
    """Enregistre une lecture de `path` dans le quorum trust_gate, depuis
    l'intérieur d'UE5 (accès disque Windows natif)."""
    tg = _load_trust_gate()
    result = tg.record_reading(path, backend, note=note)
    if result.get("ok"):
        unreal.log("[trust_gate] {} :: {} enregistre (sha256={}...)".format(backend, path, result["sha256"][:12]))
    else:
        unreal.log_error("[trust_gate] ECHEC enregistrement {} :: {} -> {}".format(backend, path, result.get("error")))
    return result


def trust_gate_compare(path):
    """Verdict de quorum (TRUSTED/DIVERGENT/UNVERIFIED) pour un fichier."""
    tg = _load_trust_gate()
    result = tg.compare_readings(path)
    if result["verdict"] == "DIVERGENT":
        unreal.log_error("[trust_gate] DIVERGENCE sur {} : {}".format(path, result["reason"]))
    else:
        unreal.log("[trust_gate] {} sur {} : {}".format(result["verdict"], path, result["reason"]))
    return result


def trust_gate_check(paths=None, strict_unverified=False):
    """Audit global du quorum trust_gate pour tous les fichiers connus (ou une
    liste donnée)."""
    tg = _load_trust_gate()
    return tg.check_ledger(paths=paths, strict_unverified=strict_unverified)


# ══════════════════════════════════════════════════════
# ANTI-RÉGRESSION AUTOMATIQUE — safe_modify_plugin() (portage HorrorGame, 2026-07-14)
# ══════════════════════════════════════════════════════

def safe_modify_plugin(fn, *args, **kwargs):
    """Encapsule une modification risquée du plugin (C++/Python, BP wiring, etc.)
    avec un filet de sécurité anti-régression automatique.

    Principe : lance test_suite.run_all() AVANT (baseline), exécute fn(), relance
    test_suite.run_all() APRÈS, compare test par test. Si un test qui passait avant
    échoue maintenant → régression réelle détectée → lève une Exception. Un test déjà
    cassé avant qui l'est encore après n'est PAS une régression.

    fn : callable sans argument obligatoire qui effectue la modification.
    Retourne le résultat de fn() si aucune régression n'est détectée.
    Lève une Exception si la baseline elle-même échoue, ou si une régression réelle
    est détectée après la modif.
    """
    import sys

    def _reload_and_run(verbose):
        for m in list(sys.modules):
            if m in ("ue5_utils", "test_suite"):
                del sys.modules[m]
        try:
            import test_suite as _ts
        except ImportError:
            # test_suite.py etait le lanceur de tests de RPG_Test : il visait /Game/RPGTest et
            # les modules Blockout Python, tous retires lors de la fusion du 2026-08-29 (le
            # plugin C++ BlockoutTools les remplace). Cette fonction dependait de lui pour
            # etablir une reference AVANT/APRES modification.
            #
            # Message explicite plutot qu'un ModuleNotFoundError nu : sans lui, l'appelant
            # croirait a une installation cassee alors que c'est une dependance volontairement
            # supprimee.
            raise RuntimeError(
                "test_suite est absent : il appartenait a RPG_Test et n'a pas ete repris. "
                "Pour reutiliser cette garde, la brancher sur le gate de ce projet "
                "(test_sit_detection.py / test_npc_behavior.py) plutot que sur test_suite.")
        ok = _ts.run_all(verbose=verbose)
        snapshot = {name: status for status, name, _ in _ts._results}
        return ok, snapshot, _ts._OK, _ts._FAIL

    with span("safe_modify_plugin", fn=getattr(fn, "__name__", str(fn))) as sp:
        print("[safe_modify_plugin] Baseline AVANT modification...")
        baseline_ok, baseline, OK_TAG, FAIL_TAG = _reload_and_run(verbose=False)
        sp.set_attribute("baseline_ok", bool(baseline_ok))
        sp.set_attribute("baseline_test_count", len(baseline))
        if not baseline_ok:
            failed = [n for n, s in baseline.items() if s == FAIL_TAG]
            sp.set_attribute("verdict", "ARRET_BASELINE_ROUGE")
            sp.set_attribute("baseline_failed_tests", ",".join(failed))
            raise Exception(
                "[safe_modify_plugin] ARRET : {} test(s) déjà en échec AVANT la "
                "modification (baseline non fiable) : {}. Corriger l'existant "
                "d'abord — ne pas modifier sur une base déjà rouge.".format(
                    len(failed), failed))
        print("[safe_modify_plugin] Baseline OK ({} tests). Exécution de la "
              "modification...".format(len(baseline)))

        result = fn(*args, **kwargs)

        print("[safe_modify_plugin] Vérification APRÈS modification...")
        after_ok, after, _, _ = _reload_and_run(verbose=False)
        sp.set_attribute("after_ok", bool(after_ok))
        sp.set_attribute("after_test_count", len(after))

        regressions = [
            name for name, status in after.items()
            if baseline.get(name) == OK_TAG and status == FAIL_TAG
        ]
        sp.set_attribute("regression_count", len(regressions))

        if regressions:
            sp.set_attribute("verdict", "REGRESSION_DETECTEE")
            sp.set_attribute("regressions", ",".join(regressions))
            try:
                from opentelemetry.trace import StatusCode
                sp.set_status(StatusCode.ERROR, f"{len(regressions)} régression(s)")
            except Exception:
                pass
            raise Exception(
                "[safe_modify_plugin] REGRESSION DETECTEE : {} test(s) passaient "
                "avant la modification et échouent maintenant : {}. La modification "
                "N'A PAS été considérée sûre — corriger avant de sauvegarder ou de "
                "continuer.".format(len(regressions), regressions))

        if not after_ok:
            still_failing = [n for n, s in after.items() if s == FAIL_TAG]
            print("[safe_modify_plugin] ATTENTION : {} test(s) toujours en échec, "
                  "mais déjà cassés avant la modif — pas une nouvelle régression : "
                  "{}".format(len(still_failing), still_failing))

        sp.set_attribute("verdict", "OK")
        print("[safe_modify_plugin] OK — aucune régression détectée ({} tests).".format(
            len(after)))
        return result


# ══════════════════════════════════════════════════════
# BOUCLE GENERATION → VALIDATION FERMEE — execute_validated() (2026-08-10)
# ══════════════════════════════════════════════════════
#
# Point d'entrée pensé pour du code Python généré par un LLM (panneau in-editor
# "Tools -> Claude AI", ExecutePython() côté C++ dans ClaudeEditorSubsystem.cpp) —
# jusqu'ici ce panneau exécutait le code renvoyé par le modèle (Groq
# llama-3.3-70b-versatile) SANS AUCUNE validation : pas de compile-check, pas de
# test_suite, aucune détection de régression, aucun rapport structuré — seul le
# stdout/stderr brut revenait dans la conversation, et un LLM (a fortiori un modèle
# plus petit que celui utilisé dans cette session Cowork) peut très bien produire du
# code qui ne lève aucune exception mais casse un Blueprint, une génération existante,
# ou un test déjà vert — exactement la famille de piège déjà documentée dans
# CLAUDE.md ("un rendu qui a l'air correct ne garantit pas un calcul juste",
# "compile_blueprint() ne garantit pas l'absence de bug de logique").
#
# Différence avec safe_modify_plugin() ci-dessus : safe_modify_plugin() prend un
# callable *de confiance* (écrit par un humain/agent qui lit le résultat) et laisse
# l'exception de régression remonter à l'appelant. execute_validated() est le point
# d'entrée pour du code *non fiable* (LLM) : elle n'exécute qu'UNE fois un baseline
# figé, ne laisse JAMAIS d'exception remonter crue, et retourne TOUJOURS une chaîne de
# rapport structurée avec un VERDICT explicite (OK / ECHEC / REGRESSION DETECTEE) —
# le but est que le LLM voie clairement s'il doit se corriger au tour suivant, plutôt
# qu'un panneau qui affiche un faux "OK" ou une trace Python illisible.
#
# Limite connue, volontairement pas traitée ici : AUCUN rollback automatique des
# assets/Blueprints modifiés en cas de régression détectée (même limite que
# safe_modify_plugin()/wire_and_compile() dans ce projet à date) — une régression
# doit être corrigée par un tour de conversation suivant, pas silencieusement
# annulée. Coût : deux passages de test_suite.run_all() par génération (~1-2s chacun
# sur RPG_Test) — accepté volontairement (fiabilité avant vitesse), pas d'échappatoire
# "skip validation" exposée au LLM pour éviter qu'il ne l'utilise pour contourner le
# filet de sécurité lui-même.

def execute_validated(code, source="unknown"):
    """Exécute `code` (string Python généré par un LLM) à travers le même filet de
    sécurité anti-régression que safe_modify_plugin(), mais retourne toujours un
    rapport texte structuré au lieu de laisser une exception remonter.
    `source` : étiquette libre pour le rapport (ex. "claude_panel").
    """
    import sys, io, traceback, time

    def _reload_and_run(verbose):
        for m in list(sys.modules):
            if m in ("ue5_utils", "test_suite"):
                del sys.modules[m]
        try:
            import test_suite as _ts
        except ImportError:
            # test_suite.py etait le lanceur de tests de RPG_Test : il visait /Game/RPGTest et
            # les modules Blockout Python, tous retires lors de la fusion du 2026-08-29 (le
            # plugin C++ BlockoutTools les remplace). Cette fonction dependait de lui pour
            # etablir une reference AVANT/APRES modification.
            #
            # Message explicite plutot qu'un ModuleNotFoundError nu : sans lui, l'appelant
            # croirait a une installation cassee alors que c'est une dependance volontairement
            # supprimee.
            raise RuntimeError(
                "test_suite est absent : il appartenait a RPG_Test et n'a pas ete repris. "
                "Pour reutiliser cette garde, la brancher sur le gate de ce projet "
                "(test_sit_detection.py / test_npc_behavior.py) plutot que sur test_suite.")
        ok = _ts.run_all(verbose=verbose)
        snapshot = {name: status for status, name, _ in _ts._results}
        return ok, snapshot, _ts._OK, _ts._FAIL

    with span("execute_validated", source=source, code_len=len(code or "")) as sp:
        t0 = time.time()
        report = ["[execute_validated:{}] Baseline AVANT exécution...".format(source)]

        try:
            baseline_ok, baseline, OK_TAG, FAIL_TAG = _reload_and_run(verbose=False)
        except Exception as e:
            report.append(
                "[execute_validated] ECHEC : impossible de lancer la baseline test_suite "
                "avant même d'exécuter le code généré : {}. Code NON exécuté.".format(e))
            text = "\n".join(report)
            record_verdict(sp, text)
            sp.set_attribute("verdict", "ECHEC_BASELINE_IMPOSSIBLE")
            return text

        sp.set_attribute("baseline_ok", bool(baseline_ok))
        sp.set_attribute("baseline_test_count", len(baseline))

        if not baseline_ok:
            failed = [n for n, s in baseline.items() if s == FAIL_TAG]
            report.append(
                "[execute_validated] ARRET AVANT EXECUTION : {} test(s) déjà en échec "
                "avant même de lancer ce code ({}). Baseline non fiable — corriger "
                "l'existant d'abord plutôt que d'exécuter par-dessus une base déjà "
                "rouge. Code NON exécuté.".format(len(failed), failed))
            sp.set_attribute("verdict", "ARRET_BASELINE_ROUGE")
            return "\n".join(report)

        # --- Exécution du code généré, dans un namespace isolé et frais ---
        buf = io.StringIO()
        old_out, old_err = sys.stdout, sys.stderr
        sys.stdout = sys.stderr = buf
        exec_error = None
        try:
            exec(code, {"__name__": "__claude_panel_exec__"})
        except Exception:
            exec_error = traceback.format_exc()
        finally:
            sys.stdout, sys.stderr = old_out, old_err
        exec_output = buf.getvalue().strip()
        sp.set_attribute("exec_raised_exception", exec_error is not None)

        report.append("[execute_validated:{}] Vérification APRES exécution...".format(source))
        try:
            after_ok, after, OK_TAG, FAIL_TAG = _reload_and_run(verbose=False)
        except Exception as e:
            report.append(
                "[execute_validated] ATTENTION : impossible de relancer test_suite après "
                "exécution ({}) — verdict de régression INCONNU, à vérifier "
                "manuellement (ne pas supposer que c'est OK).".format(e))
            after_ok, after = None, {}

        regressions = []
        if after:
            regressions = [
                name for name, status in after.items()
                if baseline.get(name) == OK_TAG and status == FAIL_TAG
            ]
        sp.set_attribute("regression_count", len(regressions))
        if regressions:
            sp.set_attribute("regressions", ",".join(regressions))

        dt = time.time() - t0
        sp.set_attribute("duration_s", round(dt, 2))

        if exec_output:
            report.append("--- Sortie du code exécuté ---\n" + exec_output)
        if exec_error:
            report.append("--- EXCEPTION Python pendant l'exécution ---\n" + exec_error)

        if regressions:
            report.append(
                "--- VERDICT : REGRESSION DETECTEE ({} test(s)) ---\n{} test(s) "
                "passaient avant ce code et échouent maintenant : {}. La modification "
                "N'EST PAS considérée sûre. Corriger avant de continuer sur cette base "
                "— ne pas ignorer ce message ni relancer le même code tel quel.".format(
                    len(regressions), len(regressions), regressions))
        elif exec_error:
            report.append(
                "--- VERDICT : ECHEC (exception Python ; aucune régression de "
                "test_suite détectée en plus) ---")
        elif after_ok is None:
            report.append("--- VERDICT : INCONNU (vérification après-coup indisponible) ---")
        else:
            report.append(
                "--- VERDICT : OK — {} test(s), aucune régression, {:.1f}s ---".format(
                    len(after), dt))

        text = "\n".join(report)
        record_verdict(sp, text)
        return text


# ══════════════════════════════════════════════════════
# OCCUPANCY GRID — placement garanti zéro overlap
# ══════════════════════════════════════════════════════

class OccupancyGrid:
    """Grille d'occupation spatiale (plan XY)."""
    def __init__(self, x_min, x_max, y_min, y_max, cell_size=50):
        self.cell_size = float(cell_size)
        self.x_min = float(x_min); self.y_min = float(y_min)
        self.nx = max(1, int((x_max - x_min) / cell_size) + 2)
        self.ny = max(1, int((y_max - y_min) / cell_size) + 2)
        self.grid = [[False] * self.ny for _ in range(self.nx)]

    def _to_cell(self, x, y):
        cx = int((x - self.x_min) / self.cell_size)
        cy = int((y - self.y_min) / self.cell_size)
        return max(0, min(cx, self.nx-1)), max(0, min(cy, self.ny-1))

    def mark_occupied(self, x, y, radius):
        cx, cy = self._to_cell(x, y)
        r = int(radius / self.cell_size) + 1
        for dx in range(-r, r+1):
            for dy in range(-r, r+1):
                if dx*dx + dy*dy <= r*r:
                    nx_ = cx+dx; ny_ = cy+dy
                    if 0 <= nx_ < self.nx and 0 <= ny_ < self.ny:
                        self.grid[nx_][ny_] = True

    def is_free(self, x, y, radius):
        cx, cy = self._to_cell(x, y)
        r = int(radius / self.cell_size) + 1
        for dx in range(-r, r+1):
            for dy in range(-r, r+1):
                if dx*dx + dy*dy <= r*r:
                    nx_ = cx+dx; ny_ = cy+dy
                    if 0 <= nx_ < self.nx and 0 <= ny_ < self.ny:
                        if self.grid[nx_][ny_]: return False
        return True

    def find_nearest_free(self, x, y, radius, max_dist=600):
        step = self.cell_size; d = step
        while d <= max_dist:
            for angle_deg in range(0, 360, 15):
                angle = math.radians(angle_deg)
                cx = x + d * math.cos(angle)
                cy = y + d * math.sin(angle)
                if self.is_free(cx, cy, radius): return cx, cy
            d += step
        return None, None


_global_grid = None


def build_occupancy_grid_from_level(cell_size=50, max_extent=20000.0, max_cells_per_actor=200000):
    """Construit une OccupancyGrid à partir de la géométrie existante du level.
    Ignore les dalles horizontales (sols, plafonds).

    GARDE-FOU (porté depuis HorrorGame le 2026-07-14, absent de cette version jusqu'ici —
    trouvé en écrivant test_suite.py) : sans filtre de taille, un acteur à bounding box
    démesurée (typiquement SM_SkySphere du template Third Person que RPG_Test utilise comme
    base, voir CLAUDE.md "Template de base") fait exploser le nombre d'itérations de la
    boucle de balayage ci-dessous (O((extent/cell_size)^2)) → freeze puis crash UE5 (OOM).
    Bug déjà rencontré et corrigé sur HorrorGame (AgentDemo, 2026-07-03) — jamais reproduit
    ici car aucun appel avec un skydome présent n'avait encore été tenté, pas parce que le
    risque n'existait pas. Deux filtres : (1) ignorer tout actor dont extent.x/extent.y
    dépasse max_extent ; (2) capper le nombre de cellules balayées par actor en filet de
    sécurité générique.
    """
    global _global_grid
    actors_list = aeas().get_all_level_actors()
    all_x, all_y = [], []
    for a in actors_list:
        loc = a.get_actor_location()
        all_x.append(loc.x); all_y.append(loc.y)
    if not all_x:
        _global_grid = OccupancyGrid(-10000, 10000, -10000, 10000, cell_size)
        return _global_grid
    _global_grid = OccupancyGrid(
        min(all_x)-1000, max(all_x)+1000,
        min(all_y)-1000, max(all_y)+1000, cell_size
    )
    marked = skipped_flat = skipped_huge = 0
    for a in actors_list:
        if "StaticMeshActor" in a.get_class().get_name():
            origin, extent = a.get_actor_bounds(False)
            if extent.z < 30.0:
                skipped_flat += 1; continue
            if extent.x > max_extent or extent.y > max_extent:
                skipped_huge += 1
                unreal.log_warning(
                    f"[build_occupancy_grid_from_level] Ignoré (bbox énorme, probable "
                    f"skydome/backdrop) : {a.get_actor_label()} extent=({extent.x:.0f},{extent.y:.0f})")
                continue
            x_steps = max(1, int(extent.x * 2 / cell_size))
            y_steps = max(1, int(extent.y * 2 / cell_size))
            if (x_steps + 1) * (y_steps + 1) > max_cells_per_actor:
                skipped_huge += 1
                unreal.log_warning(
                    f"[build_occupancy_grid_from_level] Ignoré (trop de cellules: "
                    f"{(x_steps + 1) * (y_steps + 1)}) : {a.get_actor_label()}")
                continue
            for xi in range(x_steps+1):
                for yi in range(y_steps+1):
                    px = origin.x - extent.x + xi * (extent.x*2 / max(1, x_steps))
                    py = origin.y - extent.y + yi * (extent.y*2 / max(1, y_steps))
                    _global_grid.mark_occupied(px, py, cell_size)
            marked += 1
    unreal.log(f"[build_occupancy_grid_from_level] {marked} objets marqués, "
               f"{skipped_flat} sols ignorés, {skipped_huge} bbox énormes ignorées")
    return _global_grid


def safe_place(cls_or_path, x, y, z_hint=0, actor_radius=60.0, grid=None, label=None):
    """Place un acteur avec garantie zéro overlap — 3 couches de vérification.

    Deux bugs corrigés le 2026-07-14 (trouvés en écrivant test_suite.py — même bugs déjà
    documentés et corrigés côté HorrorGame, voir son CLAUDE.md section "safe_place — bugs
    corrigés") :
    1. final_z = floor_z + actor_radius posait la sphère de collision EXACTEMENT au
       contact du sol (0 UU de jeu) — sphere_overlap_actors() détecte ce contact exact
       comme un overlap avec le sol lui-même, faisant échouer Couche 3 sur un placement
       pourtant valide. Fix : marge _FLOOR_CLEARANCE = 2.0 UU ajoutée à final_z.
    2. sphere_overlap_actors() retourne None (pas une liste vide) quand aucun overlap
       n'existe — le cas le plus fréquent. Itérer directement dessus levait un TypeError
       avalé silencieusement par l'except, désactivant Couche 3 par accident à chaque
       placement propre. Fix : `overlaps or []`.
    """
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    g = grid if grid is not None else _global_grid
    start_z = z_hint + 50
    _FLOOR_CLEARANCE = 2.0

    hit = unreal.SystemLibrary.line_trace_single(
        world, unreal.Vector(x, y, start_z), unreal.Vector(x, y, z_hint-500),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [],
        unreal.DrawDebugTrace.NONE, True,
        unreal.LinearColor(1,0,0,1), unreal.LinearColor(0,1,0,1), 0.0
    )
    t = hit.to_tuple()
    if not t[0]:
        unreal.log_warning(f"[safe_place] Pas de sol sous ({int(x)},{int(y)}) — skip")
        return None
    floor_z = start_z - t[3]
    final_z = floor_z + actor_radius + _FLOOR_CLEARANCE
    final_x, final_y = x, y

    if g is not None and not g.is_free(x, y, actor_radius):
        nx_, ny_ = g.find_nearest_free(x, y, actor_radius)
        if nx_ is None:
            unreal.log_warning(f"[safe_place] Zone saturée autour de ({int(x)},{int(y)}) — skip")
            return None
        final_x, final_y = nx_, ny_
        hit2 = unreal.SystemLibrary.line_trace_single(
            world, unreal.Vector(final_x, final_y, z_hint+50), unreal.Vector(final_x, final_y, z_hint-500),
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [],
            unreal.DrawDebugTrace.NONE, True,
            unreal.LinearColor(1,0,0,1), unreal.LinearColor(0,1,0,1), 0.0
        )
        t2 = hit2.to_tuple()
        if t2[0]: floor_z = (z_hint+50) - t2[3]; final_z = floor_z + actor_radius + _FLOOR_CLEARANCE

    try:
        obj_types = unreal.Array(unreal.ObjectTypeQuery)
        ignore    = unreal.Array(unreal.Actor)
        overlaps  = unreal.SystemLibrary.sphere_overlap_actors(
            world, unreal.Vector(final_x, final_y, final_z),
            actor_radius, obj_types, unreal.Actor, ignore
        )
        geo = [o for o in (overlaps or []) if "StaticMeshActor" in o.get_class().get_name()]
        if geo:
            unreal.log_warning(f"[safe_place] Overlap physique @ ({int(final_x)},{int(final_y)}) — skip")
            return None
    except Exception as e:
        unreal.log_warning(f"[safe_place] sphere_overlap_actors échoué ({e}) — couche 3 ignorée")

    cls_obj = None
    if isinstance(cls_or_path, str):
        cls_obj = unreal.load_class(None, cls_or_path)
        if cls_obj is None:
            cls_obj = unreal.EditorAssetLibrary.load_blueprint_class(cls_or_path)
        if cls_obj is None and "." not in cls_or_path.rstrip("/").split("/")[-1]:
            asset_name = cls_or_path.rstrip("/").split("/")[-1]
            cls_obj = unreal.load_class(None, f"{cls_or_path}.{asset_name}_C")
    else:
        cls_obj = cls_or_path

    if cls_obj is None:
        unreal.log_warning(f"[safe_place] Classe introuvable : {cls_or_path}")
        return None

    actor = aeas().spawn_actor_from_class(cls_obj, unreal.Vector(final_x, final_y, final_z), unreal.Rotator(0,0,0))
    if actor is None:
        unreal.log_warning("[safe_place] spawn_actor_from_class a retourné None")
        return None
    if label: actor.set_actor_label(label)
    if g is not None: g.mark_occupied(final_x, final_y, actor_radius)
    unreal.log(f"[safe_place] ✅ {label or 'actor'} @ ({int(final_x)},{int(final_y)},{int(final_z)})")
    return actor


def safe_spawn_enemy(x, y, z_hint=100, grid=None, label="Enemy"):
    """Spawn un ennemi RPG avec tag 'Enemy' et placement garanti sans overlap.
    À adapter quand BP_RPG_Enemy sera créé.
    """
    # Corrigé le 2026-07-14 : le chemin réel est ".../Blueprints/Enemy/..." (singulier),
    # pas ".../Enemies/..." — voir GAME_MEMORY.md table "Assets disponibles". L'ancien
    # chemin ne correspondait à aucun asset (does_asset_exist -> False), donc tout appel
    # à safe_spawn_enemy() échouait silencieusement (safe_place retourne None sur une
    # classe introuvable). Trouvé en écrivant test_suite.py.
    ENEMY_BP = "/Game/RPGTest/Blueprints/Enemy/BP_RPGEnemy"
    actor = safe_place(ENEMY_BP, x, y, z_hint=z_hint, actor_radius=60.0, grid=grid, label=label)
    if actor:
        tag_actor(actor, "Enemy")
    return actor

# ══════════════════════════════════════════════════════
# BPGraph DSL — câble tout un graphe en 1 appel Python
# Nécessite BlueprintEditingSubsystem (plugin RoomGenerator)
# ══════════════════════════════════════════════════════

class NodeRef:
    """Référence à un noeud dans un BPGraph en construction."""
    def __init__(self, graph, nid):
        self._g = graph; self.nid = nid

    def then(self, other, from_pin="then", to_pin="execute"):
        self._g._conn(self.nid, from_pin, other.nid, to_pin)
        return other

    def __rshift__(self, other): return self.then(other)

    def data(self, other, fp, tp):
        self._g._conn(self.nid, fp, other.nid, tp)
        return other


class BPGraph:
    """Builder de graphe Blueprint. Accumule noeuds + connexions
    puis les envoie en UN seul appel C++ via BatchWireGraph.
    """
    def __init__(self, bp, graph_name="EventGraph"):
        self._bp = bp; self._gn = graph_name
        self._nodes = []; self._conns = []; self._ctr = 0

    def _nid(self):
        n = f"n{self._ctr}"; self._ctr += 1; return n

    def _node(self, d):
        nid = self._nid(); d["id"] = nid
        self._nodes.append(d)
        return NodeRef(self, nid)

    def _conn(self, f, fp, t, tp):
        self._conns.append({"from": f, "fp": fp, "to": t, "tp": tp})

    def event(self, name, x=0, y=0):
        return self._node({"type": "event", "name": name, "x": x, "y": y})

    def custom_event(self, name, x=0, y=0):
        return self._node({"type": "custom_event", "name": name, "x": x, "y": y})

    def call(self, fn, cls, x=0, y=0, defaults=None):
        d = {"type": "function", "fn": fn, "cls": cls, "x": x, "y": y}
        if defaults: d["defaults"] = defaults
        return self._node(d)

    def cast(self, cls, x=0, y=0):
        return self._node({"type": "cast", "cls": cls, "x": x, "y": y})

    def branch(self, x=0, y=0):
        return self._node({"type": "branch", "x": x, "y": y})

    def foreach(self, x=0, y=0):
        return self._node({"type": "macro", "name": "ForEachLoop", "x": x, "y": y})

    def macro(self, name, x=0, y=0):
        return self._node({"type": "macro", "name": name, "x": x, "y": y})

    def var_get(self, var, x=0, y=0):
        return self._node({"type": "var_get", "var": var, "x": x, "y": y})

    def var_set(self, var, x=0, y=0):
        return self._node({"type": "var_set", "var": var, "x": x, "y": y})

    def sequence(self, x=0, y=0):
        return self._node({"type": "sequence", "x": x, "y": y})

    def conn(self, fr, fp, to, tp):
        self._conn(fr.nid, fp, to.nid, tp); return self

    def wire(self):
        import json
        payload = json.dumps({"nodes": self._nodes, "connections": self._conns})
        result = bpes().batch_wire_graph(self._bp, self._gn, payload)
        unreal.log(f"[BPGraph] {result}")
        return result

    def wire_and_compile(self):
        """Câble + compile, avec vérification anti-régression automatique
        (safe_modify_plugin(), portage HorrorGame 2026-07-14) — coût réel : deux
        passages de test_suite.run_all() (~1-2s chacun sur RPG_Test). Pour l'ancien
        comportement sans vérification (itération rapide sur un BP jetable),
        appeler .wire() puis compile_bp(bp) séparément."""
        def _do():
            r = self.wire(); compile_bp(self._bp); return r
        return safe_modify_plugin(_do)

    def reset(self):
        self._nodes = []; self._conns = []; self._ctr = 0; return self

# ══════════════════════════════════════════════════════
# RUN_STEPS — exécution autonome de séquences
# ══════════════════════════════════════════════════════

def run_steps(steps, stop_on_error=True, auto_save=True, verbose=True):
    """Exécute une séquence d'étapes Python de façon autonome."""
    import io, sys, traceback
    shared = {}
    exec("from ue5_utils import *\nimport unreal", shared)
    results = {}; all_ok = True; total = len(steps)
    print(f"[run_steps] Démarrage — {total} étape(s)")
    for i, (name, code) in enumerate(steps.items(), 1):
        print(f"\n[{i}/{total}] {name}...")
        buf = io.StringIO()
        old_out, old_err = sys.stdout, sys.stderr
        sys.stdout = sys.stderr = buf
        try:
            exec(code, shared)
            sys.stdout, sys.stderr = old_out, old_err
            output = buf.getvalue().strip()
            results[name] = {"status": "OK", "output": output}
            if verbose:
                print(f"  ✅ OK — {output[:200] if output else '(pas de sortie)'}")
        except Exception:
            sys.stdout, sys.stderr = old_out, old_err
            tb = traceback.format_exc()
            results[name] = {"status": "ERROR", "output": tb}
            all_ok = False
            print(f"  ❌ ERREUR :\n{tb}")
            if stop_on_error:
                print(f"[run_steps] Arrêt à '{name}'.")
                break
    status_str = "SUCCÈS" if all_ok else "ÉCHEC"
    print(f"\n[run_steps] {status_str} — {sum(1 for r in results.values() if r['status']=='OK')}/{total} étapes OK")
    if auto_save and all_ok: save(); print("[run_steps] Level sauvegardé.")
    return results

# ══════════════════════════════════════════════════════
# MÉMOIRE PERSISTANTE — GAME_MEMORY.md
# ══════════════════════════════════════════════════════

def _memory_path():
    """Pointe vers GAME_MEMORY.md du projet RPG_Test."""
    return os.path.join(unreal.Paths.project_dir(), "GAME_MEMORY.md")

def update_memory(section, content):
    """Met à jour une section de GAME_MEMORY.md."""
    import re, datetime
    path = _memory_path()
    if not os.path.exists(path):
        with open(path, "w", encoding="utf-8") as f:
            f.write(f"# RPG_Test — Mémoire\nDernière mise à jour : {datetime.date.today()}\n\n")
    with open(path, encoding="utf-8") as f: text = f.read()
    text = re.sub(r"Dernière mise à jour : .*",
                  f"Dernière mise à jour : {datetime.date.today().isoformat()}", text)
    pattern = rf"({re.escape(section)}\n)(.*?)(?=\n## |\Z)"
    replacement = f"{section}\n{content}\n"
    if re.search(pattern, text, re.DOTALL):
        text = re.sub(pattern, replacement, text, flags=re.DOTALL)
    else:
        text = text.rstrip() + f"\n\n{section}\n{content}\n"
    with open(path, "w", encoding="utf-8") as f: f.write(text)
    print(f"[update_memory] Section '{section}' mise à jour.")

def append_todo(item):
    """Ajoute un item TODO dans GAME_MEMORY.md."""
    path = _memory_path()
    if not os.path.exists(path): update_memory("## TODO", "")
    with open(path, encoding="utf-8") as f: text = f.read()
    new_line = f"- [ ] {item}\n"
    if "## TODO" in text:
        text = text.replace("## TODO\n", f"## TODO\n{new_line}")
    else:
        text += f"\n## TODO\n{new_line}"
    with open(path, "w", encoding="utf-8") as f: f.write(text)
    print(f"[append_todo] Ajouté : {item}")

def mark_done(item_substring):
    """Marque un TODO comme terminé dans GAME_MEMORY.md."""
    import re
    path = _memory_path()
    with open(path, encoding="utf-8") as f: text = f.read()
    text = re.sub(rf"- \[ \] (.*{re.escape(item_substring)}.*)", r"- [x] \1", text)
    with open(path, "w", encoding="utf-8") as f: f.write(text)
    print(f"[mark_done] Marqué terminé : '{item_substring}'")

def memory_snapshot():
    """Affiche un résumé de GAME_MEMORY.md (TODOs en cours uniquement)."""
    path = _memory_path()
    if not os.path.exists(path): print("GAME_MEMORY.md non trouvé."); return
    with open(path, encoding="utf-8") as f: lines = f.readlines()
    todos = [l.strip() for l in lines if l.strip().startswith("- [ ]")]
    done  = [l.strip() for l in lines if l.strip().startswith("- [x]")]
    print(f"=== RPG_TEST MEMORY ===")
    print(f"TODO ({len(todos)}) :"); [print(f"  {t}") for t in todos]
    print(f"DONE ({len(done)}) :"); [print(f"  {d}") for d in done]


# ══════════════════════════════════════════════════════
# SCREENSHOT FIABLE (portage HorrorGame CLAUDE.md, 2026-07-14)
# ══════════════════════════════════════════════════════
# take_screenshot() (AutomationLibrary.take_high_res_screenshot) est mis en file
# d'attente pour un frame futur : dans un contexte d'exécution agent (bridge MCP),
# ce frame n'arrive parfois jamais avant la lecture du fichier → image noire, ou
# image périmée d'un appel précédent. Confirmé sur RPG_Test/UE5.8 le 2026-07-14
# (take_screenshot() a retourné un fichier vieux de ~14 jours, STALE détecté par
# le check déjà présent dans take_screenshot() ci-dessus). Utiliser
# capture_reference_screenshot() à la place partout où c'est possible — voir
# HorrorGame CLAUDE.md section "Screenshot fiable" pour le détail du bug.
CAPTURE_RT_PATH = "/Game/Temp"
CAPTURE_RT_NAME = "TempCaptureRT"
CAPTURE_ACTOR_LABEL = "UTIL_SceneCapture"

@traced("capture_reference_screenshot")
def capture_reference_screenshot(x, y, z, pitch=0, yaw=0, roll=0, name="capture", resolution=(1280, 720)):
    """Screenshot fiable via SceneCaptureComponent2D — À UTILISER À LA PLACE de
    take_screenshot(). Capture synchrone (capture_scene()) depuis une position/
    rotation fixes, contrairement à take_screenshot() qui capture la vue (parfois
    périmée) du viewport éditeur.

    Piège : le TextureRenderTarget2D créé par défaut est en RGBA16F (HDR) —
    l'exporter en ".png" écrit en réalité un fichier .exr illisible malgré
    l'extension. Forcer RTF_RGBA8 AVANT la capture (render_target_format est en
    lecture seule via assignation directe `.render_target_format = ...`).

    Retourne le chemin absolu du fichier PNG généré dans
    Saved/Screenshots/WindowsEditor/.
    """
    at = unreal.AssetToolsHelpers.get_asset_tools()

    rt = unreal.load_asset(f"{CAPTURE_RT_PATH}/{CAPTURE_RT_NAME}")
    if rt is None:
        unreal.EditorAssetLibrary.make_directory(CAPTURE_RT_PATH)
        rt = at.create_asset(CAPTURE_RT_NAME, CAPTURE_RT_PATH, unreal.TextureRenderTarget2D,
                             unreal.TextureRenderTargetFactoryNew())
    rt.set_editor_property("size_x", resolution[0])
    rt.set_editor_property("size_y", resolution[1])
    rt.set_editor_property("render_target_format", unreal.TextureRenderTargetFormat.RTF_RGBA8)

    cap = actor_by_label(CAPTURE_ACTOR_LABEL)
    if cap is None:
        cap = aeas().spawn_actor_from_class(unreal.SceneCapture2D.static_class(),
                                            unreal.Vector(x, y, z),
                                            unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll))
        cap.set_actor_label(CAPTURE_ACTOR_LABEL)
    else:
        cap.set_actor_location(unreal.Vector(x, y, z), False, False)
        cap.set_actor_rotation(unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll), False)

    comp = cap.capture_component2d
    comp.texture_target = rt
    comp.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
    comp.capture_scene()

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    out_dir = os.path.join(unreal.Paths.project_saved_dir(), "Screenshots", "WindowsEditor")
    filename = f"{name}.png"
    unreal.RenderingLibrary.export_render_target(world, rt, out_dir, filename)
    path = os.path.join(out_dir, filename)
    unreal.log(f"[capture_reference_screenshot] {path}")
    return path


def get_live_viewport_transform():
    """Retourne la position/rotation EXACTE de la caméra du viewport éditeur
    actuel — utile pour faire correspondre une capture_reference_screenshot() à
    ce que Thomas voit réellement à l'écran au même instant.

    Retourne (x, y, z, pitch, yaw, roll).
    """
    loc, rot = unreal.EditorLevelLibrary.get_level_viewport_camera_info()
    return (loc.x, loc.y, loc.z, rot.pitch, rot.yaw, rot.roll)


def build_and_critique(build_fn, capture_pos=None, name="critique", *args, **kwargs):
    """Exécute une fonction de construction puis prend IMMÉDIATEMENT un
    screenshot pour que Claude Cowork (qui a la vision) puisse le lire et
    critiquer avant de déclarer quoi que ce soit "terminé".

    NE REMPLACE PAS le jugement visuel — garantit juste que l'étape "capture"
    n'est jamais oubliée après un build. Le vrai travail (lire le screenshot
    avec Read, juger, corriger si besoin, recapturer) reste à faire par
    l'agent qui a appelé cette fonction.

    build_fn    : fonction à exécuter (ex: lambda: gen_room(...))
    capture_pos : (x,y,z,pitch,yaw,roll) — si None, utilise get_live_viewport_transform()
    name        : préfixe du fichier screenshot

    Retourne {"build_result": ..., "screenshot": path}.
    """
    result = build_fn(*args, **kwargs)
    if capture_pos is None:
        capture_pos = get_live_viewport_transform()
    x, y, z, pitch, yaw, roll = capture_pos
    path = capture_reference_screenshot(x, y, z, pitch=pitch, yaw=yaw, roll=roll, name=name)
    print(f"[build_and_critique] Build terminé. Screenshot: {path}")
    print("[build_and_critique] ÉTAPE OBLIGATOIRE SUIVANTE : lire ce fichier avec Read "
          "et juger visuellement avant de dire que c'est fait.")
    return {"build_result": result, "screenshot": path}


def capture_pipeline_selftest(cleanup=True):
    """Test canari : confirme que capture_reference_screenshot() reflète bien
    un changement réel de scène plutôt que de le supposer — porté depuis
    HorrorGame suite au bug de staleness (voir CLAUDE.md HorrorGame, session
    "quinquies" 2026-07-03 : 7 captures consécutives étaient revenues
    pixel-identiques malgré des changements radicaux de scène).

    Construit une scène isolée à (733000, 733000) — loin de toute géométrie de
    jeu réelle —, capture une baseline, ajoute un cube + lumière colorée
    identifiable À CÔTÉ (pas devant, pour ne pas confondre occlusion légitime
    et pipeline figé), recapture, compare (taille + MD5, pas de PIL/numpy
    disponible dans le Python embarqué UE5).

    cleanup : si True (défaut), détruit les acteurs de test après capture.

    Retourne {"baseline": path, "after": path, "size_baseline": int,
              "size_after": int, "md5_baseline": str, "md5_after": str,
              "identical": bool, "verdict": str}.
    """
    import hashlib

    TX, TY = 733000.0, 733000.0
    a_sub = aeas()

    def _cleanup_canary():
        for a in a_sub.get_all_level_actors():
            if a.get_actor_label() and a.get_actor_label().startswith("CANARY_"):
                a_sub.destroy_actor(a)

    _cleanup_canary()  # résidu d'un appel précédent interrompu

    cube_mesh = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")

    floor = a_sub.spawn_actor_from_class(unreal.StaticMeshActor.static_class(), unreal.Vector(TX, TY, 0))
    floor.set_actor_label("CANARY_Floor")
    floor.set_actor_scale3d(unreal.Vector(20, 20, 1))
    floor.get_component_by_class(unreal.StaticMeshComponent.static_class()).set_static_mesh(cube_mesh)

    light = a_sub.spawn_actor_from_class(unreal.PointLight.static_class(), unreal.Vector(TX, TY, 300))
    light.set_actor_label("CANARY_Light")
    lc = light.point_light_component
    lc.set_editor_property("intensity", 5000.0)
    lc.set_editor_property("attenuation_radius", 1500.0)
    lc.set_mobility(unreal.ComponentMobility.MOVABLE)

    path_baseline = capture_reference_screenshot(TX, TY - 400, 150, pitch=0, yaw=90, name="canary_selftest_baseline")

    cube = a_sub.spawn_actor_from_class(unreal.StaticMeshActor.static_class(), unreal.Vector(TX + 150, TY - 250, 100))
    cube.set_actor_label("CANARY_SideCube")
    cube.set_actor_scale3d(unreal.Vector(1.2, 1.2, 1.2))
    cube.get_component_by_class(unreal.StaticMeshComponent.static_class()).set_static_mesh(cube_mesh)

    side_light = a_sub.spawn_actor_from_class(unreal.PointLight.static_class(), unreal.Vector(TX + 150, TY - 250, 200))
    side_light.set_actor_label("CANARY_SideLight")
    slc = side_light.point_light_component
    slc.set_editor_property("light_color", unreal.Color(0, 0, 255, 255))
    slc.set_editor_property("intensity", 15000.0)
    slc.set_editor_property("attenuation_radius", 400.0)
    slc.set_mobility(unreal.ComponentMobility.MOVABLE)

    path_after = capture_reference_screenshot(TX, TY - 400, 150, pitch=0, yaw=90, name="canary_selftest_after")

    if cleanup:
        _cleanup_canary()

    def _md5(p):
        with open(p, "rb") as f:
            return hashlib.md5(f.read()).hexdigest()

    size_b, size_a = os.path.getsize(path_baseline), os.path.getsize(path_after)
    md5_b, md5_a = _md5(path_baseline), _md5(path_after)
    identical = (md5_b == md5_a)

    if identical:
        verdict = ("SUSPECT: fichiers identiques (meme MD5) malgre un changement de scene "
                   "radical -> pipeline de capture probablement fige/perime. NE PAS FAIRE "
                   "CONFIANCE a capture_reference_screenshot() avant investigation.")
    else:
        verdict = ("OK: fichiers differents (MD5 differents, tailles {} vs {} octets) -> le "
                   "pipeline reflete bien le changement de scene.".format(size_b, size_a))

    unreal.log("[capture_pipeline_selftest] " + verdict)

    return {
        "baseline": path_baseline, "after": path_after,
        "size_baseline": size_b, "size_after": size_a,
        "md5_baseline": md5_b, "md5_after": md5_a,
        "identical": identical, "verdict": verdict,
    }


print(f"[ue5_utils RPG_Test] loaded — bpes: {'✅' if _BPES_AVAILABLE else '❌'} | bgh: {'✅' if _BGH_AVAILABLE else '❌ (BlueprintGraphHelper absent)'} | Niagara: {'✅' if _NIAGARA_EDITING_AVAILABLE else '❌'} | RoomGen: {'✅' if _ROOM_AVAILABLE else '❌'}")
print("from ue5_utils import *")


def create_sequence_puzzle(cx, cy, n_switches=3, z=100, spacing=200, package_path=None, suffix=""):
    """
    Construit un puzzle generique "N interrupteurs dans le bon ordre -> porte s'ouvre".
    Reutilisable (Axe D, ROADMAP_JEU_COMPLET.md - bibliotheque de systemes gameplay).
    Retourne un dict {door_path, switch_paths, door_actor, switch_actors}.

    Bugs contournes ici (session 2026-07-15, voir GAME_MEMORY.md pour le detail complet) :
    - double-evaluation d'un noeud pur partage (Add_IntInt lu 2x) -> lecture fraiche vg2
      APRES le var_set, jamais de reutilisation d'un noeud pur deja consomme par une mutation.
    - target/WorldContextObject d'un appel de fonction laisse a null silencieusement quand le
      node est cree par reflection (AddFunctionCallNode/BatchWireGraph) plutot que par l'UI ->
      necessite un node K2Node_Self explicite (type "self" de BatchWireGraph, C++ ajoute le
      2026-07-15) cable manuellement sur le pin self/target.
    - bgh.connect_pins() ne propage PAS la resolution de type des pins wildcard (ForEachLoop
      "Array Element", Cast "Object") -> bgh.reconstruct_node() obligatoire sur le node macro
      PUIS le node cast juste apres avoir connecte le pin Array, sinon compile_blueprint()
      renvoie True alors que le vrai compilateur Blueprint a rejete le graphe (voir aussi le
      bug documente ailleurs : compile_blueprint() ne reflete pas fiablement un echec reel).
    """
    import unreal, json
    bpes = unreal.get_editor_subsystem(unreal.BlueprintEditingSubsystem)
    bgh = unreal.BlueprintGraphHelper
    aeas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    pkg = package_path or "/Game/RPGTest/Blueprints/Puzzle"
    door_name = f"BP_PuzzleDoor{suffix}"
    door_open_z = z + 300

    create_blueprint(unreal.StaticMeshActor, pkg, door_name)
    door_bp = load_bp(f"{pkg}/{door_name}")
    add_var(door_bp, "ExpectedIndex", "int", "1")

    # Mobilite Movable OBLIGATOIRE sur le CDO : bug reel trouve le 2026-07-15 en testant via
    # playtest_agent.py en PIE reel (jamais vu via call_method() sur l'instance EDITEUR, qui ne
    # respecte pas cette contrainte runtime) -- un StaticMeshActor a une mobilite Static par
    # defaut, et K2_SetActorLocation echoue SILENCIEUSEMENT dessus a l'execution reelle (log :
    # "Mobility of X has to be Movable if you'd like to move"). Sans ce fix, ExpectedIndex
    # atteint bien n_switches+1 (la logique de sequence est correcte) mais la porte ne bouge
    # jamais visuellement en jeu.
    cdo = unreal.get_default_object(door_bp.generated_class())
    cdo.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)

    MATHLIB = "/Script/Engine.KismetMathLibrary"
    nodes, conns = [], []
    for n in range(1, n_switches + 1):
        yoff = (n - 1) * 700
        nodes += [
            {"id": f"self{n}", "type": "self", "x": -400, "y": -200 + yoff},
            {"id": f"ev{n}", "type": "custom_event", "name": f"OnSwitch{n}Pressed", "x": 0, "y": yoff},
            {"id": f"vg1_{n}", "type": "var_get", "var": "ExpectedIndex", "x": 200, "y": 200 + yoff},
            {"id": f"eq1_{n}", "type": "function", "fn": "EqualEqual_IntInt", "cls": MATHLIB, "x": 400, "y": 200 + yoff,
             "defaults": {"B": str(n)}},
            {"id": f"br1_{n}", "type": "branch", "x": 300, "y": yoff},
            {"id": f"add1_{n}", "type": "function", "fn": "Add_IntInt", "cls": MATHLIB, "x": 400, "y": 350 + yoff,
             "defaults": {"B": "1"}},
            {"id": f"vs1_{n}", "type": "var_set", "var": "ExpectedIndex", "x": 600, "y": yoff},
            {"id": f"vg2_{n}", "type": "var_get", "var": "ExpectedIndex", "x": 800, "y": 300 + yoff},
            {"id": f"eq2_{n}", "type": "function", "fn": "EqualEqual_IntInt", "cls": MATHLIB, "x": 900, "y": 200 + yoff,
             "defaults": {"B": str(n_switches + 1)}},
            {"id": f"br2_{n}", "type": "branch", "x": 900, "y": yoff},
            {"id": f"setloc{n}", "type": "function", "fn": "K2_SetActorLocation", "cls": "/Script/Engine.Actor",
             "x": 1100, "y": yoff,
             "defaults": {"NewLocation": f"(X={cx},Y={cy},Z={door_open_z})", "bSweep": "false", "bTeleport": "true"}},
            {"id": f"vsreset{n}", "type": "var_set", "var": "ExpectedIndex", "x": 600, "y": -300 + yoff,
             "defaults": {"ExpectedIndex": "1"}},
        ]
        conns += [
            {"from": f"ev{n}", "fp": "then", "to": f"br1_{n}", "tp": "execute"},
            {"from": f"br1_{n}", "fp": "then", "to": f"vs1_{n}", "tp": "execute"},
            {"from": f"br1_{n}", "fp": "else", "to": f"vsreset{n}", "tp": "execute"},
            {"from": f"vs1_{n}", "fp": "then", "to": f"br2_{n}", "tp": "execute"},
            {"from": f"br2_{n}", "fp": "then", "to": f"setloc{n}", "tp": "execute"},
            {"from": f"vg1_{n}", "fp": "ExpectedIndex", "to": f"eq1_{n}", "tp": "A"},
            {"from": f"eq1_{n}", "fp": "ReturnValue", "to": f"br1_{n}", "tp": "Condition"},
            {"from": f"vg1_{n}", "fp": "ExpectedIndex", "to": f"add1_{n}", "tp": "A"},
            {"from": f"add1_{n}", "fp": "ReturnValue", "to": f"vs1_{n}", "tp": "ExpectedIndex"},
            {"from": f"vg2_{n}", "fp": "ExpectedIndex", "to": f"eq2_{n}", "tp": "A"},
            {"from": f"eq2_{n}", "fp": "ReturnValue", "to": f"br2_{n}", "tp": "Condition"},
        ]

    r = bpes.batch_wire_graph(door_bp, "EventGraph", json.dumps({"nodes": nodes, "connections": conns}))
    if not r.startswith("OK"):
        raise Exception(f"create_sequence_puzzle: echec cablage porte: {r}")

    gnodes = bgh.list_graph_nodes(door_bp, "EventGraph")
    def find_node(graph_nodes, bp, cls_frag, x, y):
        for gn in graph_nodes:
            parts = gn.split("|")
            if cls_frag in parts[1] and int(parts[2]) == x and int(parts[3]) == y:
                return bgh.find_node_by_name(bp, "EventGraph", parts[0])
        return None

    for n in range(1, n_switches + 1):
        yoff = (n - 1) * 700
        self_obj = find_node(gnodes, door_bp, "K2Node_Self", -400, -200 + yoff)
        setloc_obj = find_node(gnodes, door_bp, "K2Node_CallFunction", 1100, yoff)
        if self_obj and setloc_obj:
            bgh.connect_pins(self_obj, "self", setloc_obj, "self")

    if not bgh.compile_blueprint(door_bp):
        raise Exception("create_sequence_puzzle: compile porte a echoue")
    bgh.save_blueprint(door_bp)

    door_cls_path = f"{pkg}/{door_name}.{door_name}_C"
    door_asset_path = f"{pkg}/{door_name}.{door_name}"
    switch_paths = []
    for n in range(1, n_switches + 1):
        sw_name = f"BP_Switch_{n}{suffix}"
        create_blueprint(unreal.StaticMeshActor, pkg, sw_name)
        sw_bp = load_bp(f"{pkg}/{sw_name}")

        sw_nodes = {
            "nodes": [
                {"id": "self0", "type": "self", "x": -400, "y": -100},
                {"id": "ev", "type": "custom_event", "name": "OnPlayerInteract", "x": 0, "y": 0},
                {"id": "gaawt", "type": "function", "fn": "GetAllActorsWithTag", "cls": "/Script/Engine.GameplayStatics",
                 "x": 300, "y": 0, "defaults": {"Tag": "PuzzleDoor"}},
                {"id": "fe", "type": "macro", "name": "ForEachLoop", "x": 700, "y": 0},
                {"id": "cast", "type": "cast", "cls": door_asset_path, "x": 1000, "y": 0},
                {"id": "callp", "type": "function", "fn": f"OnSwitch{n}Pressed", "cls": door_cls_path, "x": 1300, "y": 0},
            ],
            "connections": [],
        }
        r = bpes.batch_wire_graph(sw_bp, "EventGraph", json.dumps(sw_nodes))
        if not r.startswith("OK"):
            raise Exception(f"create_sequence_puzzle: echec creation switch {n}: {r}")

        snodes = bgh.list_graph_nodes(sw_bp, "EventGraph")
        s_self = find_node(snodes, sw_bp, "K2Node_Self", -400, -100)
        s_ev = find_node(snodes, sw_bp, "K2Node_CustomEvent", 0, 0)
        s_gaawt = find_node(snodes, sw_bp, "K2Node_CallFunction", 300, 0)
        s_fe = find_node(snodes, sw_bp, "K2Node_MacroInstance", 700, 0)
        s_cast = find_node(snodes, sw_bp, "K2Node_DynamicCast", 1000, 0)
        s_callp = find_node(snodes, sw_bp, "K2Node_CallFunction", 1300, 0)

        for a, ap, b, bp_ in [
            (s_self, "self", s_gaawt, "WorldContextObject"),
            (s_ev, "then", s_gaawt, "execute"),
            (s_gaawt, "then", s_fe, "Exec"),
            (s_gaawt, "OutActors", s_fe, "Array"),
            (s_fe, "LoopBody", s_cast, "execute"),
            (s_fe, "Array Element", s_cast, "Object"),
            (s_cast, "then", s_callp, "execute"),
        ]:
            bgh.connect_pins(a, ap, b, bp_)

        cast_pins = bgh.list_node_pins(s_cast)
        as_pin = next((p.split("|")[0] for p in cast_pins if p.startswith("As")), None)
        if as_pin:
            bgh.connect_pins(s_cast, as_pin, s_callp, "self")

        bgh.reconstruct_node(s_fe)
        bgh.reconstruct_node(s_cast)

        if not bgh.compile_blueprint(sw_bp):
            raise Exception(f"create_sequence_puzzle: compile switch {n} a echoue")
        bgh.save_blueprint(sw_bp)
        switch_paths.append(f"{pkg}/{sw_name}")

    door_actor = aeas.spawn_actor_from_class(door_bp.generated_class(), unreal.Vector(cx, cy, z))
    door_actor.set_actor_label(f"{door_name}_Instance")
    door_actor.tags = [unreal.Name("PuzzleDoor")]

    switch_actors = []
    for i, sp in enumerate(switch_paths, start=1):
        sbp = load_bp(sp)
        loc = unreal.Vector(cx + i * spacing, cy - spacing, z)
        a = aeas.spawn_actor_from_class(sbp.generated_class(), loc)
        a.set_actor_label(f"BP_Switch_{i}{suffix}_Instance")
        # Tag "Interactable" : dispatch generique cote C++ (ARPGCharacter::OnInteractPressed(),
        # ajoute 2026-07-15) - sans ce tag, un vrai appui sur E en jeu ne fait rien, seul
        # call_method()/call_actor_function() direct fonctionnerait (contournant le vrai chemin
        # de jeu). Voir GAME_MEMORY.md Axe E pour le detail.
        a.tags = [unreal.Name("Interactable")]
        switch_actors.append(a)

    return {
        "door_path": f"{pkg}/{door_name}",
        "switch_paths": switch_paths,
        "door_actor": door_actor,
        "switch_actors": switch_actors,
    }


def create_win_condition(door_tag="PuzzleDoor", victory_var="bVictory",
                          message="VICTOIRE - Puzzle resolu !", duration=8.0):
    """
    Axe D, systeme "sys_win_condition" (ROADMAP_JEU_COMPLET.md / gdd_backlog.json).
    Ajoute une condition de victoire aux Blueprint(s) porte de puzzle deja generes par
    create_sequence_puzzle() et poses dans le niveau (identifies via door_tag).

    Approche : PAS un nouvel acteur qui poll en Tick (ForEachLoop/comparaisons non testees
    ici, risque de nouveaux pieges de pins wildcard). On branche directement sur les noeuds
    Branch "fin de sequence" DEJA PRESENTS dans le graphe de la porte (ceux qui ne passent
    en "then" que lorsque ExpectedIndex atteint n_switches+1 -- voir create_sequence_puzzle(),
    noeuds a x=900, y=(n-1)*700). Ce point ne s'active QUE quand la porte s'ouvre reellement,
    donc le critere d'acceptation du backlog ("se declenche uniquement apres ouverture de la
    porte") est garanti PAR CONSTRUCTION plutot que par un seuil approximatif (ex. tester
    Location.Z > un nombre choisi a la main).

    PIEGE REEL trouve le 2026-07-15 en construisant cette fonction (nouvelle confirmation du
    bug deja documente : compile_blueprint() ne reflete jamais fiablement un echec reel) :
    bgh.connect_pins() sur un pin exec de SORTIE deja connecte AJOUTE bien une deuxieme
    connexion cote donnees EdGraph (confirme via list_node_pins avant/apres sur un Blueprint
    jetable AVANT de toucher BP_PuzzleDoor_Gen -- gdd_verified, donc a risque de regression) et
    bgh.compile_blueprint() a retourne True malgre ca -- MAIS le vrai compilateur Kismet a
    rejete le graphe (visible uniquement dans Saved/Logs/RPG_Test.log, lu via open() natif
    DEPUIS ue5_execute, jamais via le bash sandbox Cowork) : "[Compiler] Exec output pin True
    cannot have more than one connection". Un pin exec de SORTIE ne supporte qu'UNE connexion
    (contrairement a un pin exec d'ENTREE, qui accepte un fan-in illimite -- confirme par le
    meme test : le pin execute de winset recoit bien 3 connexions entrantes sans erreur). Fix :
    passer par un noeud "sequence" (K2Node_ExecutionSequence, pins IN "execute", OUT "then_0"/
    "then_1") entre chaque Branch de fin de sequence et ses DEUX destinations (le
    SetActorLocation existant + la nouvelle condition de victoire) -- jamais de deuxieme
    connexion directe sur un meme pin exec de sortie deja cable.

    Modifie un Blueprint deja gdd_verified -> TOUJOURS enveloppe dans safe_modify_plugin()
    (baseline test_suite.run_all() avant/apres), conformement a CLAUDE.md.

    Retourne la liste des Blueprint(s) porte modifies (pour compile_bp() par
    verified_build_for_unit(), qui recompile de toute facon sans effet de bord).
    """
    import unreal, json

    def _do():
        bpes = unreal.get_editor_subsystem(unreal.BlueprintEditingSubsystem)
        bgh = unreal.BlueprintGraphHelper
        aeas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

        doors = [a for a in aeas.get_all_level_actors() if unreal.Name(door_tag) in list(a.tags)]
        if not doors:
            raise Exception(f"create_win_condition: aucun acteur tague '{door_tag}' trouve dans le niveau "
                             "(attendu : une instance de porte posee par create_sequence_puzzle())")

        def _find(gn_list, bp, cls_frag, x, y):
            for gn in gn_list:
                parts = gn.split("|")
                if cls_frag in parts[1] and int(parts[2]) == x and int(parts[3]) == y:
                    return bgh.find_node_by_name(bp, "EventGraph", parts[0])
            return None

        # Coordonnee FIXE (pas "max y courant + marge") : rend la fonction idempotente --
        # un deuxieme appel sur un Blueprint deja modifie retrouve le meme noeud plutot que
        # d'en empiler un nouveau a une position decalee a chaque fois.
        WIN_Y = 5000

        bp_paths = sorted(set(d.get_class().get_outermost().get_name() for d in doors))
        modified = []
        for bp_path in bp_paths:
            door_bp = unreal.load_asset(bp_path)

            gnodes_before = bgh.list_graph_nodes(door_bp, "EventGraph")

            # Idempotence : si le noeud PrintString de victoire existe deja a la coordonnee
            # fixe attendue, cette porte a deja ete traitee par un appel precedent -- ne rien
            # recreer (evite d'empiler des noeuds dupliques a chaque relance, ex. via
            # advance_backlog() rejoue apres un statut "blocked").
            if _find(gnodes_before, door_bp, "K2Node_CallFunction", 900, WIN_Y) is not None:
                modified.append(door_bp)
                continue

            # add_var leve une RuntimeError si la variable existe deja ("nom deja pris ?") --
            # peut arriver si un appel precedent a echoue APRES avoir ajoute la variable mais
            # AVANT de creer les noeuds (ex. crash) ; on tolere ce cas precis, on ne masque
            # aucune autre erreur.
            try:
                add_var(door_bp, victory_var, "bool", "false")
            except RuntimeError as e:
                if "deja pris" not in str(e) and "already exists" not in str(e).lower():
                    raise

            # Localise tous les noeuds Branch de fin de sequence deja presents, ET le
            # SetActorLocation que chacun declenche deja (un par interrupteur, voir
            # create_sequence_puzzle -- couplage assume au layout genere par cette fonction,
            # x=900/1100, pas de 700 en y, pas un choix arbitraire ici).
            end_branches = []
            n = 1
            while True:
                yoff = (n - 1) * 700
                br = _find(gnodes_before, door_bp, "K2Node_IfThenElse", 900, yoff)
                if br is None:
                    break
                setloc = _find(gnodes_before, door_bp, "K2Node_CallFunction", 1100, yoff)
                if setloc is None:
                    raise Exception(f"create_win_condition: Branch de fin de sequence {n} trouve "
                                     f"mais son SetActorLocation (x=1100,y={yoff}) introuvable dans {bp_path}")
                end_branches.append((br, setloc))
                n += 1
            if not end_branches:
                raise Exception(f"create_win_condition: aucun noeud de fin de sequence trouve dans {bp_path} "
                                 "(attendu : graphe genere par create_sequence_puzzle())")

            free_y = WIN_Y
            nodes = [
                {"id": "winself", "type": "self", "x": -400, "y": free_y},
                {"id": "winset", "type": "var_set", "var": victory_var, "x": 600, "y": free_y,
                 "defaults": {victory_var: "true"}},
                {"id": "winprint", "type": "function", "fn": "PrintString",
                 "cls": "/Script/Engine.KismetSystemLibrary", "x": 900, "y": free_y,
                 "defaults": {"InString": message, "Duration": str(duration),
                              "TextColor": "(R=0.0,G=1.0,B=0.0,A=1.0)"}},
            ]
            conns = [{"from": "winset", "fp": "then", "to": "winprint", "tp": "execute"}]
            r = bpes.batch_wire_graph(door_bp, "EventGraph", json.dumps({"nodes": nodes, "connections": conns}))
            if not r.startswith("OK"):
                raise Exception(f"create_win_condition: echec creation noeuds victoire: {r}")

            gnodes_after = bgh.list_graph_nodes(door_bp, "EventGraph")
            win_self = _find(gnodes_after, door_bp, "K2Node_Self", -400, free_y)
            win_set = _find(gnodes_after, door_bp, "K2Node_VariableSet", 600, free_y)
            win_print = _find(gnodes_after, door_bp, "K2Node_CallFunction", 900, free_y)
            if not (win_self and win_set and win_print):
                raise Exception("create_win_condition: noeuds victoire introuvables apres creation")

            # WorldContextObject laisse null silencieusement sur un noeud cree par reflection
            # (meme piege deja documente pour GetAllActorsWithTag dans create_sequence_puzzle) --
            # verifier le nom exact du pin plutot que de le supposer (regle CLAUDE.md).
            pins = bgh.list_node_pins(win_print)
            wc_pin = next((p.split("|")[0] for p in pins if p.split("|")[0] == "WorldContextObject"), None)
            if wc_pin is None:
                raise Exception("create_win_condition: pin WorldContextObject introuvable sur PrintString")
            bgh.connect_pins(win_self, "self", win_print, wc_pin)
            bgh.connect_pins(win_set, "then", win_print, "execute")

            # Un noeud "sequence" par Branch de fin de sequence : execute (IN) <- br.then,
            # then_0 (OUT) -> le SetActorLocation d'origine, then_1 (OUT) -> la victoire.
            # Jamais une deuxieme connexion directe sur le pin "then" du Branch lui-meme (voir
            # piege documente au-dessus de cette fonction : un pin exec de SORTIE ne supporte
            # qu'UNE connexion, contrairement a un pin exec d'ENTREE qui accepte le fan-in --
            # win_set.execute reçoit ainsi legitimement une connexion par interrupteur).
            seq_nodes_spec = [{"id": f"winseq{i}", "type": "sequence", "x": 1000, "y": (i * 700) - 150}
                               for i in range(len(end_branches))]
            r = bpes.batch_wire_graph(door_bp, "EventGraph",
                                       json.dumps({"nodes": seq_nodes_spec, "connections": []}))
            if not r.startswith("OK"):
                raise Exception(f"create_win_condition: echec creation noeuds sequence: {r}")
            gnodes_seq = bgh.list_graph_nodes(door_bp, "EventGraph")

            for i, (br, setloc) in enumerate(end_branches):
                seq = _find(gnodes_seq, door_bp, "K2Node_ExecutionSequence", 1000, (i * 700) - 150)
                if seq is None:
                    raise Exception(f"create_win_condition: noeud sequence {i} introuvable apres creation")
                if not bgh.break_all_pin_links(br, "then"):
                    raise Exception(f"create_win_condition: echec break_all_pin_links sur Branch {i}")
                bgh.connect_pins(br, "then", seq, "execute")
                bgh.connect_pins(seq, "then_0", setloc, "execute")
                bgh.connect_pins(seq, "then_1", win_set, "execute")

            if not bgh.compile_blueprint(door_bp):
                raise Exception(f"create_win_condition: compile a echoue pour {bp_path}")
            bgh.save_blueprint(door_bp)
            modified.append(door_bp)

        return modified

    return safe_modify_plugin(_do)


# ══════════════════════════════════════════════════════
# AXE D SYSTEME 2 — VARIANTES D'ARCHETYPE ENNEMI (2026-07-22)
# ══════════════════════════════════════════════════════

# CONTEXTE IMPORTANT (voir GAME_MEMORY.md session 2026-07-22, CLAUDE.md section "Ennemis") :
# ARPGEnemy N'EST PAS passif contrairement a ce que la doc disait avant cette date. Son .cpp
# implemente deja une vraie machine a etats Idle->Patrol->Chase->Attack->Dead qui detecte
# proactivement le joueur (DetectRadius) meme sans etre frappee, le poursuit (ChaseSpeed),
# l'attaque au contact (AttackDamage/AttackCooldown), et revient patrouiller pres de son spawn
# si le joueur decroche (PatrolRadius/PatrolSpeed). Confirme en PIE reel. Consequence : les
# "variantes d'archetype" ne necessitent PAS de nouveau C++/Behavior Tree (la description de
# ROADMAP_JEU_COMPLET.md, "extension BT_AI/BBD_AI avec cle Blackboard EnemyArchetype", est un
# nom herite de HorrorGame qui ne correspond a rien dans ce projet) -- juste des Blueprints
# enfants de BP_RPGEnemy avec un preset de proprietes deja UPROPERTY EditAnywhere.
# Le "patrouille" (comportement par defaut) EST DEJA BP_RPGEnemy tel quel -- pas duplique ici.

_ENEMY_ARCHETYPE_PRESETS = {
    "stationary": {
        # Ne patrouille jamais (PatrolRadius/PatrolSpeed a 0 -> PickPatrolTarget reste sur
        # place), mais detecte a plus grande distance qu'un patrouilleur classique -- un
        # "garde" qui tient un poste fixe tant que le joueur n'entre pas dans son rayon.
        "PatrolRadius": 0.0, "PatrolSpeed": 0.0, "DetectRadius": 600.0,
        "ChaseSpeed": 380.0, "AttackRadius": 150.0, "AttackDamage": 10.0, "AttackCooldown": 2.0,
    },
    "ambush": {
        # Rayon de detection reduit (ne reagit que si le joueur est vraiment proche) mais
        # riposte plus vite et plus fort une fois declenche -- une "embuscade".
        "PatrolRadius": 0.0, "PatrolSpeed": 0.0, "DetectRadius": 220.0,
        "ChaseSpeed": 520.0, "AttackRadius": 150.0, "AttackDamage": 15.0, "AttackCooldown": 1.2,
    },
}

def create_enemy_archetype(archetype, package_path="/Game/RPGTest/Blueprints/Enemy", suffix=""):
    """
    Axe D systeme 2 (ROADMAP_JEU_COMPLET.md / gdd_backlog.json, unite sys_enemy_archetypes).
    Cree un Blueprint enfant de la classe C++ ARPGEnemy (parent = BP_RPGEnemy) avec un preset
    de proprietes (voir _ENEMY_ARCHETYPE_PRESETS) -- reutilise entierement la FSM existante,
    aucun nouveau noeud/graphe cree (pas de bgh/BatchWireGraph requis pour ce systeme).

    archetype : "stationary" | "ambush" (pas "patrol" -- c'est BP_RPGEnemy par defaut).
    Idempotent : si l'asset existe deja, le recharge et re-applique juste le preset (utile si
    un rebuild anterieur a change les defauts C++ de ARPGEnemy).

    Retourne le Blueprint (deja compile + sauvegarde).
    """
    if archetype not in _ENEMY_ARCHETYPE_PRESETS:
        raise ValueError(f"create_enemy_archetype: archetype invalide: {archetype} "
                          f"(attendu: {list(_ENEMY_ARCHETYPE_PRESETS.keys())})")
    preset = _ENEMY_ARCHETYPE_PRESETS[archetype]
    asset_name = f"BP_Enemy_{archetype.capitalize()}{suffix}"

    def _do():
        bp = create_blueprint("/Script/RPG_Test.RPGEnemy", package_path, asset_name)
        if bp is None:
            raise Exception(f"create_enemy_archetype: creation Blueprint echouee pour {asset_name}")
        cdo = unreal.get_default_object(bp.generated_class())
        for k, v in preset.items():
            cdo.set_editor_property(k, v)
        if not compile_bp(bp):
            raise Exception(f"create_enemy_archetype: compile_bp() a echoue pour {asset_name}")
        return bp

    return safe_modify_plugin(_do)


def build_sys_enemy_archetypes():
    """build_fn pour l'unite backlog 'sys_enemy_archetypes' (Axe D systeme 2,
    gdd_backlog.json). Appelee par advance_backlog()/verified_build_for_unit() : cree (ou
    recharge si deja presents, idempotent) les 2 Blueprints d'archetype et retourne la liste
    pour compile_bp(). Les instances dans le niveau (BP_Enemy_Stationary_C/BP_Enemy_Ambush_C a
    (1400,0)/(-1400,0)) sont deja placees et testees en PIE reel le 2026-07-22 (voir
    GAME_MEMORY.md) -- cette fonction ne fait que garantir que les 2 assets existent/compilent
    pour la boucle QC generique, pas un nouveau placement a chaque appel."""
    return [create_enemy_archetype("stationary"), create_enemy_archetype("ambush")]


def spawn_enemy_archetype(archetype, x, y, z_hint=100, grid=None, label=None, suffix=""):
    """Spawn une instance d'un archetype deja cree par create_enemy_archetype(), place sans
    overlap via safe_place() (meme garantie que safe_spawn_enemy())."""
    bp_path = f"/Game/RPGTest/Blueprints/Enemy/BP_Enemy_{archetype.capitalize()}{suffix}"
    lbl = label or f"Enemy_{archetype.capitalize()}"
    actor = safe_place(bp_path, x, y, z_hint=z_hint, actor_radius=60.0, grid=grid, label=lbl)
    if actor:
        tag_actor(actor, "Enemy")
    return actor


# ══════════════════════════════════════════════════════
# INVENTAIRE (Axe D systeme 6, ROADMAP_JEU_COMPLET.md)
# ══════════════════════════════════════════════════════
#
# Prerequis cote BP_RPGCharacter (ajoutes le 2026-07-22, PUR BLUEPRINT, aucun rebuild C++) :
#   - Variables Slot1..Slot4 (string, "")
#   - Custom Event "AddInventoryItem(ItemName: string)" : remplit le 1er slot vide (silencieux
#     si les 4 sont deja pleins -- v1 simple, pas de UI "inventaire plein").
#   - Fonction "GetInventoryText() -> string" : "Slot1 | Slot2 | Slot3 | Slot4" (slots vides
#     laissent juste un segment vide entre les "|" -- cosmetique mineur accepte, meme tolerance
#     que le total code en dur de WBP_Objectives).
# Les DEUX ont ete crees via l'editeur (computer-use) parce que bgh/bpes ne savent PAS creer de
# nouvelle FONCTION Blueprint (seulement des Custom Event sans valeur de retour) -- limitation
# deja documentee pour BP_PuzzleDoor_Gen::GetVictoryState (session objectifs multiples). Le
# CORPS de GetInventoryText est cable via bgh normalement (contexte "soi-meme", pas de
# limitation ici) : chaine de Concat_StrStr (KismetStringLibrary).
#
# BUG REEL trouve en construisant AddInventoryItem (2026-07-22) : appeler batch_wire_graph()
# UNE SECONDE FOIS avec les MEMES "id" logiques ("ev", "br1", etc.) sur le MEME graphe NE
# RETROUVE PAS les noeuds crees par le premier appel -- ca en cree une DEUXIEME copie complete
# aux memes coordonnees x/y (silencieusement, sans erreur). Les "id" du DSL BatchWireGraph ne
# sont que des alias LOCAUX a un seul appel, jamais des identifiants persistants. Si un premier
# appel echoue en cours de route (ex: "Error: Source not found: ev" parce qu'un id reference un
# noeud d'un appel precedent), les noeuds deja crees AVANT l'echec restent orphelins dans le
# graphe -- ne jamais supposer qu'un appel qui a echoue n'a rien laisse. Fix / regle a suivre :
# construire un event/graphe complexe en UN SEUL appel batch_wire_graph (le node custom_event
# lui-meme inclus), et si un id externe doit etre reference (ex: un noeud cree par une session
# ue5_execute PRECEDENTE), le retrouver via list_graph_nodes()+find_node_by_name() (comme le fait
# deja create_sequence_puzzle() pour le noeud "self"), jamais via son "id" logique du DSL.

def create_inventory_pickup(item_name, cx, cy, z=100, package_path=None, mesh_path=None):
    """
    Axe D systeme 6 (inventaire). Cree un Blueprint de pickup generique taggue "Interactable"
    (dispatch generique existant cote C++, ARPGCharacter::OnInteractPressed() -- aucune
    modification C++ requise, meme mecanisme que les interrupteurs de sys_puzzle_1) : au vrai
    appui sur E a portee, appelle AddInventoryItem(item_name) sur le joueur puis se detruit.

    item_name : nom affiche/stocke dans un slot (ex. "Potion", "Cle").
    Un nom de Blueprint distinct par item_name (BP_Pickup_<ItemName>) -- pas de variable
    d'instance requise (evite la limitation bgh "peut creer un get de variable EXTERNE" en ne
    l'utilisant tout simplement pas ici : le nom est un defaut litteral sur le pin d'appel).

    Retourne {"bp_path", "actor"}.
    """
    import unreal, json
    bgh = unreal.BlueprintGraphHelper
    aeas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    pkg = package_path or "/Game/RPGTest/Blueprints/Pickups"
    safe_name = "".join(c for c in item_name if c.isalnum())
    bp_name = f"BP_Pickup_{safe_name}"
    char_cls_path = "/Game/RPGTest/Blueprints/Character/BP_RPGCharacter.BP_RPGCharacter_C"

    def _do():
        create_blueprint(unreal.StaticMeshActor, pkg, bp_name)
        bp = load_bp(f"{pkg}/{bp_name}")

        # BUG REEL trouve le 2026-07-22 en testant en PIE : sans mesh assigne, le
        # StaticMeshComponent n'a AUCUNE geometrie de collision -- SphereOverlapActors()
        # (dans ARPGCharacter::OnInteractPressed()) ne trouve alors JAMAIS l'acteur, meme a
        # distance 0 du joueur. Echec silencieux cote C++ (aucune erreur, juste "rien trouve")
        # -- seul un test PIE REEL via la vraie touche E l'a revele (un call_method() direct
        # sur OnPlayerInteract fonctionnait tres bien, masquant totalement le probleme). Fix :
        # toujours assigner un mesh, /Engine/BasicShapes/Sphere par defaut (toujours present
        # sur tout projet UE5) si mesh_path non fourni -- jamais laisser le composant sans
        # geometrie sur un Blueprint destine a etre "Interactable".
        cdo = unreal.get_default_object(bp.generated_class())
        mesh = unreal.load_asset(mesh_path or "/Engine/BasicShapes/Sphere.Sphere")
        if mesh:
            cdo.static_mesh_component.set_static_mesh(mesh)
            cdo.static_mesh_component.set_relative_scale3d(unreal.Vector(0.3, 0.3, 0.3))

        nodes = [
            {"id": "self0", "type": "self", "x": -400, "y": 0},
            {"id": "ev", "type": "custom_event", "name": "OnPlayerInteract", "x": 0, "y": 0},
            {"id": "getp", "type": "function", "fn": "GetPlayerCharacter",
             "cls": "/Script/Engine.GameplayStatics", "x": 300, "y": 200,
             "defaults": {"PlayerIndex": "0"}},
            {"id": "cast", "type": "cast", "cls": char_cls_path, "x": 600, "y": 0},
            {"id": "additem", "type": "function", "fn": "AddInventoryItem", "cls": char_cls_path,
             "x": 900, "y": 0, "defaults": {"ItemName": item_name}},
            {"id": "destroy", "type": "function", "fn": "K2_DestroyActor",
             "cls": "/Script/Engine.Actor", "x": 1200, "y": 0},
        ]
        conns = [
            {"from": "self0", "fp": "self", "to": "getp", "tp": "WorldContextObject"},
            {"from": "ev", "fp": "then", "to": "cast", "tp": "execute"},
            {"from": "getp", "fp": "ReturnValue", "to": "cast", "tp": "Object"},
            {"from": "cast", "fp": "then", "to": "additem", "tp": "execute"},
            {"from": "additem", "fp": "then", "to": "destroy", "tp": "execute"},
        ]
        r = bpes().batch_wire_graph(bp, "EventGraph", json.dumps({"nodes": nodes, "connections": conns}))
        if not r.startswith("OK"):
            raise Exception(f"create_inventory_pickup: echec cablage {bp_name}: {r}")

        gnodes = bgh.list_graph_nodes(bp, "EventGraph")
        def find_node(x, y, cls_frag):
            for gn in gnodes:
                parts = gn.split("|")
                if cls_frag in parts[1] and int(parts[2]) == x and int(parts[3]) == y:
                    return bgh.find_node_by_name(bp, "EventGraph", parts[0])
            return None

        self_obj = find_node(-400, 0, "K2Node_Self")
        cast_obj = find_node(600, 0, "K2Node_DynamicCast")
        additem_obj = find_node(900, 0, "K2Node_CallFunction")
        destroy_obj = find_node(1200, 0, "K2Node_CallFunction")

        # Pin "self"/target d'un appel cree par reflexion : jamais auto-resolu, meme piege que
        # documente pour create_sequence_puzzle() (setloc/self) -- cablage explicite requis.
        bgh.connect_pins(self_obj, "self", destroy_obj, "self")
        cast_pins = bgh.list_node_pins(cast_obj)
        as_pin = next((p.split("|")[0] for p in cast_pins if p.startswith("As")), None)
        if as_pin:
            bgh.connect_pins(cast_obj, as_pin, additem_obj, "self")
        bgh.reconstruct_node(cast_obj)

        if not bgh.compile_blueprint(bp):
            raise Exception(f"create_inventory_pickup: compile {bp_name} a echoue")
        bgh.save_blueprint(bp)

        actor = aeas.spawn_actor_from_class(bp.generated_class(), unreal.Vector(cx, cy, z))
        actor.set_actor_label(f"{bp_name}_Instance")
        actor.tags = [unreal.Name("Interactable")]
        return {"bp_path": f"{pkg}/{bp_name}", "actor": actor}

    return safe_modify_plugin(_do)
