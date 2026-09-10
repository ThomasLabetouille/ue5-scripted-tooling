# Protocole de test manuel — Panneau "Outil Blockout" (outils Slate-only)

**Date** : 2026-08-10
**Périmètre** : les 4 outils du panneau `Tools → Outil Blockout` dont le cœur de calcul dépend
d'une sonde de scène réelle (line trace / sweep contre de la géométrie déjà placée) ou d'une
transform caméra réelle — FOV/Frustum, Vérificateur de pente, Vérificateur de hauteur libre,
Gabarit de référence. Ce document formalise leur seul mode de validation possible aujourd'hui,
cohérent avec la règle n°9 du panneau (CLAUDE.md) : **critère de validation = test à l'écran DANS
L'ÉDITEUR, pas un test scripté.**

## Pourquoi ces 4 outils restent hors automatisation

Suite au chantier du 2026-08-10 (voir `Docs/AUDIT_DETTE_TECHNIQUE_BlockoutTools.md`), 3 des 7
outils encore Slate-only à cette date ont eu leur cœur de calcul extrait vers
`UBlockoutGeometrySubsystem` et sont désormais testés automatiquement (Alignement sur grille,
Duplication en série, Générateur de plan 2D — voir `test_blockout_tools.py`). Une vérification du
code réel (pas une estimation à distance) a montré que les 4 outils restants ne s'y prêtent pas :
ils lisent tous une géométrie de niveau ou une transform caméra AU MOMENT de l'appel — les rendre
testables en pur Python demanderait de mocker une scène + une caméra, un effort jugé disproportionné
par rapport au risque réel (ce sont des indicateurs visuels en lecture seule, pas des générateurs
qui créent de la géométrie durable). Ce protocole comble le vrai manque : une checklist répétable,
au lieu d'un test à l'œil improvisé à chaque fois.

## Quand exécuter ce protocole

- Après toute modification de `SBlockoutToolPanel_Overlays.cpp` (FOV, Pente, Hauteur libre) ou de
  `SBlockoutToolPanel_Actions.cpp` (Gabarit de référence).
- Après tout rebuild complet du plugin `BlockoutTools` touchant à un de ces 4 outils.
- Avant une livraison au level designer externe (`Tools/Package_BlockoutTools.ps1`).
- Setup recommandé pour les 4 : ouvrir `/Game/ThirdPerson/Lvl_ThirdPerson`, se placer dans une
  zone avec à la fois du sol plat, un mur/rebord franchissable, et si possible un intérieur avec
  plafond bas (pour tester le cas "rouge" de la Hauteur libre) — la zone de spawn du joueur
  convient pour la plupart des cas.

---

## 1. Simulateur FOV / Frustum

**Précondition** : un viewport perspective actif dans l'éditeur.

| # | Étape | Résultat attendu |
|---|-------|-------------------|
| 1 | Cliquer "Utiliser la caméra" (remplit Position/Yaw/Pitch depuis le viewport actuel) | Les champs Position/Yaw/Pitch se remplissent avec des valeurs cohérentes avec la vue actuelle du viewport |
| 2 | Activer l'overlay frustum | Un frustum (4 lignes convergeant vers l'apex + un rectangle à `Distance` UU) se dessine dans le viewport, aligné visuellement avec la direction de la caméra |
| 3 | Changer `FOV` (ex. 60° → 100°) | Le rectangle du frustum s'élargit visiblement, sans se déformer de travers |
| 4 | Changer `Distance` | Le rectangle recule/avance le long de l'axe de vue, sa taille change proportionnellement (perspective correcte) |
| 5 | Cliquer "Aller à cette vue" (`OnJumpToFovViewClicked`) | Le viewport SAUTE à la position/rotation/FOV saisis — vérifier que la vue obtenue correspond visuellement au frustum dessiné juste avant |
| 6 | Cliquer "Utiliser la caméra" après avoir bougé le viewport | Les champs se remettent à jour avec les nouvelles valeurs (pas figés sur le premier appel) |

**Point d'attention spécifique** (voir `SBlockoutToolPanel_Actions.cpp`, commentaire ligne ~363) :
l'API `FEditorViewportClient::SetViewLocation/SetViewRotation/ViewFOV/FOVAngle` utilisée par
"Aller à cette vue" n'a jamais été reconfirmée par une compilation réelle récente — si l'étape 5
échoue ou ne fait rien, vérifier en premier que ces membres existent toujours tels quels dans la
version d'UE5.8 utilisée.

