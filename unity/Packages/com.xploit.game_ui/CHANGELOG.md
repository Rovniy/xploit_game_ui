# Изменения

Версии пакета соответствуют этапам плана (`docs/PLAN.md`), пока проект не дошёл
до 1.0.

## 0.8.0

- Инспектор `HtmlView`: выбор документа из `StreamingAssets`, живое состояние в
  Play Mode, кнопки Reload и Pause, предупреждения о недостающем `RawImage`,
  `EventSystem` и выключенном Raycast Target.
- События жизненного цикла `DomReady`, `JsReady`, `Interactive` и событие `Log`
  у представления; `HtmlViewManager.Log` и `SuppressConsoleOutput` для своей
  консоли.
- Сообщения рантайма теперь знают, какое представление их породило.
- Метод `HtmlView.Resize`.
- Пример **HUD** в Samples.

## 0.7.0

- Мост: `Unity.emit`, `Unity.on`, `Unity.off`, `Unity.call` со стороны страницы;
  `Send`, `On`, `Off`, `RegisterFunction`, `RegisterFunctionAsync` со стороны
  игры.
- `WebJson`, `WebValue`, `WebArguments` для полезной нагрузки.

## 0.6.0

- Ввод: `WebInput`, `WebInputEvent`, `HtmlView.SendInput/SetFocus/ScreenToView`.
- События DOM с фазами, `:hover`, `:active`, `:focus`, редактирование полей.

## 0.5.0

- Отрисовка документа: фоны, границы, тени, изображения, текст, `opacity`,
  `transform`, обрезка по `overflow`.

## 0.3.0

- Загрузка документов: `Load`, `LoadHtml`, `Reload`, DOM в JavaScript.

## 0.2.0

- JavaScript: `ExecuteJS`, `console.*` в Unity Console, состояние представления.

## 0.1.0

- Первый срез: создание представления, текстура Skia на D3D12 и программный
  провайдер, `HtmlViewManager`.
