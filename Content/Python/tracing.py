"""tracing.py — instrumentation OpenTelemetry pour l'agent Python qui pilote RPG_Test.

Objectif concret : pouvoir déboguer une panne (régression détectée par safe_modify_plugin(),
playtest qui échoue un waypoint, execute_validated() qui renvoie ECHEC) en lisant UNE trace —
la chronologie exacte des appels, leurs durées, leurs attributs et le point précis où ça a
dérapé — plutôt qu'en rejouant la session à l'oeil dans les logs UE5 (Saved/Logs/*.log, un seul
flux texte plat sans structure ni durée par étape).

Design pensé pour tourner DANS l'interpréteur Python embarqué d'UE5.8, où l'installation
d'`opentelemetry` n'est pas garantie :
  - import 100% optionnel — si le SDK n'est pas installé, chaque span devient un no-op
    transparent (même pattern défensif que bpes()/nes()/room() dans ue5_utils.py : ne jamais
    faire planter le pipeline agent pour un outil d'observabilité).
  - un seul TracerProvider process-wide, initialisé paresseusement au premier span utilisé.
  - export configurable par variable d'environnement, sans changer une ligne d'instrumentation
    quel que soit le backend choisi :

      UE5_AGENT_TRACE_EXPORTER=console   (défaut) — affiche les spans dans stdout (visible dans
          la Output Log de l'éditeur) — zéro dépendance externe, zéro service à lancer.
      UE5_AGENT_TRACE_EXPORTER=otlp — envoie en OTLP/HTTP vers OTEL_EXPORTER_OTLP_ENDPOINT.
          Marche tel quel avec un collecteur OTel local (Jaeger, Tempo, otel-collector) ET avec
          Langfuse, qui ingère de l'OTLP nativement (endpoint
          https://cloud.langfuse.com/api/public/otel, Basic Auth via
          OTEL_EXPORTER_OTLP_HEADERS="Authorization=Basic <base64(public_key:secret_key)>") —
          donc pas de branche de code séparée par backend, juste deux variables d'env.
      UE5_AGENT_TRACE_EXPORTER=none — désactive totalement (utile en CI/tests, overhead nul).

Voir Docs/OBSERVABILITE_TRACING.md pour l'installation et un exemple de debug pas à pas.

Usage :
    from tracing import traced, span, record_verdict

    @traced("save_level")
    def save():
        ...

    with span("playtest_waypoint", waypoint=2) as sp:
        sp.set_attribute("teleport_ok", True)
"""
import functools
import os
import re
import time

_TRACER = None
_INIT_DONE = False
_INIT_LOCK_NAME = "_tracing_init_in_progress"  # simple re-entrancy guard, no threading here


def _service_name():
    return os.environ.get("UE5_AGENT_SERVICE_NAME", "rpg_test-ue5-agent")


def _init_tracer():
    """Lazily build the OTel TracerProvider. Never raises — returns None on any failure,
    which downstream code treats as "tracing disabled"."""
    global _TRACER, _INIT_DONE
    if _INIT_DONE:
        return _TRACER
    _INIT_DONE = True  # only attempt once per process, even if it fails

    try:
        from opentelemetry import trace
        from opentelemetry.sdk.trace import TracerProvider
        from opentelemetry.sdk.trace.export import (
            SimpleSpanProcessor, ConsoleSpanExporter,
        )
        from opentelemetry.sdk.resources import Resource
    except ImportError:
        print("[tracing] opentelemetry non installé — tracing désactivé (no-op). "
              "Voir Docs/OBSERVABILITE_TRACING.md pour l'installer dans le Python embarqué UE5.")
        return None

    exporter_kind = os.environ.get("UE5_AGENT_TRACE_EXPORTER", "console").lower()
    resource = Resource.create({"service.name": _service_name()})
    provider = TracerProvider(resource=resource)

    if exporter_kind == "none":
        # Provider créé mais sans processor : les spans se créent (coût quasi nul) et
        # disparaissent sans jamais être exportés. Utile pour ne pas bifurquer le code
        # d'instrumentation entre CI et session interactive.
        pass
    elif exporter_kind == "otlp":
        try:
            from opentelemetry.exporter.otlp.proto.http.trace_exporter import OTLPSpanExporter
            endpoint = os.environ.get("OTEL_EXPORTER_OTLP_ENDPOINT")
            if not endpoint:
                print("[tracing] UE5_AGENT_TRACE_EXPORTER=otlp mais OTEL_EXPORTER_OTLP_ENDPOINT "
                      "absent — repli sur ConsoleSpanExporter.")
                provider.add_span_processor(SimpleSpanProcessor(ConsoleSpanExporter()))
            else:
                provider.add_span_processor(SimpleSpanProcessor(OTLPSpanExporter()))
        except ImportError:
            print("[tracing] opentelemetry-exporter-otlp-proto-http non installé — "
                  "repli sur ConsoleSpanExporter. pip install opentelemetry-exporter-otlp-proto-http")
            provider.add_span_processor(SimpleSpanProcessor(ConsoleSpanExporter()))
    else:  # "console" ou valeur inconnue → repli sûr
        provider.add_span_processor(SimpleSpanProcessor(ConsoleSpanExporter()))

    trace.set_tracer_provider(provider)
    _TRACER = trace.get_tracer(_service_name())
    print(f"[tracing] initialisé — service={_service_name()} exporter={exporter_kind}")
    return _TRACER


