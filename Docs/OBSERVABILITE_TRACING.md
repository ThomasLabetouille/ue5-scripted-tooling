# Observabilité — tracing OpenTelemetry de l'agent Python

## Pourquoi

Avant ce système, déboguer une panne de l'agent (une régression détectée par
`safe_modify_plugin()`, un playtest qui échoue à un waypoint, un `execute_validated()` qui
renvoie `ECHEC`) voulait dire rejouer la session à l'oeil dans `Saved/Logs/*.log` — un flux
texte plat, sans durée par étape, sans lien explicite entre "ce test a échoué" et "à quel
moment / dans quel appel exact". `tracing.py` (`Content/Python/tracing.py`) ajoute une couche
de traces structurées par-dessus les points d'entrée qui comptent déjà dans ce projet :
`save()`, `safe_modify_plugin()`, `execute_validated()`, `test_suite.run_all()`,
`capture_reference_screenshot()`, et les sessions `playtest_agent.py`.

**Non-négociable pour ce projet** : le pipeline agent doit continuer de fonctionner à
l'identique si `opentelemetry` n'est pas installé — voir le pattern d'import défensif dans
`tracing.py` (même famille que `bpes()`/`nes()`/`room()` dans `ue5_utils.py`). Une session sans
le SDK installé tourne exactement comme avant, juste sans traces.

## Installation (optionnelle)

```
pip install -r Content/Python/requirements-tracing.txt --break-system-packages
```

Voir le commentaire en tête de ce fichier requirements pour la nuance Python embarqué UE5 vs
Python système — à installer dans l'interpréteur que `PythonScriptPlugin` charge réellement,
pas nécessairement celui du `PATH` système.

## Choisir un backend — trois variables d'environnement, zéro changement de code

| `UE5_AGENT_TRACE_EXPORTER` | Effet | Setup requis |
|---|---|---|
| `console` (défaut) | Spans affichés dans l'Output Log / stdout | Aucun |
| `otlp` | Envoi OTLP/HTTP vers `OTEL_EXPORTER_OTLP_ENDPOINT` | Un collecteur qui parle OTLP |
| `none` | Désactivé | — |

### Option A — Jaeger local (le plus rapide à essayer)

```bash
docker run -d --name jaeger -p 16686:16686 -p 4318:4318 jaegertracing/all-in-one:latest
```

```
set UE5_AGENT_TRACE_EXPORTER=otlp
set OTEL_EXPORTER_OTLP_ENDPOINT=http://localhost:4318
```

UI des traces : http://localhost:16686 (choisir le service `rpg_test-ue5-agent`).

### Option B — Langfuse (cloud ou self-hosted)

Langfuse ingère de l'OTLP nativement — mêmes variables, juste un endpoint et un header d'auth
différents, aucune branche de code séparée dans `tracing.py` :

```
set UE5_AGENT_TRACE_EXPORTER=otlp
set OTEL_EXPORTER_OTLP_ENDPOINT=https://cloud.langfuse.com/api/public/otel
set OTEL_EXPORTER_OTLP_HEADERS=Authorization=Basic <base64(public_key:secret_key)>
```

(`echo -n "pk-lf-...:sk-lf-..." | base64` pour construire la valeur du header.)

### Désactiver totalement (CI, perf)

```
set UE5_AGENT_TRACE_EXPORTER=none
```

## Ce qui est instrumenté aujourd'hui

| Fonction | Fichier | Attributs de span notables |
|---|---|---|
| `save()` | `ue5_utils.py` | `umap_save_ok`, `ofpa_packages_saved` — le compteur qui aurait rendu visible les 3 régressions de position Enemy/Ramp documentées dans `CLAUDE.md` (save qui renvoie `True` en écrivant 0 package OFPA) |
| `safe_modify_plugin()` | `ue5_utils.py` | `baseline_test_count`, `after_test_count`, `regression_count`, `regressions`, `verdict` |
| `execute_validated()` | `ue5_utils.py` | `source`, `code_len`, `exec_raised_exception`, `regression_count`, `verdict` (parsé depuis la ligne `--- VERDICT : ... ---` du rapport) |
| `test_suite.run_all()` | `test_suite.py` | `passed`, `failed`, `skipped`, `total`, `failing_tests` — appelé deux fois par `safe_modify_plugin()`/`execute_validated()` (avant/après), donc visible comme deux spans enfants dans la même trace |
| `capture_reference_screenshot()` | `ue5_utils.py` | `duration_ms`, `ok` |
| session `playtest_agent.py` | `playtest_agent.py` | span longue durée ouvert à `start_playtest()`, fermé à `_finish_session()` : `reason`, `waypoints_reached`/`waypoints_total`, `events_count`, `player_final_hp` ; chaque `_record_event()` (téléportation, action déclenchée, HP changé, ennemi vaincu...) devient un **event** horodaté dans la trace |

## Exemple concret — déboguer une régression en lisant une trace, pas les logs

Scénario : `safe_modify_plugin()` lève `REGRESSION DETECTEE` après un changement de plugin.

1. Ouvrir la trace correspondante (Jaeger : filtrer par service `rpg_test-ue5-agent`, span
   racine `safe_modify_plugin`).
2. Le span racine affiche directement `regression_count` et `regressions` en attribut — pas
   besoin de grep le log pour retrouver la liste des tests cassés.
3. Les deux spans enfants `test_suite.run_all` (baseline / après) montrent `passed`/`failed`
   par run et leur durée respective — comparer les deux `failing_tests` donne la liste exacte
   des tests qui sont passés de OK à FAIL, sans recompter à la main.
4. Si la régression touche un test qui appelle lui-même `capture_reference_screenshot` ou
   `save()`, ces spans apparaissent nested avec leurs propres attributs (`duration_ms`,
   `ofpa_packages_saved`) — la trace pointe directement vers l'étape qui a une durée anormale
   ou un attribut à zéro inattendu, plutôt que de rejouer toute la session.

C'est la différence pratique visée : une trace donne la chronologie + les attributs en un seul
écran, contre plusieurs allers-retours dans `Saved/Logs/*.log` pour reconstituer la même
information à la main.

## Limites assumées

- `SimpleSpanProcessor` (export synchrone, pas de batching) — choix délibéré : le volume
  d'appels de ce projet est faible (quelques dizaines par session), la fiabilité de l'export
  prime sur le débit. À revoir si le volume de spans augmente significativement.
- Aucune corrélation automatique entre une trace et une session Cowork/`ue5_execute`
  particulière — `source` sur `execute_validated()` est le seul lien texte libre disponible
  aujourd'hui. Ajouter un vrai trace-id partagé entre les deux côtés serait l'extension
  naturelle si le besoin de corrélation se confirme.
- Pas d'instrumentation du panneau in-editor Claude AI (Groq) — seul le tooling Python MCP
  (`ue5_utils.py`, `test_suite.py`, `playtest_agent.py`) est couvert, cohérent avec la
  recommandation de `CLAUDE.md` de préférer `mcp__ue5-mcp__*` pendant une session Cowork.
