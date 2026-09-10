# Outil Blockout — Guide du level designer

Tout se passe dans **un seul panneau** : menu **Tools → Outil Blockout**.
Aucun code, aucun script, aucun Blueprint à ouvrir. Le panneau s'ancre à côté du viewport et
reste utilisable pendant que tu travailles.

À l'ouverture, tu vois une liste de titres repliés. Clique sur un titre pour déplier l'outil.

**Trois réflexes à connaître avant de commencer :**

1. Presque tous les outils ont un bouton **📍 Utiliser ma position actuelle** : il recopie la
   position de ta caméra dans les champs. Place-toi dans le viewport là où tu veux générer, clique,
   puis génère — c'est plus rapide que de taper des coordonnées.
2. En bas du panneau, **↩ Annuler** supprime la dernière génération, et **🎯 Sélectionner** la
   re-sélectionne pour la déplacer au gizmo. Ça ne touche que la dernière génération.
3. Tes valeurs sont **conservées quand tu fermes l'éditeur**. Pas besoin de tout retaper demain.

Les distances sont en **unités Unreal (UU)**. Repère utile : le personnage mesure ~176 UU de haut,
donc **100 UU ≈ 1 mètre**.

---

## Générer de la géométrie

### Salle
Quatre murs, un sol, un plafond. Position = centre de la salle, au sol.
Coche **Sans plafond** si tu veux voir dedans depuis le dessus — c'est le réglage le plus pratique
pendant qu'on travaille.

### Couloir
Un couloir entre deux points. Le bouton caméra remplit le point de **départ** ; l'arrivée est à
régler à la main. Utile pour relier deux salles déjà posées.

### Dalle / Plancher
Un plancher seul, sans murs. Pose-le à une hauteur intermédiaire au milieu d'une salle plus haute
pour créer une mezzanine ou un étage.

### Escalier
Donne juste la **hauteur totale à franchir** : le nombre de marches, leur hauteur et leur
profondeur sont calculés automatiquement pour rester confortables et réellement praticables par le
personnage. Position = pied de l'escalier, au sol. **Direction (yaw)** = orientation en degrés
(0 = vers +X, 90 = vers +Y).

### Rampe
Comme l'escalier, mais en pente lisse. Remplis **soit** la longueur horizontale, **soit** l'angle
voulu — si tu remplis l'angle, il est prioritaire. Le panneau t'avertit si la pente est trop raide
pour que le personnage puisse la monter.

### Cône de vision IA
Un secteur plat au sol, en éventail, pour visualiser le champ de vision d'un ennemi. Position =
le sommet (l'œil de l'ennemi), Direction = là où il regarde, Portée = jusqu'où il voit.
C'est un **repère de composition**, pas un système de détection : il ne rend pas l'ennemi voyant.

### Arche de pont
Deux piliers, une voûte courbe, un tablier par-dessus.
**Portée** = distance entre les piliers, **Flèche** = hauteur de la courbe, **Voussoirs** = nombre
de segments de l'arc (plus il y en a, plus la courbe est lisse).

### Tunnel courbé
Un couloir qui tourne. **Rayon de courbure** = plus il est grand, plus le virage est large.
**Angle signé** : positif tourne d'un côté, négatif de l'autre. Si le virage part du mauvais côté,
inverse simplement le signe.

### Générateur depuis un plan 2D
Dessine ton plan dans n'importe quel logiciel d'image : **les traits sombres deviennent des murs**,
le reste est ignoré.

Tu peux fournir soit une texture importée dans le projet, soit directement un fichier image sur
ton disque. Si les deux sont remplis, la texture importée gagne.

- **UU par pixel** : l'échelle. À 10, une image de 200 px de large fait 2000 UU (20 m).
- **Résolution** : la finesse de lecture. Baisse-la pour plus de fidélité, augmente-la si trop de
  murs sont générés.
- **Seuil de noir** : ce qui compte comme « sombre ». Augmente-le si des traits sont ignorés,
  baisse-le si le fond est pris pour un mur.
- **Dalle de sol** : optionnelle, ajoute un plancher sous tout le plan.

Le plan est centré sur la Position. Commence par un plan simple (traits épais sur fond blanc) pour
valider l'échelle avant de lancer un plan détaillé. Si l'outil refuse de générer en annonçant trop
de murs, augmente la Résolution.

---

## Vérifier ce que tu as construit

Ces trois outils dessinent des repères **par-dessus** la scène, en direct. Ils ne créent rien et ne
modifient rien : décoche la case et tout disparaît.

Quand un de ces outils est actif, son titre affiche **● actif** — même section repliée, tu sais
qu'il tourne.

### Vérificateur de pente
Colore le sol autour de la caméra : **vert** = le personnage peut gravir, **rouge** = trop raide,
il glissera.

Laisse le champ vide pour utiliser la pente maximale réelle du personnage. Saisis une valeur pour
tester un autre seuil.

Deux choses à savoir pour ne pas se méprendre :
- La sonde descend à la verticale et mesure **le sol sur lequel on se tiendrait**. Elle ne colore
  pas les faces verticales des murs (elle en voit le dessus, qui est plat).
- Un blockout fait de boîtes et de rampes douces est **légitimement tout vert**. C'est normal, pas
  une panne. Pour voir du rouge, baisse le seuil sous la pente de tes rampes, ou teste sur du
  relief raide.

