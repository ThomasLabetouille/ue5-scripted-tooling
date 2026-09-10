"""
qc_gate.py — Verdict QC consolidé (structurel + numérique) pour RPG_Test.

Portage depuis HorrorGame/Tools/qc_gate.py (2026-07-14) — mécanique générique, aucun
changement de fond nécessaire (le manifest Saved/QC/qc_manifest.json a le même schéma que
côté verify_rpg_test.py, clé "zone_name" générique — peut représenter une zone de niveau ou
tout autre unité construite/vérifiée).

CONTEXTE : `verify_rpg_test.verified_build()` (côté UE5) ferme la partie STRUCTURELLE de la
boucle avec relance automatique plafonnée, mais tourne dans le Python embarqué UE5 — sans PIL
ni numpy, impossible d'y mesurer un vrai pixel. Tools/analyze_screenshot.py tourne à côté
(bash Claude Cowork, PIL+numpy disponibles) et complète avec des métriques numériques. Ce
script fusionne les deux verdicts en UNE SEULE commande, UN SEUL verdict JSON — pour ne pas
compter sur un agent qui croise les deux appels à la main.

NE REMPLACE TOUJOURS PAS le jugement visuel direct (Read + œil humain/vision-agent).

Usage :
    python3 Tools/qc_gate.py --screenshots a.png b.png --errors-json report.json
    # report.json = {"errors": [...], "warnings": [...]}  — sortie telle quelle de
    # verify_rpg_test.verified_build() ou de run_verify().

    python3 Tools/qc_gate.py --screenshots a.png

    python3 Tools/qc_gate.py --screenshots a.png --errors-json report.json --zone Z1

    python3 Tools/qc_gate.py --confirm-visual-read Z1 --note "point focal clair"

    python3 Tools/qc_gate.py --check-manifest

Retourne un JSON sur stdout + code de sortie 0 (PASS) / 1 (FAIL).
"""

import sys
import os
import json
import argparse
import datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_screenshot import analyze, DEFAULT_KWARGS  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _manifest_path():
    d = os.path.join(ROOT, "Saved", "QC")
    os.makedirs(d, exist_ok=True)
    return os.path.join(d, "qc_manifest.json")


def _load_manifest():
    path = _manifest_path()
    if not os.path.isfile(path):
        return {}
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}


def _save_manifest(data):
    path = _manifest_path()
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
    return path


def _record_qc_gate_verdict(zone, result):
    data = _load_manifest()
    entry = data.setdefault(zone, {
        "zone_name": zone, "screenshot": None, "structural_errors": [],
        "structural_warnings": [], "built_at": None,
        "visual_read_confirmed": False, "visual_read_note": None,
    })
    entry["qc_gate_verdict"] = result["verdict"]
    entry["qc_gate_ran_at"] = datetime.datetime.now().isoformat(timespec="seconds")
    entry["numeric_problems"] = result["numeric_problems"]
    _save_manifest(data)


def _cmd_check_manifest():
    data = _load_manifest()
    if not data:
        print(json.dumps({"verdict": "EMPTY", "total_zones": 0, "pending_zones": []},
                          indent=2, ensure_ascii=False))
        sys.exit(0)

    pending = []
    for zone, entry in data.items():
        reasons = []
        verdict = entry.get("qc_gate_verdict")
        if verdict is None:
            reasons.append("qc_gate.py jamais lancé sur cette zone (--zone manquant)")
        elif verdict != "PASS":
            reasons.append(f"dernier verdict qc_gate: {verdict}")
        if not entry.get("visual_read_confirmed"):
            reasons.append("lecture visuelle jamais confirmée (--confirm-visual-read manquant)")
        if reasons:
            pending.append({"zone": zone, "reasons": reasons,
                             "screenshot": entry.get("screenshot")})

    report = {
        "total_zones": len(data),
        "pending_zones": pending,
        "verdict": "ALL_CLEAR" if not pending else "PENDING_VERIFICATION",
    }
    print(json.dumps(report, indent=2, ensure_ascii=False))
    sys.exit(0 if not pending else 1)


def _cmd_confirm_visual_read(zone, note):
    data = _load_manifest()
    if zone not in data:
        print(f"ERREUR: zone '{zone}' introuvable dans le manifest QC ({_manifest_path()}). "
              f"Zones connues: {list(data.keys())}", file=sys.stderr)
        sys.exit(2)
    data[zone]["visual_read_confirmed"] = True
    data[zone]["visual_read_note"] = note
    data[zone]["visual_read_at"] = datetime.datetime.now().isoformat(timespec="seconds")
    _save_manifest(data)
    print(f"[qc_gate] Zone '{zone}' marquée comme visuellement vérifiée.")
    sys.exit(0)


