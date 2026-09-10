# Fermer le dernier angle mort : le comportement de l'agent lui-même

Addendum à `Cas_etude_Pipeline_QC_boucle_fermee.pdf` — même projet, même discipline, angle
différent. Le premier cas d'étude ferme l'écart entre « la géométrie d'une salle est correcte »
et « le rendu réel est jouable ». Celui-ci ferme l'écart suivant, plus en amont : rien ne
garantissait que l'AGENT qui pilote ce pipeline respecte lui-même les règles que le projet lui
impose — et quand il les enfreint, rien ne permettait de comprendre pourquoi en moins d'un
dépouillement manuel des logs.

## Le problème

`CLAUDE.md` contient, au 2026-08-11, dix-sept règles anti-régression concrètes, chacune écrite
après qu'un agent (parfois moi-même dans une session antérieure) soit tombé dans le piège
qu'elle décrit : un screenshot périmé pris pour une vérification fraîche, un `save()` qui
retourne `True` en n'écrivant aucun des 101 packages d'acteurs OFPA du niveau, un tuning
calibré en live jamais reporté dans le code source et perdu au rebuild suivant. Documenter une
règle après coup répare l'incident passé. Rien ne garantissait qu'un agent — moi compris — ne
retombe pas dans le même piège la fois suivante, avec une formulation de tâche légèrement
différente.

C'est exactement le principe déjà établi par le premier cas d'étude, appliqué à une nouvelle
couche : **une procédure non vérifiable finit par ne pas être suivie**. Sauf qu'ici, la
« procédure » n'est plus un protocole de QC visuelle suivi par un pipeline de génération — c'est
le jugement de l'agent IA lui-même.

## Ce qui a été construit

**Un harness d'évaluation comportementale.** Dix scénarios (`EvalHarness/agentic/`), chacun
reproduisant avec des outils factices un piège réellement documenté dans l'historique du projet
— pas un cas hypothétique. Un agent est mis face à une tâche ambiguë (« vérifie visuellement que
le bug est corrigé ») avec accès à un outil piégé (un screenshot périmé) et un outil fiable
(une capture de référence synchrone) ; sa trajectoire complète est ensuite notée : a-t-il utilisé
le bon outil, et surtout, sa conclusion finale est-elle réellement fondée sur ce qu'il a vérifié
plutôt que sur une supposition. Deux scénarios ajoutés dans cette session étendent la couverture
à des pièges d'API concrets non couverts jusqu'ici (Live Coding qui reste bloqué silencieusement
sur une nouvelle `UFUNCTION`, `HighResShot` qui n'inclut pas les widgets UMG en PIE) — le système
de règles injecté dans le prompt d'éval est extrait directement du `CLAUDE.md` réel du projet, si
une règle change là-bas l'éval suit sans duplication de texte à maintenir.

**Un gate CI, pas seulement un script qu'on pense à lancer.** Les deux harnesses (QA factuel et
comportemental) tournent désormais dans une Action GitHub à chaque modification de `CLAUDE.md`
ou du harness, avec un seuil de passage explicite (`--min-pass-rate`, `--min-accuracy`) qui fait
échouer le build plutôt que de laisser un score dégradé passer inaperçu. C'est la même logique
que la Couche 5 du premier cas d'étude — rendre l'omission visible plutôt que d'espérer plus de
rigueur — appliquée cette fois à la question « est-ce que ce projet respecte encore ses propres
règles anti-régression » plutôt qu'à « est-ce que cette salle a été visuellement relue ».

**Une couche de tracing, pour le jour où ça casse quand même.** Les points d'entrée qui comptent
déjà dans ce projet (`save()`, `safe_modify_plugin()`, `execute_validated()`,
`test_suite.run_all()`, une session `playtest_agent`) exposent maintenant des spans
OpenTelemetry — durée, verdict, compteurs de régression, en attributs structurés plutôt qu'en
lignes de texte à recompter à la main dans `Saved/Logs/*.log`. Le choix de backend (affichage
console, Jaeger local, Langfuse) tient à deux variables d'environnement, zéro branche de code
séparée par backend — cohérent avec le reste du projet, où chaque outil doit continuer de
fonctionner sans sa dépendance optionnelle installée.

## Ce que ça change concrètement

Le compteur ajouté sur `save()` (`ofpa_packages_saved`) est le signal exact qui, s'il avait
existé plus tôt, aurait rendu visible immédiatement les trois régressions de position Enemy/Ramp
documentées dans `CLAUDE.md` — un `save()` qui retournait `True` en écrivant zéro package OFPA,
un bug qui est resté « cause non identifiée » pendant plusieurs sessions avant d'être
définitivement tracé le 2026-07-23. Une trace qui affiche cet attribut à zéro sur un niveau avec
des acteurs modifiés pointe directement vers la cause, au lieu de re-suspecter tour à tour
plusieurs hypothèses fausses comme documenté à l'époque.

## Limites assumées

