"""
Lancement d'un rebuild C++ depuis l'editeur, en survivant a sa fermeture.

POURQUOI PAS subprocess.Popen
-----------------------------
Constate le 2026-08-27 : lancer l'orchestrateur avec subprocess.Popen depuis le Python de
l'editeur NE MARCHE PAS, meme avec DETACHED_PROCESS. L'editeur Unreal tourne dans un Windows
*job object* qui tue toute sa descendance a sa fermeture ; DETACHED_PROCESS ne concerne que la
console, pas l'appartenance au job. Symptome : l'editeur se ferme, aucun fichier de statut n'est
ecrit, le build n'a jamais demarre, et l'agent perd son seul acces a Windows.

LA PARADE, verifiee
-------------------
Passer par le Planificateur de taches : schtasks enregistre la tache, et c'est le SERVICE Windows
qui demarre le processus. Celui-ci n'appartient donc pas au job de l'editeur et lui survit.

UTILISATION :
    import rebuild; rebuild.run()

Suivre ensuite Saved/Tests/rebuild_status.json (et rebuild_alive.txt comme preuve de vie).
"""

import json
import os
import subprocess
import unreal

TASK_NAME = "GAS_RebuildAndRelaunch"


def _project_dir():
    p = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return p.replace("/", "\\").rstrip("\\")


def run(quit_editor=True):
    proj = _project_dir()
    ps1 = os.path.join(proj, "Tools", "rebuild_and_relaunch.ps1")
    tests = os.path.join(proj, "Saved", "Tests")
    if not os.path.isdir(tests):
        os.makedirs(tests)

    info = {"ps1": ps1, "ps1_exists": os.path.isfile(ps1)}

    # Nettoyer les traces du run precedent : sinon on relit un vieux verdict sans s'en
    # apercevoir -- la version "fichier" des screenshots perimes.
    for stale in ("rebuild_status.json", "rebuild_alive.txt", "build_log.txt"):
        path = os.path.join(tests, stale)
        if os.path.isfile(path):
            try:
                os.remove(path)
            except Exception as exc:
                info.setdefault("nettoyage", []).append("%s: %s" % (stale, exc))

    command = 'powershell -ExecutionPolicy Bypass -WindowStyle Hidden -File "%s"' % ps1

    try:
        subprocess.call(["schtasks", "/delete", "/tn", TASK_NAME, "/f"],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except Exception:
        pass

    try:
        create = subprocess.run(["schtasks", "/create", "/tn", TASK_NAME, "/tr", command,
                                 "/sc", "once", "/st", "00:00", "/f"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        info["create_rc"] = create.returncode
        if create.returncode == 0:
            runres = subprocess.run(["schtasks", "/run", "/tn", TASK_NAME],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            info["run_rc"] = runres.returncode
    except Exception as exc:
        info["exception"] = str(exc)

    with open(os.path.join(tests, "rebuild_launch.json"), "w") as fh:
        json.dump(info, fh, indent=2, default=str)
    unreal.log("[rebuild] " + json.dumps(info, default=str))

    if info.get("create_rc") == 0 and info.get("run_rc") == 0 and quit_editor:
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        unreal.SystemLibrary.quit_editor()
    return info
