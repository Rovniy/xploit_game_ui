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
| `overflow` | **готово** | visible, hidden, scroll, auto; обрезка по padding box со скруглением, прокрутка колесом с передачей предку |
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
| `getBoundingClientRect` | **готово** | обычный объект, не живой DOMRect |
| `scrollTop`, `scrollLeft`, `scrollWidth`, `scrollHeight`, `clientWidth`, `clientHeight`, `offsetWidth`, `offsetHeight` | **готово** |
| `scrollTo`, `scrollBy`, `scrollIntoView` | **готово** | без плавной прокрутки: `behavior: smooth` игнорируется |
| `addEventListener`/`removeEventListener`/`dispatchEvent`, `new Event(type, init)`, `Event` (`preventDefault`, `stopPropagation`, `stopImmediatePropagation`, `target`, `currentTarget`, `eventPhase`), `MouseEvent`, `KeyboardEvent`, `InputEvent`, `FocusEvent`, `WheelEvent`, опции `capture`/`once` | **готово** |
| `setTimeout`/`setInterval`/`clearTimeout`/`clearInterval`, `requestAnimationFrame`/`cancelAnimationFrame`, `performance.now` | **готово** |
| `console.log/warn/error/info/debug` → Unity Console | **готово** |
| `element.focus()`, `element.blur()`, `document.activeElement` | **готово** |
| `input`/`textarea`: `value`, `selectionStart`, `selectionEnd`, `setSelectionRange`, `select()` | **готово** |
| `Unity.emit`, `Unity.on`, `Unity.off`, `Unity.call` (Promise) | **готово** |
| `fetch`, `XMLHttpRequest`, `localStorage`, `history`, `location`, ES-модули | не планируется / позже |

## События и ввод

| Возможность | Статус | Примечания |
|---|---|---|
| Мышь: `mousedown`, `mouseup`, `click`, `dblclick`, `mousemove`, `mouseover`, `mouseout`, `mouseenter`, `mouseleave` | **готово** | `click` — на общем предке нажатия и отпускания; `dblclick` в пределах 500 мс и 4 px |
| Колесо: `wheel` | **готово** | `deltaMode` всегда 0 (пиксели): хост пересчитывает щелчки |
| Клавиатура: `keydown`, `keyup` | **готово** | уходят в сфокусированный элемент, иначе в `body` |
| Текст: `beforeinput`, `input`, `change` | **готово** | `change` — по потере фокуса и по Enter в однострочном поле |
| Фокус: `focus`, `blur`, `focusin`, `focusout` | **готово** | фокус переходит к ближайшему фокусируемому предку по нажатию |
| Псевдоклассы `:hover`, `:active`, `:focus`, `:focus-within` | **готово** | состояние ставится на цепочку предков |
| Редактирование `input`/`textarea` | **готово** | вставка, Backspace/Delete, стрелки, Home/End, Shift-выделение, Ctrl+A, установка каретки мышью, выделение перетаскиванием, маска пароля, placeholder |
| Касания | **готово** (частично) | первый палец зеркалится в мышь; мультитач и `TouchEvent` не выдаются |
| Буфер обмена, IME-композиция, отмена/повтор | не планируется в MVP | Unity передаёт уже составленный текст |

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
16. `window` и `document` — одна и та же цель событий: слушатель, добавленный на `window`, срабатывает на фазе всплытия до документа. Отдельного объекта `Window` над `document` нет.
17. Каретка не мигает. Мигание требует перерисовки дважды в секунду на каждое сфокусированное поле; для игрового UI это лишняя работа, поэтому каретка просто видна, пока поле в фокусе.
18. Порядок обхода по Tab не реализован: фокус ставится нажатием мыши, `element.focus()` или `xgu_view_set_focus`.
19. Слушателем может быть только функция; объект с методом `handleEvent` не поддерживается. `AbortSignal` в опциях не поддерживается.
20. `input` показывает значение одной строкой без прокрутки: текст длиннее поля обрезается по `overflow: hidden`, каретка за край не уводит содержимое.
21. Аргументы моста проходят через JSON, поэтому действуют правила `JSON.stringify`: функции и `undefined` внутри массива становятся `null`, `NaN` и `Infinity` тоже, циклическая структура отклоняет вызов. Классы C# без атрибута `[Serializable]` сериализуются как `null`.
22. Перечисления C# передаются в страницу строкой с именем значения, а не числом: сравнивать в JavaScript удобнее по имени.
23. Таймеры срабатывают ровно раз за кадр, из `xgu_tick`, и измеряют время по часам хоста (`Time.unscaledTimeAsDouble` в Unity). Точность поэтому равна длине кадра, а `setTimeout(fn, 0)` означает «в следующем кадре», а не «как можно скорее в этом». Отдельного потока таймеров нет намеренно: игровой интерфейс живёт в такте игры.
24. `setTimeout`/`setInterval` принимают только функцию; вариант со строкой кода не поддерживается, потому что рантайм не даёт `eval` внешнему коду.
25. `performance.now()` отсчитывает время от первого кадра представления, а не от старта процесса.
26. Полоса прокрутки рисуется как простой ползунок шириной 4 px и не настраивается стилями: `::-webkit-scrollbar` и `scrollbar-width` не поддерживаются. Игровой интерфейс, которому нужна своя полоса, собирает её из элементов и двигает через `scrollTop`.
27. Прокрутка мгновенная: `behavior: 'smooth'` в `scrollTo`/`scrollBy`/`scrollIntoView` игнорируется, плавность делается своей анимацией.
28. `getBoundingClientRect` возвращает обычный объект с полями, а не живой `DOMRect`; значения посчитаны на момент вызова.
