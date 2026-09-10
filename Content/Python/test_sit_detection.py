"""
AC-1 -- On ne s'assoit que la ou c'est valide.

CE QUE CE TEST COUVRE, ET CE QU'IL NE COUVRE PAS
------------------------------------------------
Il verifie la SONDE de detection : pour une geometrie donnee, la surface est-elle acceptee ou
refusee, et refusee pour le BON MOTIF. Il ne couvre pas encore le declencheur reel d'AC-1
(l'appui sur la touche d'assise), parce que l'action d'assise n'existe pas encore. Ce volet
s'ajoutera quand le gameplay sera la -- il est explicitement absent, pas oublie.

POURQUOI LES CAS NEGATIFS COMPTENT AUTANT QUE LES POSITIFS
----------------------------------------------------------
Un systeme qui accepterait TOUT passerait sans effort tous les cas positifs. Un systeme qui
refuserait tout passerait tous les cas negatifs si on ne lisait qu'un booleen. C'est pourquoi
chaque refus est verifie sur son MOTIF (`reject_reason`) et pas seulement sur `b_valid` --
meme famille de piege que la cle Blackboard absente qui renvoie zero en silence.

PAS DE PIE, MAIS UN EFFET DE BORD A CONNAITRE
----------------------------------------------
La sonde ne fait que des traces : elle n'a besoin ni d'une partie lancee ni d'un pawn joueur.
Le test fabrique donc sa propre geometrie dans le monde de l'editeur, mesure, puis DETRUIT tout
ce qu'il a cree. Verifie : aucun acteur de test ne subsiste apres le passage.

EN REVANCHE, spawner puis detruire marque le niveau comme MODIFIE. Si un rebuild suit (rebuild.py
appelle save_dirty_packages avant de quitter), le niveau est reecrit sur disque. Le contenu est
identique -- les cubes de test ont bien ete retires -- mais le .umap change d'octets, et
`git status` le signale.

Ce n'est pas grave, c'est juste a savoir : apres un passage de ce test, la bonne reaction est
    git checkout -- Content/Levels/DefaultLevel.umap
pour repartir du fichier d'origine plutot que de committer une reserialisation sans contenu.
C'est git qui a revele cet effet de bord, pas moi.

UTILISATION :
    import test_sit_detection; test_sit_detection.run()

Resultat dans <Projet>/Saved/Tests/sit_detection_result.json
LIRE LE JSON, PAS LA VALEUR DE RETOUR (CLAUDE.md piege 2).

CONVENTION DE NOMMAGE : UE retire le prefixe `b` des booleens exposes a Python. `bValid` cote
C++ se lit `valid` cote Python, `bLegsDangle` se lit `legs_dangle`. Demander `b_valid` leve une
exception plutot que de renvoyer une valeur par defaut -- ce qui est une bonne nouvelle : l'erreur
est bruyante au lieu d'etre silencieuse.
"""

import json
import os
import unreal

CUBE_MESH = "/Engine/BasicShapes/Cube.Cube"   # 100 x 100 x 100 cm, origine au centre
PREFIXE_FIXTURE = "ZZZ_TestSit_"
STATION_SPACING = 900.0                        # ecart entre deux cas, en cm
PROBE_BACK_OFFSET = 120.0                      # recul du personnage-sonde devant le cube

