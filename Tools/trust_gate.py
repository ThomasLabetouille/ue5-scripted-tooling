#!/usr/bin/env python3
"""trust_gate.py — quorum de confiance multi-chemins pour les fichiers critiques
du projet (auto-défiance active de l'agent envers ses propres canaux d'I/O).

CONTEXTE (ajouté 2026-07-14, voir CLAUDE.md) : ce projet a documenté, à CINQ reprises
distinctes, le même piège : le pont Windows↔sandbox Linux de Claude Cowork peut voir un
fichier périmé ou corrompu (mount FUSE en retard, octets NUL de padding après une écriture
tronquée) SANS lever la moindre erreur — ni au moment de l'écriture, ni au moment de la
lecture. `verify_file_integrity()`/`scan_content_python_integrity()` (ue5_utils.py) et
`check_content_python_integrity.py` (version standalone bash) vérifient chacun UN SEUL
chemin d'accès et le disent explicitement dans leur propre docstring : "ne jamais déclarer
un fichier corrompu sur la seule foi de CE script si un doute existe, confirmer avec
scan_content_python_integrity() dans UE5". C'était une convention écrite, jamais un
mécanisme — exactement le trou déjà fermé pour qc_gate.py (manifest QC) et pour
safe_write/safe_append (écriture atomique) : une règle qu'un agent pressé peut simplement
oublier de suivre.

CE QUE CE SCRIPT FAIT DE DIFFÉRENT : au lieu de faire confiance à UN check lancé depuis UN
seul canal d'I/O, il fait enregistrer une signature (sha256 + taille + mtime) du MÊME
fichier par plusieurs canaux INDÉPENDANTS ("backends"), puis compare. Si tous les backends
sont d'accord → TRUSTED. S'ils divergent → DIVERGENT, mis en quarantaine, avec le détail de
qui voit quoi — au lieu qu'un agent choisisse silencieusement de croire un seul canal.
Backends prévus (voir CLAUDE.md pour le détail de chacun) :
  - "bash_sandbox" : ce script exécuté tel quel dans le bash sandbox Cowork (le canal
    historiquement peu fiable — mount FUSE, cache parfois périmé de plusieurs minutes).
  - "ue5_native"   : ce script importé et exécuté DANS le Python embarqué UE5 via
    ue5_utils.trust_gate_record() (accès disque Windows natif — le canal qui, jusqu'ici,
    faisait foi par convention plutôt que par mécanisme).
Un troisième canal (lecture directe via l'outil Read de Claude Cowork, qui accède aussi au
vrai disque Windows) reste un jugement humain/agent de dernier recours, PAS un backend
scriptable ici — même philosophie que qc_gate.py qui ne remplace jamais la lecture directe
d'un screenshot : ce script ferme la partie automatisable, pas la partie qui a besoin d'un
oeil.

STOCKAGE — sidecar files, pas un ledger fusionné (choix délibéré) : un design "un seul
fichier JSON partagé, lu-modifié-réécrit par chaque backend" aurait réintroduit exactement
le bug qu'il essaie de détecter — si le canal bash_sandbox écrit sa lecture, puis que le
canal ue5_native lit ce même fichier JSON à travers un mount périmé avant d'ajouter la
sienne, il verrait une version en retard et écraserait silencieusement l'entrée bash_sandbox
en la sauvegardant (lost update). À la place, CHAQUE backend écrit dans SON PROPRE fichier
sidecar (Saved/QC/trust_readings/<hash_du_chemin>__<backend>.json), jamais partagé en
écriture avec un autre backend. La comparaison (--compare / --check-ledger) se contente de
lister et lire tous les sidecars existants pour un chemin donné — aucune écriture concurrente
possible entre deux backends.

Usage :
    # Depuis le bash sandbox Cowork :
    python3 Tools/trust_gate.py --record bash_sandbox Content/Python/ue5_utils.py

    # Depuis UE5 (via ue5_utils.trust_gate_record, voir ue5_utils.py) :
    #   trust_gate_record("Content/Python/ue5_utils.py")  -> écrit le sidecar ue5_native

    # Comparer les deux (depuis n'importe quel canal, y compris bash) :
    python3 Tools/trust_gate.py --compare Content/Python/ue5_utils.py

    # Audit de tous les fichiers jamais enregistrés au moins une fois :
    python3 Tools/trust_gate.py --check-ledger
    # --strict-unverified : échoue aussi (exit 1) sur les fichiers vus par un seul backend

Retourne du JSON sur stdout. Code de sortie : 0 = TRUSTED (ou UNVERIFIED hors --strict-
unverified), 1 = DIVERGENCE détectée (ou UNVERIFIED avec --strict-unverified), 2 = erreur
d'usage.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
READINGS_DIR = os.path.join(ROOT, "Saved", "QC", "trust_readings")
INCIDENTS_LOG = os.path.join(ROOT, "Saved", "QC", "trust_incidents.jsonl")


def _now():
    return datetime.datetime.now().isoformat(timespec="seconds")


def _abs(path):
    """Normalise en chemin absolu, résolu depuis ROOT si relatif — pour OUVRIR
    le fichier depuis CE canal (chaque canal a son propre ROOT, voir module
    ROOT ci-dessus, calculé depuis son propre __file__)."""
    if os.path.isabs(path):
        return os.path.normpath(path)
    return os.path.normpath(os.path.join(ROOT, path))


def _project_rel(path):
    """Chemin CANONIQUE utilisé comme IDENTITÉ d'un fichier dans le quorum :
    relatif à ROOT, séparateurs '/'. C'est ce chemin — PAS le chemin absolu —
    qui doit être identique quel que soit le canal d'I/O.

    BUG RÉEL rencontré en construisant ce script (voir CLAUDE.md 2026-07-14) :
    la première version de _reading_key() dérivait sa clé du chemin ABSOLU. Or
    bash_sandbox et ue5_native voient le MÊME fichier sous deux racines
    absolues totalement différentes (/sessions/.../mnt/HorrorGame 5.7/...
    côté bash, D:\\Travail\\...\\HorrorGame 5.7\\... côté UE5) — donc chaque
    canal écrivait dans un sidecar DIFFÉRENT pour "le même" fichier,
    compare_readings() ne les voyait jamais ensemble, et le verdict restait
    UNVERIFIED (1 seul backend trouvé) au lieu de TRUSTED/DIVERGENT — alors
    même que les DEUX enregistrements avaient bien eu lieu. Attrapé en testant
    ce mécanisme sur lui-même, en conditions réelles, pas en théorie."""
    ap = _abs(path)
    rel = os.path.relpath(ap, ROOT)
    return rel.replace("\\", "/")


def _reading_key(path):
    """Clé de fichier stable et filesystem-safe dérivée du chemin PROJET-
    RELATIF (voir _project_rel) — invariant par canal, contrairement au
    chemin absolu."""
    return hashlib.sha1(_project_rel(path).encode("utf-8")).hexdigest()[:16]


def _sidecar_path(path, backend):
    return os.path.join(READINGS_DIR, "{}__{}.json".format(_reading_key(path), backend))


def _atomic_write_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = "{}.tmp_{}".format(path, os.getpid())
    try:
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            try:
                os.remove(tmp)
            except Exception:
                pass


def compute_signature(path):
    """Signature d'un fichier TEL QUE VU PAR CE PROCESS, LÀ, MAINTENANT — lecture
    binaire (jamais texte, pour ne rater aucun octet NUL, même raison que
    verify_file_integrity() dans ue5_utils.py)."""
    ap = _abs(path)
    if not os.path.isfile(ap):
        return None, "fichier introuvable: {}".format(ap)
    data = open(ap, "rb").read()
    sig = {
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
        "nul_bytes": data.count(b"\x00"),
        "mtime": os.path.getmtime(ap),
    }
    return sig, None


def record_reading(path, backend, note=None):
    """Calcule la signature du fichier via CE process et l'écrit dans le sidecar
    exclusif de ce backend. N'écrit JAMAIS dans le sidecar d'un autre backend —
    aucune lecture-modification-écriture d'un état partagé, voir docstring
    module pour pourquoi (évite le lost-update entre canaux asynchrones)."""
    sig, err = compute_signature(path)
    if sig is None:
        return {"ok": False, "error": err}

    entry = {
        "path": _project_rel(path),
        "seen_as": _abs(path).replace("\\", "/"),
        "backend": backend,
        "recorded_at": _now(),
        "note": note,
    }
    entry.update(sig)
    _atomic_write_json(_sidecar_path(path, backend), entry)
    return {"ok": True, "backend": backend, "sha256": sig["sha256"], "size": sig["size"]}


def _load_readings_for_path(path):
    """Liste tous les sidecars existants pour ce chemin, backend par backend —
    lecture indépendante de chaque fichier, aucun état partagé entre backends."""
    key = _reading_key(path)
    readings = {}
    if not os.path.isdir(READINGS_DIR):
        return readings
    prefix = key + "__"
    for fname in os.listdir(READINGS_DIR):
        if fname.startswith(prefix) and fname.endswith(".json"):
            backend = fname[len(prefix):-len(".json")]
            try:
                with open(os.path.join(READINGS_DIR, fname), encoding="utf-8") as f:
                    readings[backend] = json.load(f)
            except Exception as e:
                readings[backend] = {"error": "sidecar illisible: {}".format(e)}
    return readings


def _record_incident(path, verdict, readings):
    """Log append-only (une ligne JSON par incident) — non critique si une écriture
    concurrente rare écrase une ligne (append en mode 'a' reste néanmoins sûr pour
    de petites écritures séquentielles dans ce workflow), contrairement aux
    sidecars de lecture qui, eux, DOIVENT rester sans race possible."""
    os.makedirs(os.path.dirname(INCIDENTS_LOG), exist_ok=True)
    line = {
        "detected_at": _now(),
        "path": _abs(path).replace("\\", "/"),
        "verdict": verdict,
        "backends": {b: r.get("sha256", r.get("error")) for b, r in readings.items()},
    }
    with open(INCIDENTS_LOG, "a", encoding="utf-8") as f:
        f.write(json.dumps(line, ensure_ascii=False) + "\n")


def compare_readings(path):
    """Verdict de quorum pour un chemin donné, à partir de TOUS les sidecars
    existants — pas seulement des deux derniers appelés. TRUSTED seulement si
    au moins 2 backends existent ET tous s'accordent sur le sha256."""
    readings = _load_readings_for_path(path)

    result = {
        "path": _project_rel(path),
        "seen_as": _abs(path).replace("\\", "/"),
        "backends_checked": sorted(readings.keys()),
    }

    if not readings:
        result["verdict"] = "UNVERIFIED"
        result["reason"] = "aucune lecture enregistree pour ce fichier"
        return result

    if len(readings) == 1:
        only = next(iter(readings.values()))
        result["verdict"] = "UNVERIFIED"
        result["reason"] = "un seul backend ({}) a enregistre une lecture — pas de quorum, exactement le cas que ce mecanisme existe pour eviter de trancher seul".format(next(iter(readings.keys())))
        result["single_reading"] = only
        return result

    hashes = {}
    for backend, r in readings.items():
        h = r.get("sha256")
        hashes.setdefault(h, []).append(backend)

    if len(hashes) == 1 and None not in hashes:
        result["verdict"] = "TRUSTED"
        result["sha256"] = next(iter(hashes.keys()))
        result["reason"] = "{} backends d'accord: {}".format(len(readings), ", ".join(sorted(readings.keys())))
        return result

    result["verdict"] = "DIVERGENT"
    result["reason"] = "les backends ne voient PAS le meme contenu pour ce fichier — quarantaine"
    result["hash_groups"] = {
        (h if h else "ERREUR_LECTURE"): backends for h, backends in hashes.items()
    }
    result["readings"] = readings
    _record_incident(path, "DIVERGENT", readings)
    return result


