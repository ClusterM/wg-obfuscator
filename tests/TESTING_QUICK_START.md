# Quick Start: Testing Without WireGuard

## Быстрый старт

### 1. Запустить все тесты одной командой

```bash
make test
```

### 2. Запустить только unit тесты (< 1 сек)

```bash
make test-unit
```

**Результат:**
```
=== WireGuard Obfuscator Unit Tests ===
[TEST] WireGuard packet type detection... PASS
[TEST] XOR data function... PASS
[TEST] Encode/decode handshake roundtrip... PASS
...
Passed: 10
Failed: 0
Total:  10
```

### 3. Интеграционные тесты (полный flow)

```bash
make test-integration
```

Что тестируется:
- Запуск fake WireGuard сервера
- Запуск обфускатора
- Отправка WireGuard пакетов
- Проверка обработки handshake

## Ручное тестирование (интерактивно)

### Вариант 1: Простой эхо-тест

**Терминал 1** - Запустить fake WireGuard сервер:
```bash
cd tests
./test_wg_emulator server 51820
```

**Терминал 2** - Запустить обфускатор (из корня проекта):
```bash
./wg-obfuscator -p 3333 -t 127.0.0.1:51820 -k "test_key" -v DEBUG
```

**Терминал 3** - Отправить тестовые пакеты:
```bash
cd tests
./test_wg_emulator client 127.0.0.1 3333
```

Вы увидите:
- Клиент отправляет handshake и data пакеты
- Обфускатор обрабатывает и пересылает
- Сервер отвечает
- В логах видна вся активность

### Вариант 2: Тест с разными ключами

**Терминал 1:**
```bash
cd tests
./test_wg_emulator server 51820
```

**Терминал 2:**
```bash
./wg-obfuscator -p 3333 -t 127.0.0.1:51820 -k "secret123" -v DEBUG
```

**Терминал 3:**
```bash
./wg-obfuscator -p 4444 -t 127.0.0.1:51820 -k "another_key" -v DEBUG
```

**Терминал 4:**
```bash
cd tests
# Отправить через первый обфускатор
./test_wg_emulator client 127.0.0.1 3333

# Затем через второй
./test_wg_emulator client 127.0.0.1 4444
```

### Вариант 3: Тест с маскировкой STUN

**Терминал 1:**
```bash
cd tests
./test_wg_emulator server 51820
```

**Терминал 2:**
```bash
./wg-obfuscator -p 3333 -t 127.0.0.1:51820 -k "key" -m STUN -v DEBUG
```

**Терминал 3:**
```bash
cd tests
./test_wg_emulator client 127.0.0.1 3333
```

## Что тестируют unit тесты

| Тест | Что проверяет |
|------|---------------|
| `test_wg_packet_detection` | Распознавание типов WireGuard пакетов (handshake, data, cookie) |
| `test_xor_data` | XOR шифрование обратимо (encrypt -> decrypt = original) |
| `test_encode_decode_handshake` | Кодирование/декодирование handshake пакетов |
| `test_encode_decode_data` | Кодирование/декодирование data пакетов |
| `test_wrong_key` | Неправильный ключ -> неправильная расшифровка |
| `test_dummy_padding_handshake` | Случайные dummy данные добавляются |
| `test_version_detection` | Определение версии обфускации |
| `test_minimum_packet_size` | Минимальные пакеты (4 байта) |
| `test_large_packet` | MTU-размер пакетов (1400 байт) |
| `test_different_key_lengths` | Разные длины ключей (1-70+ символов) |

## Интерпретация результатов

### ✓ PASS - Всё хорошо

```
[TEST] XOR data function... PASS
```

### ✗ FAIL - Есть проблема

```
[TEST] Encode/decode handshake roundtrip... FAIL: Decoded length doesn't match original
```

Смотрите детали в сообщении об ошибке.

## Отладка

### Посмотреть логи интеграционных тестов

```bash
SHOW_LOGS=1 make test-integration
```

или

```bash
cat /tmp/wg-obfuscator-test/obfuscator.log
cat /tmp/wg-obfuscator-test/wg_client.log
cat /tmp/wg-obfuscator-test/wg_server.log
```

### Повесились процессы

```bash
killall test_wg_emulator wg-obfuscator
make clean-tests
```

### Порт занят

```bash
ss -ulnp | grep -E '(3333|51820|4444)'
```

## Примеры сценариев тестирования

### Сценарий 1: Проверка после изменения кода

```bash
# 1. Внесли изменения в obfuscation.h
# 2. Проверяем что ничего не сломалось
make clean && make test-unit

# 3. Если unit тесты прошли, проверяем интеграцию
make test-integration
```

### Сценарий 2: Разработка новой функции

```bash
# 1. Пишем unit тест для новой функции в test_harness.c
# 2. Запускаем тест (он должен FAIL)
make test-unit

# 3. Реализуем функцию
# 4. Тест должен стать PASS
make test-unit
```

### Сценарий 3: Проверка производительности

```bash
# Запустить fake сервер
./test_wg_emulator server 51820 &

# Запустить обфускатор с DEBUG
./wg-obfuscator -p 3333 -t 127.0.0.1:51820 -k "key" -v DEBUG &

# Отправить много пакетов
for i in {1..100}; do
    ./test_wg_emulator client 127.0.0.1 3333
    sleep 0.1
done

# Проверить логи на утечки памяти, ошибки
```

## Часто задаваемые вопросы

**Q: Почему интеграционные тесты иногда показывают "timeout"?**

A: Это нормально для неОБФУСЦИРОВАННЫХ пакетов. Обфускатор игнорирует plain WireGuard пакеты, пока не получит обфусцированный handshake.

**Q: Зачем нужен fake WireGuard если есть настоящий?**

A: Для быстрого тестирования без настройки интерфейсов, ключей, конфигов. Unit тесты работают < 1 сек.

**Q: Можно ли тестировать с реальным WireGuard?**

A: Да! Тестовый стенд не заменяет полное тестирование, но позволяет быстро проверять базовую функциональность.

**Q: Как добавить свой тест?**

A: См. `TESTING.md` раздел "Adding New Tests"

## Следующие шаги

После успешных unit тестов рекомендуем:
1. Протестировать с реальным WireGuard (см. README.md)
2. Проверить на production-like окружении
3. Протестировать с разными MTU
4. Проверить long-running stability

Для полной документации см. **TESTING.md**
