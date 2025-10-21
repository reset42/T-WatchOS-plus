# 04 – UI / APPS & RENDERER (condensed)
- Apps render via `ui.*`, system actions via bus (universal API). No direct driver calls.
- Manifest: captures_swipe, show_in_drawer, budgets, icon path.
- Drawer: scrollbar, grid/list, global swipe (unless captured).
- Renderer API: frame begin/end, draw rect/text/bmp; budgets & E_BUDGET.
- DoD: App switch ≤120 ms feedback; Boot stickies ≤200 ms.