def check_ledger(paths=None, strict_unverified=False):
    """Audit global : reconstruit la liste des chemins connus à partir des
    sidecars présents sur disque (pas d'index séparé à tenir synchronisé — la
    liste des fichiers de Saved/QC/trust_readings/ EST la source de vérité)."""
    known_paths = {}
    if os.path.isdir(READINGS_DIR):
        for fname in os.listdir(READINGS_DIR):
            if not fname.endswith(".json"):
                continue
            try:
                with open(os.path.join(READINGS_DIR, fname), encoding="utf-8") as f:
                    p = json.load(f).get("path")
                if p:
                    known_paths[_reading_key(p)] = p
            except Exception:
                continue

    if paths:
        wanted_keys = {_reading_key(p) for p in paths}
        known_paths = {k: v for k, v in known_paths.items() if k in wanted_keys}

    report = []
    any_divergent = False
    any_unverified = False
    for p in sorted(known_paths.values()):
        v = compare_readings(p)
        report.append(v)
        if v["verdict"] == "DIVERGENT":
            any_divergent = True
        elif v["verdict"] == "UNVERIFIED":
            any_unverified = True

    verdict = "ALL_CLEAR"
    if any_divergent:
        verdict = "DIVERGENCE_DETECTED"
    elif any_unverified and strict_unverified:
        verdict = "PENDING_QUORUM"

    return {"verdict": verdict, "files": report}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--record", nargs=2, metavar=("BACKEND", "PATH"), help="Enregistre une lecture pour ce fichier depuis CE canal")
    parser.add_argument("--note", default=None, help="Note libre attachee a --record")
    parser.add_argument("--compare", metavar="PATH", help="Compare tous les backends enregistres pour ce fichier")
    parser.add_argument("--check-ledger", nargs="*", metavar="PATH", default=None, help="Audit de tous les fichiers connus (ou ceux listes)")
    parser.add_argument("--strict-unverified", action="store_true", help="--check-ledger echoue aussi (exit 1) sur les fichiers vus par un seul backend")
    args = parser.parse_args()

    if args.record:
        backend, path = args.record
        res = record_reading(path, backend, note=args.note)
        print(json.dumps(res, indent=2, ensure_ascii=False))
        return 0 if res.get("ok") else 2

    if args.compare:
        res = compare_readings(args.compare)
        print(json.dumps(res, indent=2, ensure_ascii=False))
        return 0 if res["verdict"] in ("TRUSTED", "UNVERIFIED") else 1

    if args.check_ledger is not None:
        paths = args.check_ledger if args.check_ledger else None
        res = check_ledger(paths=paths, strict_unverified=args.strict_unverified)
        print(json.dumps(res, indent=2, ensure_ascii=False))
        return 0 if res["verdict"] == "ALL_CLEAR" else 1

    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
