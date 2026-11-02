# REA и Ghidra: установленная среда

Установка выполнена по
[официальной инструкции REA](https://github.com/morluto/rea/blob/main/docs/installation.md).
Приобретён официальный npm package `rea-agents@6.8.0`, отдельно сохранён
checkout исходного репозитория. Новые toolchains установлены под каталогом
`tools` пользовательского профиля; системные Node.js и Java не заменялись.

| Компонент | Версия / путь относительно пользовательского профиля |
|---|---|
| Node.js | 24.22.2, `tools/node-v24.22.2-win-x64` |
| REA CLI/MCP | 6.8.0, `tools/rea/node_modules/rea-agents` |
| REA source | `tools/rea-source` |
| REA skill | `.agents/skills/reverse-engineer-anything`, version 49 |
| Ghidra | 12.1.4, `tools/ghidra_12.1.4_PUBLIC` |
| Temurin full JDK | 21.0.13+9, `tools/jdk-21.0.13+9` |
| Zig | 0.15.2, `tools/zig-x86_64-windows-0.15.2` |

Архивы Node/Zig/Ghidra/JDK проверены по SHA-256, полученным из официальных
метаданных. Нативный Frida binding не устанавливался: он не нужен для
проведённого статического анализа и не является причиной отказа Ghidra.

Codex MCP registration находится в `.codex/config.toml`:
абсолютный `node.exe`, `rea.mjs mcp`, пути Ghidra/Java,
`REA_ANALYSIS_PROVIDER=ghidra`, startup timeout 900000 ms.
Настройки остальных разделов сравнены через TOML parser с исходным backup:
семантически совпадают. Backup: `.codex/config.toml.rea.backup`;
setup также сохранил recovery copy перед добавлением provider settings.
Существующие model/approval/security settings не изменялись.

Проверка `rea doctor --client codex --provider ghidra --skill --json`:
`healthy: true`. Это проверка установки, а не доказательство подключения
новых MCP tools к уже работающей сессии. Для него требуется перезапуск Codex.
См. [официальную документацию Codex MCP](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).

## Реальный статический анализ

Через CLI/Ghidra успешно декомпилирована экспортированная функция
`GetGameAPI` в локальном `Gamex86.dll`, адрес `0x2006da50`.
Evidence ID: `ev_e71fe9cc532d95412be48fb4b2e043e6de31aa0b305661d9329d8e999add7587`.
Ghidra completed default auto-analysis; provider 12.1.4; PDB load state unknown.
Декомпилятор наблюдает сравнение первого поля импортного интерфейса с `0x13`
(19). Для сравнения `doom3/neo/game/Game.h` закреплённого baseline содержит
`GAME_API_VERSION = 8`. Это дополнительное подтверждение несовпадения ABI,
а не описание уже восстановленной структуры Wolf SP-интерфейса.

Полный pseudocode и snapshot оставлены только в локальных
`evidence/getgameapi.local.json` и `evidence/game.snapshot.json`.
Оригинальная DLL не загружалась как исполняемый модуль и не модифицировалась;
анализ выполнялся на private immutable copy. CLI session закрылась после
выдачи результата. Бинарные хеши Wolf2.exe и Gamex86.dll после анализа совпали
с исходными значениями REA.

```powershell
.\tools\rea.ps1 --version
.\tools\rea.ps1 doctor --client codex --provider ghidra --skill --json
```

Сохранённый snapshot содержит результаты выполненного анализа; это не
постоянная Ghidra GUI database. Новый запрос, которого нет в snapshot,
может потребовать повторного cold import.
