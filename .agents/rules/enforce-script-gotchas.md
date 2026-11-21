# Enforce Script Gotchas (Arma Reforger 1.8 / 1.9)

Verified compiler, VM, and replication pitfalls in Enfusion Enforce Script. Check these before writing or reviewing `.c` files.

## Syntax & Compiler Traps
- **No ternary operator (`cond ? a : b`)**: Fails with `Broken expression (missing ';'?)` / `Invalid statement ':'` and desyncs parsing later in the file. Always use `if` / `else`. (Enforced by `ENF003_NoTernary`.)
- **No parameter shadowing own class fields**: A method, constructor, or factory parameter cannot share a name with a member field of the **same** class (`Variable name conflict`). Prefix parameters (e.g. `px`, `inTarget`).
- **Reserved words and type names cannot be identifiers**: Never name locals or parameters `out`, `owned`, `unowned`, `reference`, `map`, or `set`. They trigger misleading `Broken expression` or `Variable name ... is already used as type name` errors.
- **No stacked attribute decorators**: Placing `[Attribute(...)]` on one line and `[RplProp(...)]` on the next line breaks the parser (often reporting errors at the *next* `class` declaration). A field gets at most one decorator; split config `[Attribute]` and replicated `[RplProp]` into separate fields. (Enforced by `ENF001_StackedAttributes`.)
- **No compound assignment on array elements**: `arr[i] += x` (and `-=`, `*=`, `/=`) fails with `Operator '+=' not supported with array accessor []` and cascades into phantom errors in unrelated vanilla scripts. Write `arr[i] = arr[i] + x;`. (Enforced by `ENF002_ArrayCompoundAssign`.)
- **No chained method calls on `new`**: `list.Insert(new Foo().Init())` fails to compile. Assign to a local first: `Foo f = new Foo(); f.Init(); list.Insert(f);`.
- **No non-constant member initializers**: `protected ref Color m_Color = Color.FromRGBA(...);` fails at compile time. Initialize runtime objects in `OnPostInit` or an init method (empty array literals `{}` are fine).
- **Arity & complexity caps**:
  - `Rpc(method, ...)` rejects 9+ arguments after `method` (keep RPC payload args `<= 7`).
  - `string.Format` rejects 10+ format arguments (`<= 9` compile).
  - Long `+` chains (~12+ terms) or calls with ~10 indexed array arguments fail with `Formula too complex`; split into locals/statements.
- **Float-context modulo `%`**: `int % int` inside a `float` parameter expression fails with `Unknown operator '%'`. Compute the modulo into an `int` local first.
- **`modded class` drops vanilla class attributes**: Always check whether the vanilla class carries attributes (such as `[BaseContainerProps()]`) and restore them with `[InheritedAttribute()]`. (Enforced by `BOUNDARY003_ModdedClassStripsVanillaAttributes`.)

## Class, Component & Container Semantics
- **`ScriptComponentClass` parallel inheritance**: When `class FooComponent : BarComponent`, its companion **must** declare `class FooComponentClass : BarComponentClass` (not `ScriptComponentClass`). Otherwise it compiles cleanly, but `FindComponent(BarComponent)` silently returns `null`.
- **`ScriptComponent` cannot declare a parameterless constructor**: It inherits a parameterized engine constructor; declaring `void MyComponent()` fails with `Overloaded function ... not compatible`. Allocate `ref` arrays/maps in `OnPostInit` — and allocate `[RplProp]` containers **above** any `if (!Replication.IsServer()) return;` guard so clients can receive replicated data.
- **Child constructors must mirror parent parameter lists**: Enforce has no `super(args)` constructor chaining; subclass constructors must match the parent's signature.
- **`Managed` already defines `Clone()`**: Never declare `Clone()` on a `Managed` subclass (`Multiple declaration of function 'Clone'`). Use a distinct name such as `CopyX()`.
- **`map.Find(key, out value)` clobbers `value` on a miss**: A failed `Find` resets `value` to `0`/empty/null. Always assign the fallback **after** a failed `Find` (`if (!m.Find(k, v)) v = fallback;`).
- **`array.RemoveItem(x)` and `array.Remove(i)` do NOT preserve order**: They swap the last element into the removed slot. Use `RemoveItemOrdered` / `RemoveOrdered` whenever order matters.
- **VM call stack limit (~117 frames)**: Deep recursion throws a caught VM `Stack overflow` exception in `crash.log` and silently returns to the caller. Bound recursion strictly.

## Replication & Multiplayer Pitfalls
- **`RplId.Invalid()` is `0xFFFFFFFF`, NOT `0`**: A default-initialized `RplId` has numeric value `0`, for which `id.IsValid()` returns **true**! Always initialize unset `RplId` fields explicitly to `RplId.Invalid()` and validate with `id.IsValid()` plus `Replication.FindItem(id)` before use.
- **Server RPC identity must be derived, never trusted from the wire**: Enfusion has no generic RPC sender API. Never trust a `playerId` parameter sent to an `RplRcver.Server` RPC — derive acting identity from the owning `PlayerController.GetPlayerId()`, and move entity-level player-initiated RPCs onto a `PlayerController` hop when sender identity is required. (Enforced by `MP001_IdentityDerivedNotWire`.)
- **`[RplProp]` arrays resend the entire array on every `BumpMe()`**: Replicated arrays are NOT element-diffed; every bump destroys and recreates the entire client-side array (O(N) wire + client GC churn). Keep `[RplProp]` arrays small and infrequently bumped; for large or high-churn collections, use a JIP-baseline `[RplProp]` with small delta RPCs instead.
- **Stale action prompts & UI DPI**: `GetFilteredActions()` can lag fast-changing state (validate on the server rather than relying solely on prompt visibility), and `TextWidget.GetTextSize` returns reference 1920x1080 pixels while `Widget.GetScreenSize` returns physical pixels (convert with `GetGame().GetWorkspace().DPIUnscale`).
