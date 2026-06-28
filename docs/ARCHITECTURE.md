# Architecture

OmniDiagnostics-ESP32 is structured as a small "micro-OS": a kernel runs a
finite-state machine over a set of interchangeable apps. Only one app is active
at a time, which keeps the shared radio and the small display under a single
owner and makes each feature self-contained.

## Components

```
            +--------------------------------------------------+
            |                     Kernel                       |
            |  app registry  |  event dispatch  |  main loop   |
            +------+----------------+----------------+---------+
                   |                |                |
            +------v-----+   +------v------+   +-----v------+
            | InputMgr   |   |  UIManager  |   |  active App |
            | buttons -> |   | TFT, theme, |   | onEnter/    |
            | Event      |   | header/foot |   | Exit/Event/ |
            +------------+   +-------------+   | Tick/Draw   |
                                              +-------------+
```

### Kernel (`core/Kernel.*`)

Owns the `UIManager`, the `InputManager` and an array of registered apps. Each
`tick()`:

1. polls input and produces an `Event`;
2. dispatches the event to the current app (`onEvent`);
3. runs the app's periodic logic (`onTick`);
4. redraws the app **only if** its `dirty` flag is set.

`switchTo(app)` calls `onExit()` on the outgoing app (where it must release the
radio and any other resources), clears the screen, and calls `onEnter()` on the
incoming app. `goHome()` returns to the main menu.

### App base class (`core/AppBase.h`)

Every feature derives from `App` and overrides the lifecycle hooks it needs:

| Hook | Called when | Typical use |
|------|-------------|-------------|
| `onEnter()` | the app becomes active | start a scan, init a radio, reset state |
| `onExit()` | the app is left | stop scan, deinit radio, free heap |
| `onEvent(e)` | an input event occurs | navigation, selection |
| `onTick(now)` | every kernel tick | poll async results, periodic refresh |
| `onDraw(ui)` | when `dirty` is set | render the screen |

The `dirty` flag drives rendering: apps set it whenever their visible state
changes, and the kernel clears it after drawing. This avoids redrawing the
display every loop iteration.

### Input (`core/InputManager.*`)

Reads the two on-board buttons and emits abstract `Event` values
(`NEXT`, `SELECT`, `BACK`). Debouncing is time-based (`millis()`), with no
blocking `delay()`, so input stays responsive:

- **NEXT** (GPIO35 short press) → `Event::NEXT`
- **OK** (GPIO0 short press) → `Event::SELECT`
- **OK** (GPIO0 held ≥ `LONGPRESS_MS`) → `Event::BACK`

Because apps consume abstract events, the physical input device can be replaced
(e.g. a rotary encoder) by changing only `InputManager` — no app code changes.

### Display (`core/UIManager.*`)

Wraps `TFT_eSPI` and provides the shared visual language:

- `header(title)` / `headerRight(text, color)` — amber title bar with an
  optional right-aligned status indicator;
- `footer(hint)` — bottom hint line plus a battery percentage;
- `clearBody()` — clears the work area between header and footer (apps call
  this before redrawing to avoid leftover pixels);
- `batteryVolts()` / `batteryPct()` — Li-Po readings via the on-board divider.

### List widget (`core/ListView.h`)

A reusable scrolling list with a highlighted active row, a viewport that keeps
the selection visible, and a scrollbar. Row text is provided through a callback
`getText(index, buffer, size)`, so the same widget renders the menu, the WiFi
network list and the BLE device list.

## Event and render flow

```
loop() -> kernel.tick()
            |
            +-- input.poll() ----------------> Event
            +-- app.onEvent(Event)            (state change, may set dirty)
            +-- app.onTick(now)               (async polling, may set dirty)
            +-- if app.dirty: app.onDraw(ui); app.dirty = false
            +-- delay(5)
```

## Radio ownership

WiFi and BLE share the ESP32 radio. The "one active app" model makes this
safe: an app initialises its radio in `onEnter()` and tears it down in
`onExit()`, so two radio stacks are never live at the same time.

## Adding a new app

1. Create `src/apps/AppFoo.{h,cpp}` deriving from `App`.
2. Implement `title()` and the hooks you need. Acquire resources in
   `onEnter()`, release them in `onExit()`.
3. Set `dirty = true` whenever the screen needs to change.
4. In `main.cpp`, instantiate it and call `kernel.registerApp(&appFoo)`.
   Its position in the registration order is its position in the menu.

A minimal example:

```cpp
class AppFoo : public App {
public:
    explicit AppFoo(Kernel& k) : _k(k) {}
    const char* title() const override { return "Foo"; }
    void onEvent(Event e) override { if (e == Event::BACK) _k.goHome(); }
    void onDraw(UIManager& ui) override {
        ui.header("FOO");
        ui.clearBody();
        ui.tft().drawString("hello", 8, 36, 2);
        ui.footer("OK(hold): back");
    }
private:
    Kernel& _k;
};
```
