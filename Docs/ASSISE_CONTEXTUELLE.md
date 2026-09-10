# Assise contextuelle — spécification

Projet : GameAnimationSample (UE 5.8) · Créé le 2026-08-28 · Statut : en développement

> Le joueur peut s'asseoir partout où la géométrie le permet, à **n'importe quelle hauteur**
> dans le domaine défini plus bas — pas sur une liste de paliers.

---

## 1. Le domaine : ce que « quand c'est possible » veut dire

Une surface est **sittable** quand elle satisfait *toutes* ces conditions. Ce ne sont pas des
paliers d'animation, c'est la frontière du domaine — en dehors, il ne se passe rien.

**Tout est proportionnel à la taille du personnage**, jamais écrit en dur. Ce qui est sittable
dépend de qui s'assoit : un rebord de 80 cm est un siège pour un adulte et un mur pour un enfant.
La hauteur est lue sur la capsule de collision du pawn, et `MakeDomainForCharacter()` en dérive
tout le reste. Les valeurs de la colonne de droite sont celles obtenues pour le mannequin UE
standard (176 cm) — elles changent d'elles-mêmes si le mannequin change.

| Paramètre | Proportion | Mannequin UE | Raison |
|---|---|---|---|
| Hauteur de siège | 0 à 0,50 × H | 0 à 88 cm | **Le sol compte comme siège.** Au-dessus de la hauteur de hanche, il faut grimper d'abord |
| Profondeur de surface | ≥ 0,20 × H | 35 cm | Sans quoi on est assis sur une arête |
| Largeur de surface | ≥ 0,25 × H | 44 cm | Largeur de bassin plus une marge |
| Inclinaison | ≤ 44° | 44° | La pente qu'on peut arpenter debout. Un angle : il ne dépend pas de la taille |
| Dégagement au-dessus | ≥ 0,75 × H | 132 cm | Ne pas s'asseoir sous un plafond bas |
| Portée de jambe | 0,27 × H | 47 cm | Au-dessus, les pieds ne touchent plus : les jambes pendent |

**Bord ou intérieur.** Si le sol devant la surface descend de plus de 40 cm dans les 40 cm qui
suivent le bord, les jambes pendent. Sinon elles se replient. Personne ne s'assoit au centre
d'une grande plateforme les jambes dans le vide — il n'y a pas de vide.

## 2. Architecture

**Détection** — une sonde trace vers l'avant depuis le buste, puis vers le bas, cherche une
surface dans la bande de hauteur, vérifie le domaine ci-dessus, et sort une transform de siège
plus deux informations : la hauteur normalisée et le drapeau bord/intérieur.

**Approche** — Motion Warping (déjà utilisé par la traversée) déforme l'animation d'installation
pour que le bassin arrive exactement sur la transform de siège.

**Pose** — un Blend Space piloté par la hauteur normalisée sur un axe et le drapeau bord/intérieur
sur l'autre. **Les échantillons du Blend Space ne sont pas des paliers** : ce sont les extrémités
d'une interpolation continue, invisibles individuellement au joueur, exactement comme les
échantillons d'un Blend Space de locomotion.

**Contact** — une passe d'IK par-dessus : bassin sur la surface réelle, pieds sur le sol effectif
sous le siège. `CR_UEFN_Mannequin_FullBodyIK` existe déjà dans le projet, sur le squelette du
pawn joueur.

## 3. Ce qui est reporté, et assumé

- **La fidélité visuelle n'est pas un critère.** Décidé le 2026-08-29 : ce qui compte est que le
  joueur *puisse* s'asseoir partout où la géométrie le permet. Une animation déformée est
  acceptable, une assise refusée ne l'est pas. `bWithinAnimationRange` mesure la couverture
  animée mais **ne doit jamais servir de verrou** — s'en servir pour bloquer une assise
  contredirait directement l'intention.