### Vérificateur de hauteur libre
Le pendant du précédent, pour la hauteur : **vert** = le personnage passe debout, **rouge** =
plafond trop bas, passage bloqué.

C'est l'outil qui attrape les couloirs écrasés, les dessous d'arche trop justes et les dalles
posées trop près du sol — des erreurs invisibles en vue de dessus. Laisse le champ vide pour
utiliser la hauteur réelle du personnage. Un endroit à ciel ouvert est toujours vert.

### Simulateur FOV / Frustum
Deux usages :
- **📷 Aller à cette vue** téléporte la caméra du viewport à une position et une orientation
  données, avec le champ de vision réel du jeu — pour voir exactement ce que le joueur verra en
  arrivant à cet endroit.
- La case **Afficher le frustum** dessine le cône de vision en filaire, visible depuis un autre
  angle (une vue de dessus, par exemple) : pratique pour vérifier ce qui entre dans le cadre sans
  quitter ta vue de travail.

### Métriques Gameplay
Cercles jaune/rouge autour des ennemis (portées de détection et d'attaque) et ligne verte
indiquant la hauteur de saut maximale du joueur.

### Gabarit de référence
Pose sous la caméra un repère physique aux dimensions réelles du personnage, plus une dalle fine à
sa hauteur de saut max. Sert à juger une échelle ou à savoir si un rebord est franchissable, à
l'œil, sans lancer le jeu.

C'est un objet jetable : supprime-le avec **↩ Annuler** quand tu as fini.

---

## Passer du blockout à l'art final

### Swap intelligent
Remplace **toutes** les boîtes de blockout d'un même type par le mesh d'art définitif, d'un coup.

1. Tape dans **Identifiant** un morceau du nom des boîtes à remplacer (par exemple
   `Boîte_Porte_A` — toutes les boîtes dont le nom contient ce texte seront prises, y compris les
   copies suffixées `_2`, `_3`…).
2. Choisis le mesh final dans le sélecteur.
3. Clique **🔍 Compter** pour vérifier combien de boîtes correspondent. **Fais-le toujours avant de
   remplacer** : un identifiant trop court attrape plus de boîtes que prévu.
4. Clique **♻ Remplacer**.

Position, rotation, tags et nom sont conservés. Le mesh garde **sa taille d'origine** : il n'est
jamais étiré, parce qu'un kit d'art modulaire est conçu à la bonne dimension.

⚠️ C'est la seule action **destructive** du panneau. Pour l'annuler, utilise **Ctrl+Z** dans le
viewport (et non le bouton Annuler du panneau, qui ne sait pas ressusciter des boîtes supprimées).

---

## Organiser et accélérer ton travail

### Alignement sur grille
Le bouton caméra remplit une position brute (ex. 1237,42) qui n'est alignée sur rien. Sans
conséquence tant que c'est du blockout — mais le jour où le **Swap intelligent** remplace ces
boîtes par un kit d'art modulaire, des meshes non alignés ne se raccordent pas entre eux (joints
ouverts, chevauchements).

- Coche **Aligner automatiquement à la génération** : chaque nouvelle génération arrondit sa
  position d'origine au multiple de la **Taille de grille** choisie (100 UU par défaut). Ça ne
  touche que le point d'ancrage de la forme, jamais ses sous-parties (les marches d'un escalier ou
  les voussoirs d'une arche gardent leurs positions relatives).
- **📐 Aligner la sélection sur la grille** : arrondit la position d'acteurs déjà posés et
  sélectionnés dans le viewport. Utile pour rattraper une salle placée avant que tu n'actives la
  case ci-dessus. Ctrl+Z pour annuler.

### Rangement automatique dans l'Outliner
Chaque génération va maintenant dans son propre dossier `Blockout/<Nom>` dans l'Outliner, au lieu
de s'entasser à la racine. Rien à faire de ton côté — c'est automatique, basé sur le champ Nom que
tu remplis pour chaque outil.

### Duplication en série
Sélectionne un ou plusieurs acteurs déjà posés dans le viewport, règle **Nombre de copies**,
**Espacement** et **Direction (yaw)**, puis clique **🧱 Dupliquer la sélection**. Chaque acteur
sélectionné est copié N fois le long de cette direction, à intervalle régulier — pratique pour une
rangée de piliers, une palissade, ou des segments de couloir en ligne que tu poserais sinon un par
un. Les copies gardent le mesh, les matériaux et les tags de l'original, et rejoignent son dossier
dans l'Outliner. Ctrl+Z pour annuler.

---

## En cas de souci

**La ligne de statut en bas du panneau explique toujours ce qui vient de se passer** — succès en
vert, problème en rouge, avec la raison. Commence par la lire.

Les cas les plus fréquents :

| Symptôme | Cause probable |
|---|---|
| « Aucun viewport perspective actif » | Clique une fois dans le viewport 3D pour lui donner le focus. |
| Le générateur de plan ne trouve aucun mur | Seuil de noir trop bas, ou l'image n'a pas de traits assez foncés. |
| Le générateur de plan refuse de générer | Trop de murs : augmente la Résolution. |
| Le swap ne trouve rien | L'identifiant ne correspond à aucun nom d'acteur. Vérifie l'orthographe dans l'Outliner. |
| Un vérificateur reste tout vert | C'est probablement normal — relis la section correspondante ci-dessus. |

Une modification du panneau lui-même (nouvel outil, correction) demande une **recompilation C++
avec l'éditeur fermé** : ça passe par Thomas.
