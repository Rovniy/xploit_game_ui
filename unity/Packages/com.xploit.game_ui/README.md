# xploit_game_ui — HTML/CSS/JS как игровой интерфейс Unity

Пакет рисует документ HTML/CSS/JavaScript в текстуру и отдаёт её обычному
`RawImage`. Движок собственный: lexbor разбирает HTML, каскад CSS и раскладка на
Yoga свои, текст и растеризация — Skia, скрипты — V8. Ни Gameface, ни Ultralight,
ни встроенного браузера.

## Требования

- Unity 6000.2 или новее, Windows x64.
- Графический API **Direct3D 12** для аппаратного пути. На D3D11 и в
  `-nographics` пакет автоматически переключается на программный провайдер.

## За пять минут

1. Положите страницу в `Assets/StreamingAssets/UI/MainMenu/index.html`. Всё, что
   она грузит, должно лежать внутри `StreamingAssets`: выход за эту границу
   рантайм отклоняет.
2. На `Canvas` создайте объект с `RawImage`, добавьте `HtmlView` и `WebInput`.
3. В инспекторе `HtmlView` укажите путь `UI/MainMenu/index.html` и снимите
   галочку **Draw Test Frame On Enable**.
4. Запустите сцену.

```csharp
public sealed class Menu : MonoBehaviour
{
    [SerializeField] HtmlView view;

    void OnEnable()
    {
        view.On("play", _ => StartGame());              // страница: Unity.emit("play")
        view.RegisterFunction("getBestScore", _ => 42); // страница: await Unity.call("getBestScore")
        view.JsReady += v => v.Send("healthChanged", 100); // страница: Unity.on("healthChanged", ...)
    }
}
```

Готовый пример целиком — **Samples → HUD** в окне Package Manager.

## Что есть в API

| Тип | Назначение |
|---|---|
| `HtmlView` | одно представление: загрузка, состояние, текстура, мост, ввод |
| `HtmlViewManager` | один на процесс: инициализация рантайма, кадр, вычитка логов |
| `WebInput` | ввод Unity → документ через события uGUI и клавиатуру |
| `WebEvent`, `WebArguments`, `WebValue` | то, что пришло из страницы |
| `WebJson` | сериализация и разбор полезной нагрузки моста |
| `WebLogMessage` | строка от рантайма с уровнем и виновным представлением |
| `WebTexture` | текстура представления и её пересоздание при resize |

### HtmlView

```csharp
view.Load("UI/MainMenu/index.html");   // относительно StreamingAssets
view.LoadHtml("<b>привет</b>");        // разметка из памяти
view.Reload();
view.ExecuteJS("console.log(document.title)");

view.Send("healthChanged", 75);                       // → Unity.on
view.On("play", e => Play(e.Args.GetString(0)));      // ← Unity.emit
view.RegisterFunction("getAmmo", _ => ammo);          // ← await Unity.call
view.RegisterFunctionAsync("loadSave", async _ => await LoadAsync());

view.SendInput(WebInputEvent.Mouse(WebInputType.MouseDown, point));
view.SetFocus("search");
view.Resize(1280, 720);

view.DomReady    += v => { };   // DOM построен, скрипты ещё не выполнялись
view.JsReady     += v => { };   // скрипты выполнены
view.Interactive += v => { };   // первый кадр отрисован
view.Log         += m => { };   // console.* и ошибки этой страницы
```

### Логи

Всё, что печатает страница, по умолчанию уходит в Unity Console. Чтобы увести
вывод в свою консоль, подпишитесь на `HtmlViewManager.Log` и включите
`HtmlViewManager.SuppressConsoleOutput`.

## Границы

Поддержка HTML, CSS и DOM сознательно ограничена тем, что нужно игровому
интерфейсу. Полная матрица и список отличий от браузера — в `docs/css-support.md`
репозитория. Коротко о главном:

- нет grid, float, transition и animation; есть flexbox, block и inline;
- нет сети: `fetch`, `XMLHttpRequest` и модули ES недоступны;
- JavaScript не может читать файлы и не может вызвать native-код иначе, чем
  через явно зарегистрированные функции моста.
