# init_unreal.py -- auto-execute par l'editeur UE5 au demarrage.
#
# Pre-charge ue5_utils pour que "from ue5_utils import *" soit immediat dans la console Python
# et via ue5_execute.
#
# HISTORIQUE (2026-08-29, fusion RPG_Test -> GameAnimationSample) : ce fichier importait aussi
# blockout_tool_panel, qui enregistrait un menu "Outil Blockout" en Python. Ce panneau a ete
# retire : le plugin C++ BlockoutTools enregistre lui-meme son onglet nomade et son entree de
# menu (voir FBlockoutToolsModule::StartupModule et RegisterMenus), et son subsystem couvre
# Room, Corridor, Staircase, Ramp, BridgeArch, CurvedTunnel et VisionCone. La version Python
# n'existait plus qu'en bytecode, sans source -- donc non modifiable. Elle faisait doublon avec
# une implementation C++ maintenable ; c'est la seule raison de sa suppression.
#
# try/except pour ne JAMAIS bloquer le demarrage de l'editeur si un module a un souci.
try:
    import ue5_utils
    print("[init_unreal] ue5_utils pre-charge OK")
except Exception as e:
    print(f"[init_unreal] ERREUR chargement ue5_utils: {e}")
