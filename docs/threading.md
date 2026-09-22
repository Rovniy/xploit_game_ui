# Модель потоков xploit_game_ui

## Потоки

| Поток | Владелец | Что делает | Что запрещено |
|---|---|---|---|
| Главный поток Unity | Unity | C# API `HtmlView`; подача ввода (`WebInput`); `HtmlViewManager.Update()` вызывает `xgu_view_tick`, вычитывает очередь native→C# (`xgu_view_poll_message`) и вызывает пользовательские обработчики; создаёт внешние текстуры | Ничего тяжёлого: никакого layout/JS здесь |
| Runtime-поток | `xploit_game_ui.dll`, один на процесс | Для каждого view за кадр: входящие сообщения → ввод → DOM-события → таймеры/rAF → microtask checkpoint V8 → style → layout → paint → публикация `DisplayList` | Вызовы Unity API; обращения к `GrDirectContext` |
| Submission-поток Unity | Unity (плагинное событие `XGU_EVT_PAINT`, режим `kUnityD3D12GraphicsQueueAccess_Allow`) | Создание `GrDirectContext` (лениво), проигрывание `SkPicture` в surface, `flush(kPresent)`, `submit`, отложенное освобождение GPU-ресурсов по frame fence | Всё, кроме работы со Skia GPU и D3D12 |
| Пул воркеров | runtime | Декодирование изображений (`SkCodec` → растровый `SkImage`) | Обращения к DOM |

«JavaScript thread» из ТЗ совпадает с runtime-потоком: DOM API должны быть синхронными из JS, поэтому изолят V8 (один на view) входит только в этом потоке.

## Что пересекает границы потоков

- **Главный → runtime:** `BridgeMessage` (Send/Reply/ExecuteJS/Load/Resize), `XguInputEvent`, `tick`. Очереди под мьютексом (`std::deque`), с коалесцированием `MouseMove`.
- **Runtime → главный:** `BridgeMessage` (Emit/Call/Log/Lifecycle). Очередь ограничена 10k сообщений: при переполнении отбрасываются самые старые `Emit`/`Log`, но никогда `Call`/`Reply`.
- **Runtime → submission:** `DisplayList{ sk_sp<SkPicture>, dirtyRect, frameId }` — неизменяемый объект в слоте «последний выигрывает» (один слот на view). `SkPicture` refcounted, поэтому потерянный (не показанный) кадр освобождается бесплатно.
- **Submission → главный:** только флаги статуса (`xgu_view_status`: `TEXTURE_READY`, `TEXTURE_RECREATED`, `DEVICE_LOST`, `PIXELS_READY`) и указатель на нативную текстуру; C# опрашивает их в `Update()`.

Ничего другого границы не пересекает: узлы DOM, `ComputedStyle`, `LayoutBox`, объекты V8 живут только в runtime-потоке.

## Темп кадров

Runtime-поток спит на condition variable и пробуждается по `xgu_view_tick(view, time)` из `Update()` Unity либо по появлению сообщений. Один тик — один кадр runtime для этого view; несколько тиков до обработки коалесцируются. На чистом кадре (нет dirty-битов, таймеров и rAF) `DisplayList` не публикуется, и C# не выставляет плагинное событие (`xgu_view_has_pending_frame` возвращает 0).

## Жизненный цикл

```
Create → Initialize → Load → DOM Ready → JS Ready → Interactive ⇄ Pause/Resume → Reload → Dispose
```

- `Create`/`Initialize`: `xgu_view_create` регистрирует view (id с поколением), выделяет изолят V8 при первом `Load`.
- `Load`: чтение HTML через `IAssetLoader`, парсинг, применение UA- и авторских стилей, `DOMContentLoaded` (**DOM Ready**), исполнение `<script>` в порядке документа, событие `load` (**JS Ready**), затем первый layout/paint → **Interactive**.
- `Pause`: ввод и тики игнорируются, таймеры замораживаются; `Resume` продолжает с сохранённым временем.
- `Reload`: полный `Dispose` внутреннего состояния view (изолят включительно) и повторный `Load`; id view сохраняется.
- `Dispose` (`xgu_view_destroy`): главный поток помечает view как уничтожаемый → runtime-поток заканчивает текущий кадр, сбрасывает все `v8::Global`, освобождает контекст и изолят, DOM/layout/paint → submission-поток при следующем событии откладывает GPU-ресурсы в список с тегом `GetNextFrameFenceValue()` и освобождает их, когда `GetFrameFence()->GetCompletedValue()` догоняет тег. Поколение id инкрементируется, поэтому «висящие» события с старым id игнорируются.
- **Domain reload в редакторе:** `HtmlViewManager` вызывает `xgu_views_destroy_all()` в `AssemblyReloadEvents.beforeAssemblyReload`; DLL и платформа V8 остаются загруженными; view пересоздаются после перезагрузки.
- **Потеря устройства (`isDeviceLost`, `DXGI_ERROR_DEVICE_REMOVED`) и `kUnityGfxDeviceEventBeforeReset`:** контекст Skia бросается (`releaseResourcesAndAbandonContext`), все view получают `DEVICE_LOST`; C# пересоздаёт view.

## Правила для кода

1. Любая функция C ABI документирует поток, из которого её можно вызывать; по умолчанию — главный поток Unity.
2. Колбэки в C# (`Action<WebEvent>`, `Func<WebArguments, object>`) вызываются только из `HtmlViewManager.Update()`.
3. Runtime-поток никогда не блокируется в ожидании GPU; submission-поток никогда не ждёт runtime-поток.
4. Все обращения к Skia GPU (`GrDirectContext`, `SkSurface` поверх D3D12) — только из submission-потока; растровые `SkImage`/`SkPicture` можно создавать в runtime-потоке.
5. Тесты (`xgu_tests`, `xgu_cli`) запускают runtime в «однопоточном режиме» (тик выполняется синхронно в вызывающем потоке) — это тот же код, без отдельного потока.
