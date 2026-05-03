import sys, json, os

try:
    data = json.loads(sys.stdin.read())
    cmd = data.get("tool_input", {}).get("command", "")
    if "cmake --build" in cmd:
        todo = os.path.join(os.path.dirname(__file__), "..", "..", "ToDo.md")
        with open(todo, encoding="utf-8") as f:
            content = f.read()
        print("=== ToDo.md (post-build reminder) ===")
        print(content)
except Exception:
    pass
