# Dessin libre — porter l'outil Unity sur UE5

Portage du mode « Dessin libre » de `LevelDesignTools` (Unity) dans le plugin
`BlockoutTools`. Au lieu de saisir des dimensions dans le panneau, on clique le contour de
la salle au sol, on le ferme, et la hauteur suit la souris jusqu'au clic de validation.

**État au 2026-08-26.** Le C++ compile et lie ; `test_suite.run_all()` rend 65/65, dont
12/12 sur les propriétés géométriques — vérifiées par deux chemins indépendants (le harnais
hors moteur `Tools/GeometryTests`, 40 000 contours en 0,9 s, et `test_blockout_panels.py`
avec les vrais types Unreal).

⚠️ **Ce qui suit décrit l'interaction telle qu'elle est CODÉE, pas telle qu'elle a été
observée** : rien du viewport n'a encore été vu à l'écran. Voir « Ce qu'il reste à vérifier »
en bas — c'est six points, trois minutes.

## Utilisation prévue

`Tools → Outil Blockout` → section **Dessin libre (contour cliqué dans le viewport)**.

1. Régler le nom, le plan de dessin (Z), la hauteur de départ, l'épaisseur, sol/plafond.
2. **Activer le mode dessin** — le viewport passe en mode « Dessin Blockout » (il apparaît
   aussi dans la barre de modes de l'éditeur, à côté de Select / Landscape / Foliage).
3. Dans le viewport :

| Geste | Effet |
|---|---|
| Clic gauche | pose un point du contour, sur le plan Z |
| Clic sur le 1er point (≤ 12 px) ou `Entrée` | ferme le contour, passe au réglage de hauteur |
| `Retour arrière` | supprime le dernier point |
| `Échap` | efface le contour ; un 2e `Échap` (contour vide) quitte le mode |
| Souris (étape hauteur) | règle la hauteur ; **tirer sous le plan extrude vers le bas** |
| `Ctrl` (étape hauteur) | aimante sur le pas de grille de l'éditeur |
| Clic gauche ou `Entrée` (étape hauteur) | génère la salle |
| `Échap` (étape hauteur) | revient à l'édition du contour, sans rien perdre |

Le contour, la prévisualisation du prisme et la hauteur en cours s'affichent dans le
viewport ; les instructions sont rappelées en surimpression en haut à gauche.

## Ce qui est généré

- **Murs** : un cube d'échelle par arête du contour, via `SpawnScaledRotatedCube` — la même
  primitive que tous les autres outils du panneau.
- **Sol / plafond** : un `AStaticMeshActor` par panneau, avec un **asset `UStaticMesh`
  généré** sous `/Game/Blockout/GeneratedMeshes/`, et un composant `UBlockoutPanelComponent`
  qui porte l'état logique (contour, épaisseur, axes, trous).

Tout part dans un dossier d'Outliner `Blockout/<Nom>`. Un seul `Ctrl+Z` retire la salle
entière — **mais pas les assets de mesh générés**, qui restent dans le Content Browser
(même limite que côté Unity).

Collision des panneaux : `CTF_UseComplexAsSimple`. C'est ce qui permettra, une fois la
découpe portée, de traverser réellement une porte percée — une collision simple
reboucherait le trou.

## Pourquoi c'est architecturé comme ça

**Le mesh est une conséquence, pas la source de vérité.** `UBlockoutPanelComponent` garde la
description logique du panneau (son contour, son épaisseur, la liste de ses trous) et le
maillage est régénéré à partir de là. C'est ce qui permettra de percer vingt ouvertures dans
un même mur sans dégradation, d'en retirer une, ou d'annuler — la version Unity a fait
exactement ce choix, après avoir constaté qu'un mesh troué ne permet plus de retrouver ni
les dimensions d'origine ni les trous déjà percés.

**Le noyau géométrique ne touche à aucune API moteur.** `BlockoutPanelGeometry.{h,cpp}`
n'utilise que `FVector2D` / `FMath` : pas de `UStaticMesh`, pas de `FMeshDescription`, pas
de `GEditor`. La frontière avec Unreal vit entièrement dans `BlockoutPanelMeshAsset.cpp`.
Ce n'est pas cosmétique : dès qu'une ligne de géométrie dépend de l'éditeur, elle n'est plus
vérifiable qu'en lançant l'éditeur, et une boucle de test à 40 secondes cesse d'être une
boucle de test.

**Un mode d'édition, pas un bouton de plus.** Capter un clic *dans* le viewport (et empêcher
la sélection normale pendant ce temps) n'est possible que depuis un `UEdMode`. Le panneau
Slate reste l'endroit où l'on règle les valeurs ; le mode ne fait que les lire.

## Fichiers

```
Plugins/BlockoutTools/Source/BlockoutTools/
  Public/BlockoutPanelGeometry.h      noyau pur : triangulation + assemblage 3D
  Private/BlockoutPanelGeometry.cpp   (portage de PolygonTriangulator.cs + PanelMeshBuilder.cs)
  Public/BlockoutPanelComponent.h     état logique d'un panneau, sérialisé avec l'acteur
  Public/BlockoutPanelMeshAsset.h     seule frontière avec Unreal : écriture de l'asset UStaticMesh
  Public/BlockoutDrawSettings.h       réglages partagés panneau <-> mode
  Public/BlockoutDrawMode.h           mode d'édition viewport
  Private/BlockoutDrawMode.cpp        interaction (clics, touches, dessin)
  Private/BlockoutDrawMode_Generate.cpp  construction des acteurs
  Private/BlockoutGeometrySubsystem_Panel.cpp  pont UFUNCTION vers le noyau pur (tests Python)

Content/Python/test_blockout_panels.py  vérification par propriétés du noyau géométrique
```

