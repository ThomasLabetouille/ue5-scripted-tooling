#!/usr/bin/env python3
"""check_content_python_integrity.py — garde-fou d'intégrité pour Content/Python/.

Portage depuis HorrorGame/Tools/check_content_python_integrity.py (2026-07-14) — voir
D:\\Travail\\ProjectUnreal\\HorrorGame 5.7\\CLAUDE.md pour le contexte complet du bug que ceci
détecte (pont Windows↔sandbox Linux de Cowork qui peut corrompre/servir une version périmée
d'un fichier sans lever d'erreur).

ue5_utils.py de RPG_Test contient déjà `verify_file_integrity()` et
`scan_content_python_integrity()`, qui font exactement cette vérification (octets NUL +
ast.parse) — mais ces deux fonctions importent `unreal` et ne peuvent donc tourner QUE dans
le Python embarqué de UE5 (via ue5_execute). Ce script est la version standalone (stdlib
uniquement) de la même vérification, utilisable depuis le bash sandbox Cowork sans UE5 ouvert.

Ce script ne remplace PAS `scan_content_python_integrity()` lancé depuis UE5 — voir le piège
de mount FUSE périmé documenté dans le CLAUDE.md de HorrorGame : ne jamais déclarer un
fichier corrompu sur la seule foi de CE script si un doute existe, confirmer avec
`scan_content_python_integrity()` dans UE5.

Usage :
    python3 Tools/check_content_python_integrity.py
    # exit 0 = tout sain, exit 1 = au moins un problème bloquant trouvé

_PARSE_EXEMPT vide pour RPG_Test (contrairement à HorrorGame) : pas de script legacy
pré-BatchWireGraph ici. À garder synchronisé avec _INTEGRITY_PARSE_EXEMPT dans
Content/Python/ue5_utils.py si un tel script apparaît un jour.
"""
from __future__ import annotations

import ast
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CONTENT_PYTHON = ROOT / "Content" / "Python"

_PARSE_EXEMPT: set[str] = set()


def verify_file(path: pathlib.Path, check_parse: bool = True) -> tuple[bool, str]:
    """Même logique que verify_file_integrity() dans ue5_utils.py, sans expected_text
    (ce script n'a pas accès au contenu "attendu" — juste "est-ce que ce qui est sur
    disque là, maintenant, est sain")."""
    if not path.is_file():
        return False, f"fichier introuvable: {path}"

    data = path.read_bytes()

    if b"\x00" in data:
        count = data.count(b"\x00")
        first = data.index(b"\x00")
        return False, f"{count} octet(s) NUL trouve(s) (premier a l'offset {first}) — fichier probablement tronque/corrompu"

    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as e:
        return False, f"decode UTF-8 echoue: {e}"

    if path.suffix == ".py" and check_parse:
        try:
            ast.parse(text, filename=str(path))
        except (SyntaxError, ValueError) as e:
            return False, f"ne parse pas comme Python valide: {e}"

    return True, ""


def scan(verbose: bool = True) -> list[tuple[pathlib.Path, str]]:
    problems: list[tuple[pathlib.Path, str]] = []
    warnings: list[tuple[pathlib.Path, str]] = []

    if not CONTENT_PYTHON.is_dir():
        if verbose:
            print(f"[integrity] Content/Python/ introuvable a {CONTENT_PYTHON} — rien a verifier.")
        return problems

    all_py = sorted(CONTENT_PYTHON.glob("*.py"))
    for f in all_py:
        exempt = f.name in _PARSE_EXEMPT
        ok, msg = verify_file(f, check_parse=not exempt)
        if not ok:
            problems.append((f, msg))
        elif exempt:
            parse_ok, parse_msg = verify_file(f, check_parse=True)
            if not parse_ok:
                warnings.append((f, parse_msg))

    if verbose:
        if problems:
            print(f"[integrity] {len(problems)} probleme(s) trouve(s) dans Content/Python/ :")
            for f, msg in problems:
                print(f"  - {f.name}: {msg}")
        else:
            print(f"[integrity] OK — tous les .py de Content/Python/ sont sains ({len(all_py)} fichiers)")
        for f, msg in warnings:
            print(f"[integrity][warning connu, non bloquant] {f.name}: {msg}")

    return problems


def main() -> int:
    problems = scan(verbose=True)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