# Chaque cas : nom, decalage X du cube par rapport a la station, dimensions, inclinaison,
# hauteur d'un eventuel plafond, et ce qu'on attend.
#
# `attendu_motif` a None signifie "doit etre accepte".
# nom, cube (offset_x, sx, sy, h), pitch, plafond, hauteur_sonde_relative, attendu_ok, motif
#
# `hauteur_sonde_relative` sert aux cas ou le personnage ne se tient pas sur le plateau plat :
# sur une rampe, il faut le poser au-dessus d'elle pour que sa trace de sol trouve la pente.
CAS = [
    ("sol nu",             None,                       0.0,  None,  88.0, True,  None),
    ("cube 20 cm",         (0.0, 100.0, 100.0, 20.0),  0.0,  None,  88.0, True,  None),
    ("cube 45 cm",         (0.0, 100.0, 100.0, 45.0),  0.0,  None,  88.0, True,  None),
    ("cube 80 cm",         (0.0, 100.0, 100.0, 80.0),  0.0,  None,  88.0, True,  None),
    ("cube 120 cm",        (0.0, 100.0, 100.0, 120.0), 0.0,  None,  88.0, False, "TropHaut"),
    ("cube etroit",        (0.0, 100.0, 30.0, 45.0),   0.0,  None,  88.0, False, "PasAssezLarge"),
    ("cube peu profond",   (-32.0, 30.0, 100.0, 45.0), 0.0,  None,  88.0, False, "PasAssezProfond"),
    ("cube incline 60",    (0.0, 100.0, 100.0, 45.0),  60.0, None,  88.0, False, "TropIncline"),
    ("plafond bas",        (0.0, 100.0, 100.0, 45.0),  0.0,  105.0, 88.0, False, "PlafondBas"),
    ("rebord sous surplomb", (0.0, 100.0, 100.0, 45.0), 0.0, 210.0, 88.0, True,  None),

    # Signale par le joueur : un mur fin laissait la sonde mesurer le sol DERRIERE lui et
    # annoncer une assise possible de l'autre cote de l'obstacle.
    ("mur fin devant le sol", (-70.0, 15.0, 200.0, 200.0), 0.0, None, 88.0, False, "Obstrue"),

    # Signale par le joueur : un sol en pente etait refuse pour "PasAssezProfond", alors qu'on
    # doit pouvoir s'asseoir partout ou le personnage tient debout. La rampe sert de sol au
    # personnage ET de siege : c'est le meme plan, la hauteur relative doit donc valoir zero.
    ("sol en pente 25 deg", (0.0, 900.0, 400.0, 40.0), 25.0, None, 200.0, True, None),

    # Signale par le joueur : au bord d'un rebord, la sonde trouvait le SOL et son controle de
    # profondeur tombait sur le rebord -- plus haut -- concluant "PasAssezProfond". C'etait faux :
    # un rebord derriere le siege est un DOSSIER. Le bloc commence apres le point de siege
    # (88 cm) et couvre le point de controle de profondeur (123 cm).
    ("dossier derriere le siege", (60.0, 140.0, 200.0, 60.0), 0.0, None, 88.0, True, None),

    # Trouve par l'agent de playtest en promenant le personnage : en descendant d'une plateforme,
    # le sol d'arrivee -- plus bas que les pieds -- etait invisible pour la sonde. Ici le
    # personnage se tient sur une marche de 40 cm et regarde le sol en contrebas.
    ("marche descendante", (-160.0, 200.0, 300.0, 40.0), 0.0, None, 128.0, True, None),
]


