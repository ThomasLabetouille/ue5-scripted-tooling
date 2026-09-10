"""
playtest_agent.py -- Agent playtesteur autonome (GameAnimationSample)

ORIGINE
-------
Portage de l'agent ecrit par Thomas pour HorrorGame 5.7. Architecture reprise telle quelle :
le PIE tourne en temps reel ENTRE deux appels ue5_execute, donc le pilotage passe par un
callback enregistre avec register_slate_post_tick_callback, qui continue seul jusqu'a la fin du
scenario ; start_playtest() rend la main immediatement, get_playtest_status() et
get_playtest_report() se lisent dans un appel SUIVANT. Navigation par vrai chemin NavMesh avec
repli ligne droite, journal d'evenements horodates, captures a chaque evenement, detection de
blocage : tout cela vient de son agent et n'a pas ete reinvente.

CE QUI A DU CHANGER, ET POURQUOI
--------------------------------
Un seul point, mais il est structurant : **add_movement_input() ne deplace pas le pawn de ce
projet**. Mesure en PIE reelle le 2026-08-27 : 0 cm parcouru. Le pawn est Mover-based
(CharacterMoverComponent, aucun CharacterMovementComponent) et son input passe par son propre
producteur. Le pilotage se fait donc par injection Enhanced Input via UScriptedInputLibrary --
c'est-a-dire le meme chemin qu'un vrai appui clavier, triggers et modifiers compris. Mesure de
controle : 1037 cm en 4 s.

Corollaire : on ne pousse pas un vecteur de direction, on oriente le CONTROLEUR et on maintient
l'avance. La correspondance entre le cap vise et le cap obtenu est mesuree au demarrage de
chaque session (phase de calibration) plutot que supposee -- la convention de IA_Move n'est
documentee nulle part et pourrait changer.

CE QU'IL OBSERVE ICI
--------------------
Au lieu du Blackboard ennemi et des jumpscares de HorrorGame, il echantillonne en continu la
sonde d'assise : la surface devant le joueur est-elle valide, sinon pour quel motif, et a
quelle hauteur. C'est exactement le genre de bug que les tests unitaires ont laisse passer et
qu'un humain a trouve en dix minutes de jeu -- une sphere rouge au mauvais endroit ne se voit
qu'en se promenant.

USAGE :
    from playtest_agent import start_playtest, get_playtest_status, get_playtest_report
    start_playtest(scenario="tour_du_niveau", duration=60.0)
    # ... appel ue5_execute SUIVANT, apres du temps reel ...
    get_playtest_status()
    get_playtest_report()
"""

import json
import math
import os
import unreal

MOVE_ACTION = "/Game/Input/IA_Move"

_SESSION = {}


# ---------------------------------------------------------------------------
# Utilitaires
# ---------------------------------------------------------------------------

def _game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def _les():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def _report_dir():
    d = os.path.join(
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()),
        "QC", "playtest_reports")
    if not os.path.isdir(d):
        os.makedirs(d)
    return d


def _report_path(scenario):
    return os.path.abspath(os.path.join(_report_dir(), "playtest_%s.json" % scenario))


def _dist2d(a, b):
    return math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2)


def _heading(frm, to):
    return math.degrees(math.atan2(to.y - frm.y, to.x - frm.x))


def _find_nav_path(world, start_loc, end_loc):
    """Vrai chemin NavMesh, repli ligne droite. Reprise directe de l'agent HorrorGame.

    Piege conserve tel quel car il vaut pour tout projet : is_valid peut etre True avec
    path_points VIDE quand la destination est hors NavMesh -- d'ou le garde-fou sur la longueur
    plutot que sur is_valid seul.
    """
    try:
        path = unreal.NavigationSystemV1.find_path_to_location_synchronously(
            world, start_loc, end_loc)
        if not path or not getattr(path, "is_valid", False):
            return None
        points = list(path.path_points) if path.path_points else []
        if len(points) < 2:
            return None
        return [(p.x, p.y) for p in points]
    except Exception:
        return None


