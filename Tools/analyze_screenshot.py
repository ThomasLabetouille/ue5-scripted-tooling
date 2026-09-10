"""
analyze_screenshot.py — Check perceptuel réel d'un screenshot RPG_Test.

Portage depuis HorrorGame/Tools/analyze_screenshot.py (2026-07-14). Le Python embarqué UE5
n'a NI PIL NI numpy (vérifié empiriquement sur HorrorGame, comportement identique attendu ici
— même build UE5 Python) donc impossible d'analyser un PNG pixel par pixel depuis
verify_rpg_test.py directement. Ce script tourne à côté, côté Claude Cowork (bash sandbox,
PIL+numpy disponibles), sur le fichier PNG déjà exporté par capture_reference_screenshot().

DIFFÉRENCE DE CALIBRATION vs HorrorGame (important) : HorrorGame vise une ambiance horror
sombre (cible ~40% de pixels sombres, ~60% max visibles — voir HORROR_DESIGN.md de ce
projet). RPG_Test est un RPG action 3e personne normalement éclairé, PAS un jeu d'horreur —
appliquer les mêmes seuils de noirceur signalerait à tort une scène bien éclairée comme un
"problème". Les checks de noirceur ciblée (`min_pct_dark`/`max_pct_visible`) sont donc
DÉSACTIVÉS par défaut ici (None). Les checks qui détectent un vrai bug de rendu — scène
QUASI NOIRE, image PLATE (aucune géométrie/lumière visible), AUCUN CONTRASTE, hautes lumières
CRAMÉES — restent actifs par défaut : ce sont des signaux de bug de pipeline, pas de choix
artistique, valables quel que soit le genre du jeu.

Usage (depuis le bash de Claude Cowork, PAS depuis ue5_execute) :
    python3 Tools/analyze_screenshot.py /chemin/vers/capture.png

Retourne un rapport texte + code de sortie 0 (OK) / 1 (probable problème de rendu).

Ce script ne remplace JAMAIS la lecture directe de l'image avec le Read tool — il donne
juste des chiffres objectifs qui permettent de détecter à coup sûr le cas "scène cassée"
même quand l'œil hésite sur une image compressée ou mal calibrée à l'écran.
"""

import sys
import json


