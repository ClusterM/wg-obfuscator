# Добавление wg-obfuscator в официальный репозиторий OpenWRT

## Необходимые файлы

Для добавления в официальный репозиторий OpenWRT нужны только 2 файла:

1. **Makefile** - основной файл пакета
2. **files/wg-obfuscator.init** - init скрипт

## Структура для репозитория

```
feeds/packages/net/wg-obfuscator/
├── Makefile
└── files/
    └── wg-obfuscator.init
```

## Процесс добавления

1. **Форкните репозиторий OpenWRT**:
   ```bash
   git clone https://github.com/openwrt/packages.git
   cd packages
   ```

2. **Создайте папку пакета**:
   ```bash
   mkdir -p net/wg-obfuscator/files
   ```

3. **Скопируйте файлы**:
   ```bash
   cp openwrt/Makefile net/wg-obfuscator/
   cp openwrt/files/wg-obfuscator.init net/wg-obfuscator/files/
   ```

4. **Создайте коммит**:
   ```bash
   git add net/wg-obfuscator/
   git commit -m "net/wg-obfuscator: add WireGuard Obfuscator package

   - Add wg-obfuscator package for obfuscating WireGuard traffic
   - Supports DPI evasion through masking (STUN protocol)
   - Minimal dependencies, only requires libc
   - Includes init script for procd integration"
   ```

5. **Создайте Pull Request** в официальный репозиторий OpenWRT

## Проверка перед отправкой

1. **Проверьте сборку**:
   ```bash
   make package/wg-obfuscator/compile V=s
   ```

2. **Проверьте зависимости**:
   ```bash
   make package/wg-obfuscator/check-depends
   ```

3. **Проверьте установку**:
   ```bash
   make package/wg-obfuscator/install
   ```

## Описание пакета

- **Название**: wg-obfuscator
- **Версия**: 1.4
- **Лицензия**: GPL-2.0
- **Категория**: Network
- **Зависимости**: +libc
- **Размер**: ~50KB

## Особенности

- Минимальные зависимости (только libc)
- Поддержка всех архитектур OpenWRT
- Интеграция с procd
- Автоматический перезапуск при изменении конфигурации
- Поддержка множественных экземпляров через конфигурационный файл

## Контакты

- **Maintainer**: ClusterM <cluster@cluster.wtf>
- **URL**: https://github.com/ClusterM/wg-obfuscator
- **License**: GPL-2.0