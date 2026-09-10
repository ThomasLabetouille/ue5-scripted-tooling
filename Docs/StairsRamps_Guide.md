# ⚠️ Ce guide a été remplacé

Ce fichier décrivait un panneau Python (avec un menu déroulant « Action ») qui **n'existe plus** :
il a été supprimé le 2026-07-30, ainsi que les fichiers qu'il mentionnait
(`blockout_tool_panel.py`, `room_corridor_gen.py`, `stairs_ramps_menu.py`,
`blockout_tools_menu.py`).

**Le guide à jour est ici : [`Guide_Outil_Blockout.md`](Guide_Outil_Blockout.md)**

Il couvre les 16 outils du panneau actuel (menu **Tools → Outil Blockout**), dont les escaliers et
les rampes qui faisaient l'objet de ce document.

---

Note technique : le module de calcul `Content/Python/stairs_ramps.py` existe toujours et reste
utilisable depuis la console Python pour du script ou du debug. Sa math (formule de Blondel,
calcul d'angle de rampe) a été portée en C++ dans le panneau, qui est désormais le chemin
recommandé. Détails dans `GAME_MEMORY.md`, session 2026-07-30.