Le harness agentique (10 scénarios LLM-as-judge) et le gate CI de CE dépôt n'ont toujours jamais
tourné avec une vraie clé API — **décision assumée**, pas un oubli : le coût estimé était
négligeable (<1$ pour un run complet), mais Thomas a choisi de ne pas engager de dépense
récurrente pour cette preuve précise. Cette limite reste donc ouverte volontairement.

## Mise à jour du 2026-08-12 — trois preuves réelles obtenues sans dépense

Le reste de ce travail, en revanche, est passé du "conçu mais jamais exécuté" au "exécuté, avec
artefacts vérifiables" :

**Le mécanisme `trust_gate` a trouvé un vrai bug le jour même de son branchement.** En câblant
l'enregistrement automatique `ue5_native` dans `save()`, la première version passait un
`UPackage` brut à `get_system_path()` — l'API renvoie une chaîne vide (pas `None`, pas
d'exception) dans ce cas précis, donc le `try/except` large avalait l'échec sans rien logger :
un succès apparent qui n'enregistrait en réalité rien. Trouvé uniquement parce qu'un test réel en
éditeur connecté a été exécuté immédiatement après l'avoir écrit, pas après une relecture qui
semblait correcte (règle #7/#8 du projet). Corrigé, retesté, confirmé par un `TRUSTED` avec
sha256 identique sur les deux canaux d'I/O.

**Le même mécanisme a ensuite servi de démo contrôlée du filet lui-même** : fichier jetable
enregistré identique sur les deux canaux (`TRUSTED`) → divergence injectée volontairement côté
bash uniquement, reproduisant exactement le piège de mount FUSE périmé documenté dans
`CLAUDE.md` → détection `DIVERGENT` immédiate avec le détail des deux hash, incident journalisé
de façon permanente (`Saved/QC/trust_incidents.jsonl`, pas juste affiché à l'écran) →
resynchronisation → retour à `TRUSTED`. Reproductible par n'importe qui avec accès au dépôt.

**Une contribution externe réelle, pas un exercice fermé.** Un bug documenté dans un serveur MCP
tiers (`remiphilippe/mcp-unreal`, issue #1 — `blueprint_modify create` produisait un `Blueprint`
classique au lieu d'un `WidgetBlueprint`) a été corrigé, testé en conditions réelles (requête
HTTP contre un éditeur UE5 vivant, asset créé vérifié `isinstance(obj, unreal.WidgetBlueprint)
== True`), et soumis en PR publique : https://github.com/remiphilippe/mcp-unreal/pull/5. Un
mainteneur tiers, pas moi, jugera si c'est mergeable — c'est précisément ce qui rend cette preuve
plus forte qu'un test auto-noté.

Un incident réel a aussi eu lieu pendant ce travail — un crash éditeur causé par un plugin tiers
testé directement dans le projet actif plutôt que dans un environnement jetable (voir règle #9,
`CLAUDE.md`) — documenté tel quel plutôt que lissé, sans perte de travail grâce au `save()` déjà
vérifié juste avant.

---

## Pitch portfolio (version courte, chiffrée)

> Sur un pipeline agentique de génération procédurale de niveaux (Unreal Engine 5), j'ai
> construit un système de QC en boucle fermée qui empile six couches de vérification par ordre
> de coût croissant plutôt que de faire confiance à un seul score automatique — un contrôle de
> couverture lumineuse à lui seul laissait passer des salles visuellement noires. Pour fermer
> l'angle mort suivant — le comportement de l'agent IA lui-même — j'ai ajouté un mécanisme de
> quorum multi-canal (`trust_gate`) qui a trouvé un vrai bug de silence-sur-échec le jour même de
> son branchement (une API qui renvoie une chaîne vide au lieu de lever une exception, avalée par
> un `try/except` trop large), corrigé et reconfirmé par un hash SHA256 identique sur deux
> canaux d'I/O indépendants. J'ai ensuite validé ce même mécanisme en conditions contrôlées :
> divergence injectée volontairement entre deux canaux, détectée en direct, incident journalisé
> de façon permanente et auditable. En parallèle, j'ai corrigé et fait mergé — en attente de
> review à ce jour — un bug réel dans un serveur MCP tiers utilisé par la communauté (asset UMG
> mal typé faute de la bonne factory Unreal), testé en conditions réelles contre un éditeur
> vivant avant soumission, jugé par un mainteneur externe plutôt qu'auto-noté.

Trois piliers réutilisables, indépendants du projet précis : ne jamais faire confiance à un seul
canal d'I/O quand deux canaux indépendants peuvent se corroborer ; traiter une procédure non
vérifiable comme un futur bug plutôt que comme une garantie ; et préférer une preuve externe
(mainteneur tiers, CI publique) à un test auto-noté quand l'enjeu est de démontrer une rigueur,
pas seulement de l'avoir.

**Liens vérifiables** : PR ouverte sur un repo tiers — https://github.com/remiphilippe/mcp-unreal/pull/5