def _points_aleatoires(world, autour, rayon, nombre):
    """Points de passage pour la tournee, tires du NavMesh si possible.

    REPLI GEOMETRIQUE, et pourquoi il existe : DefaultLevel n'a AUCUN NavMesh -- verifie le
    2026-08-29, ni NavMeshBoundsVolume ni RecastNavMesh dans le niveau, et les trois API de
    navigation renvoient vide. Le premier run de l'agent s'est donc arrete avec zero waypoint.

    Sans NavMesh, on tire un anneau de points autour du joueur et on marche en ligne droite. Ca
    reste utile ici, parce que ce qu'on observe est la sonde d'assise le long du parcours, pas la
    navigation elle-meme. Mais il faut le savoir en lisant le rapport : dans ce mode, l'agent ne
    verifie PAS l'atteignabilite, et un blocage journalise peut n'etre qu'un mur sur la trajectoire
    et non un defaut du jeu. Le rapport porte le drapeau navmesh_absent pour cette raison.
    """
    pts = []
    for _ in range(nombre * 4):
        if len(pts) >= nombre:
            break
        try:
            p = unreal.NavigationSystemV1.get_random_reachable_point_in_radius(
                world, autour, rayon)
        except Exception:
            break
        if p is None:
            continue
        if all(math.sqrt((p.x - q[0]) ** 2 + (p.y - q[1]) ** 2) > 600.0 for q in pts):
            pts.append((p.x, p.y))

    if pts:
        return pts, False

    # Anneau de points autour du depart, sur deux rayons pour balayer plus de terrain.
    for i in range(nombre):
        angle = 2.0 * math.pi * i / float(nombre)
        r = rayon * (0.45 if i % 2 == 0 else 0.8)
        pts.append((autour.x + math.cos(angle) * r, autour.y + math.sin(angle) * r))
    return pts, True


def _record(kind, elapsed, **extra):
    evt = {"kind": kind, "t": round(elapsed, 2)}
    evt.update(extra)
    _SESSION["events"].append(evt)
    return evt


# ---------------------------------------------------------------------------
# Pilotage du joueur
# ---------------------------------------------------------------------------

def _input_lib():
    return getattr(unreal, "ScriptedInputLibrary", None)


def _avance_vers(cap_monde):
    """Oriente le controleur vers un cap monde et maintient l'avance."""
    lib = _input_lib()
    world = _game_world()
    if lib is None or world is None or _SESSION.get("action") is None:
        return
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    if pc is None:
        return
    yaw = cap_monde - _SESSION.get("calib_offset", 0.0)
    pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    if not _SESSION.get("input_on"):
        lib.start_continuous_input(world, _SESSION["action"], unreal.Vector(0.0, 1.0, 0.0), 0)
        _SESSION["input_on"] = True


def _stop_input():
    lib = _input_lib()
    if lib is not None and _SESSION.get("input_on") and _SESSION.get("action") is not None:
        try:
            lib.stop_continuous_input(_game_world(), _SESSION["action"], 0)
        except Exception as exc:
            _SESSION["errors"].append("stop_continuous_input: %s" % exc)
    _SESSION["input_on"] = False


# ---------------------------------------------------------------------------
# Observation : la sonde d'assise
# ---------------------------------------------------------------------------

def _lire_sonde(pawn):
    lib = getattr(unreal, "SitSurfaceLibrary", None)
    if lib is None or pawn is None:
        return None
    try:
        r = lib.find_sit_surface_for_character(pawn)
        return {
            "valide": bool(r.get_editor_property("valid")),
            "motif": str(r.get_editor_property("reject_reason")),
            "hauteur": round(float(r.get_editor_property("seat_height")), 1),
        }
    except Exception:
        return None


def _capture(world, elapsed, raison):
    try:
        unreal.SystemLibrary.execute_console_command(world, "HighResShot 1280x720")
        _SESSION["shots"].append({"t": round(elapsed, 2), "raison": raison})
    except Exception as exc:
        _SESSION["errors"].append("capture: %s" % exc)


# ---------------------------------------------------------------------------
# Session
# ---------------------------------------------------------------------------

CALIBRATION = 3.0
ARRIVE_RADIUS = 160.0
STUCK_WINDOW = 5.0
STUCK_DISTANCE = 60.0


