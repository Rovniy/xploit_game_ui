# xploit_game_ui

HTML/CSS/JavaScript как игровой UI для Unity: собственный встраиваемый Game UI Runtime на open-source компонентах (V8, lexbor, Yoga, Skia), без Gameface, Ultralight, Vuplex и коммерческих WebView SDK.

- План и статус: [docs/PLAN.md](docs/PLAN.md)
- Архитектурные решения: [docs/ADR-0001-stack.md](docs/ADR-0001-stack.md)
- Архитектура и модель потоков: [docs/architecture.md](docs/architecture.md), [docs/threading.md](docs/threading.md)
- Поддержка HTML/CSS/DOM: [docs/css-support.md](docs/css-support.md)
- Лицензии сторонних компонентов: [docs/THIRD-PARTY-NOTICES.md](docs/THIRD-PARTY-NOTICES.md)

## Структура

```
native/   C++20 runtime (CMake + vcpkg): ядро, рендер Skia/D3D12, C ABI, плагин Unity, xgu_host, xgu_cli, тесты
unity/    Packages/com.xploit.game_ui — UPM-пакет (C# API + DLL); Sandbox — тестовый проект Unity 6000.5
docs/     документация проекта
tools/    build.ps1 — сборка native и копирование DLL в пакет
```

## Требования

- Windows 11 x64, Visual Studio 2022/2026 с рабочей нагрузкой «Разработка классических приложений на C++» (используются входящие в комплект CMake, Ninja и vcpkg)
- Unity 6000.2+ (тестовый проект — 6000.5.1f1), графический API Windows — Direct3D 12
- Первая сборка скачивает и собирает Skia через vcpkg (30–60 минут); результаты кэшируются в `build/vcpkg-cache`

## Сборка

```powershell
.\tools\build.ps1                # Release: конфигурация, сборка, тесты, копирование DLL в UPM-пакет
.\tools\build.ps1 -Config Debug
.\native\out\x64-windows-release\bin\xgu_host.exe --debug                            # окно с тестовым кадром Skia на D3D12 (Esc — выход)
.\native\out\x64-windows-release\bin\xgu_host.exe --screenshot out.png --frames 3    # D3D12 без Unity: readback текстуры в PNG
.\native\out\x64-windows-release\bin\xgu_cli.exe --test-frame out.png                # тот же кадр через CPU-провайдер
.\native\out\x64-windows-release\bin\xgu_cli.exe js script.js                           # выполнить JS в V8 (console.* в stdout/stderr)
.\native\out\x64-windows-release\bin\xgu_cli.exe layout page.html --width 800 --height 600  # дамп дерева боксов в JSON
.\native\out\x64-windows-release\bin\xgu_cli.exe render page.html out.png --width 800 --height 500  # отрисовать страницу в PNG
```

Эталонные изображения golden-тестов перегенерируются переменной окружения `XGU_UPDATE_GOLDEN=1`; при расхождении тест пишет рядом `<имя>.actual.png` и `<имя>.diff.png`.

Статус: Этапы 0–5 завершены (native 180/180, Unity PlayMode 20/20 на D3D12). Следующий — Этап 6: ввод и события DOM. См. [docs/PLAN.md](docs/PLAN.md).

Тесты Unity (PlayMode, batchmode):

```powershell
& "C:\Program Files\Unity\Hub\Editor\6000.5.1f1\Editor\Unity.exe" -batchmode -force-d3d12 -projectPath .\unity\Sandbox -runTests -testPlatform PlayMode -testResults .\build\test-results.xml -logFile .\build\editor.log
```

## Использование в Unity (цель)

```csharp
public class HUD : MonoBehaviour
{
    [SerializeField] HtmlView view;

    void Start()
    {
        view.Load("UI/HUD/index.html");
        view.On("inventory", e => OpenInventory());
    }

    public void SetHealth(float health) => view.Send("healthChanged", health);
}
```
