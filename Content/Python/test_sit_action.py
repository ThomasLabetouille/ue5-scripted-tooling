"""
AC-1 par le VRAI CHEMIN DE JEU : la touche d'assise.

CE QUI EST PROUVE ICI
---------------------
La touche est INJECTEE dans Enhanced Input, pas simulee par un appel a TrySit(). L'evenement
traverse le contexte de mapping, les triggers et le binding exactement comme un appui clavier.
Une assise obtenue autrement ne prouverait pas qu'un joueur peut s'asseoir.

DEUX PHASES, DEUX SESSIONS DE JEU
---------------------------------
Phase 1 : un cube valide devant le point d'apparition -> la touche doit faire asseoir, puis
          relever.
Phase 2 : un mur trop haut au meme endroit -> la touche ne doit RIEN faire, et le motif de refus
          doit etre 'TropHaut'.

Deux sessions plutot qu'une seule avec demi-tour : faire pivoter un pawn Mover par script est
incertain, et un test dont on ne sait pas s'il regarde dans la bonne direction ne prouve rien.
Le decor change entre les deux sessions, le joueur ne bouge jamais.

LE CAS NEGATIF EST LE PLUS IMPORTANT. Une capacite qui accepte tout passerait la phase 1 sans
rien valoir. C'est la phase 2 qui distingue "s'assoit ou c'est possible" de "s'assoit partout".

UTILISATION :
    import test_sit_action; test_sit_action.run()

Resultat dans <Projet>/Saved/Tests/sit_action_result.json
"""

import json
import os
import unreal

WARMUP = 12.0           # possession du pawn + greffe par le subsystem (reessai 0.5 s)
INJECT_FRAMES = 4       # maintien de l'injection : Started ne se declenche qu'une fois
SETTLE_AFTER = 1.0      # laisser l'etat se propager avant de mesurer

HAUTEUR_VALIDE = 60.0   # dans le domaine (0-88) et au-dessus de la hauteur de marche
HAUTEUR_MUR = 150.0     # hors domaine -> TropHaut
DISTANCE_DEVANT = 130.0 # centre du cube. Contrainte double : la face avant doit etre au-dela
                        # du rayon de capsule (~40 cm) pour que le pawn n'y naisse pas dedans, et
                        # le cube doit couvrir le point de sonde (88 cm) ET le controle de
                        # profondeur (123 cm). Avec un cube de 140, il s'etend de 60 a 200 cm.
PREFIXE_FIXTURE = "ZZZ_TestSitAction_"
TOLERANCE_HAUTEUR = 10.0 # le sol calibre depuis la capsule differe de quelques cm du sol reel


