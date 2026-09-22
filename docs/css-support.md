# Матрица поддержки HTML / CSS / DOM

Статусы: **план** — входит в MVP, ещё не реализовано; **готово** — реализовано и покрыто тестами; **частично** — реализовано с оговорками; **позже** — вне MVP.

## HTML-элементы

| Элемент | Статус | Примечания |
|---|---|---|
| `html`, `head`, `body` | план | |
| `div`, `p`, `ul`, `li` | план | маркеры списков — позже |
| `span`, `label` | план | `label for=` перенаправляет клик/фокус |
| `img` | план | PNG/JPEG/WebP; intrinsic size; событие `load` |
| `button` | план | inline-block, UA-стили, фокусируемый |
| `input` | план | `type=text/password`; `placeholder`, `value`, `disabled`; события `input`, `change` |
| `textarea` | план | многострочный, `pre-wrap` |
| `script` | план | inline и `src`, порядок документа; `type="module"` — позже |
| `link rel=stylesheet`, `style` | план | |
| `table`, `select`, `canvas`, `video`, `iframe` | позже / не планируется | |

## CSS-свойства

| Свойство | Статус | Примечания |
|---|---|---|
| `width`, `height`, `min-*`, `max-*` | план | px, %, em, rem, vw, vh, auto |
| `margin`, `padding` (+ стороны) | план | без margin collapsing; `margin: auto` центрирует |
| `border` (+ стороны), `border-radius` | план | однородный цвет через `drawDRRect`; разные цвета сторон — без скругления |
| `position`, `top/right/bottom/left`, `inset` | план | static/relative/absolute; `fixed` = absolute к viewport |
| `z-index` | план | |
| `display` | план | block, inline, inline-block, flex, none, contents |
| `flex-direction`, `flex-wrap`, `justify-content`, `align-items`, `align-self`, `align-content` | план | |
| `flex`, `flex-grow`, `flex-shrink`, `flex-basis` | план | |
| `gap`, `row-gap`, `column-gap` | план | |
| `background-color`, `background-image`, `background-size`, `background-position`, `background-repeat` | план | один слой; градиенты — позже |
| `color`, `opacity` | план | |
| `box-shadow` | план | внешние тени; inset — позже |
| `font-family`, `font-size`, `font-weight`, `font-style` | план | системные шрифты (DirectWrite) + `fonts/` корня UI, `@font-face` |
| `line-height` | план | число, px, normal |
| `text-align`, `text-decoration`, `text-transform`, `letter-spacing` | план | |
| `white-space` | план | normal, nowrap, pre, pre-wrap, pre-line |
| `text-overflow: ellipsis` | план | при `nowrap` + `overflow: hidden` |
| `transform`, `transform-origin` | план | translate/scale/rotate/skew/matrix; только визуально, layout не меняет |
| `overflow` | план | visible/hidden; scroll — позже |
| `box-sizing`, `visibility`, `pointer-events`, `cursor` | план | |
| `grid-*`, `float`, `clear`, `transition`, `animation`, `filter`, `backdrop-filter`, CSS-переменные, `calc()` | позже | grid и transitions — первые кандидаты после MVP |

## Селекторы

| Селектор | Статус |
|---|---|
| тип, `.class`, `#id`, `*` | план |
| потомок (` `), ребёнок (`>`), соседи (`+`, `~`) | план |
| атрибутные `[a]`, `[a=v]`, `[a~=v]`, `[a^=v]`, `[a$=v]`, `[a*=v]` | план |
| `:hover`, `:active`, `:focus`, `:focus-within`, `:disabled`, `:checked` | план |
| `:first-child`, `:last-child`, `:not()` | план |
| `:nth-child()`, `::before`, `::after` | позже |

## DOM / JS API

| API | Статус |
|---|---|
| `document.getElementById`, `querySelector`, `querySelectorAll`, `createElement`, `createTextNode` | план |
| `Element`: `id`, `className`, `classList`, `getAttribute/setAttribute/removeAttribute`, `textContent`, `innerHTML`, `children`, `parentNode`, `appendChild`, `insertBefore`, `removeChild`, `style`, `getBoundingClientRect` | план |
| `addEventListener`/`removeEventListener`, `Event` (`preventDefault`, `stopPropagation`), `MouseEvent`, `KeyboardEvent`, `InputEvent`, `FocusEvent`, `WheelEvent` | план |
| `setTimeout`/`setInterval`/`clear*`, `requestAnimationFrame`, `performance.now` | план |
| `console.log/warn/error/info/debug` → Unity Console | план |
| `Unity.emit`, `Unity.on`, `Unity.off`, `Unity.call` (Promise) | план |
| `fetch`, `XMLHttpRequest`, `localStorage`, `history`, `location`, ES-модули | не планируется / позже |

## Задокументированные отклонения от браузеров (MVP)

1. Нет margin collapsing между блоками.
2. Нет float/clear.
3. Инлайновый элемент с блочными детьми ведёт себя как inline-block.
4. Границы и паддинги инлайновых элементов (`span`) не рисуются; фон инлайна рисуется по строкам текста.
5. `overflow: hidden` обрезает и абсолютно позиционированных потомков.
6. `display: block` реализован как column flex (Yoga): проценты высоты работают как в flex-контейнере.
7. Маркеры списков (`list-style`) не рисуются.
