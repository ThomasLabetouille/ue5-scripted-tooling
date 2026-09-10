"""
Test de non-regression du comportement des PNJ (GameAnimationSample / NPCLevel).

Arbre couvert depuis le 2026-08-27 : BT_NPC_Naturalist (branche Fuite).
L'ancien BT_NPC_Patrol reste sur disque comme filet de repli, mais n'est plus reference
par BP_AIC_NPCPatrol -- et son graphe est abime, voir CLAUDE.md piege 9.

POURQUOI CE FICHIER EXISTE
--------------------------
Session du 2026-08-15 : trois regressions distinctes sont passees inapercues parce que la
verification portait sur l'etat des DONNEES (le decorator est-il present dans l'asset ?) et non
sur le COMPORTEMENT REEL en jeu. Les trois auraient ete attrapees en 60 secondes par ce test :
  - reference BehaviorTreeAsset nullifiee   -> TEST 1 echoue (aucun BT ne tourne)
  - sens Sight jamais souscrit              -> TEST 4 echoue (aucune cible acquise)
  - mode animation verrouille apres une     -> TEST 6 echoue (PNJ fige dans la boucle assise)
    interruption de la sequence Sit

UTILISATION (une seule commande, depuis Python dans l'editeur) :
    import test_npc_behavior; test_npc_behavior.run()

Le test pilote la PIE tout seul (demarrage, echantillonnage, arret) et ecrit son verdict dans
    <Projet>/Saved/Tests/npc_behavior_result.json
Il n'a besoin d'AUCUNE action manuelle et n'utilise pas Computer Use.

LIRE LE RESULTAT DANS LE FICHIER JSON, PAS LA VALEUR DE RETOUR : le test est asynchrone (il
s'etale sur ~55 s de temps de jeu via un callback de tick), donc run() rend la main immediatement.
C'est aussi la parade au fait que le pont ue5_execute renvoie toujours "OK" meme en cas d'echec.
"""

import json
import os
import unreal

# ---------------------------------------------------------------------------
# Parametres
# ---------------------------------------------------------------------------

LEVEL_PATH = "/Game/Levels/NPCLevel"
NPC_CLASS_NAME = "SandboxCharacter_CMC_C"

WARMUP_SECONDS = 6.0      # laisse OnPossess + BeginPlay s'executer
OBSERVE_SECONDS = 46.0    # duree d'observation (patrouille + assise + detection)
SAMPLE_INTERVAL = 2.0

# En dessous de ce nombre d'echantillons, le run n'a rien observe d'exploitable et son verdict
# est declare non concluant plutot que negatif.
MIN_SAMPLES = 10

# CHANGEMENT DE DESIGN, 2026-08-27.
#
# Ce test verifiait auparavant que les PNJ SE RAPPROCHENT du joueur (branche Chase de
# BT_NPC_Patrol). Le jeu ne veut plus de ce comportement : les sujets sont observes, pas
# poursuivants, et la branche Chase a ete remplacee par une branche Fuite dans
# BT_NPC_Naturalist. Conserver l'ancienne assertion aurait fait defendre au gate de
# non-regression un comportement que le jeu a explicitement abandonne -- le pire etat
# possible pour un test, puisqu'il bloquerait la bonne evolution en se presentant comme
# une protection.
#
# Le TEST 6 verifie donc desormais l'invariant inverse : un sujet ne reste jamais durablement
# a portee de fuite du joueur.

# Seuil de fuite du controller (APatrolPerceptionAIController::FleeDistance).
FLEE_DISTANCE = 600.0

# Un sujet peut passer sous le seuil (le joueur avance, ou une trajectoire de patrouille
# l'y amene) : ce qui compte est qu'il en RESSORTE. Avec un echantillonnage toutes les 2 s,
# trois echantillons consecutifs laissent ~6 s pour amorcer et executer la fuite.
MAX_ECHANTILLONS_SOUS_SEUIL = 3

# Fenetre de fin utilisee pour detecter un PNJ fige (immobile ET en animation assise).
STUCK_WINDOW_SECONDS = 14.0

SIT_MONTAGE_HINTS = ("bench_into", "bench_idle", "bench_out")


def _result_path():
    saved = unreal.Paths.project_saved_dir()
    folder = os.path.join(saved, "Tests")
    if not os.path.isdir(folder):
        os.makedirs(folder)
    return os.path.abspath(os.path.join(folder, "npc_behavior_result.json"))


def _game_world():
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    return ues.get_game_world()


def _distance_2d(a, b):
    return ((a.x - b.x) ** 2 + (a.y - b.y) ** 2) ** 0.5


