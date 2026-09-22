# HTML / CSS / DOM support matrix

Status values: **done** — implemented and covered by tests; **partial** — implemented
with the caveats named beside it; **later** — outside the current scope.

This is the honest list. Everything the engine does not do is written down, either
in a table row or in [Documented deviations](#documented-deviations-from-browsers)
at the bottom.

## HTML elements

The parser (lexbor) understands any HTML5. This table is about what the engine
lays out and paints.

| Element | Status | Notes |
|---|---|---|
| `html`, `head`, `body` | done | |
| `div`, `p`, `ul`, `li` | done | list markers are not drawn |
| `span`, `label` | done | `label for=` forwards the click and the focus |
| `img` | done | PNG/JPEG/WebP, intrinsic sizing, a `load` event |
| `button` | done | inline-block, user-agent styles, focusable |
| `input` | done | `type=text` and `type=password`; `placeholder`, `value`, `disabled`; `input` and `change` events |
| `textarea` | done | multi-line, `pre-wrap` |
| `script` | done | inline and `src`, in document order; `type="module"` is not supported |
| `link rel=stylesheet`, `style` | done | |
| `table`, `select`, `canvas`, `video`, `iframe` | later / not planned | |

## CSS properties

| Property | Status | Notes |
|---|---|---|
| `width`, `height`, `min-*`, `max-*` | done | px, %, em, rem, vw, vh, auto |
| `margin`, `padding` (and the per-side longhands) | done | no margin collapsing; `margin: auto` centres |
| `border` (and per-side), `border-radius` | done | a uniform colour goes through `drawDRRect`; sides with different colours are drawn as trapezoids, without rounding |
| `position`, `top/right/bottom/left`, `inset` | done | static, relative, absolute; `fixed` behaves as absolute to the viewport |
| `z-index` | done | stacking contexts per CSS 2.1 Appendix E |
| `display` | done | block, inline, inline-block, flex, none, contents |
| `flex-direction`, `flex-wrap`, `justify-content`, `align-items`, `align-self`, `align-content` | done | |
| `flex`, `flex-grow`, `flex-shrink`, `flex-basis` | done | |
| `gap`, `row-gap`, `column-gap` | done | |
| `background-color`, `background-image`, `background-size`, `background-position`, `background-repeat` | done | one layer; PNG/JPEG/WebP; `cover`, `contain` or an explicit size; repeats through a shader |
| `linear-gradient`, `radial-gradient` in `background-image` | done | an angle in degrees or `to <side>`; colour stops positioned in per cent; `currentColor` |
| `color`, `opacity` | done | |
| `box-shadow` | done | outer shadows (offset, blur, spread), clipped out of the border box; `inset` is not supported |
| `font-family`, `font-size`, `font-weight`, `font-style` | done | system fonts through DirectWrite, plus `fonts/` under the UI root and `@font-face` |
| `line-height` | done | a number, a length, or `normal` |
| `text-align`, `text-decoration`, `text-transform`, `letter-spacing` | done | |
| `white-space` | done | normal, nowrap, pre, pre-wrap, pre-line |
| `text-overflow: ellipsis` | done | with `nowrap` and `overflow: hidden` |
| `transform`, `transform-origin` | done | translate/scale/rotate/skew/matrix; visual only, layout is unaffected |
| `overflow` | done | visible, hidden, scroll, auto; clipped to the rounded padding box, wheel scrolling chains to the ancestor |
| `box-sizing`, `visibility`, `pointer-events`, `cursor` | done | |
| `transition`, `@keyframes` and `animation` | done | `linear`, `ease`, `ease-in`, `ease-out`, `ease-in-out`, `cubic-bezier()`; direction, iteration count, `forwards`/`backwards`/`both` |
| `grid-*`, `float`, `clear`, `filter`, `backdrop-filter`, custom properties, `calc()` | later | grid is the first candidate |

## Selectors

| Selector | Status |
|---|---|
| type, `.class`, `#id`, `*` | done |
| descendant (` `), child (`>`), siblings (`+`, `~`) | done |
| attribute `[a]`, `[a=v]`, `[a~=v]`, `[a\|=v]`, `[a^=v]`, `[a$=v]`, `[a*=v]`, and the `i` flag | done |
| `:disabled`, `:enabled`, `:checked`, `:root`, `:empty` | done |
| `:hover`, `:active`, `:focus`, `:focus-within` | done |
| `:first-child`, `:last-child`, `:only-child`, `:not()` | done |
| `:nth-child()`, `::before`, `::after` | later |

## Units and values

| Category | Support |
|---|---|
| Lengths | `px`, `%`, `em`, `rem`, `vw`, `vh`, `vmin`, `vmax`, `auto`, `none`, a unitless `0`; `pt/pc/in/cm/mm` convert to px |
| Colours | `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`, `rgb()`, `rgba()`, `hsl()`, `hsla()`, `currentcolor`, `transparent`, and 30+ named colours |
| Numbers | `line-height`, `flex-grow`, `flex-shrink`, `opacity` (and `%`), `z-index`, `font-weight` |
| Angles | `deg`, `rad`, `grad`, `turn`, inside `transform` functions |
| `transform` functions | `translate/translateX/translateY`, `scale/scaleX/scaleY`, `rotate`, `skew/skewX/skewY`, `matrix`; no 3D and no `perspective` |
| Global keywords | `inherit`, `initial`, `unset`, `revert`, for the commonly used properties |

## Shorthands

Shorthands expand to longhands while parsing, so declaration order behaves as it
does in a browser: `margin`, `padding`, `inset`, `border`,
`border-{top,right,bottom,left}`, `border-width/style/color`, `border-radius`,
`flex`, `flex-flow`, `gap`, `overflow`, `background`, `text-decoration`.

## DOM and JavaScript API

| API | Status |
|---|---|
| `document.getElementById`, `querySelector`, `querySelectorAll`, `createElement`, `createTextNode`, `createComment` | done |
| `document.documentElement`, `head`, `body`, `title`, `URL`, `getElementsByClassName`, `getElementsByTagName` | done |
| `Element`: `id`, `className`, `classList` (add/remove/toggle/contains/item/length/value), `getAttribute`/`setAttribute`/`removeAttribute`/`hasAttribute`, `textContent`, `innerHTML`, `outerHTML`, `children`, `childNodes`, `parentNode`, `parentElement`, `firstChild`/`lastChild`/`nextSibling`/`previousSibling`, `appendChild`, `insertBefore`, `removeChild`, `remove`, `contains`, `matches`, `tagName`, `nodeType`, `nodeName`, `isConnected` | done |
| `Element.style` (camelCase properties, shorthands, `setProperty`, `getPropertyValue`, `removeProperty`, `cssText`) | done |
| `getBoundingClientRect` | done (a plain object, not a live `DOMRect`) |
| `scrollTop`, `scrollLeft`, `scrollWidth`, `scrollHeight`, `clientWidth`, `clientHeight`, `offsetWidth`, `offsetHeight` | done |
| `scrollTo`, `scrollBy`, `scrollIntoView` | done (no smooth scrolling; `behavior: smooth` is ignored) |
| `addEventListener`/`removeEventListener`/`dispatchEvent`, `new Event(type, init)`, `Event` (`preventDefault`, `stopPropagation`, `stopImmediatePropagation`, `target`, `currentTarget`, `eventPhase`), `MouseEvent`, `KeyboardEvent`, `InputEvent`, `FocusEvent`, `WheelEvent`, the `capture` and `once` options | done |
| `setTimeout`/`setInterval`/`clearTimeout`/`clearInterval`, `requestAnimationFrame`/`cancelAnimationFrame`, `performance.now` | done |
| `console.log/warn/error/info/debug` → the Unity Console | done |
| `element.focus()`, `element.blur()`, `document.activeElement` | done |
| `input`/`textarea`: `value`, `selectionStart`, `selectionEnd`, `setSelectionRange`, `select()` | done |
| `Unity.emit`, `Unity.on`, `Unity.off`, `Unity.call` (returns a Promise) | done |
| `fetch`, `XMLHttpRequest`, `localStorage`, `history`, `location`, ES modules | not planned |

## Events and input

| Feature | Status | Notes |
|---|---|---|
| Mouse: `mousedown`, `mouseup`, `click`, `dblclick`, `mousemove`, `mouseover`, `mouseout`, `mouseenter`, `mouseleave` | done | `click` fires on the common ancestor of the press and the release; `dblclick` within 500 ms and 4 px |
| Wheel: `wheel` | done | `deltaMode` is always 0 (pixels); the host converts notches |
| Keyboard: `keydown`, `keyup` | done | delivered to the focused element, or to `body` |
| Text: `beforeinput`, `input`, `change` | done | `change` fires on blur, and on Enter in a single-line field |
| Focus: `focus`, `blur`, `focusin`, `focusout` | done | a press moves focus to the nearest focusable ancestor |
| `:hover`, `:active`, `:focus`, `:focus-within` | done | the state is set on the whole ancestor chain |
| Editing in `input`/`textarea` | done | insertion, Backspace/Delete, arrows, Home/End, Shift selection, Ctrl+A, caret placement by mouse, drag selection, password masking, placeholder |
| Touch | partial | the first finger is mirrored to the mouse; multi-touch and `TouchEvent` are not delivered |
| Clipboard, IME composition, undo/redo | not planned | Unity delivers text that is already composed |

## Animation

| Feature | Status | Notes |
|---|---|---|
| `transition: <property> <duration> [<easing>] [<delay>]` | done | several transitions separated by commas; `all` covers every animatable property |
| `@keyframes` with `from`/`to` and percentages | done | keyframes go through the cascade, so units and `currentColor` behave as they do anywhere else |
| `animation: <name> <duration> [<easing>] [<delay>] [<count>] [<direction>] [<fill>]` | done | `infinite`, `reverse`, `alternate`, `alternate-reverse`, `forwards`, `backwards`, `both` |
| Animatable properties | done | `opacity`, colours (`color`, `background-color`, border colours), lengths (sizes, spacing, offsets, radii, `font-size`, `letter-spacing`, `gap`), `transform`, `flex-grow`, `flex-shrink` |

## Documented deviations from browsers

1. There is no margin collapsing between blocks.
2. There is no `float` or `clear`.
3. An inline element with block children behaves as inline-block.
4. Borders and padding on inline elements (`span`) are not painted. An inline background is painted per line of text.
5. `overflow: hidden` also clips absolutely positioned descendants.
6. `display: block` is implemented as a Yoga column flex container, so percentage heights behave as they do inside a flex container.
7. List markers (`list-style`) are not drawn.
8. `childNodes`, `children` and `querySelectorAll` return ordinary JavaScript arrays rather than live `NodeList`/`HTMLCollection` objects.
9. `<script type="module">` is not supported and logs a warning; `document.write` does not exist.
10. `<html>` is laid out at the viewport size, so the root element covers the whole surface. In a browser the height of `html` is the height of its content. For UI the former is more convenient: backgrounds and hit testing work across the whole area.
11. `pt/pc/in/cm/mm` convert to pixels at 96 dpi. `calc()`, custom properties and `@media` are not supported, and rules using them are skipped with a warning.
12. `border-radius` with the `/` form (elliptical corners) is not supported; the first set of radii is used.
13. A shorthand can be written through `element.style` (`style.background = '#0b1220'`), but reading it back returns an empty string, because shorthands are not reassembled from longhands. Read the longhand instead (`style.backgroundColor`).
14. `border-image` and `inset` shadows are not painted.
15. `window` and `document` are the same event target: a listener added on `window` fires during the bubble phase at the document. There is no separate `Window` object above `document`.
16. The caret does not blink. Blinking would mean repainting twice a second for every focused field; for game UI that is wasted work, so the caret is simply visible while the field has focus.
17. Tab order is not implemented. Focus is set by a mouse press, by `element.focus()`, or by `xgu_view_set_focus`.
18. A listener has to be a function; an object with a `handleEvent` method is not supported, and `AbortSignal` in the options is not supported.
19. An `input` shows its value on one line without scrolling: text longer than the field is clipped by `overflow: hidden`, and the caret does not push the content sideways.
20. Bridge arguments travel as JSON, so `JSON.stringify` rules apply: functions and `undefined` inside an array become `null`, as do `NaN` and `Infinity`, and a cyclic structure rejects the call. C# classes without a `[Serializable]` attribute serialise as `null`.
21. A C# enum reaches the page as the name of the value, not as a number, because comparing by name reads better in JavaScript.
22. Timers fire exactly once per frame, from `xgu_tick`, and measure time on the host clock (`Time.unscaledTimeAsDouble` in Unity). Their resolution is therefore one frame, and `setTimeout(fn, 0)` means "next frame" rather than "as soon as possible within this one". There is deliberately no timer thread: game UI lives on the game's beat.
23. `setTimeout`/`setInterval` accept a function only. The string-of-code form is not supported, because the runtime does not expose `eval` to outside code.
24. `performance.now()` counts from the view's first frame, not from process start.
25. The scrollbar is drawn as a plain 4 px thumb and cannot be styled: `::-webkit-scrollbar` and `scrollbar-width` are not supported. UI that needs its own scrollbar builds it from elements and drives `scrollTop`.
26. Scrolling is instant: `behavior: 'smooth'` in `scrollTo`/`scrollBy`/`scrollIntoView` is ignored, and smoothness is left to your own animation.
27. `getBoundingClientRect` returns a plain object with fields rather than a live `DOMRect`, computed at the moment of the call.
28. Animations and transitions advance per frame, recomputed once a frame on the host clock, with no separate thread. Their resolution is one frame, and a `transition` shorter than a frame takes effect immediately.
29. There are no `transitionend` or `animationend` events, and no longhand properties (`transition-duration`, `animation-name` and the rest). Only the `transition` and `animation` shorthands work.
30. One animation per element: a comma-separated list in `animation` is not supported. A list in `transition` is.
31. `transform` is interpolated element-wise on the matrix rather than by decomposing into translation, rotation and scale. For translations, scaling and small rotations the difference is invisible; for a rotation of more than 180° the result differs from a browser's.
32. Lengths in different units (`10px` → `50%`) are not mixed: the value switches at the midpoint of the transition. `steps()` easing is not supported.
33. A gradient is one layer, above `background-color` and below `background-image`. `linear-gradient` supports an angle or `to <side>`, and `radial-gradient` an optional `circle`/`ellipse`. Size and position keywords on `radial-gradient` (`closest-side`, `at 30% 70%`) are ignored: a circle is drawn from the centre to the farthest corner. Colour stops are positioned in per cent only, and `conic-gradient` and repeating gradients are not supported.
