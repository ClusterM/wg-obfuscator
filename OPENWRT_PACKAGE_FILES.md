# Файлы пакета wg-obfuscator для OpenWRT

## Созданные файлы

### Основные файлы пакета

1. **Makefile.openwrt** - Основной Makefile для сборки пакета OpenWRT
2. **Makefile.openwrt.meta** - Расширенная версия Makefile с дополнительными функциями

### Файлы конфигурации (папка files/)

3. **files/wg-obfuscator.init** - Init скрипт для управления сервисом
4. **files/wg-obfuscator.uci** - Скрипт для работы с UCI конфигурацией
5. **files/wg-obfuscator.uci.defaults** - Скрипт установки значений по умолчанию для UCI
6. **files/wg-obfuscator.hotplug** - Hotplug скрипт для автоматического запуска/остановки
7. **files/wg-obfuscator.conf.example** - Пример конфигурационного файла

### Документация

8. **README.OpenWRT.md** - Руководство пользователя для OpenWRT
9. **INTEGRATION.OpenWRT.md** - Инструкции по интеграции в OpenWRT
10. **BUILD.OpenWRT.md** - Подробные инструкции по сборке
11. **OPENWRT_PACKAGE_FILES.md** - Этот файл со списком всех файлов

### Пустые папки

12. **patches/** - Папка для патчей (если потребуются)

## Структура для интеграции в OpenWRT

```
feeds/packages/net/wg-obfuscator/
├── Makefile                    # Скопировать из Makefile.openwrt
├── files/                      # Скопировать всю папку files/
│   ├── wg-obfuscator.init
│   ├── wg-obfuscator.uci
│   ├── wg-obfuscator.uci.defaults
│   ├── wg-obfuscator.hotplug
│   └── wg-obfuscator.conf.example
└── patches/                    # Создать пустую папку
```

## Быстрый старт

1. Скопируйте `Makefile.openwrt` как `Makefile` в папку пакета
2. Скопируйте папку `files/` в папку пакета
3. Создайте пустую папку `patches/`
4. Следуйте инструкциям в `BUILD.OpenWRT.md`

## Особенности пакета

- **Минимальные зависимости**: только libc
- **Поддержка UCI**: полная интеграция с системой конфигурации OpenWRT
- **Автозапуск**: поддержка hotplug для автоматического запуска при поднятии интерфейса
- **Множественные экземпляры**: поддержка нескольких экземпляров obfuscator'а
- **Логирование**: интеграция с системой логирования OpenWRT
- **Управление сервисом**: стандартные init скрипты OpenWRT

## Поддерживаемые архитектуры

- x86_64
- ARM64 (aarch64)
- ARMv7 (armv7)
- MIPS
- MIPS64
- PowerPC
- и другие архитектуры, поддерживаемые OpenWRT

## Требования

- OpenWRT 19.07 или новее
- Минимум 1MB свободного места
- Минимум 512KB RAM

## Лицензия

GPL-2.0 (совместимо с OpenWRT)

## Автор

ClusterM <cluster@cluster.wtf>

## Ссылки

- [Официальный репозиторий wg-obfuscator](https://github.com/ClusterM/wg-obfuscator)
- [OpenWRT Documentation](https://openwrt.org/docs)
- [OpenWRT Package Development](https://openwrt.org/docs/guide-developer/packages)