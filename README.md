# Phobos

Автоматизация развертывания защищенного WireGuard VPN с обфускацией трафика через `wg-obfuscator`.

## Описание

**Phobos** автоматизирует настройку обфусцированного WireGuard соединения между VPS сервером и клиентскими роутерами (Entware).

### Ключевые особенности

- Случайная генерация UDP порта для obfuscator (10000-60000)
- Сборка wg-obfuscator из исходников с кросс-компиляцией для роутеров (mipsel, mips, aarch64)
- Автоматическое определение архитектуры роутера и выбор правильного бинарника
- Управление через итерактивное меню (VPS)

## Функции

### Интерактивное меню управления

Система включает интерактивное меню управления.

**Основные возможности меню:**
- Управление сервисами (start/stop/status/logs для WireGuard, obfuscator, HTTP сервера)
- Управление клиентами (создание, удаление, пересоздание конфигураций)
- Системные функции (health checks, мониторинг клиентов, очистка токенов)
- Резервное копирование конфигураций
- Настройка параметров obfuscator

## Быстрый старт

### 1. Установка на VPS

Запустите установку:

```bash
bash -c "$(curl -fsSL https://raw.githubusercontent.com/Ground-Zerro/Phobos/main/phobos-deploy.sh)" </dev/tty
```

Система выполнит:
- Проверку и установку необходимых системных зависимостей
- Клонирование исходного кода `wg-obfuscator` из репозитория
- Сборку нативного бинарника для VPS сервера
- Установку cross-compile toolchain'ов для архитектур mipsel, mips и aarch64
- Кросс-компиляцию wg-obfuscator для всех поддерживаемых архитектур роутеров
- Создание и настройку конфигурационных файлов для WireGuard и obfuscator
- Настройку фаервола для разрешения необходимого трафика
- Создание первого клиента
- Генерацию установочного пакета с бинарниками и конфигурациями
- Запуск HTTP-сервера для раздачи установочных пакетов
- Генерацию одноразовой команды установки с уникальным токеном
- Вывод готовой HTTP-ссылки для установки на роутере Keenetic

### 2. Установка на роутер Keenetic

Отправьте ссылку клиенту, он выполняет ее на роутере в терминале (Entware), пример:

```bash
wget -O - http://<server_ip>:8080/init/<token>.sh | sh
```

Скрипт:
- Скачает установочный пакет
- Определит архитектуру роутера
- Установит правильный бинарник wg-obfuscator
- Настроит автозапуск obfuscator
- Настроит WireGuard через RCI API
- Создаст интерфейс "Phobos-{client_name}"
- Активирует подключение
- Проверит handshake

## License

This project is licensed under a **Proprietary License**.  
See the [LICENSE](./LICENSE) file for full terms.

## External dependency: wg-obfuscator (GPL-3.0)

This repository provides a wrapper that automates configuration or invocation of `wg-obfuscator`.  
This project does NOT include, distribute, or modify `wg-obfuscator` source or binaries.

/* RU — для внутренней справки */
Этот проект содержит только обвязку. Бинарники/исходники `wg-obfuscator` не включены и не распространяются здесь.

## Благодарности

- [ClusterM/wg-obfuscator](https://github.com/ClusterM/wg-obfuscator) — инструмент обфускации WireGuard трафика /[Поблагадарить Алексея и поддержать его разработку](https://boosty.to/cluster)/
- [WireGuard](https://www.wireguard.com/) — современный VPN протокол

## Поддержка

**Угостить автора чашечкой какао можно на** [Boosty](https://boosty.to/ground_zerro) ❤️
