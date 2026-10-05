# AGENTS.md — как работать с этим репозиторием

Краткий ориентир для людей и AI-агентов. Продуктовый паспорт — в корневом [`README.md`](README.md).

## Карта

```
esp32-watering/
├── README.md                # технический паспорт (BOM, архитектура, защиты, питание)
├── AGENTS.md                # этот файл
├── firmware/                # PlatformIO root (platformio.ini здесь)
│   ├── src/
│   │   ├── core/            # чистая логика без Arduino — покрыта native-тестами
│   │   └── *.cpp/.h         # железо, сеть, веб, планировщик
│   ├── web/index.html       # веб-интерфейс, вшивается в прошивку (embed_txtfiles)
│   ├── test/test_core/      # Unity-тесты для src/core
│   └── partitions.csv
├── tools/
│   ├── mock_server.py       # имитация API базы для работы над UI
│   └── docker-build.sh      # тесты + сборка в контейнере
└── docs/
    ├── hardware/pins.md     # распиновка «что куда» (+ pins.html — наглядная версия)
    ├── hardware/wiring.md   # общая схема подключения (+ wiring.html — наглядная версия)
    └── TESTING-bench.md     # чек-лист стенда
```

## Сборка и тесты

Локального PlatformIO может не быть — используйте Docker (Docker Desktop запущен):

```powershell
docker run --rm `
  -v "g:/projects/cursor/docs/esp32-watering/firmware:/firmware:ro" `
  -v "g:/projects/cursor/docs/esp32-watering/tools:/tools:ro" `
  -v esp32-watering-pio:/root/.platformio `
  -v esp32-watering-build:/build `
  python:3.11-slim bash /tools/docker-build.sh all
```

`all` = `pio test -e native` + `pio run -e esp32c3`; также `test` или `build`. PlatformIO Core закреплён на 6.1.16: более новый падает на `package-postinstall.py` платформы pioarduino. Первый прогон качает toolchain (5–15 мин).

Локально: `cd firmware && pio test -e native && pio run`.

После правок в `src/core/` — обязательно `test`. После любых правок прошивки — `build`.

## Синхронизация (обязательно)

| Что | С чем держать в lockstep |
|-----|--------------------------|
| `firmware/src/board_config.h` (пины) | `docs/hardware/pins.md`, `wiring.md` и их `.html`-версии (SVG-схемы внутри) |
| Экраны `firmware/src/display.cpp` | таблица экранов в `docs/hardware/pins.md` §5 и макеты экранов в `pins.html` |
| JSON-формат `firmware/src/web_ui.cpp` и `config.cpp` | `firmware/web/index.html` и `tools/mock_server.py` |
| `firmware/src/core/event_codes.h` | `EVENT_TEXT` и таблицы причин в `firmware/web/index.html` |
| Флаги `HistoryFlags` в `records.h` | фильтры графиков в `index.html`, генератор в `mock_server.py` |

## Форматы на flash — не ломать молча

- `HistoryRecord`, `LogRecord` (`records.h`): изменение структуры → поменять magic в `records.cpp` (файл начнётся заново, а не будет прочитан неправильно).
- Коды `Event` — только добавлять, никогда не перенумеровывать.
- Ключи NVS (`garden/lrN`) и ключи `config.json` — переименование сбрасывает пользовательские данные.

## Безопасность полива — инварианты

- Насос включается только из `WateringController` и только при открытом и подтверждённом клапане.
- Одновременно открыт не больше одного клапана (`ModuleBus::openValveIndex`).
- Любой путь выключения (OTA, сон, перезагрузка, сброс Wi-Fi) идёт через `WateringController::emergencyStop()`.
- `Garden::runCheck` пишет «проверено сегодня» **до** постановки полива в очередь.
- Перед `esp_deep_sleep_start()` пин насоса удерживается в LOW (`Pump::holdOffForSleep`); при старте `Pump::begin()` снимает удержание первым делом.
- Тревоги (`PowerManager`, RTC-память) снимаются **только** оценкой при пробуждении; блокировки/обрывы полива могут их только ставить.
- Плановое пробуждение (таймер) не включает AP и не показывает значок Wi-Fi; Wi-Fi-сессия — только кнопкой / включением питания.

## Работа над UI без железа

```powershell
python tools/mock_server.py --port 8080
```

Открыть `http://localhost:8080/`. Mock отдаёт настоящий `firmware/web/index.html`. `--alarms` — показать обе тревоги.

## Язык

- Пользовательские строки (UI, сообщения API об ошибках) — **русский**.
- Комментарии в коде — EN; паспорт и чек-листы — RU.
- Секреты / `.env` — не добавлять в репо.
