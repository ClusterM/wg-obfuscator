# Интеграция wg-obfuscator в OpenWRT

## Структура файлов

```
feeds/packages/net/wg-obfuscator/
├── Makefile                    # Основной Makefile пакета
├── files/                      # Файлы для установки
│   ├── wg-obfuscator.init     # Init скрипт
│   ├── wg-obfuscator.uci      # UCI конфигурация
│   ├── wg-obfuscator.uci.defaults  # UCI значения по умолчанию
│   └── wg-obfuscator.hotplug  # Hotplug скрипт
└── patches/                    # Патчи (если нужны)
```

## Интеграция в OpenWRT

### 1. Добавление в feeds

1. Скопируйте папку `wg-obfuscator` в `feeds/packages/net/`
2. Обновите feeds:
```bash
./scripts/feeds update packages
./scripts/feeds install wg-obfuscator
```

### 2. Настройка сборки

В конфигурации OpenWRT включите пакет:
```bash
make menuconfig
# Network -> wg-obfuscator
```

### 3. Сборка

```bash
make package/wg-obfuscator/compile V=s
```

## Конфигурация UCI

Пакет поддерживает конфигурацию через UCI. Структура конфигурации:

```bash
# Основная секция
uci set wg_obfuscator.@instance[0]=instance
uci set wg_obfuscator.@instance[0].name=main
uci set wg_obfuscator.@instance[0].enabled=1
uci set wg_obfuscator.@instance[0].source_if=0.0.0.0
uci set wg_obfuscator.@instance[0].source_lport=13255
uci set wg_obfuscator.@instance[0].target=your-server.com:13255
uci set wg_obfuscator.@instance[0].key=your-secret-key
uci set wg_obfuscator.@instance[0].masking=AUTO
uci set wg_obfuscator.@instance[0].verbose=INFO
uci set wg_obfuscator.@instance[0].max_clients=1024
uci set wg_obfuscator.@instance[0].idle_timeout=300
uci set wg_obfuscator.@instance[0].max_dummy_length_data=4
uci set wg_obfuscator.@instance[0].fwmark=0
uci commit wg_obfuscator
```

## Поддерживаемые архитектуры

Пакет поддерживает все архитектуры OpenWRT:
- x86_64
- ARM64 (aarch64)
- ARMv7 (armv7)
- MIPS
- MIPS64
- PowerPC
- и другие

## Зависимости

Пакет не имеет внешних зависимостей, кроме стандартной библиотеки C.

## Размер пакета

- Бинарный файл: ~50KB
- Конфигурационные файлы: ~5KB
- Общий размер: ~60KB

## Требования к системе

- OpenWRT 19.07 или новее
- Минимум 1MB свободного места
- Минимум 512KB RAM для работы

## Тестирование

Для тестирования пакета:

1. Соберите образ OpenWRT с пакетом
2. Установите на устройство
3. Настройте конфигурацию
4. Запустите сервис
5. Проверьте логи

## Отладка

Для отладки используйте:
```bash
# Логи сервиса
logread | grep wg-obfuscator

# Ручной запуск с отладкой
wg-obfuscator -c /etc/wg-obfuscator.conf --verbose=DEBUG

# Проверка конфигурации
uci show wg_obfuscator
```

## Безопасность

- Пакет запускается с правами root (необходимо для работы с сокетами)
- Конфигурационные файлы имеют права доступа 644
- Бинарный файл имеет права доступа 755

## Обновления

При обновлении пакета:
1. Конфигурационные файлы сохраняются
2. UCI конфигурация сохраняется
3. Сервис автоматически перезапускается

## Удаление

При удалении пакета:
1. Сервис останавливается
2. UCI конфигурация удаляется
3. Конфигурационные файлы сохраняются (как conffiles)