- **Les deux poses d'ancrage** (assise au sol, assise haute jambes pendantes) restent
  souhaitables pour le rendu, mais ne conditionnent plus rien.
- **La première personne** : hors périmètre, décidé le 2026-08-28.
- **S'adosser** aux surfaces trop hautes pour s'asseoir : bonne idée, pas maintenant.

## 4. Critères d'acceptation

Chaque critère se vérifie par le **vrai chemin de jeu** : input injecté par
`UScriptedInputLibrary`, jamais par un appel direct qui court-circuite ce qu'on prétend tester.
`SetActorLocation` sur le pawn reste interdit (CLAUDE.md piège 7).

### AC-1 — On ne s'assoit que là où c'est valide

- **Déclencheur réel** : le joueur fait face à une surface, appui sur la touche d'assise.
- **Résultat observable** : il s'assoit si et seulement si la surface satisfait le domaine.
  Sur une surface hors domaine — trop haute, trop étroite, trop inclinée, plafond bas — **rien
  ne se passe**, et le personnage reste en locomotion normale.
- **Vérification** : PIE réelle sur un jeu de cubes de dimensions connues placés par script.
  **Les cas négatifs comptent autant que les positifs** : un système qui accepte tout passerait
  les cas positifs sans rien valoir.

### AC-2 — Le bassin suit la hauteur, continûment

- **Déclencheur réel** : assise successive sur des cubes de hauteurs croissantes couvrant tout
  le domaine — de 0 à 0,50 × H — par pas de 5 cm.
- **Résultat observable** : la hauteur du bone `pelvis` suit celle du siège à ±8 cm près, et
  **croît de façon monotone** avec elle. Aucun palier : deux hauteurs de siège distinctes ne
  doivent jamais produire la même hauteur de bassin.
- **Vérification** : PIE, mesure de `GetSocketLocation("pelvis")` après stabilisation, pour
  chaque hauteur. C'est **le critère qui prouve la continuité** — celui qu'un système à bandes
  échouerait.

### AC-3 — Les pieds sont posés, ou pendent

- **Déclencheur réel** : assise sur un cube de hauteur h, sol plat à z = 0.
- **Résultat observable** : si h est inférieure à la longueur de jambe, les pieds touchent le sol
  à ±5 cm. Sinon ils sont au-dessus du sol et sous le niveau du siège — ils pendent, ils ne
  traversent pas.
- **Vérification** : PIE, mesure de `foot_l` et `foot_r`.

### AC-4 — Se relever ne laisse aucune trace

- **Déclencheur réel** : appui sur la touche d'assise pendant que le personnage est assis.
- **Résultat observable** : il se relève, la locomotion reprend, le `SkeletalMeshComponent`
  **n'est pas resté en `AnimationSingleNode`**, et le personnage n'est immobile à aucun moment
  plus de 3 s après la fin de l'animation.
- **Vérification** : PIE. C'est le piège 5 de `CLAUDE.md` appliqué au joueur au lieu des PNJ —
  la même famille de bug, sur le pawn dont dépend tout le reste.

### AC-5 — Interrompre pendant l'installation ne casse rien

- **Déclencheur réel** : input de déplacement injecté **pendant** l'animation d'installation,
  avant qu'elle se termine.
- **Résultat observable** : la transition s'annule, le personnage revient debout, la locomotion
  répond immédiatement. Pas de pose figée, pas de capsule coincée dans la géométrie.
- **Vérification** : PIE, injection de l'input dans la fenêtre de transition. **C'est le test le
  plus important de la liste** : c'est l'action que le joueur fera par accident cent fois, et
  c'est là que ce genre de système casse.

## 5. Ordre de travail

1. Git en place, premier commit. *(fait par Thomas, hors agent)*
2. ~~La sonde de détection en C++~~ **Fait le 2026-08-29.** `USitSurfaceLibrary`, domaine dérivé
   de la capsule du personnage. `test_sit_detection.py` : 10 cas sur 10, positifs et négatifs.
