# Матрица поддержки HTML / CSS / DOM

Статусы: **план** — входит в MVP, ещё не реализовано; **готово** — реализовано и покрыто тестами; **частично** — реализовано с оговорками; **позже** — вне MVP.

## HTML-элементы

Парсер (lexbor) понимает любой HTML5; таблица показывает, что движок отображает и стилизует. Статус «парсинг+DOM» означает, что элемент есть в дереве и доступен из JS, но ещё не рендерится.

| Элемент | Статус | Примечания |
|---|---|---|
| `html`, `head`, `body` | парсинг+DOM | |
| `div`, `p`, `ul`, `li` | парсинг+DOM | маркеры списков — позже |
| `span`, `label` | парсинг+DOM | `label for=` перенаправляет клик/фокус |
| `img` | парсинг+DOM | PNG/JPEG/WebP; intrinsic size; событие `load` |
| `button` | парсинг+DOM | inline-block, UA-стили, фокусируемый |
| `input` | парсинг+DOM | `type=text/password`; `placeholder`, `value`, `disabled`; события `input`, `change` |
| `textarea` | парсинг+DOM | многострочный, `pre-wrap` |
| `script` | **готово** | inline и `src`, порядок документа; `type="module"` — позже |
| `link rel=stylesheet`, `style` | парсинг+DOM | |
| `table`, `select`, `canvas`, `video`, `iframe` | позже / не планируется | |

## CSS-свойства

| Свойство | Статус | Примечания |
|---|---|---|
| `width`, `height`, `min-*`, `max-*` | **готово** | px, %, em, rem, vw, vh, auto |
| `margin`, `padding` (+ стороны) | **готово** | без margin collapsing; `margin: auto` центрирует |
| `border` (+ стороны), `border-radius` | **готово** | однородный цвет через `drawDRRect`; разные цвета сторон рисуются трапециями и без скругления |
| `position`, `top/right/bottom/left`, `inset` | **готово** | static/relative/absolute; `fixed` = absolute к viewport |
| `z-index` | **готово** | stacking contexts по CSS 2.1 Appendix E |
| `display` | **готово** | block, inline, inline-block, flex, none, contents |
| `flex-direction`, `flex-wrap`, `justify-content`, `align-items`, `align-self`, `align-content` | **готово** | |
| `flex`, `flex-grow`, `flex-shrink`, `flex-basis` | **готово** | |
| `gap`, `row-gap`, `column-gap` | **готово** | |
| `background-color`, `background-image`, `background-size`, `background-position`, `background-repeat` | **готово** | один слой; PNG/JPEG/WebP; `cover`/`contain`/явный размер, повтор через шейдер; градиенты — позже |
| `color`, `opacity` | **готово** | |
| `box-shadow` | **готово** | внешние тени (смещение, размытие, spread), вырезаются из border box; `inset` — позже |
| `font-family`, `font-size`, `font-weight`, `font-style` | **готово** | системные шрифты (DirectWrite) + `fonts/` корня UI, `@font-face` |
| `line-height` | **готово** | число, px, normal |
| `text-align`, `text-decoration`, `text-transform`, `letter-spacing` | **готово** | |
| `white-space` | **готово** | normal, nowrap, pre, pre-wrap, pre-line |
| `text-overflow: ellipsis` | **готово** | при `nowrap` + `overflow: hidden` |
| `transform`, `transform-origin` | **готово** | translate/scale/rotate/skew/matrix; только визуально, layout не меняет |
| `overflow` | **готово** | visible/hidden, обрезка по padding box со скруглением; scroll — позже |
| `box-sizing`, `visibility`, `pointer-events`, `cursor` | **готово** | |
| `grid-*`, `float`, `clear`, `transition`, `animation`, `filter`, `backdrop-filter`, CSS-переменные, `calc()` | позже | grid и transitions — первые кандидаты после MVP |

## Селекторы

| Селектор | Статус |
|---|---|
| тип, `.class`, `#id`, `*` | **готово** |
| потомок (` `), ребёнок (`>`), соседи (`+`, `~`) | **готово** |
| атрибутные `[a]`, `[a=v]`, `[a~=v]`, `[a\|=v]`, `[a^=v]`, `[a$=v]`, `[a*=v]`, флаг `i` | **готово** |
| `:disabled`, `:enabled`, `:checked`, `:root`, `:empty` | **готово** |
| `:hover`, `:active`, `:focus`, `:focus-within` | частично (разбираются и матчатся, но состояние приходит из ввода на Этапе 6 — сейчас всегда false) |
| `:first-child`, `:last-child`, `:only-child`, `:not()` | **готово** |
| `:nth-child()`, `::before`, `::after` | позже |

