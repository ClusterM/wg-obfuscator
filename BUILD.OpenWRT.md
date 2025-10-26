# Инструкции по сборке wg-obfuscator для OpenWRT

## Подготовка окружения

### 1. Установка OpenWRT SDK

Скачайте и распакуйте OpenWRT SDK для вашей архитектуры:
```bash
# Пример для x86_64
wget https://downloads.openwrt.org/releases/23.05.0/targets/x86/64/openwrt-sdk-23.05.0-x86-64_gcc-12.3.0_musl.Linux-x86_64.tar.xz
tar -xf openwrt-sdk-23.05.0-x86-64_gcc-12.3.0_musl.Linux-x86_64.tar.xz
cd openwrt-sdk-23.05.0-x86-64_gcc-12.3.0_musl.Linux-x86_64
```

### 2. Настройка окружения

```bash
# Установка переменных окружения
export PATH=$PATH:$(pwd)/staging_dir/toolchain-x86_64_gcc-12.3.0_musl/bin
export STAGING_DIR=$(pwd)/staging_dir
export CC=x86_64-openwrt-linux-musl-gcc
export CXX=x86_64-openwrt-linux-musl-g++
export AR=x86_64-openwrt-linux-musl-ar
export RANLIB=x86_64-openwrt-linux-musl-ranlib
export STRIP=x86_64-openwrt-linux-musl-strip
```

## Сборка пакета

### 1. Копирование файлов

```bash
# Создание директории пакета
mkdir -p package/wg-obfuscator

# Копирование файлов
cp Makefile.openwrt package/wg-obfuscator/Makefile
cp -r files package/wg-obfuscator/
cp -r patches package/wg-obfuscator/ 2>/dev/null || true
```

### 2. Сборка

```bash
# Обновление feeds
./scripts/feeds update -a

# Установка пакета
./scripts/feeds install wg-obfuscator

# Конфигурация
make menuconfig
# Выберите: Network -> wg-obfuscator

# Сборка пакета
make package/wg-obfuscator/compile V=s

# Сборка с очисткой
make package/wg-obfuscator/clean
make package/wg-obfuscator/compile V=s
```

### 3. Проверка результата

```bash
# Проверка созданных файлов
ls -la bin/packages/*/base/wg-obfuscator*

# Проверка содержимого пакета
tar -tf bin/packages/*/base/wg-obfuscator*.ipk
```

## Сборка для разных архитектур

### ARM64 (aarch64)
```bash
# Скачайте SDK для ARM64
wget https://downloads.openwrt.org/releases/23.05.0/targets/bcm27xx/bcm2711/openwrt-sdk-23.05.0-bcm27xx-bcm2711_gcc-12.3.0_musl.Linux-x86_64.tar.xz
tar -xf openwrt-sdk-23.05.0-bcm27xx-bcm2711_gcc-12.3.0_musl.Linux-x86_64.tar.xz
cd openwrt-sdk-23.05.0-bcm27xx-bcm2711_gcc-12.3.0_musl.Linux-x86_64
```

### MIPS
```bash
# Скачайте SDK для MIPS
wget https://downloads.openwrt.org/releases/23.05.0/targets/ramips/mt7621/openwrt-sdk-23.05.0-ramips-mt7621_gcc-12.3.0_musl.Linux-x86_64.tar.xz
tar -xf openwrt-sdk-23.05.0-ramips-mt7621_gcc-12.3.0_musl.Linux-x86_64.tar.xz
cd openwrt-sdk-23.05.0-ramips-mt7621_gcc-12.3.0_musl.Linux-x86_64
```

## Автоматическая сборка

Создайте скрипт для автоматической сборки:

```bash
#!/bin/bash
# build-wg-obfuscator.sh

set -e

ARCH=$1
if [ -z "$ARCH" ]; then
    echo "Usage: $0 <architecture>"
    echo "Supported architectures: x86_64, aarch64, mips, mips64"
    exit 1
fi

case $ARCH in
    x86_64)
        SDK_URL="https://downloads.openwrt.org/releases/23.05.0/targets/x86/64/openwrt-sdk-23.05.0-x86-64_gcc-12.3.0_musl.Linux-x86_64.tar.xz"
        ;;
    aarch64)
        SDK_URL="https://downloads.openwrt.org/releases/23.05.0/targets/bcm27xx/bcm2711/openwrt-sdk-23.05.0-bcm27xx-bcm2711_gcc-12.3.0_musl.Linux-x86_64.tar.xz"
        ;;
    mips)
        SDK_URL="https://downloads.openwrt.org/releases/23.05.0/targets/ramips/mt7621/openwrt-sdk-23.05.0-ramips-mt7621_gcc-12.3.0_musl.Linux-x86_64.tar.xz"
        ;;
    *)
        echo "Unsupported architecture: $ARCH"
        exit 1
        ;;
esac

echo "Building for architecture: $ARCH"
echo "Downloading SDK..."

wget -q "$SDK_URL"
SDK_FILE=$(basename "$SDK_URL")
tar -xf "$SDK_FILE"
rm "$SDK_FILE"

SDK_DIR=$(basename "$SDK_FILE" .tar.xz)
cd "$SDK_DIR"

echo "Setting up environment..."
export PATH=$PATH:$(pwd)/staging_dir/toolchain-*/bin
export STAGING_DIR=$(pwd)/staging_dir

echo "Copying package files..."
mkdir -p package/wg-obfuscator
cp ../Makefile.openwrt package/wg-obfuscator/Makefile
cp -r ../files package/wg-obfuscator/

echo "Building package..."
make package/wg-obfuscator/compile V=s

echo "Build completed!"
echo "Package location: $(pwd)/bin/packages/*/base/wg-obfuscator*.ipk"
```

Использование:
```bash
chmod +x build-wg-obfuscator.sh
./build-wg-obfuscator.sh x86_64
./build-wg-obfuscator.sh aarch64
./build-wg-obfuscator.sh mips
```

## Установка на устройство

### Через opkg
```bash
# Копирование пакета на устройство
scp wg-obfuscator*.ipk root@192.168.1.1:/tmp/

# Установка
ssh root@192.168.1.1 "opkg install /tmp/wg-obfuscator*.ipk"
```

### Через веб-интерфейс
1. Зайдите в веб-интерфейс OpenWRT
2. Перейдите в System -> Software
3. Загрузите .ipk файл
4. Установите пакет

## Проверка установки

```bash
# Проверка установки
opkg list-installed | grep wg-obfuscator

# Проверка конфигурации
ls -la /etc/wg-obfuscator.conf

# Проверка сервиса
/etc/init.d/wg-obfuscator status

# Запуск сервиса
/etc/init.d/wg-obfuscator start
```

## Отладка

Если сборка не удается:

1. Проверьте логи сборки:
```bash
make package/wg-obfuscator/compile V=s 2>&1 | tee build.log
```

2. Проверьте зависимости:
```bash
make package/wg-obfuscator/check-depends
```

3. Очистите и пересоберите:
```bash
make package/wg-obfuscator/clean
make package/wg-obfuscator/compile V=s
```