class NPCBehaviorTest(object):
    def __init__(self):
        self.elapsed = 0.0
        self.next_sample = WARMUP_SECONDS
        self.handle = None
        self.samples = []          # liste de dicts {t, npcs:{label:{...}}}
        self.init_check = None
        self.report = {"tests": [], "errors": []}

    # -- infrastructure -----------------------------------------------------

    def start(self):
        les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

        current = les.get_current_level()
        current_name = str(current.get_outer().get_name()) if current else ""
        if not current_name.endswith("NPCLevel"):
            les.load_level(LEVEL_PATH)

        if les.is_in_play_in_editor():
            les.editor_request_end_play()

        les.editor_request_begin_play()
        self.handle = unreal.register_slate_post_tick_callback(self._tick)
        unreal.log("[test_npc_behavior] demarre -- resultat dans " + _result_path())

    def _finish(self):
        if self.handle is not None:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        try:
            les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
            if les.is_in_play_in_editor():
                les.editor_request_end_play()
        except Exception as exc:
            self.report["errors"].append("arret PIE: %s" % exc)

        self._evaluate()

        with open(_result_path(), "w") as handle:
            json.dump(self.report, handle, indent=2, default=str)

        verdict = "PASS" if self.report.get("all_passed") else "FAIL"
        unreal.log("[test_npc_behavior] %s -- %s" % (verdict, _result_path()))

    def _tick(self, delta_seconds):
        try:
            self.elapsed += delta_seconds

            if self.elapsed >= WARMUP_SECONDS and self.init_check is None:
                self.init_check = self._snapshot(include_init=True)

            if self.elapsed >= self.next_sample:
                self.next_sample += SAMPLE_INTERVAL
                snap = self._snapshot()
                if snap is not None:
                    self.samples.append({"t": round(self.elapsed, 1), "npcs": snap})

            if self.elapsed >= WARMUP_SECONDS + OBSERVE_SECONDS:
                self._finish()
        except Exception:
            import traceback
            self.report["errors"].append(traceback.format_exc())
            self._finish()

    # -- collecte -----------------------------------------------------------

    def _snapshot(self, include_init=False):
        world = _game_world()
        if world is None:
            return None

        player = unreal.GameplayStatics.get_player_pawn(world, 0)
        if player is None:
            return None
        player_loc = player.get_actor_location()

        out = {}
        for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
            if actor == player or actor.get_class().get_name() != NPC_CLASS_NAME:
                continue

            entry = {}
            loc = actor.get_actor_location()
            entry["loc"] = [round(loc.x, 1), round(loc.y, 1)]
            entry["dist_player"] = round(_distance_2d(loc, player_loc), 1)

            mesh = actor.get_component_by_class(unreal.SkeletalMeshComponent)
            anim = mesh.get_anim_instance() if mesh else None
            montage = anim.get_current_active_montage() if anim else None
            entry["montage"] = montage.get_name() if montage else None

            controller = actor.get_controller()
            if controller is not None:
                bt = controller.get_component_by_class(unreal.BehaviorTreeComponent)
                bb = controller.get_component_by_class(unreal.BlackboardComponent)
                entry["bt_running"] = bool(bt.is_running()) if bt else False
                if bb is not None:
                    entry["target"] = bb.get_value_as_object("TargetActor") is not None
                    if include_init:
                        entry["patrol1"] = bb.get_value_as_object("PatrolPoint1") is not None
                        entry["sitpoint"] = bb.get_value_as_object("SitPointActor") is not None
                else:
                    entry["target"] = False
                    if include_init:
                        entry["patrol1"] = False
                        entry["sitpoint"] = False
                entry["controller"] = controller.get_class().get_name()
            else:
                entry["bt_running"] = False
                entry["target"] = False
                entry["controller"] = None
                if include_init:
                    entry["patrol1"] = False
                    entry["sitpoint"] = False

            out[actor.get_actor_label()] = entry
        return out

    # -- evaluation ---------------------------------------------------------

    def _add(self, name, passed, detail):
        self.report["tests"].append({
            "name": name,
            "passed": bool(passed),
            "detail": detail,
        })

    def _evaluate(self):
        init = self.init_check or {}
        labels = sorted(init.keys())
        self.report["npc_count"] = len(labels)
        self.report["npcs"] = labels
        self.report["sample_count"] = len(self.samples)

        # TEST 1 -- initialisation : chaque PNJ possede un BT qui tourne et un Blackboard rempli.
        # Attrape : reference BehaviorTreeAsset nullifiee, AIController absent, tags manquants.
        bad = [k for k, v in init.items()
               if not v.get("bt_running") or not v.get("patrol1") or not v.get("sitpoint")]
        self._add("1. Initialisation (BT lance + Blackboard rempli)",
                  len(labels) > 0 and not bad,
                  "PNJ en defaut: %s" % (bad or "aucun"))

        # TEST 2 -- tous les PNJ utilisent le meme AIController (exigence "meme comportement").
        controllers = sorted({v.get("controller") for v in init.values()})
        self._add("2. Controller unique pour tous les PNJ",
                  len(controllers) == 1 and controllers[0] is not None,
                  "controllers observes: %s" % controllers)

        # GARDE D'ECHANTILLONNAGE (ajoutee le 2026-08-29 apres un faux rouge).
        #
        # Un run a rendu ECHEC sur "aucun deplacement" et "aucune detection" avec UN SEUL
        # echantillon au lieu de 24 : un a-coup de l'editeur pendant le chargement du niveau a
        # fait bondir le temps accumule, et le test s'est arrete avant d'avoir observe quoi que
        # ce soit. Les PNJ allaient parfaitement bien.
        #
        # Sans assez d'echantillons, un test ne peut ni accuser ni disculper. Il doit le dire au
        # lieu de rendre un verdict -- un faux rouge coute la confiance qu'on accorde au vrai.
        if len(self.samples) < MIN_SAMPLES:
            self.report["all_passed"] = False
            self.report["non_concluant"] = (
                "seulement %d echantillon(s) sur %d attendus : run interrompu ou editeur ralenti. "
                "Ce verdict ne dit RIEN du comportement des PNJ -- relancer."
                % (len(self.samples), MIN_SAMPLES))
            return

        # TEST 3 -- patrouille : chaque PNJ s'est deplace au moins une fois.
        moved = {}
        for label in labels:
            positions = {tuple(s["npcs"][label]["loc"]) for s in self.samples if label in s["npcs"]}
            moved[label] = len(positions) > 1
        immobile = [k for k, v in moved.items() if not v]
        self._add("3. Deplacement (patrouille effective)",
                  not immobile,
                  "PNJ jamais deplaces: %s" % (immobile or "aucun"))

        # TEST 4 -- assise : au moins une animation de banc observee sur l'ensemble des PNJ.
        # Attrape : references d'animation restees nulles (bug du 2026-08-15).
        seen_sit = set()
        for sample in self.samples:
            for label, value in sample["npcs"].items():
                montage = value.get("montage") or ""
                if any(hint in montage for hint in SIT_MONTAGE_HINTS):
                    seen_sit.add(label)
        self._add("4. Animation d'assise jouee",
                  len(seen_sit) > 0,
                  "PNJ ayant joue une animation de banc: %s" % (sorted(seen_sit) or "aucun"))

        # TEST 5 -- detection : chaque PNJ a acquis le joueur via la perception reelle.
        # Attrape : sens Sight jamais souscrit (bug "Listener must have a valid id").
        acquired = set()
        for sample in self.samples:
            for label, value in sample["npcs"].items():
                if value.get("target"):
                    acquired.add(label)
        never = [k for k in labels if k not in acquired]
        self._add("5. Detection du joueur (perception reelle)",
                  not never,
                  "PNJ n'ayant jamais detecte le joueur: %s" % (never or "aucun"))

        # TEST 6 -- distance de fuite : aucun sujet ne stationne a portee de fuite.
        # Attrape : branche Fuite jamais declenchee (decorator mal regle, cle AwarenessLevel
        # non ecrite), ou fuite qui demarre sans aboutir (FleeLocation hors navmesh).
        collant = {}
        for label in labels:
            serie = [s["npcs"][label]["dist_player"] for s in self.samples if label in s["npcs"]]
            pire, courant = 0, 0
            for d in serie:
                courant = courant + 1 if d < FLEE_DISTANCE else 0
                pire = max(pire, courant)
            collant[label] = {"consecutifs_sous_seuil": pire,
                              "distance_min": round(min(serie), 0) if serie else None}
        fautifs = [k for k, v in collant.items()
                   if v["consecutifs_sous_seuil"] > MAX_ECHANTILLONS_SOUS_SEUIL]
        self._add("6. Distance de fuite respectee (aucun sujet ne stationne pres du joueur)",
                  not fautifs,
                  "sujets fautifs: %s | detail: %s" % (fautifs or "aucun", collant))

        # TEST 7 -- aucun PNJ fige : immobile ET bloque en animation assise sur la fin du test.
        # Attrape : mode animation laisse en AnimationSingleNode apres interruption de la Sit.
        tail = [s for s in self.samples if s["t"] >= self.elapsed - STUCK_WINDOW_SECONDS]
        stuck = []
        if len(tail) >= 2:
            for label in labels:
                positions = {tuple(s["npcs"][label]["loc"]) for s in tail if label in s["npcs"]}
                montages = [s["npcs"][label].get("montage") for s in tail if label in s["npcs"]]
                frozen = len(positions) == 1
                sitting = all(m and any(h in m for h in SIT_MONTAGE_HINTS) for m in montages)
                if frozen and sitting:
                    stuck.append(label)
        self._add("7. Aucun PNJ fige en animation assise",
                  not stuck,
                  "PNJ figes: %s" % (stuck or "aucun"))

        self.report["all_passed"] = all(t["passed"] for t in self.report["tests"]) \
            and not self.report["errors"]


_ACTIVE = None


def run():
    """Lance le test. Rend la main immediatement : lire le JSON de resultat ensuite."""
    global _ACTIVE
    _ACTIVE = NPCBehaviorTest()
    _ACTIVE.start()
    return _result_path()