def start_playtest(scenario="tour_du_niveau", waypoints=None, duration=60.0,
                   rayon_exploration=4000.0, nb_points=8):
    """Lance une session. Rend la main immediatement : lire le rapport ensuite."""
    global _SESSION

    if _SESSION.get("handle") is not None:
        return {"refuse": "une session est deja en cours"}

    _SESSION = {
        "scenario": scenario, "duration": float(duration), "elapsed": 0.0,
        "handle": None, "events": [], "errors": [], "shots": [],
        "wp": list(waypoints) if waypoints else None, "wp_idx": 0,
        "leg": None, "leg_idx": 0, "path_fallback": 0,
        "action": None, "input_on": False, "calib_offset": 0.0,
        "calib_start": None, "phase": "calibration",
        "positions": [], "stuck_active": False, "navmesh_absent": False,
        "sonde_precedente": None, "motifs": {}, "hauteurs_vues": [],
    }

    if _les().is_in_play_in_editor():
        _les().editor_request_end_play()
    # EditorAssetLibrary ne resout PLUS les assets pendant une session PIE
    # (mesure du 2026-09-03 : /Game/AI rapporte "absent" en PIE, present hors
    # PIE). unreal.load_asset(), lui, fonctionne dans les deux cas. C'est la
    # cause du "IA_Move introuvable" qui arretait chaque playtest en 8 s.
    _SESSION["action"] = (unreal.load_asset(MOVE_ACTION)
                          or unreal.EditorAssetLibrary.load_asset(MOVE_ACTION))
    if _SESSION["action"] is None:
        _SESSION["errors"].append("IA_Move introuvable -- pilotage impossible")
    _les().editor_request_begin_play()

    _SESSION["handle"] = unreal.register_slate_post_tick_callback(_tick)
    unreal.log("[playtest] '%s' demarre -- rapport dans %s" % (scenario, _report_path(scenario)))
    return {"demarre": scenario, "rapport": _report_path(scenario)}


def _tick(delta):
    try:
        _tick_inner(delta)
    except Exception:
        import traceback
        _SESSION["errors"].append(traceback.format_exc())
        _finish("exception")


def _tick_inner(delta):
    _SESSION["elapsed"] += delta
    t = _SESSION["elapsed"]
    world = _game_world()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
    if pawn is None:
        if t > 15.0:
            _SESSION["errors"].append("aucun pawn joueur")
            _finish("pas_de_pawn")
        return

    loc = pawn.get_actor_location()

    # --- Phase 1 : calibration du cap ---
    if _SESSION["phase"] == "calibration":
        if _SESSION["calib_start"] is None:
            _SESSION["calib_start"] = loc
            _SESSION["calib_t0"] = t
        _avance_vers(0.0)
        if t - _SESSION["calib_t0"] >= CALIBRATION:
            _stop_input()
            parcouru = _dist2d(_SESSION["calib_start"], loc)
            if parcouru < 80.0:
                _SESSION["errors"].append(
                    "calibration: %d cm parcourus -- le joueur ne repond pas a l'input" % parcouru)
                _finish("input_inoperant")
                return
            _SESSION["calib_offset"] = _heading(_SESSION["calib_start"], loc)
            _record("calibration", t, distance=round(parcouru, 1),
                    cap_obtenu=round(_SESSION["calib_offset"], 1))
            if _SESSION["wp"] is None:
                pts, sans_nav = _points_aleatoires(world, loc, 4000.0, 8)
                _SESSION["wp"] = pts
                _SESSION["navmesh_absent"] = sans_nav
                _record("waypoints_generes", t, nombre=len(pts),
                        navmesh_absent=sans_nav)
            _SESSION["phase"] = "parcours"
        return

    # --- Phase 2 : parcours ---
    if t >= _SESSION["duration"]:
        _finish("duree_atteinte")
        return

    wps = _SESSION["wp"] or []
    if _SESSION["wp_idx"] >= len(wps):
        _finish("waypoints_epuises")
        return

    cible = wps[_SESSION["wp_idx"]]

    # Chemin NavMesh vers le waypoint courant, calcule une fois par jambe.
    if _SESSION["leg"] is None:
        chemin = _find_nav_path(world, loc, unreal.Vector(cible[0], cible[1], loc.z))
        if chemin is None:
            _SESSION["path_fallback"] += 1
            chemin = [(loc.x, loc.y), cible]
        _SESSION["leg"] = chemin
        _SESSION["leg_idx"] = 1

    etape = _SESSION["leg"][min(_SESSION["leg_idx"], len(_SESSION["leg"]) - 1)]
    cible_v = unreal.Vector(etape[0], etape[1], loc.z)

    if _dist2d(loc, cible_v) <= ARRIVE_RADIUS:
        _SESSION["leg_idx"] += 1
        if _SESSION["leg_idx"] >= len(_SESSION["leg"]):
            _record("waypoint_atteint", t, index=_SESSION["wp_idx"],
                    x=round(loc.x, 1), y=round(loc.y, 1))
            _capture(world, t, "waypoint")
            _SESSION["wp_idx"] += 1
            _SESSION["leg"] = None
            _stop_input()
            return
    _avance_vers(_heading(loc, cible_v))

    # --- Observation continue de la sonde d'assise ---
    sonde = _lire_sonde(pawn)
    if sonde is not None:
        cle = "valide" if sonde["valide"] else sonde["motif"]
        _SESSION["motifs"][cle] = _SESSION["motifs"].get(cle, 0) + 1
        prec = _SESSION["sonde_precedente"]
        if prec is None or prec != cle:
            _record("assise_change", t, etat=cle, hauteur=sonde["hauteur"],
                    x=round(loc.x, 1), y=round(loc.y, 1))
            if sonde["valide"]:
                _SESSION["hauteurs_vues"].append(sonde["hauteur"])
            # Une transition est exactement ce qu'un humain remarque a l'ecran : on la capture.
            _capture(world, t, "assise:%s" % cle)
            _SESSION["sonde_precedente"] = cle

    # --- Detection de blocage (reprise de l'agent HorrorGame) ---
    _SESSION["positions"].append((t, loc.x, loc.y))
    _SESSION["positions"] = [p for p in _SESSION["positions"] if t - p[0] <= STUCK_WINDOW]
    if len(_SESSION["positions"]) > 5 and t > CALIBRATION + STUCK_WINDOW:
        xs = [p[1] for p in _SESSION["positions"]]
        ys = [p[2] for p in _SESSION["positions"]]
        etendue = math.sqrt((max(xs) - min(xs)) ** 2 + (max(ys) - min(ys)) ** 2)
        if etendue < STUCK_DISTANCE and not _SESSION["stuck_active"]:
            _record("joueur_bloque", t, x=round(loc.x, 1), y=round(loc.y, 1),
                    waypoint=_SESSION["wp_idx"])
            _capture(world, t, "bloque")
            _SESSION["stuck_active"] = True
            _SESSION["leg"] = None      # recalculer le chemin, comme dans l'agent d'origine
        elif etendue >= STUCK_DISTANCE:
            _SESSION["stuck_active"] = False