3. ~~La capacité d'assise greffée sur le pawn~~ **Fait le 2026-08-29.** `USitAbilityComponent` +
   `USitAbilitySubsystem`, qui l'attache au pawn joueur à l'exécution — **aucun asset du sample
   n'est modifié**. Input Action créée à l'exécution, donc rien n'est ajouté au Content non plus.
   `test_sit_action.py` : 5 sur 5, la capacité répond depuis le pawn réel en PIE.

   ~~*Reste sur AC-1*~~ **AC-1 est complet le 2026-08-29.** `test_sit_action.py` : 5 sur 5,
   touche injectée dans Enhanced Input, assise sur un cube de 60 cm (mesurée 63,9), relevé à la
   seconde pression, et **rien du tout devant un mur de 150 cm** avec le motif `TropHaut`.
   *Reste sur l'assise elle-même* : le personnage entre dans l'état assis mais n'est ni déplacé
   vers le siège ni figé. Les deux passent par les mécanismes natifs de Mover —
   `queue_layered_move_activation` pour le déplacement, `queue_next_mode` pour le gel.
4. L'installation : Motion Warping vers la transform de siège, animation de banc uniquement.
5. La passe d'IK de contact, puis AC-2 et AC-3.
6. AC-4 et AC-5 — se relever et interrompre.
7. Les poses d'ancrage, quand elles arrivent : la plage supportée s'élargit, les critères ne
   bougent pas.

## Journal des décisions

| Date | Décision | Raison |
|---|---|---|
| 2026-08-28 | Détection procédurale, pas de SmartObjects posés à la main | Dans un monde en cubes, chaque rebord est un siège potentiel ; les annoter un par un serait absurde |
| 2026-08-28 | Continuité par Blend Space + IK, pas par bandes | Une bande se voit ; une interpolation entre ancrages ne se voit pas |
| 2026-08-28 | Plage supportée exposée en constante C++ lue par les tests | Les critères restent vrais quand les animations d'ancrage arrivent |
| 2026-08-28 | Première personne hors périmètre | Le motion matching bouge trop la tête ; c'est un chantier de stabilisation à part |
| 2026-08-29 | Domaine dérivé de la capsule du personnage, pas de constantes | Un rebord de 80 cm est un siège pour un adulte et un mur pour un enfant ; le domaine doit suivre le mannequin |
| 2026-08-29 | Le sol est une surface d'assise valide | Demande explicite : « ça peut être le sol ou bien sur des cubes en hauteur » |
| 2026-08-29 | La couverture animée ne bloque jamais une assise | La capacité prime sur le rendu ; un refus est un bug, une animation approximative n'en est pas un |
| 2026-08-29 | Hauteur du siège mesurée par rapport au **plan** du sol, pas à son altitude | Sur une pente, le sol devant est plus haut sans être un rebord ; l'altitude seule le faisait passer pour un siège surélevé, puis échouer au contrôle de profondeur |
| 2026-08-29 | Pente maximale portée de 15° à 44° | Règle voulue : ce sur quoi on tient debout, on doit pouvoir s'y asseoir |
| 2026-08-29 | Contrôle d'accessibilité du siège (motif `Obstrue`) | La sonde validait une surface située derrière un mur mince, en mesurant le sol de l'autre côté |

## Limite connue, non résolue

Sur une rampe à 25°, la hauteur de siège mesurée est de **27 cm** au lieu du zéro théorique : la
correction par le plan du sol rattrape l'essentiel du dénivelé, pas la totalité. Sans conséquence
aujourd'hui — l'assise est acceptée et 27 cm reste loin des 88 cm du domaine — mais sur une pente
plus forte ou une distance de sonde plus grande, ce résidu finirait par franchir le plafond et
provoquer un refus injustifié. À reprendre si des pentes raides apparaissent dans le level design.
