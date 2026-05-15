import sys, json, os

try:
    data = json.loads(sys.stdin.read())
    cmd = data.get("tool_input", {}).get("command", "")
    if "cmake --build" in cmd:
        backlog = os.path.join(os.path.dirname(__file__), "..", "..", "backlog.md")
        with open(backlog, encoding="utf-8") as f:
            content = f.read()
        # Show only the Open section as a post-build reminder
        lines = content.splitlines()
        in_open = False
        out = []
        for line in lines:
            if line.startswith("## 🔴 Open"):
                in_open = True
            elif line.startswith("## "):
                in_open = False
            if in_open:
                out.append(line)
        print("=== backlog.md — Open issues (post-build reminder) ===")
        print("\n".join(out))
except Exception:
    pass