class _NoOpSpan:
    """Retourné quand le SDK OTel est indisponible ou désactivé — a la même surface d'API
    que le strict minimum utilisé ici (set_attribute/add_event/record_exception/set_status),
    pour que le code appelant n'ait jamais besoin de tester "si le tracing est actif"."""
    def set_attribute(self, *a, **k): pass
    def add_event(self, *a, **k): pass
    def record_exception(self, *a, **k): pass
    def set_status(self, *a, **k): pass
    def end(self, *a, **k): pass
    def __enter__(self): return self
    def __exit__(self, *a): return False


class span:
    """Context manager span, no-op transparent si le tracing n'est pas dispo.

    with span("save_level", level="Lvl_ThirdPerson") as sp:
        ...
        sp.set_attribute("ofpa_packages_saved", n)
    """
    def __init__(self, name, **attrs):
        self.name = name
        self.attrs = attrs
        self._cm = None
        self._span = None

    def __enter__(self):
        tracer = _init_tracer()
        if tracer is None:
            self._span = _NoOpSpan()
            return self._span
        self._cm = tracer.start_as_current_span(self.name)
        self._span = self._cm.__enter__()
        for k, v in self.attrs.items():
            try:
                self._span.set_attribute(k, v)
            except Exception:
                self._span.set_attribute(k, str(v))
        return self._span

    def __exit__(self, exc_type, exc, tb):
        if exc is not None:
            try:
                self._span.record_exception(exc)
                from opentelemetry.trace import StatusCode
                self._span.set_status(StatusCode.ERROR, str(exc))
            except Exception:
                pass
        if self._cm is not None:
            return self._cm.__exit__(exc_type, exc, tb)
        return False


def traced(name=None, **static_attrs):
    """Décorateur : enveloppe un appel de fonction dans un span nommé `name` (défaut : nom de
    la fonction). Ajoute duration_ms, ok=True/False et exception_type en attributs
    automatiquement — pas besoin de les poser à la main dans chaque fonction décorée."""
    def deco(fn):
        span_name = name or fn.__name__

        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            t0 = time.time()
            with span(span_name, **static_attrs) as sp:
                try:
                    result = fn(*args, **kwargs)
                    sp.set_attribute("ok", True)
                    return result
                except Exception as e:
                    sp.set_attribute("ok", False)
                    sp.set_attribute("exception_type", type(e).__name__)
                    raise
                finally:
                    sp.set_attribute("duration_ms", round((time.time() - t0) * 1000, 1))
        return wrapper
    return deco


_VERDICT_LINE_RE = re.compile(r"VERDICT\s*:\s*(.+)")
# Tokens réellement produits par execute_validated(), les plus spécifiques en premier (une
# comparaison par préfixe : "REGRESSION DETECTEE (2 test(s)) ---" doit matcher le token
# "REGRESSION DETECTEE", pas s'arrêter au premier "(" comme le faisait un regex plus naïf
# testé et rejeté ici — voir Docs/OBSERVABILITE_TRACING.md, il ne matchait aucun cas réel
# à part "INCONNU" par défaut.
_KNOWN_VERDICT_TOKENS = ("REGRESSION DETECTEE", "ECHEC", "INCONNU", "OK")


def record_verdict(sp, report_text):
    """Extrait le verdict de la ligne '--- VERDICT : ... ---' d'un rapport
    execute_validated()-like et la pose comme attribut de span + statut d'erreur si ce n'est
    pas OK. Centralisé ici pour ne pas dupliquer cette extraction à chaque site d'appel."""
    if not report_text:
        return
    m = _VERDICT_LINE_RE.search(report_text)
    verdict = "INCONNU"
    if m:
        tail = m.group(1).strip()
        for token in _KNOWN_VERDICT_TOKENS:
            if tail.startswith(token):
                verdict = token
                break
    sp.set_attribute("verdict", verdict)
    if verdict != "OK":
        try:
            from opentelemetry.trace import StatusCode
            sp.set_status(StatusCode.ERROR, f"verdict={verdict}")
        except Exception:
            pass


def start_span(name, **attrs):
    """Ouvre un span qui NE se ferme PAS automatiquement — pour les opérations async/tick-based
    de ce projet (playtest_agent.py : un scénario s'étale sur des dizaines d'appels ue5_execute
    successifs, impossible à englober dans un `with span(...)` classique). L'appelant est
    responsable de fermer le span retourné (méthode `.end()`), typiquement dans le callback de
    fin de session. Retourne un _NoOpSpan si le tracing est indisponible/désactivé — même
    surface d'API (`set_attribute`, `add_event`, `end`), pas de branchement conditionnel côté
    appelant."""
    tracer = _init_tracer()
    if tracer is None:
        return _NoOpSpan()
    sp = tracer.start_span(name)
    for k, v in attrs.items():
        try:
            sp.set_attribute(k, v)
        except Exception:
            sp.set_attribute(k, str(v))
    return sp


def flush():
    """Force l'export des spans en attente. À appeler explicitement en fin de script
    ue5_execute long (le SimpleSpanProcessor exporte déjà à chaque span.end(), donc ce n'est
    utile qu'en filet de sécurité, ex. avant un rebuild qui va tuer le process éditeur)."""
    try:
        from opentelemetry import trace
        provider = trace.get_tracer_provider()
        if hasattr(provider, "force_flush"):
            provider.force_flush()
    except Exception:
        pass