## Vérification

```python
import test_blockout_panels; test_blockout_panels.run_all()
```

Également agrégé dans `test_suite.run_all()` (une ligne `blockout_panels`).

Ce harnais est le portage de `Tools/GeometryTests` du projet Unity. Il ne vérifie pas une
liste de résultats figés mais des **propriétés** qui doivent tenir pour n'importe quelle
entrée :

- l'aire triangulée égale l'aire du contour moins celle des trous ;
- un point dans la matière est couvert par **exactement un** triangle, un point dans un trou
  par **aucun** ;
- le maillage est un prisme recto-verso dont l'épaisseur est exactement celle demandée ;
- aucun triangle ne se replie sur un seul texel (seuil **relatif** à la taille du panneau) ;
- deux appels rendent la même surface au bit près.

Plus trois cas de régression repris des bugs réellement trouvés côté Unity : invariance à la
position dans le niveau, arête quasi-horizontale près d'un trou, 21 ouvertures dans un même
mur. Chacun correspond à du code qui compilait et qui avait l'air correct à l'écran.

## Ce qu'il reste à vérifier — à l'écran, six points

Fait : compilation, lien, `test_suite.run_all()` → 65/65, propriétés géométriques 12/12 par
deux chemins indépendants. Aucun de ces tests ne touche au viewport. Restent :

1. **Le mode existe** — barre de modes du Level Editor, à côté de Select / Landscape /
   Foliage : une entrée « Dessin Blockout ». C'est le seul point que rien ne peut vérifier
   par script : l'enregistrement d'un `UEdMode` n'est pas exposé à Python.
2. **Le clic pose un point au bon endroit**, à toutes les distances de caméra. Le point se
   projette sur le plan Z réglé dans le panneau — pas sur la géométrie sous le curseur.
3. **La fermeture du contour** : le premier point passe au vert quand le curseur est à
   moins de 12 px. Est-ce confortable, ou faut-il élargir ?
4. **La hauteur suit la souris** sans dépendre du zoom, et **tirer sous le plan extrude vers
   le bas**. `Ctrl` aimante sur le pas de grille de l'éditeur.
5. **Persistance** : sauver le niveau, le rouvrir, vérifier que le sol est toujours là avec
   son mesh. C'est exactement le piège que l'asset `UStaticMesh` est censé éviter — un
   maillage créé à la volée disparaît à la réouverture.
6. **Normales** : les panneaux doivent être éclairés correctement des deux côtés. Le maillage
   est émis recto ET verso, normales posées à la main, recalcul moteur désactivé. **Si les
   panneaux ressortent noirs, c'est ici qu'il faut regarder en premier** — pas dans la
   triangulation, qui est couverte.

## Comment ça s'est passé (pour la prochaine fois)

Quatre erreurs de compilation puis une de lien, sur trois tours de `Build.bat` :

| Erreur | Nature |
|---|---|
| `PI` utilisé comme nom de variable | macro moteur — le **portage** a créé le bug, il n'existait pas dans la source C# |
| `SSpinBox<float>` incomplet (×2) | le header du panneau ne fait que le déclarer ; déréférencer exige l'include |
| `SCheckBox::IsChecked()` comparé à `ECheckBoxState` | il rend un `bool` ; c'est `GetCheckedState()` qui rend l'énum |
| `EKeys::*` non résolus | dépendance de **module** (`InputCore`), invisible à la lecture d'un `.cpp` |

Les 12 APIs qui avaient été vérifiées contre les en-têtes réels d'UE 5.8 étaient toutes
justes, dont `DrawWireSphere` qui a changé de header en 5.8. Les trois symboles non vérifiés
sont exactement les trois qui ont cassé — tous jugés « évidents » sur le moment. D'où le
harnais hors moteur : déplacer la boucle de vérification hors du rebuild.

## Écarts assumés vs. la version Unity

- **UV en [0,1]** sur la face, comme côté Unity : une texture damier apparaîtra étirée sur
  un grand sol. Sans conséquence sur le matériau de blockout par défaut, à revoir si l'outil
  sert à autre chose que du volume.
- **Le champ « Hauteur » du panneau ne suit pas la souris en direct.** Unity mettait le champ
  à jour pendant le réglage ; ici la valeur est écrite dans les réglages à la validation, le
  panneau la relira au prochain démarrage.
- **Le survol de la Découpe se fait au lancer de rayon**, là où Unity utilisait
  `HandleUtility.PickGameObject`. Conséquence concrète : une surface masquée par un autre
  volume n'est pas sélectionnable tant qu'on ne la voit pas. Unity, lui, pouvait accrocher
  une face cachée. Écart assumé : viser ce qu'on voit est le comportement attendu ici.
- **La Découpe n'accepte que les boîtes**, comme Unity, et par le même test
  triangle-par-triangle (`IsBoxMesh`). Le test naïf sur les bornes acceptait les cylindres
  — c'est le bug corrigé côté Unity, et le portage garde la correction.
