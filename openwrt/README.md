# WireGuard Obfuscator для OpenWRT

Этот пакет предоставляет WireGuard Obfuscator для OpenWRT, позволяя обфусцировать трафик WireGuard для обхода DPI (Deep Packet Inspection).

## Установка

### Через opkg (если пакет доступен в репозитории)
```bash
opkg update
opkg install wg-obfuscator
```

### Сборка из исходного кода

1. Скопируйте файлы пакета в папку `feeds/packages/net/wg-obfuscator/` вашего OpenWRT SDK
2. Выполните сборку:
```bash
make package/wg-obfuscator/compile
```

## Конфигурация

Отредактируйте файл `/etc/wg-obfuscator.conf`:

```ini
[main]
source-if = 0.0.0.0
source-lport = 13255
target = your-server.com:13255
key = your-secret-key
masking = AUTO
verbose = INFO
max-clients = 1024
idle-timeout = 300
max-dummy-length-data = 4
```

## Использование

1. Настройте WireGuard для подключения к локальному адресу obfuscator'а:
```ini
[Peer]
Endpoint = 127.0.0.1:13255
```

2. Запустите obfuscator:
```bash
/etc/init.d/wg-obfuscator start
```

3. Запустите WireGuard как обычно

## Управление сервисом

```bash
# Запуск
/etc/init.d/wg-obfuscator start

# Остановка
/etc/init.d/wg-obfuscator stop

# Перезапуск
/etc/init.d/wg-obfuscator restart

# Перезагрузка конфигурации
/etc/init.d/wg-obfuscator reload

# Статус
/etc/init.d/wg-obfuscator status
```

## Автозапуск

Для автоматического запуска при загрузке системы:
```bash
/etc/init.d/wg-obfuscator enable
```

## Логи

Логи сервиса можно просмотреть через:
```bash
logread | grep wg-obfuscator
```

## Поддерживаемые архитектуры

Пакет поддерживает все архитектуры, поддерживаемые OpenWRT:
- x86_64
- ARM64
- ARMv7
- MIPS
- MIPS64
- и другие

## Требования

- OpenWRT 19.07 или новее
- Минимум 1MB свободного места
- Минимум 512KB RAM

## Примечания

- Убедитесь, что порт, указанный в `source-lport`, не используется другими сервисами
- Для работы в режиме сервера может потребоваться настройка файрвола для проброса портов
- При использовании `fwmark` убедитесь, что obfuscator запускается с правами root

## Поддержка

Для получения помощи и сообщения об ошибках обращайтесь к:
- [Официальному репозиторию](https://github.com/ClusterM/wg-obfuscator)
- [Документации OpenWRT](https://openwrt.org/docs)