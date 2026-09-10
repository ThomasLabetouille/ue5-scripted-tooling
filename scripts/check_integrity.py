#!/usr/bin/env python3
"""Vérifie que la copie filtrée n'a pas été corrompue par un sync.

Deux fois déjà, un fichier recopié s'est retrouvé tronqué en plein milieu d'une
fonction ou complété par des octets NUL, sans qu'aucun outil ne lève d'erreur au
moment de l'écriture. La copie avait la bonne extension et une taille plausible ;
elle levait une SyntaxError au premier import réel.

    python3 scripts/check_integrity.py

Rend 0 si tout est sain, 1 sinon.
"""
import ast
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent
IGNORES = {".git", "__pycache__", ".ruff_cache", ".pytest_cache"}
TEXTE = {".py", ".cpp", ".h", ".cs", ".md", ".txt", ".ps1", ".bat", ".json", ".uplugin", ".yml"}


def fichiers():
    for chemin in RACINE.rglob("*"):
        if not chemin.is_file():
            continue
        if IGNORES & set(chemin.relative_to(RACINE).parts):
            continue
        if chemin.suffix in TEXTE:
            yield chemin


def main():
    problemes = []
    total = 0

    for chemin in fichiers():
        total += 1
        rel = chemin.relative_to(RACINE)
        brut = chemin.read_bytes()

        if b"\x00" in brut:
            problemes.append("%s : octets NUL (écriture tronquée par un pont de fichiers)" % rel)
            continue
        if not brut.strip():
            problemes.append("%s : fichier vide" % rel)
            continue

        if chemin.suffix == ".py":
            try:
                ast.parse(brut.decode("utf-8"))
            except (SyntaxError, UnicodeDecodeError) as err:
                problemes.append("%s : ne parse pas (%s)" % (rel, err))
                continue

        if chemin.suffix in {".cpp", ".h", ".cs"}:
            texte = brut.decode("utf-8", errors="replace")
            if texte.count("{") != texte.count("}"):
                problemes.append("%s : accolades déséquilibrées (%d ouvrantes, %d fermantes)"
                                 % (rel, texte.count("{"), texte.count("}")))

    print("%d fichier(s) texte vérifié(s)." % total)
    if problemes:
        print("\n%d problème(s) :" % len(problemes))
        for p in problemes:
            print("  " + p)
        return 1
    print("Aucun problème.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