def _finish(raison):
    _stop_input()
    if _SESSION.get("handle") is not None:
        try:
            unreal.unregister_slate_post_tick_callback(_SESSION["handle"])
        except Exception:
            pass
        _SESSION["handle"] = None
    try:
        if _les().is_in_play_in_editor():
            _les().editor_request_end_play()
    except Exception as exc:
        _SESSION["errors"].append("arret PIE: %s" % exc)

    rapport = {
        "scenario": _SESSION.get("scenario"),
        "raison_fin": raison,
        "duree_s": round(_SESSION.get("elapsed", 0.0), 1),
        "waypoints_atteints": _SESSION.get("wp_idx", 0),
        "waypoints_total": len(_SESSION.get("wp") or []),
        "chemins_en_repli": _SESSION.get("path_fallback", 0),
        "navmesh_absent": bool(_SESSION.get("navmesh_absent", False)),
        "cap_calibre": round(_SESSION.get("calib_offset", 0.0), 1),
        "motifs_assise": _SESSION.get("motifs", {}),
        "hauteurs_sittables_vues": sorted(set(_SESSION.get("hauteurs_vues", []))),
        "evenements": _SESSION.get("events", []),
        "captures": _SESSION.get("shots", []),
        "erreurs": _SESSION.get("errors", []),
    }
    chemin = _report_path(_SESSION.get("scenario", "sans_nom"))
    with open(chemin, "w") as fh:
        json.dump(rapport, fh, indent=2, default=str)
    unreal.log("[playtest] termine (%s) -- %s" % (raison, chemin))


def get_playtest_status():
    return {
        "active": _SESSION.get("handle") is not None,
        "phase": _SESSION.get("phase"),
        "elapsed": round(_SESSION.get("elapsed", 0.0), 1),
        "waypoint": _SESSION.get("wp_idx"),
        "evenements": len(_SESSION.get("events", [])),
    }


def get_playtest_report():
    chemin = _report_path(_SESSION.get("scenario", "sans_nom"))
    if not os.path.isfile(chemin):
        return {"erreur": "aucun rapport", "chemin": chemin}
    with open(chemin) as fh:
        return json.load(fh)


def stop_playtest(raison="arret_manuel"):
    if _SESSION.get("handle") is None:
        return {"deja_arrete": True}
    _finish(raison)
    return {"arrete": raison}