def _cmd_confirm_gdd_compliance(unit, note):
    # Fix 2026-07-21 (audit RoomGenerator, Axe F volet 2) : ce flag était décrit dans
    # ROADMAP_JEU_COMPLET.md ("le statut gdd_verified d'une unité ne doit jamais être atteint
    # sans ce flag") mais n'existait pas concrètement dans qc_gate.py — les 3 premières unités
    # du backlog ont été marquées gdd_verified sans jamais passer par ce garde-fou. Même
    # philosophie que --confirm-visual-read : ne peut pas vérifier techniquement qu'une vraie
    # relecture du GDD a eu lieu, rend juste l'omission visible dans le manifest plutôt que
    # silencieuse. gdd_backlog.mark_status() refuse maintenant "gdd_verified" tant que ce flag
    # n'a pas été posé pour l'unité (voir Content/Python/gdd_backlog.py).
    data = _load_manifest()
    if unit not in data:
        print(f"ERREUR: unité '{unit}' introuvable dans le manifest QC ({_manifest_path()}). "
              f"Unités connues: {list(data.keys())}", file=sys.stderr)
        sys.exit(2)
    data[unit]["gdd_compliance_confirmed"] = True
    data[unit]["gdd_compliance_note"] = note
    data[unit]["gdd_compliance_at"] = datetime.datetime.now().isoformat(timespec="seconds")
    _save_manifest(data)
    print(f"[qc_gate] Unité '{unit}' marquée conforme à l'intention du GDD (jugement humain, "
          f"non automatisable).")
    sys.exit(0)


def run_gate(screenshots, errors=None, warnings=None):
    errors = list(errors or [])
    warnings = list(warnings or [])

    per_screenshot = {}
    numeric_problems = []
    for path in screenshots:
        report = analyze(path, **DEFAULT_KWARGS)
        per_screenshot[path] = report
        if report["verdict"] != "OK":
            numeric_problems.extend(
                "[{}] {}".format(os.path.basename(path), p) for p in report["problems"]
            )

    overall_pass = (len(errors) == 0) and (len(numeric_problems) == 0)

    return {
        "structural_errors": errors,
        "structural_warnings": warnings,
        "numeric_problems": numeric_problems,
        "per_screenshot": per_screenshot,
        "verdict": "PASS" if overall_pass else "FAIL",
        "still_needs_visual_read": True,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--screenshots", nargs="+", default=None,
                     help="Chemin(s) vers les PNG à analyser (capture_reference_screenshot)")
    ap.add_argument("--errors-json", default=None,
                     help="Chemin vers un JSON {'errors': [...], 'warnings': [...]} — "
                          "sortie de verified_build() ou run_verify()")
    ap.add_argument("--zone", "--unit", dest="zone", default=None,
                     help="Nom de zone/unité (id gdd_backlog.py, Axe C) — si fourni, consigne ce "
                          "verdict dans le manifest QC persistant (Saved/QC/qc_manifest.json). "
                          "'--unit' est un alias strict de '--zone' : même clé de manifest, "
                          "aucune distinction technique — level_zone et gameplay_system y "
                          "cohabitent (voir verify_rpg_test.verified_build_for_unit()).")
    ap.add_argument("--check-manifest", action="store_true",
                     help="Ignore --screenshots : audit de toutes les zones du manifest QC")
    ap.add_argument("--confirm-visual-read", metavar="ZONE", default=None,
                     help="Marque ZONE comme visuellement vérifiée dans le manifest QC")
    ap.add_argument("--confirm-gdd-compliance", metavar="UNIT", default=None,
                     help="Marque UNIT comme fidèle à l'intention du GDD (Axe F volet 2, "
                          "jugement humain — voir ROADMAP_JEU_COMPLET.md). Requis avant que "
                          "gdd_backlog.mark_status() accepte de passer cette unité à "
                          "'gdd_verified'.")
    ap.add_argument("--note", default="",
                     help="Note optionnelle jointe à --confirm-visual-read / --confirm-gdd-compliance")
    args = ap.parse_args()

    if args.check_manifest:
        _cmd_check_manifest()
        return

    if args.confirm_visual_read:
        _cmd_confirm_visual_read(args.confirm_visual_read, args.note)
        return

    if args.confirm_gdd_compliance:
        _cmd_confirm_gdd_compliance(args.confirm_gdd_compliance, args.note)
        return

    if not args.screenshots:
        ap.error("--screenshots est requis (sauf --check-manifest / --confirm-visual-read)")

    errors, warnings = [], []
    if args.errors_json:
        with open(args.errors_json, encoding="utf-8") as f:
            data = json.load(f)
        errors = data.get("errors", [])
        warnings = data.get("warnings", [])

    result = run_gate(args.screenshots, errors, warnings)

    if args.zone:
        _record_qc_gate_verdict(args.zone, result)
        try:
            from visual_diff import compare_zone  # noqa: E402 (import tardif, optionnel)
            result["visual_diff_vs_baseline"] = compare_zone(args.zone, record=True)
        except Exception as exc:
            result["visual_diff_vs_baseline"] = {"verdict": "ERROR", "message": str(exc)}

    print(json.dumps(result, indent=2, ensure_ascii=False))

    if args.zone:
        print(f"\n[qc_gate] Verdict consigné dans le manifest QC pour la zone '{args.zone}'.",
              file=sys.stderr)
        vdiff = result.get("visual_diff_vs_baseline", {})
        if vdiff.get("verdict") == "DERIVE_VISUELLE_DETECTEE":
            print("[qc_gate] ATTENTION: dérive visuelle détectée vs la baseline enregistrée "
                  "pour cette zone — relire le screenshot avant de continuer.", file=sys.stderr)

    print("\n--- RAPPEL ---", file=sys.stderr)
    print("Ce verdict ne remplace pas la lecture directe des screenshots avec Read.",
          file=sys.stderr)
    if args.zone:
        print("Manque encore : --confirm-visual-read {} après avoir lu le/les screenshot(s).".format(args.zone),
              file=sys.stderr)

    sys.exit(0 if result["verdict"] == "PASS" else 1)


if __name__ == "__main__":
    main()