def _result_path():
    folder = os.path.join(
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Tests")
    if not os.path.isdir(folder):
        os.makedirs(folder)
    return os.path.abspath(os.path.join(folder, "sit_detection_result.json"))


class SitDetectionTest(object):
    def __init__(self):
        self.spawned = []
        self.report = {"tests": [], "erreurs": []}
        self.cube_mesh = None

    # -- fabrication de la geometrie ---------------------------------------

    def _spawn_box(self, center, size, pitch=0.0):
        """Pave de dimensions exactes en cm. Le cube moteur fait 100 cm de cote."""
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.StaticMeshActor, unreal.Vector(*center),
            unreal.Rotator(0.0, pitch, 0.0))
        comp = actor.static_mesh_component
        comp.set_static_mesh(self.cube_mesh)
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
        actor.set_actor_label("%s%d" % (PREFIXE_FIXTURE, len(self.spawned)))
        self.spawned.append(actor)
        return actor

    def _spawn_probe(self, location, yaw):
        """Personnage-sonde : sa capsule sert a deriver le domaine, comme en jeu."""
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.Character, unreal.Vector(*location), unreal.Rotator(0.0, 0.0, yaw))
        actor.set_actor_label(PREFIXE_FIXTURE + "Probe")
        self.spawned.append(actor)
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

    def _cleanup(self):
        subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        for actor in self.spawned:
            try:
                subsystem.destroy_actor(actor)
            except Exception as exc:
                self.report["erreurs"].append("nettoyage: %s" % exc)
        self.spawned = []

    # -- deroulement --------------------------------------------------------

    def run(self):
        self._purger_restes()
        self.cube_mesh = unreal.EditorAssetLibrary.load_asset(CUBE_MESH)
        if self.cube_mesh is None:
            self.report["erreurs"].append("cube moteur introuvable: %s" % CUBE_MESH)
            self._finish()
            return _result_path()

        try:
            # Un sol propre et isole, tres au-dessus du niveau existant : le test ne doit
            # dependre d'aucune geometrie deja presente dans la map.
            base_z = 5000.0

            # Le plateau doit couvrir TOUTES les stations plus le recul du personnage-sonde.
            # Premiere version trop courte : les quatre dernieres stations tombaient dans le vide
            # et remontaient "PasDeSol" -- un bug du decor de test, pas de la sonde. On calcule la
            # longueur au lieu de l'ecrire a la main, pour qu'ajouter un cas ne recasse pas le sol.
            derniere_station = 800.0 + (len(CAS) - 1) * STATION_SPACING
            longueur = derniere_station + PROBE_BACK_OFFSET * 2 + 1200.0
            centre_x = longueur / 2.0 - PROBE_BACK_OFFSET - 400.0
            self._spawn_box((centre_x, 0.0, base_z - 50.0), (longueur, 2000.0, 100.0))
            self.report["plateau"] = {"longueur_cm": longueur, "centre_x": centre_x,
                                      "derniere_station_x": derniere_station}

            probe = self._spawn_probe((0.0, 0.0, base_z + 88.0), 0.0)
            domain = unreal.SitSurfaceLibrary.make_domain_for_character(probe)
            self.report["domaine_derive"] = {
                p: round(float(domain.get_editor_property(p)), 1) for p in
                ("min_seat_height", "max_seat_height", "min_depth", "min_width",
                 "max_slope_degrees", "min_headroom", "search_distance", "leg_reach")}

            for index, (nom, box, pitch, plafond, sonde_z, attendu_ok, attendu_motif) in enumerate(CAS):
                station_x = 800.0 + index * STATION_SPACING

                if box is not None:
                    off_x, sx, sy, h = box
                    self._spawn_box((station_x + off_x, 0.0, base_z + h / 2.0), (sx, sy, h), pitch)

                if plafond is not None:
                    self._spawn_box((station_x, 0.0, base_z + plafond + 10.0), (300.0, 300.0, 20.0))

                probe.set_actor_location(
                    unreal.Vector(station_x - PROBE_BACK_OFFSET, 0.0, base_z + sonde_z),
                    False, False)
                probe.set_actor_rotation(unreal.Rotator(0.0, 0.0, 0.0), False)

                res = unreal.SitSurfaceLibrary.find_sit_surface_for_character(probe)
                valide = bool(res.get_editor_property("valid"))
                motif = str(res.get_editor_property("reject_reason"))
                hauteur = round(float(res.get_editor_property("seat_height")), 1)
                pendantes = bool(res.get_editor_property("legs_dangle"))

                if attendu_ok:
                    passe = valide
                    detail = "accepte, siege a %s cm, jambes pendantes=%s" % (hauteur, pendantes)
                    if not valide:
                        detail = "REFUSE (%s) alors qu'on attendait une acceptation" % motif
                else:
                    passe = (not valide) and motif == attendu_motif
                    detail = "refuse pour '%s' (attendu '%s')" % (motif, attendu_motif)
                    if valide:
                        detail = "ACCEPTE a %s cm alors qu'on attendait un refus '%s'" % (
                            hauteur, attendu_motif)

                self.report["tests"].append({
                    "cas": nom, "passe": bool(passe), "detail": detail,
                    "valide": valide, "motif": motif, "hauteur_cm": hauteur,
                })
        except Exception:
            import traceback
            self.report["erreurs"].append(traceback.format_exc())
        finally:
            self._cleanup()

        self._finish()
        return _result_path()

    def _finish(self):
        self.report["tout_vert"] = (bool(self.report["tests"])
                                    and all(t["passe"] for t in self.report["tests"])
                                    and not self.report["erreurs"])
        self.report["verdict"] = "PASS" if self.report["tout_vert"] else "FAIL"
        with open(_result_path(), "w") as fh:
            json.dump(self.report, fh, indent=2, default=str)
        unreal.log("[test_sit_detection] %s -- %s" % (self.report["verdict"], _result_path()))


def run():
    """Lance le test. Synchrone : le resultat est ecrit avant le retour."""
    return SitDetectionTest().run()
