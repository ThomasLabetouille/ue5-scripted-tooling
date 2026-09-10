# Eval Harness

Deux harnesses distincts dans ce dossier :

- **Ici (`run_eval.py`)** : QA factuel générique — le modèle connaît-il la bonne réponse.
- **`agentic/`** : évalue le COMPORTEMENT de l'agent Claude qui pilote ce projet UE5 —
  respecte-t-il les règles anti-régression de `CLAUDE.md` (vraie vérification PIE, `save()`
  correct, tuning en code source, vrai `Build.bat`...). Voir `agentic/README.md`.

Ce README couvre le harness QA factuel.

## Setup

```bash
pip install -r requirements.txt --break-system-packages
export ANTHROPIC_API_KEY=sk-...
```

## Lancer un eval

```bash
python run_eval.py --dataset datasets/sample_qa.jsonl --model claude-sonnet-5 --scorer llm_judge
```

Options principales :
- `--model` : modèle évalué (défaut `claude-sonnet-5`)
- `--system` / `--system-file` : prompt système à tester
- `--scorer` : `exact_match`, `contains`, `llm_judge` (défaut) ou `none`
- `--judge-model` : modèle utilisé comme juge (défaut `claude-haiku-4-5-20251001`, rapide/pas cher)
- `--concurrency` : nombre de requêtes en parallèle (défaut 5)
- `--limit` : ne lancer que les N premiers items (pour itérer vite)
- `--output` : chemin du rapport JSON (défaut `results/run_<timestamp>.json`)

## Format du dataset (JSONL)

```json
{"id": "1", "input": "What is the capital of France?", "expected": "Paris", "category": "geography"}
```

- `input` (obligatoire) : le prompt envoyé au modèle
- `expected` (optionnel) : réponse de référence — sans elle, l'item est juste enregistré, pas noté
- `category` (optionnel) : pour le breakdown par catégorie dans le résumé
- `id` (optionnel) : sinon assigné automatiquement

## Comparer deux prompts ou deux modèles

Relancer avec des `--output` différents, puis comparer les `summary.overall_accuracy` /
`summary.by_category` des deux rapports JSON. Exemple :

```bash
python run_eval.py --dataset datasets/sample_qa.jsonl --model claude-sonnet-5 \
  --output results/sonnet.json
python run_eval.py --dataset datasets/sample_qa.jsonl --model claude-haiku-4-5-20251001 \
  --output results/haiku.json
```

## Étendre

- Ajouter un scorer : nouvelle fonction dans `scoring.py` suivant la signature
  `(response, expected, item, client=None) -> {"score": float, "detail": str}`, puis l'ajouter
  au dict `SCORERS`.
- Ajouter des items : append au JSONL, un objet par ligne.
- `results/` contient l'historique des runs (rapport complet + résumé) — utile pour tracker les
  régressions dans le temps.
