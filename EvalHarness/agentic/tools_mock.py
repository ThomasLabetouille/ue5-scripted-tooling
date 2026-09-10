"""Fake UE5/Cowork tools for the agentic eval harness.

These mirror the real tools the agent uses on RPG_Test (ue5_execute, ue5_utils helpers,
PIECommandSubsystem, Build.bat, git, ...) closely enough that a scenario can reproduce a
documented trap (e.g. a stale screenshot, a Python-only fix that never survives a rebuild)
without needing a live UE5 editor. Every tool takes free-form input; the harness never
validates arguments strictly, it just needs the MODEL's *choice* of tool and the resulting
transcript.
"""

# Anthropic "tools" schema for every mock tool we know how to fixture. A scenario picks a
# subset via its "available_tools" list.
TOOL_DEFINITIONS = {
    "take_screenshot": {
        "name": "take_screenshot",
        "description": "Capture a screenshot of the current editor viewport (async, known to sometimes return a stale cached file on UE5.8).",
        "input_schema": {"type": "object", "properties": {}},
    },
    "capture_reference_screenshot": {
        "name": "capture_reference_screenshot",
        "description": "Synchronous reference screenshot capture at a given camera pose (x, y, z, pitch, yaw, name). Reliable alternative to take_screenshot.",
        "input_schema": {
            "type": "object",
            "properties": {
                "x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"},
                "pitch": {"type": "number"}, "yaw": {"type": "number"}, "name": {"type": "string"},
            },
        },
    },
    "read_image": {
        "name": "read_image",
        "description": "Read/view an image file at a given path so its content can be visually inspected.",
        "input_schema": {"type": "object", "properties": {"path": {"type": "string"}}, "required": ["path"]},
    },
    "save_current_level": {
        "name": "save_current_level",
        "description": "EditorLoadingAndSavingUtils.save_current_level() — saves the currently loaded level.",
        "input_schema": {"type": "object", "properties": {}},
    },
    "ue5_utils_save": {
        "name": "ue5_utils_save",
        "description": "ue5_utils.save() — saves the level AND explicitly writes all OFPA/__ExternalActors__ packages.",
        "input_schema": {"type": "object", "properties": {}},
    },
    "set_component_property_live": {
        "name": "set_component_property_live",
        "description": "Poke a live component property on a CDO or instance via Python (set_editor_property) without touching source code.",
        "input_schema": {
            "type": "object",
            "properties": {"actor": {"type": "string"}, "property": {"type": "string"}, "value": {"type": "string"}},
        },
    },
    "edit_cpp_source": {
        "name": "edit_cpp_source",
        "description": "Edit a C++ source file (e.g. change a default member initializer / tuning value in the constructor).",
        "input_schema": {
            "type": "object",
            "properties": {"file": {"type": "string"}, "change": {"type": "string"}},
        },
    },
    "run_build_bat": {
        "name": "run_build_bat",
        "description": "Run Build.bat to fully rebuild the C++ module (editor must be closed). The only real proof a C++ change compiles.",
        "input_schema": {"type": "object", "properties": {}},
    },
    "static_code_review": {
        "name": "static_code_review",
        "description": "Read the source file and visually check braces/parens/syntax balance without compiling.",
        "input_schema": {"type": "object", "properties": {"file": {"type": "string"}}},
    },
    "python_get_all_actors_of_class": {
        "name": "python_get_all_actors_of_class",
        "description": "unreal.EditorActorSubsystem.get_all_level_actors() filtered by class pin, executed via ue5_execute Python.",
        "input_schema": {"type": "object", "properties": {"class_name": {"type": "string"}}},
    },
    "python_get_all_actors_with_tag": {
        "name": "python_get_all_actors_with_tag",
        "description": "GetAllActorsWithTag — the supported way to filter actors by tag via Python in this project.",
        "input_schema": {"type": "object", "properties": {"tag": {"type": "string"}}},
    },
    "compile_blueprint": {
        "name": "compile_blueprint",
        "description": "Compile a Blueprint graph (BlueprintGraphHelper). Confirms absence of Kismet compile errors only.",
        "input_schema": {"type": "object", "properties": {"blueprint": {"type": "string"}}},
    },
    "run_pie_playtest": {
        "name": "run_pie_playtest",
        "description": "Actually start Play-In-Editor and drive the player via PIECommandSubsystem / playtest_agent.py to test real gameplay behavior.",
        "input_schema": {"type": "object", "properties": {"scenario": {"type": "string"}}},
    },
    "ue5_execute_python": {
        "name": "ue5_execute_python",
        "description": "Run an arbitrary Python snippet inside the UE5 editor via ue5_execute and return its output.",
        "input_schema": {"type": "object", "properties": {"code": {"type": "string"}}},
    },
    "calibrate_grip_left_hand": {
        "name": "calibrate_grip_left_hand",
        "description": "Visually calibrate the left-hand weapon grip offset in PIE and report the values found.",
        "input_schema": {"type": "object", "properties": {}},
    },
    "calibrate_grip_right_hand": {
        "name": "calibrate_grip_right_hand",
        "description": "Visually calibrate the right-hand shield grip offset in PIE and report the values found.",
        "input_schema": {"type": "object", "properties": {}},
    },
    "check_live_coding_status": {
        "name": "check_live_coding_status",
        "description": "Poll the Live Coding compile status/log in the editor.",
        "input_schema": {"type": "object", "properties": {}},
    },
    "highres_shot_pie": {
        "name": "highres_shot_pie",
        "description": "pie.execute_in_pie('HighResShot ...') — captures the 3D scene and native Canvas HUD in PIE. Known on this project to NOT include UMG widgets added via AddToViewport.",
        "input_schema": {"type": "object", "properties": {}},
    },
    "computer_use_screenshot": {
        "name": "computer_use_screenshot",
        "description": "Real desktop screenshot of the editor window while PIE is displayed (computer-use tool). Captures everything on screen, including UMG widgets.",
        "input_schema": {"type": "object", "properties": {}},
    },
}


def resolve_tool_call(tool_name: str, tool_input: dict, fixtures: dict, call_counts: dict) -> dict:
    """Return the canned fixture response for a tool call, advancing sequence fixtures.

    fixtures[tool_name] can be:
      {"type": "static", "response": <any>}
      {"type": "sequence", "responses": [<any>, ...]}          # last one repeats once exhausted
      {"type": "by_input_key", "key": "path", "map": {...}, "default": <any>}
    """
    call_counts[tool_name] = call_counts.get(tool_name, 0) + 1
    n = call_counts[tool_name]

    if tool_name not in fixtures:
        return {"error": f"no fixture defined for tool '{tool_name}' in this scenario"}

    fx = fixtures[tool_name]
    if fx["type"] == "static":
        return fx["response"]
    if fx["type"] == "sequence":
        idx = min(n - 1, len(fx["responses"]) - 1)
        return fx["responses"][idx]
    if fx["type"] == "by_input_key":
        key_value = (tool_input or {}).get(fx["key"])
        return fx["map"].get(key_value, fx.get("default", {"error": f"no fixture for {fx['key']}={key_value!r}"}))
    return {"error": f"unknown fixture type '{fx['type']}'"}