def _result_path():
    folder = os.path.join(
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Tests")
    if not os.path.isdir(folder):
        os.makedirs(folder)
    return os.path.abspath(os.path.join(folder, "sit_action_result.json"))


def _game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


class SitActionTest(object):
    def __init__(self):
        self.state = "prepare_1"
        self.state_entered = 0.0
        self.elapsed = 0.0
        self.frames_injected = 0
        self.handle = None
        self.fixtures = []
        self.cube_mesh = None
        self.ground_z = 0.0
        self.start_loc = None
        self.start_fwd = None
        self.comp = None
        self.report = {"tests": [], "erreurs": []}

    # -- decor, dans le monde de l'editeur ----------------------------------

    def _editor_world(self):
        return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

    def _spawn_box(self, center, size):
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.StaticMeshActor, unreal.Vector(*center), unreal.Rotator(0, 0, 0))
        comp = actor.static_mesh_component
        comp.set_static_mesh(self.cube_mesh)
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
        actor.set_actor_label("%s%d" % (PREFIXE_FIXTURE, len(self.fixtures)))
        self.fixtures.append(actor)
        return actor

    def _purger_restes(self):
        """Detruit tout decor de test survivant d'un run precedent.

        INDISPENSABLE, et appris a la dure. Un run interrompu -- exception, redemarrage de
        l'editeur, PIE avortee -- laisse ses cubes dans le niveau. Le rebuild suivant appelle
        save_dirty_packages et les grave dans le .umap. Les runs d'apres mesurent alors contre
        une geometrie fantome : un mur de 150 cm oublie par une phase 2 a fait echouer une phase
        1 avec le motif 'TropHaut', pour un cube de 60 cm parfaitement valide.
        Un test doit etre independant de l'echec du precedent.
        """
        sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        purges = []
        for a in sub.get_all_level_actors():
            try:
                if a.get_actor_label().startswith(PREFIXE_FIXTURE):
                    purges.append(a.get_actor_label())
                    sub.destroy_actor(a)
            except Exception:
                continue
        if purges:
            self.report["restes_purges"] = purges

    def _clear_fixtures(self):
        sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        for a in self.fixtures:
            try:
                sub.destroy_actor(a)
            except Exception as exc:
                self.report["erreurs"].append("nettoyage: %s" % exc)
        self.fixtures = []

    def _spawn_devant_joueur(self, hauteur):
        """Un pave devant le point d'apparition, pose sur le sol reel."""
        centre = (self.start_loc.x + self.start_fwd.x * DISTANCE_DEVANT,
                  self.start_loc.y + self.start_fwd.y * DISTANCE_DEVANT,
                  self.ground_z + hauteur / 2.0)
        self._spawn_box(centre, (140.0, 140.0, hauteur))

    def _calibrer_depuis_pawn(self):
        """Mesure la position et l'orientation REELLES du pawn, en jeu.

        Premiere approche abandonnee : tracer vers le sol depuis le PlayerStart dans le monde de
        l'editeur. La trace ne renvoyait rien d'exploitable -- HitResult n'expose aucun champ via
        get_editor_property, et le resultat de line_trace_single en monde editeur s'est revele
        inutilisable. Plutot que de m'acharner sur cette API, je prends la mesure a la source :
        une courte partie de calibration, ou le pawn lui-meme dit ou il est.

        C'est aussi plus juste : ce qui compte est l'endroit ou le JOUEUR apparait, pas ou le
        PlayerStart est pose.
        """
        world = _game_world()
        pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
        if pawn is None:
            return False

        loc = pawn.get_actor_location()
        self.start_loc = loc
        self.start_fwd = pawn.get_actor_forward_vector()

        caps = pawn.get_component_by_class(unreal.CapsuleComponent)
        demi = float(caps.get_scaled_capsule_half_height()) if caps else 88.0
        self.ground_z = loc.z - demi

        self.report["calibration"] = {
            "pawn": pawn.get_class().get_name(),
            "pawn_z": round(float(loc.z), 1),
            "demi_capsule": round(demi, 1),
            "sol_z": round(float(self.ground_z), 1),
            "avant": [round(float(self.start_fwd.x), 2), round(float(self.start_fwd.y), 2)],
        }
        return True

    # -- PIE ----------------------------------------------------------------

    def _les(self):
        return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

    def _start_pie(self):
        if self._les().is_in_play_in_editor():
            self._les().editor_request_end_play()
        self._les().editor_request_begin_play()

    def _capture_comp(self):
        world = _game_world()
        pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
        self.comp = pawn.get_component_by_class(unreal.SitAbilityComponent) if pawn else None
        return self.comp is not None

    def _inject(self):
        """Injecte la touche par Enhanced Input -- le meme chemin qu'un appui reel."""
        if self.comp is None:
            return
        action = self.comp.get_editor_property("sit_action")
        if action is None:
            return
        unreal.ScriptedInputLibrary.inject_input_vector(
            _game_world(), action, unreal.Vector(1.0, 0.0, 0.0), 0)

    def _lire(self):
        return {
            "assis": bool(self.comp.get_editor_property("is_seated")),
            "motif": str(self.comp.get_editor_property("last_reject_reason")),
            "hauteur": round(float(self.comp.get_editor_property("seat_height")), 1),
            "tentatives": int(self.comp.get_editor_property("attempt_count")),
        }

    # -- machine a etats ----------------------------------------------------

    def _goto(self, state):
        self.state = state
        self.state_entered = self.elapsed
        self.frames_injected = 0

    def _depuis(self):
        return self.elapsed - self.state_entered

    def _add(self, nom, passe, detail):
        self.report["tests"].append({"test": nom, "passe": bool(passe), "detail": detail})

    def start(self):
        global _ACTIVE
        if _ACTIVE is not None and getattr(_ACTIVE, "handle", None) is not None:
            self.report["erreurs"].append(
                "un autre test est deja en cours -- lancement refuse pour ne pas ecraser "
                "son verdict")
            self._finish()
            return

        self._purger_restes()
        self.cube_mesh = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube.Cube")
        if self.cube_mesh is None:
            self.report["erreurs"].append("cube moteur introuvable")
            self._finish()
            return

        # Partie de calibration : aucun decor, on veut juste savoir ou le joueur apparait.
        self._start_pie()
        self._goto("calibration")
        self.handle = unreal.register_slate_post_tick_callback(self._tick)
        unreal.log("[test_sit_action] demarre -- " + _result_path())

    def _tick(self, delta):
        try:
            self.elapsed += delta
            st = self.state

            if st == "calibration":
                if self._depuis() >= WARMUP:
                    if not self._calibrer_depuis_pawn():
                        self.report["erreurs"].append("pawn introuvable pendant la calibration")
                        self._finish()
                        return
                    self._les().editor_request_end_play()
                    self._goto("apres_calibration")
                return

            if st == "apres_calibration":
                if self._depuis() < 6.0 or self._les().is_in_play_in_editor():
                    return
                self._spawn_devant_joueur(HAUTEUR_VALIDE)
                self._start_pie()
                self._goto("warmup_1")
                return

            if st == "warmup_1":
                if self._depuis() >= WARMUP:
                    if not self._capture_comp():
                        self._add("Capacite greffee (phase 1)", False, "composant introuvable")
                        self._finish()
                        return
                    self._add("Capacite greffee (phase 1)", True, "composant present")
                    self._add("Input branche", bool(self.comp.get_editor_property("input_bound")),
                              "branche" if self.comp.get_editor_property("input_bound") else "NON")
                    self._goto("inject_sit_1")
                return

            if st in ("inject_sit_1", "inject_stand_1", "inject_sit_2"):
                self._inject()
                self.frames_injected += 1
                if self.frames_injected >= INJECT_FRAMES:
                    self._goto({"inject_sit_1": "settle_sit_1",
                                "inject_stand_1": "settle_stand_1",
                                "inject_sit_2": "settle_sit_2"}[st])
                return

            if st == "settle_sit_1":
                if self._depuis() >= SETTLE_AFTER:
                    e = self._lire()
                    ok = e["assis"] and abs(e["hauteur"] - HAUTEUR_VALIDE) <= TOLERANCE_HAUTEUR
                    self._add("La touche fait asseoir sur le cube", ok,
                              "assis=%s hauteur=%s (attendu %s +/-%s) motif='%s' tentatives=%s"
                              % (e["assis"], e["hauteur"], HAUTEUR_VALIDE, TOLERANCE_HAUTEUR,
                                 e["motif"], e["tentatives"]))
                    self._goto("inject_stand_1")
                return

            if st == "settle_stand_1":
                if self._depuis() >= SETTLE_AFTER:
                    e = self._lire()
                    self._add("La meme touche fait se relever", not e["assis"],
                              "assis=%s apres seconde pression" % e["assis"])
                    self._goto("gap")
                return

            if st == "gap":
                # Fin de la phase 1 : arreter la PIE, changer le decor, repartir.
                if self._depuis() < 0.2:
                    self._les().editor_request_end_play()
                    return
                if self._depuis() < 6.0 or self._les().is_in_play_in_editor():
                    return
                self._clear_fixtures()
                self._spawn_devant_joueur(HAUTEUR_MUR)
                self._start_pie()
                self._goto("warmup_2")
                return

            if st == "warmup_2":
                if self._depuis() >= WARMUP:
                    if not self._capture_comp():
                        self._add("Capacite greffee (phase 2)", False, "composant introuvable")
                        self._finish()
                        return
                    self._goto("inject_sit_2")
                return

            if st == "settle_sit_2":
                if self._depuis() >= SETTLE_AFTER:
                    e = self._lire()
                    ok = (not e["assis"]) and e["motif"] == "TropHaut" and e["tentatives"] > 0
                    self._add("Devant un mur trop haut, la touche ne fait rien", ok,
                              "assis=%s motif='%s' tentatives=%s (attendu assis=False, "
                              "motif='TropHaut', tentatives>0)"
                              % (e["assis"], e["motif"], e["tentatives"]))
                    self._finish()
                return

        except Exception:
            import traceback
            self.report["erreurs"].append(traceback.format_exc())
            self._finish()

    def _signer(self):
        """Le rapport doit dire quel code l'a produit, et le dire LUI-MEME.

        Deux incidents l'ont impose. D'abord un verdict PASS rendu par du code periME : le cache
        d'import de Python servait une version anterieure au correctif. Ensuite deux instances du
        test lancees a quelques minutes d'intervalle, la premiere encore en cours, toutes deux
        ecrivant dans le meme fichier -- le verdict lu appartenait a l'autre run.

        Une empreinte posee par l'appelant ne protege d'aucun des deux : elle decrit ce que
        l'appelant CROIT avoir lance. Calculee ici, au moment d'ecrire, elle decrit ce qui a
        reellement tourne.
        """
        import hashlib
        try:
            with open(__file__, "r") as fh:
                src = fh.read()
            self.report["empreinte_source"] = hashlib.sha1(src.encode("utf-8")).hexdigest()[:10]
            self.report["taille_source"] = len(src)
        except Exception as exc:
            self.report["empreinte_source"] = "illisible: %s" % exc

    def _finish(self):
        self._signer()
        if self.handle is not None:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        try:
            if self._les().is_in_play_in_editor():
                self._les().editor_request_end_play()
        except Exception as exc:
            self.report["erreurs"].append("arret PIE: %s" % exc)
        self._clear_fixtures()

        self.report["tout_vert"] = (bool(self.report["tests"])
                                    and all(t["passe"] for t in self.report["tests"])
                                    and not self.report["erreurs"])
        self.report["verdict"] = "PASS" if self.report["tout_vert"] else "FAIL"
        with open(_result_path(), "w") as fh:
            json.dump(self.report, fh, indent=2, default=str)
        unreal.log("[test_sit_action] %s" % self.report["verdict"])


_ACTIVE = None


def run():
    """Lance le test. Rend la main immediatement : lire le JSON ensuite."""
    global _ACTIVE
    precedent = _ACTIVE
    nouveau = SitActionTest()
    if precedent is not None and getattr(precedent, "handle", None) is not None:
        # Ne pas ecraser _ACTIVE : l'instance en cours doit rester joignable et vivante.
        nouveau.report["erreurs"].append("test deja en cours -- lancement refuse")
        nouveau._finish()
        return _result_path()
    _ACTIVE = nouveau
    _ACTIVE.start()
    return _result_path()