## 2. Vérificateur de pente

**Précondition** : se placer au-dessus d'une zone avec à la fois du terrain plat ET une pente ou
un escalier existant (ou en générer un rapidement avec l'outil Rampe pour le test).

| # | Étape | Résultat attendu |
|---|-------|-------------------|
| 1 | Activer l'overlay pente | Une grille de marqueurs colorés (vert/rouge) apparaît, plaquée au sol sous la caméra |
| 2 | Survoler du sol plat | Marqueurs VERTS |
| 3 | Survoler une pente/rampe au-delà du seuil marchable | Marqueurs ROUGES sur la partie trop raide |
| 4 | Se déplacer lentement dans le viewport | L'overlay suit la caméra avec un léger délai (throttle ~0.1-0.15s attendu, PAS de lag important ni de scintillement à chaque frame) |
| 5 | Passer au-dessus d'un vide/trou (aucune géométrie sous la caméra) | Aucun marqueur erroné à Z=0 ou ailleurs — soit rien ne s'affiche, soit un état neutre explicite, jamais un faux vert/rouge sur du vide |
| 6 | Entrer dans un intérieur avec plafond bas puis ressortir | La sonde ne transperce pas le plafond pour toucher un sol extérieur plus bas (piège documenté dans CLAUDE.md : une sonde à 1 seul impact partie de trop haut rate un plafond) |

## 3. Vérificateur de hauteur libre

**Précondition** : une zone avec un plafond bas quelque part (couloir, arche, sous un escalier)
et une zone à plafond haut/ciel ouvert pour le contraste.

| # | Étape | Résultat attendu |
|---|-------|-------------------|
| 1 | Activer l'overlay hauteur libre | Marqueurs colorés au sol, mêmes conventions que le Vérificateur de pente |
| 2 | Survoler une zone à ciel ouvert / plafond haut | Marqueurs VERTS |
| 3 | Survoler une zone à plafond bas (< hauteur de capsule joueur) | Marqueurs ROUGES |
| 4 | Vérifier la limite : se placer pile sous un rebord à hauteur proche de la capsule joueur | Le changement vert/rouge se produit au bon seuil (comparer avec la métrique réelle affichée par le Gabarit de référence, section 4, pour la même zone) |
| 5 | Zone sans plafond détectable (sonde plafond ne touche rien dans sa portée) | Comportement explicite et cohérent (pas de rouge par défaut sur une absence de plafond — l'absence de contrainte devrait lire comme "vert"/praticable) |

## 4. Gabarit de référence

**Précondition** : viewport pointé vers une zone de sol dégagée.

| # | Étape | Résultat attendu |
|---|-------|-------------------|
| 1 | Cliquer "Poser le gabarit" | Deux acteurs apparaissent : un volume (capsule joueur, `_Corps`) posé AU SOL sous la caméra (pas flottant en l'air), et une dalle fine plus large (`_SautMax`) à la hauteur de saut atteignable |
| 2 | Comparer visuellement le volume `_Corps` à `BP_RPGCharacter` réel (spawné ou en PIE) | Dimensions cohérentes (même rayon de capsule, même hauteur — le gabarit ne doit ni écraser ni surdimensionner le joueur réel) |
| 3 | Lire le message de statut affiché après le clic | Les valeurs annoncées (rayon×hauteur capsule, hauteur de saut max) doivent avoir une SOURCE cohérente (PIE si une session tourne, sinon CDO/défauts — pas de valeur aberrante type 0 ou négative) |
| 4 | Poser le gabarit au-dessus d'un vide (aucun sol sous la caméra à portée de sonde) | Le gabarit se pose quand même à une position de repli sensée (pas d'acteur à Z=-5000 ou autre valeur absurde issue d'un échec de sweep silencieux) |
| 5 | Cliquer "Annuler" juste après | Les 2 acteurs du gabarit disparaissent proprement (c'est un repère jetable, pas un ajout permanent au niveau) |

---

## Journal des exécutions

Consigner ici la date et le résultat (OK / KO + détail) à chaque passage complet du protocole —
objectif : transformer "testé une fois en session X" en historique consultable, plutôt qu'une
confiance qui s'évapore d'une session à l'autre (voir CLAUDE.md, règle anti-régression n°5).

| Date | Outils testés | Résultat | Notes |
|------|---------------|----------|-------|
| — | — | — | Protocole créé le 2026-08-10, pas encore exécuté une seule fois — à faire au prochain passage de Thomas dans l'éditeur. |