## Поддерживаемые единицы и значения

| Категория | Поддержка |
|---|---|
| Длины | `px`, `%`, `em`, `rem`, `vw`, `vh`, `vmin`, `vmax`, `auto`, `none`, безразмерный `0`; `pt/pc/in/cm/mm` → px |
| Цвета | `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`, `rgb()`, `rgba()`, `hsl()`, `hsla()`, `currentcolor`, `transparent` и 30+ именованных |
| Числа | `line-height`, `flex-grow`, `flex-shrink`, `opacity` (и `%`), `z-index`, `font-weight` |
| Углы | `deg`, `rad`, `grad`, `turn` (в функциях `transform`) |
| Функции `transform` | `translate/translateX/translateY`, `scale/scaleX/scaleY`, `rotate`, `skew/skewX/skewY`, `matrix`; 3D и `perspective` — нет |
| Глобальные ключевые слова | `inherit`, `initial`, `unset`, `revert` (для часто используемых свойств) |

## Шортхенды

Раскрываются в longhand при парсинге, поэтому порядок объявлений работает как в браузере: `margin`, `padding`, `inset`, `border`, `border-{top,right,bottom,left}`, `border-width/style/color`, `border-radius`, `flex`, `flex-flow`, `gap`, `overflow`, `background`, `text-decoration`.

## DOM / JS API

| API | Статус |
|---|---|
| `document.getElementById`, `querySelector`, `querySelectorAll`, `createElement`, `createTextNode`, `createComment` | **готово** |
| `document.documentElement`, `head`, `body`, `title`, `URL`, `getElementsByClassName`, `getElementsByTagName` | **готово** |
| `Element`: `id`, `className`, `classList` (add/remove/toggle/contains/item/length/value), `getAttribute/setAttribute/removeAttribute/hasAttribute`, `textContent`, `innerHTML`, `outerHTML`, `children`, `childNodes`, `parentNode`, `parentElement`, `firstChild`/`lastChild`/`nextSibling`/`previousSibling`, `appendChild`, `insertBefore`, `removeChild`, `remove`, `contains`, `matches`, `tagName`, `nodeType`, `nodeName`, `isConnected` | **готово** |
| `Element.style` (свойства через camelCase, сокращённые свойства, `setProperty`, `getPropertyValue`, `removeProperty`, `cssText`) | **готово** |
| `getBoundingClientRect` | план (Этап 6) |
| `addEventListener`/`removeEventListener`, `Event` (`preventDefault`, `stopPropagation`), `MouseEvent`, `KeyboardEvent`, `InputEvent`, `FocusEvent`, `WheelEvent` | план |
| `setTimeout`/`setInterval`/`clear*`, `requestAnimationFrame`, `performance.now` | план |
| `console.log/warn/error/info/debug` → Unity Console | **готово** |
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
8. `childNodes`, `children`, `querySelectorAll` возвращают обычные JS-массивы, а не живые `NodeList`/`HTMLCollection`.
9. `<script type="module">` не поддерживается (предупреждение в консоли); `document.write` отсутствует.
10. `<html>` раскладывается по размеру viewport, поэтому корневой элемент покрывает всю поверхность (в браузере высота `html` равна высоте содержимого). Для UI это удобнее: фон и hit-test работают по всей площади.
11. Единицы `pt/pc/in/cm/mm` переводятся в пиксели по 96 dpi; `calc()`, CSS-переменные и `@media` не поддерживаются — соответствующие правила пропускаются с предупреждением.
12. `border-radius` с формой `/` (эллиптические углы) не поддерживается: берётся первый набор радиусов.
13. Сокращённое свойство можно записать через `element.style` (`style.background = '#0b1220'`), но чтение возвращает пустую строку: сокращение не собирается обратно из longhand'ов. Читать нужно longhand (`style.backgroundColor`).
14. Грязные прямоугольники не вычисляются: каждый кадр перерисовывает весь viewport, кэша `SkPicture` на бокс нет. Это задача Этапа 10.
15. `border-image`, `inset`-тени и градиенты в `background-image` не рисуются.
