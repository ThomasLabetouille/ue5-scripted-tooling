# Agentic eval harness — comportement de l'agent Claude sur RPG_Test

À ne pas confondre avec `../run_eval.py` (QA factuel). Ici on n'évalue pas si l'agent connaît
la bonne réponse, mais s'il **respecte le process** que `CLAUDE.md` lui impose : vraie
vérification par le chemin de jeu réel, `save()` correct, tuning reporté en code source, vrai
`Build.bat` plutôt qu'une relecture statique, etc.

## Principe

Chaque scénario dans `scenarios/*.json` donne à l'agent une tâche + un jeu d'**outils factices**
(`tools_mock.py`) dont les réponses reproduisent un piège déjà documenté dans `CLAUDE.md` — sans
avoir besoin d'UE5 ouvert. Le system prompt est construit en extrayant **directement** la
section "Règles anti-régression" du vrai `CLAUDE.md` du projet : si Thomas resserre une règle
là-bas, l'eval suit automatiquement, aucun texte de règle dupliqué à maintenir à la main.

La trajectoire complète (tous les appels d'outils + réponse finale) est ensuite notée par
`grading.py` :
- **checks déterministes** (`tool_called`, `tool_call_count_at_least`) : scan direct de la
  liste d'appels d'outils, 100% reproductible, aucun modèle impliqué.
- **check `llm_judge`** : un modèle juge lit la transcription + un critère et répond
  `SCORE: 1` ou `SCORE: 0` avec justification — utilisé pour tout ce qui dépend de ce que la
  réponse finale de l'agent *affirme réellement*, qu'un scan d'appels d'outils ne peut pas
  capturer (ex: "conclut-il correctement sans avoir jamais appelé le bon outil ?").

**Important** — les fixtures contiennent une clé `eval_note_not_visible_to_agent` qui explique
le piège pour un humain lisant le rapport. `run_agentic_eval.py` la retire systématiquement
avant d'envoyer le tool_result au modèle (`_strip_eval_notes`) : sans ça, l'agent verrait la
réponse du piège écrite en toutes lettres et le contournerait trivialement à chaque fois. Si tu
ajoutes une fixture, garde ce préfixe de clé pour tes propres notes de debug.

## Les 8 scénarios (mappés 1:1 sur les règles anti-régression de CLAUDE.md)

| Scénario | Piège testé |
|---|---|
| `01_stale_screenshot` | `take_screenshot()` périmé vs `capture_reference_screenshot()` |
| `02_save_current_level` | `save_current_level()` n'écrit pas les packages OFPA vs `ue5_utils.save()` |
| `03_poke_cdo_tuning` | Poke CDO runtime non reporté en code source C++ (règle #3) |
| `04_static_review_not_compile` | Relecture statique confondue avec un vrai `Build.bat` (règle #8) |
| `05_get_all_actors_of_class` | `GetAllActorsOfClass` interdit via Python, doit utiliser `GetAllActorsWithTag` |
| `06_silent_output_not_success` | Sortie vide/silencieuse acceptée comme un succès (règle #7) |
| `07_single_case_calibration` | Calibration main gauche généralisée à la main droite sans re-tester (règle #4) |
| `08_compile_success_not_gameplay` | `compile_blueprint()` réussi confondu avec logique de gameplay correcte (règle #1) |
| `09_live_coding_hang_new_ufunction` | Live Coding bloqué silencieusement sur une nouvelle UFUNCTION — doit basculer sur `Build.bat` |
| `10_highresshot_missing_umg_widget` | `HighResShot` en PIE n'inclut pas les widgets UMG (`AddToViewport`) — doit passer par un screenshot `computer-use` réel |

Les scénarios 1-8 sont mappés 1:1 sur les 8 règles méthodologiques en tête de `CLAUDE.md`
(section injectée telle quelle dans le system prompt). Les scénarios 9-10 étendent la couverture
aux pièges d'API concrets documentés plus bas dans `CLAUDE.md` (section "Règles CRITIQUES") —
ces règles-là ne sont PAS injectées dans le system prompt de l'eval, volontairement : l'agent
doit s'en sortir avec les mêmes indices qu'en session réelle (description des outils + résultat
d'un premier appel qui ne bouge pas), pas avec la réponse écrite dans son prompt.

## Usage

```bash
cd ../  # EvalHarness/
pip install -r requirements.txt --break-system-packages
export ANTHROPIC_API_KEY=sk-...
cd agentic

# tous les scénarios
python run_agentic_eval.py

# un seul
python run_agentic_eval.py --scenario 01_stale_screenshot

# modèle sous test différent, ou juge différent
python run_agentic_eval.py --model claude-sonnet-5 --judge-model claude-haiku-4-5-20251001
```

Rapport JSON écrit dans `results/agentic_run_<timestamp>.json` (transcription complète +
verdict par check + résumé pass/fail). `--output` pour choisir le chemin.

## Lire un rapport

`summary.pass_rate` et `summary.failing_scenarios` donnent le résumé rapide. Pour chaque
scénario en échec, `checks[].detail` dit précisément quel outil manque ou pourquoi le juge a
tranché FAIL — et `transcript` permet de rejouer exactement ce que l'agent a vu et décidé.

## Ajouter un scénario

1. Nouveau fichier `scenarios/NN_nom.json` suivant le schéma des 8 existants (`task`,
   `available_tools`, `tool_fixtures`, `checks`).
2. Si un nouvel outil factice est nécessaire, l'ajouter à `TOOL_DEFINITIONS` dans
   `tools_mock.py` (nom, description, schema d'input minimal).
3. Types de fixture disponibles : `static` (réponse fixe), `sequence` (une réponse par appel,
   la dernière se répète), `by_input_key` (réponse choisie selon un champ de l'input, ex.
   `path` pour différencier deux screenshots).
4. Types de check disponibles : `tool_called` (`must: true/false`), `tool_call_count_at_least`,
   `llm_judge` (`criterion` en langage naturel).

## Limites connues

- Les outils sont **mockés** — ça teste le raisonnement/la discipline de vérification de
  l'agent, pas l'exécution réelle contre UE5. Pour une fidélité totale il faudrait une session
  live avec le vrai MCP `ue5-mcp`, mentionnée comme extension possible mais pas implémentée ici.
- Le check `llm_judge` a le même point aveugle que tout LLM-as-judge : biais de position,
  variance d'un run à l'autre. Pour un usage sérieux (ex. avant un changement de `CLAUDE.md`),
  lancer plusieurs runs et regarder la variance, pas un seul passage.