def analyze(path, dark_threshold=5, bright_threshold=40,
            min_mean_luminance=3.0, max_uniform_pct=97.0,
            min_pct_dark=None, max_pct_visible=None,
            clip_threshold=250, max_pct_clipped=None, max_p99=None):
    """Analyse un PNG et retourne un dict de métriques + verdict.

    dark_threshold       : luminance (0-255) en dessous de laquelle un pixel est "noir"
    bright_threshold     : luminance au-dessus de laquelle un pixel est "visible/lisible"
    min_mean_luminance   : luminance moyenne minimale pour ne pas déclencher "SCENE NOIRE"
                            — détecte un vrai bug de rendu (rien affiché), pas un choix
                            artistique, donc actif par défaut même pour RPG_Test.
    max_uniform_pct      : % de pixels quasi-identiques (±2) au-delà duquel "PLATE/SANS RELIEF"
                            — détecte typiquement un écran gris/noir uniforme (bug), pas un
                            choix de composition.
    min_pct_dark         : % minimum de pixels sombres. None (défaut RPG_Test) = désactivé —
                            contrairement à HorrorGame, RPG_Test n'a pas vocation à être sombre.
    max_pct_visible       : % maximum de pixels "visibles". None (défaut RPG_Test) = désactivé,
                            même raison que min_pct_dark.
    clip_threshold        : luminance (0-255) au-dessus de laquelle un pixel est considéré "cramé"
    max_pct_clipped        : % maximum de pixels cramés toléré. None = pas de check (les seuils
                            HorrorGame — max_pct_clipped=3.0, max_p99=235.0 — sont calibrés pour
                            une ambiance sombre à hautes lumières ponctuelles ; à recalibrer pour
                            RPG_Test si besoin plutôt que de réutiliser tels quels).
    max_p99                : valeur maximale tolérée pour le 99e percentile de luminance. None =
                            pas de check.
    """
    from PIL import Image
    import numpy as np

    img = np.array(Image.open(path).convert("RGB")).astype(float)
    lum = 0.2126 * img[:, :, 0] + 0.7152 * img[:, :, 1] + 0.0722 * img[:, :, 2]

    mean_lum = float(lum.mean())
    std_lum = float(lum.std())
    p50 = float(np.percentile(lum, 50))
    p99 = float(np.percentile(lum, 99))
    pct_dark = float((lum <= dark_threshold).mean() * 100)
    pct_visible = float((lum >= bright_threshold).mean() * 100)
    pct_clipped = float((lum >= clip_threshold).mean() * 100)
    pct_channel_clipped = float((img.max(axis=2) >= 253).mean() * 100)

    h, w = lum.shape
    flat_idx = int(np.argmax(lum))
    peak_y, peak_x = divmod(flat_idx, w)
    peak_dx_pct = abs(peak_x - w / 2) / (w / 2) * 100
    peak_dy_pct = abs(peak_y - h / 2) / (h / 2) * 100

    pct_uniform = float((np.abs(lum - p50) <= 2).mean() * 100)

    problems = []
    if mean_lum < min_mean_luminance:
        problems.append(
            f"SCENE QUASI NOIRE: luminance moyenne {mean_lum:.2f}/255 "
            f"(seuil mini {min_mean_luminance}) — probable bug de rendu (rien affiché)"
        )
    if pct_visible < 5.0:
        problems.append(
            f"QUASI RIEN DE LISIBLE: seulement {pct_visible:.1f}% des pixels au-dessus "
            f"du seuil de visibilité ({bright_threshold}/255)"
        )
    if pct_uniform > max_uniform_pct:
        problems.append(
            f"IMAGE PLATE: {pct_uniform:.1f}% des pixels quasi-identiques — pas de relief, "
            f"probablement pas de géométrie/lumière visible (écran gris/noir uniforme)"
        )
    if std_lum < 2.0 and mean_lum < 10:
        problems.append(
            f"AUCUN CONTRASTE: écart-type luminance {std_lum:.2f} — image quasi monochrome"
        )
    if min_pct_dark is not None and pct_dark < min_pct_dark:
        problems.append(
            f"PAS ASSEZ D'OMBRE: seulement {pct_dark:.1f}% de pixels sombres (mini {min_pct_dark}%)"
        )
    if max_pct_visible is not None and pct_visible > max_pct_visible:
        problems.append(
            f"TROP UNIFORMÉMENT ÉCLAIRÉ: {pct_visible:.1f}% de pixels visibles (max {max_pct_visible}%)"
        )
    if max_pct_clipped is not None and pct_clipped > max_pct_clipped:
        problems.append(
            f"HAUTES LUMIÈRES CRAMÉES: {pct_clipped:.1f}% des pixels au-dessus de "
            f"{clip_threshold}/255 (max toléré {max_pct_clipped}%)"
        )
    if max_p99 is not None and p99 > max_p99:
        problems.append(
            f"99e PERCENTILE TROP CLAIR: p99={p99:.1f}/255 (max {max_p99})"
        )

    verdict_ok = len(problems) == 0

    report = {
        "path": path,
        "mean_luminance": round(mean_lum, 3),
        "std_luminance": round(std_lum, 3),
        "p99_luminance": round(p99, 2),
        "pct_pixels_dark": round(pct_dark, 2),
        "pct_pixels_visible": round(pct_visible, 2),
        "pct_pixels_uniform": round(pct_uniform, 2),
        "pct_pixels_clipped": round(pct_clipped, 2),
        "pct_pixels_channel_clipped": round(pct_channel_clipped, 2),
        "brightest_point_offset_from_center_pct": {
            "x": round(peak_dx_pct, 1), "y": round(peak_dy_pct, 1)
        },
        "problems": problems,
        "verdict": "OK" if verdict_ok else "PROBLEME_RENDU_DETECTE",
    }
    return report


# Pas d'équivalent de DEFAULT_CLIP_KWARGS (HorrorGame) ici : les seuils de clipping sourcés
# pour HorrorGame sont calibrés sur une ambiance horror sombre à hautes lumières ponctuelles,
# pas transposables tels quels à un RPG normalement éclairé. main() n'applique donc que les
# checks génériques de bug de rendu (scène noire/plate/sans contraste), pas de check de
# clipping par défaut — à activer explicitement (max_pct_clipped=..., max_p99=...) une fois
# calibré sur des captures RPG_Test réelles si besoin.
DEFAULT_KWARGS = dict()


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 analyze_screenshot.py <chemin.png> [chemin2.png ...]")
        sys.exit(2)

    any_problem = False
    for path in sys.argv[1:]:
        report = analyze(path, **DEFAULT_KWARGS)
        print(f"\n{'='*60}")
        print(f"ANALYSE: {path}")
        print(f"{'='*60}")
        print(json.dumps(report, indent=2, ensure_ascii=False))
        if report["verdict"] != "OK":
            any_problem = True

    sys.exit(1 if any_problem else 0)


if __name__ == "__main__":
    main()
