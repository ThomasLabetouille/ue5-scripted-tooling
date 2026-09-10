# GeometryTests — propriétés du noyau géométrique, hors moteur

Portage de `Tools/GeometryTests` du projet Unity `LevelDesignTools`.

```
run.bat        # ~1 s, 40 000 contours ; rend 0 si tout passe
run.bat 100    # passe plus longue quand on chasse un cas rare
```

## Pourquoi

`BlockoutPanelGeometry.cpp` ne dépend d'aucune API Unreal — uniquement `FVector2D`,
`FVector3f` et `FMath`. Une centaine de lignes de doublures (`shims/CoreMinimal.h`)
suffisent donc à le compiler et à l'exercer hors moteur. Ce répertoire compile **les
fichiers du projet**, pas des copies : si la géométrie se met à utiliser un type Unreal
absent des doublures, ce harnais ne compile plus et on le sait immédiatement.

L'intérêt : une passe de 40 000 panneaux prend une seconde, contre un rebuild du plugin
(éditeur fermé, ~2 min) plus un aller-retour dans l'éditeur.

## Ce qui est vérifié

Pas une liste de résultats figés, mais des propriétés qui doivent tenir pour n'importe
quelle entrée :

- l'aire triangulée égale l'aire du contour moins celle des trous ;
- un point dans la matière est couvert par exactement un triangle, un point dans un trou
  par aucun ;
- le maillage est un prisme recto-verso d'épaisseur exacte, UV bornés à [0,1] ;
- aucun triangle sous le seuil de dégénérescence, qui est **relatif** à la taille du
  panneau ;
- aucune T-jonction : aucun sommet posé au milieu de l'arête d'un triangle voisin ;
- deux appels rendent la même surface au bit près.

Plus trois régressions figées, reprises de bugs réellement trouvés côté Unity.

## Calibration — la partie qui coûte

Deux seuils ont dû être recalés sur le **contrat réel du code**, après avoir crié au loup :

- Le code écarte un triangle quand `|2·Aire| <= bbox × 1e-9`. Un test plus strict que ça
  reproche au code de faire ce qu'il annonce. Sur 329 289 triangles émis, le plus petit
  fait 3,1e-9 de la bbox — conforme.
- Sous l'`Epsilon` du triangulateur (1e-3 UU), deux lignes de tranche fusionnent
  volontairement et un éclat d'aire disparaît. Les panneaux dont deux ordonnées sont plus
  proches que ça sont **écartés et comptés** (7 sur 12 000), pas comptés comme des échecs.

Et un bug du harnais lui-même, trouvé avant qu'il ne serve : le générateur de contours
tirait N angles uniformes puis les triait, ce qui ne donne un polygone simple que si les
sommets font le tour du centre. 57 contours auto-intersectants sur 2 000 étaient rapportés
comme des erreurs d'aire du code testé. Le générateur pose désormais un sommet par secteur
angulaire, et `IsGeneratedCaseValid` — écrit ici, sans appeler `IsSimplePolygon` du code
testé — vérifie chaque cas produit.

## Ce que ça ne couvre pas

Tout ce qui touche l'éditeur : `BlockoutPanelMeshAsset` (écriture de l'asset), le mode
`UBlockoutDrawMode` (interaction viewport), les composants. Ceux-là passent par
`Content/Python/test_blockout_panels.py` (mêmes propriétés, mais avec les vrais types
Unreal) et par le test à l'écran.

⚠️ `run.bat` (MSVC) n'a pas encore été exécuté sur Windows — le harnais est développé et
lancé sous g++. Si `cl.exe` bronche, c'est côté doublures qu'il faut regarder, pas côté
géométrie.
