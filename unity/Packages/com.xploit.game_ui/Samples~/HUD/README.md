# HUD sample

A complete game HUD: a health bar, an ammo counter, two buttons, and a request
that waits on the game. The page decides how it looks; `Hud.cs` decides what it
means.

## Setting it up

1. Move the `StreamingAssets/UI/HUD` folder into your project at
   `Assets/StreamingAssets/UI/HUD`. The runtime only reads documents from
   `StreamingAssets`, so the page cannot reach the rest of the disk.
2. Create a `Canvas` (Screen Space — Overlay) and, inside it, an object with a
   `RawImage`, an `HtmlView` and a `WebInput`.
3. On the `HtmlView`, set **Path (in StreamingAssets)** to `UI/HUD/index.html`
   and clear **Draw Test Frame On Enable**.
4. Add `Hud.cs` to the same object.
5. Make sure the scene has an `EventSystem` — Unity adds one along with the
   Canvas. The inspector warns you if something is missing.

## What crosses the bridge

| Direction | Name | Meaning |
|---|---|---|
| game → page | `healthChanged` | the new health, a number from 0 to 100 |
| game → page | `ammoChanged` | the new round count |
| page → game | `inventory` | the INVENTORY button was pressed |
| page → game | `reload` (a call) | reload the weapon; resolves with the number of rounds chambered, or rejects with a reason |

The RELOAD button is the point of `Unity.call`: the page waits for the game to
finish and then shows either the result or why it was refused.

## From your own code

```csharp
var hud = GetComponent<Hud>();
hud.ApplyDamage(15);
hud.SpendRound();
```
