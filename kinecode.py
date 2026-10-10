#!/usr/bin/env python3
"""kinecode - терминальный клиент для OpenAI-совместимых API
(DeepSeek, Groq, OpenRouter, NVIDIA NIM, Google и любые свои).
Только стандартная библиотека Python 3.8+, ставить ничего не нужно."""
import atexit
import base64
import getpass
import json
import mimetypes
import os
import readline
import sys
import urllib.error
import urllib.request

APP = "kinecode"
VERSION = "0.2"
CONF_DIR = os.path.join(
    os.environ.get("XDG_CONFIG_HOME", os.path.expanduser("~/.config")), APP)
CONF_FILE = os.path.join(CONF_DIR, "config.json")
HIST_FILE = os.path.join(CONF_DIR, "history")

BANNER = r"""
______  _____ _____ ______  _____ _____ _____ _   __
|  _  \|  ___|  ___|| ___ \/  ___|  ___|  ___| | / /
| | | || |__ | |__  | |_/ /\ `--.| |__ | |__ | |/ /
| | | ||  __||  __| |  __/  `--. \  __||  __||    \
| |/ / | |___| |___ | |    /\__/ / |___| |___| |\  \
|___/  \____/\____/ \_|    \____/\____/\____/\_| \_/

 _   _   ___  ______ _   _  _____ _____ _____
| | | | / _ \ | ___ \ \ | ||  ___/  ___/  ___|
| |_| |/ /_\ \| |_/ /  \| || |__ \ `--.\ `--.
|  _  ||  _  ||    /| . ` ||  __| `--. \`--. \
| | | || | | || |\ \| |\  || |___/\__/ /\__/ /
\_| |_/\_| |_/\_| \_\_| \_/\____/\____/\____/
"""

# Заготовки: base_url и примерные модели. Актуальный список подтянет /fetchmodels
PRESETS = {
    "deepseek": ("https://api.deepseek.com/v1",
                 ["deepseek-v4-flash", "deepseek-v4-pro"]),
    "groq": ("https://api.groq.com/openai/v1",
             ["openai/gpt-oss-120b", "openai/gpt-oss-20b",
              "llama-3.3-70b-versatile"]),
    "openrouter": ("https://openrouter.ai/api/v1",
                   ["deepseek/deepseek-v4-flash:free"]),
    "nvidia": ("https://integrate.api.nvidia.com/v1",
               ["deepseek-ai/deepseek-v4-pro"]),
    "google": ("https://generativelanguage.googleapis.com/v1beta/openai",
               ["gemini-2.5-flash"]),
}

DEFAULT_SYSTEM = "Ты помощник по программированию. Отвечай кратко и по делу."

COLOR = sys.stdout.isatty() and not os.environ.get("NO_COLOR")


def paint(code, s):
    return f"\033[{code}m{s}\033[0m" if COLOR else s


def dim(s): return paint("90", s)
def red(s): return paint("31", s)
def green(s): return paint("32", s)
def yellow(s): return paint("33", s)
def cyan(s): return paint("1;36", s)


def rl_paint(code, s):
    """Цвет для приглашения readline (невидимые символы в \\001..\\002)."""
    return f"\001\033[{code}m\002{s}\001\033[0m\002" if COLOR else s


class APIError(Exception):
    pass


# ---------- конфиг ----------

def load():
    if os.path.exists(CONF_FILE):
        try:
            with open(CONF_FILE, encoding="utf-8") as f:
                cfg = json.load(f)
        except (OSError, ValueError):
            print(red(f"Не смог прочитать {CONF_FILE}, начинаю с пустого."))
            cfg = {}
    else:
        cfg = {}
    cfg.setdefault("providers", {})
    cfg.setdefault("active", {"provider": None, "model": None})
    cfg.setdefault("system", DEFAULT_SYSTEM)
    return cfg


def save(cfg):
    os.makedirs(CONF_DIR, exist_ok=True)
    with open(CONF_FILE, "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)
    os.chmod(CONF_FILE, 0o600)  # в файле лежат ключи


def get_key(name, p):
    env = name.upper().replace("-", "_") + "_API_KEY"
    return p.get("key") or os.environ.get(env, "")


# ---------- HTTP ----------

def request(method, url, key, payload=None, stream=False, timeout=120):
    headers = {
        "Content-Type": "application/json",
        "Accept": "text/event-stream" if stream else "application/json",
        "User-Agent": f"{APP}/{VERSION}",
    }
    if key:
        headers["Authorization"] = "Bearer " + key
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        return urllib.request.urlopen(req, timeout=timeout)
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", "replace")[:500]
        msg = body.strip()
        try:
            err = json.loads(body).get("error")
            msg = err.get("message", msg) if isinstance(err, dict) else (err or msg)
        except Exception:
            pass
        hints = {
            401: "неверный или пустой ключ (/key)",
            403: "доступ запрещён",
            404: "не найдено: проверь модель (/models) и base_url",
            429: "лимит запросов: подожди или смени модель/провайдера "
                 "(/model, /provider)",
        }
        raise APIError(f"HTTP {e.code}: {hints.get(e.code, '')}\n{msg}")
    except urllib.error.URLError as e:
        raise APIError(f"нет соединения: {e.reason}")
    except OSError as e:
        raise APIError(f"ошибка сети: {e}")


def fetch_models(name, p):
    with request("GET", p["base_url"] + "/models", get_key(name, p),
                 timeout=30) as r:
        obj = json.load(r)
    items = obj.get("data") or obj.get("models") or []
    ids = set()
    for it in items:
        mid = it.get("id") if isinstance(it, dict) else it
        if mid:
            ids.add(str(mid).replace("models/", "", 1))
    return sorted(ids)


def chat_stream(name, p, model, messages):
    payload = {"model": model, "messages": messages, "stream": True}
    with request("POST", p["base_url"] + "/chat/completions",
                 get_key(name, p), payload, stream=True) as r:
        for raw in r:
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("data:"):
                continue
            data = line[5:].strip()
            if data == "[DONE]":
                break
            try:
                obj = json.loads(data)
            except ValueError:
                continue
            if obj.get("error"):
                raise APIError(str(obj["error"]))
            choices = obj.get("choices") or []
            if not choices:
                continue
            delta = choices[0].get("delta") or {}
            think = delta.get("reasoning_content") or delta.get("reasoning")
            if think:
                yield "think", think
            if delta.get("content"):
                yield "text", delta["content"]


# ---------- команды ----------

ABOUT = """kinecode v{version} — минималистичный терминальный клиент
для OpenAI-совместимых API.

Created by fawwelll, Telegramm channel @jailbraked

Только стандартная библиотека Python, ничего ставить не нужно."""


HELP = """Команды:
  /providers              список провайдеров
  /provider [имя]         переключить провайдера
  /models                 модели текущего провайдера
  /model [имя|номер]      выбрать модель (можно вписать любое имя)
  /fetchmodels            обновить список моделей с сервера провайдера
  /addprovider [имя]      добавить провайдера (заготовки: {presets})
  /delprovider имя        удалить провайдера
  /key [имя]              сменить API-ключ
  /system [текст]         показать/задать системный промпт
  /think on|off           показывать рассуждения модели
  /file <путь> [вопрос]   отправить текстовый файл
  /img <путь> [вопрос]    отправить фото (png/jpg/webp/gif)
  /about                  о проекте
  /clear                  очистить диалог
  /help  /exit
Строка, оканчивающаяся на \\, продолжается на следующей строке.
Ctrl+C прерывает ответ."""


def choose(items, current=None):
    for i, it in enumerate(items, 1):
        mark = green("●") if it == current else " "
        print(f" {mark} {i:>2}. {it}")
    try:
        ans = input(dim("номер (Enter - отмена): ")).strip()
    except (EOFError, KeyboardInterrupt):
        print()
        return None
    if ans.isdigit() and 1 <= int(ans) <= len(items):
        return items[int(ans) - 1]
    return None


def active(cfg):
    name = cfg["active"].get("provider")
    p = cfg["providers"].get(name)
    return (name, p) if p else (None, None)


def set_provider(cfg, name):
    cfg["active"]["provider"] = name
    models = cfg["providers"][name].get("models", [])
    if cfg["active"].get("model") not in models:
        cfg["active"]["model"] = models[0] if models else None
    save(cfg)


def refresh_models(cfg, name, quiet=False):
    p = cfg["providers"][name]
    try:
        ids = fetch_models(name, p)
    except (APIError, ValueError) as e:
        if not quiet:
            print(red(f"Не удалось получить список моделей: {e}"))
        return False
    if ids:
        p["models"] = ids
        save(cfg)
        if not quiet:
            print(green(f"Моделей получено: {len(ids)}"))
        return True
    return False


def add_provider(cfg, arg):
    name = arg.strip() or input("Имя провайдера: ").strip()
    if not name:
        return
    preset = PRESETS.get(name)
    default_url = preset[0] if preset else ""
    url = input(f"base_url [{default_url}]: ").strip() or default_url
    if not url:
        print(red("base_url обязателен (например https://host/v1)"))
        return
    key = getpass.getpass("API-ключ (ввод скрыт): ").strip()
    cfg["providers"][name] = {
        "base_url": url.rstrip("/"),
        "key": key,
        "models": list(preset[1]) if preset else [],
    }
    print(dim("Запрашиваю список моделей..."))
    if not refresh_models(cfg, name, quiet=True) and not preset:
        raw = input("Сервер не отдал список. Модели через запятую: ")
        cfg["providers"][name]["models"] = [
            m.strip() for m in raw.split(",") if m.strip()]
    save(cfg)
    if not active(cfg)[0]:
        set_provider(cfg, name)
    print(green(f"Провайдер '{name}' добавлен. Переключиться: /provider {name}"))



# ---------- файлы / фото (минимализм) ----------

MAX_FILE_CHARS = 120_000  # ~30k токенов
IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".webp", ".gif"}

def _guess_mime(path):
    mt, _ = mimetypes.guess_type(path)
    if mt and mt.startswith("image/"):
        return mt
    ext = os.path.splitext(path)[1].lower()
    return {".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".png": "image/png",
            ".webp": "image/webp", ".gif": "image/gif"}.get(ext, mt or "application/octet-stream")

def _is_image(path):
    return os.path.splitext(path)[1].lower() in IMAGE_EXTS

def _read_text_file(path):
    # читаем как текст, лимит по размеру
    size = os.path.getsize(path)
    if size > 2_000_000:
        raise APIError(f"файл слишком большой ({size//1024}KB), максимум 2MB для /file")
    with open(path, "r", encoding="utf-8", errors="strict") as f:
        data = f.read(MAX_FILE_CHARS + 1)
    trunc = len(data) > MAX_FILE_CHARS
    if trunc:
        data = data[:MAX_FILE_CHARS]
    return data, trunc

def _encode_image(path):
    size = os.path.getsize(path)
    if size > 10_000_000:
        raise APIError(f"фото слишком большое ({size//1024}KB), максимум 10MB")
    mime = _guess_mime(path)
    if not mime.startswith("image/"):
        raise APIError(f"неизвестный формат фото: {mime}")
    with open(path, "rb") as f:
        b64 = base64.b64encode(f.read()).decode("ascii")
    return f"data:{mime};base64,{b64}"

def _supports_vision(model):
    if not model:
        return False
    m = model.lower()
    # эвристика: большинство провайдеров держат vision в тех же чат-моделях
    # считаем что поддерживают gpt-4o, gpt-4-vision, claude-3, gemini, vision
    keywords = ["vision", "gpt-4o", "gpt-4-turbo", "gpt-4.1", "claude-3", "gemini", "llava", "pixtral", "qwen-vl"]
    return any(k in m for k in keywords) or "4o" in m or "vision" in m


def send_with_content(cfg, state, content):
    """Отправка сообщения где content может быть строкой или list[parts]."""
    name, p = active(cfg)
    model = cfg["active"].get("model")
    if not p or not model:
        print(red("Не выбран провайдер/модель: /addprovider, /provider, /model"))
        return
    messages = state["messages"]
    messages.append({"role": "user", "content": content})
    payload = ([{"role": "system", "content": cfg["system"]}]
               if cfg.get("system") else []) + messages
    answer, thinking = "", False
    try:
        for kind, piece in chat_stream(name, p, model, payload):
            if kind == "think":
                if not state["think"]:
                    continue
                if not thinking:
                    print(dim("┌ рассуждает"))
                    thinking = True
                sys.stdout.write(dim(piece))
            else:
                if thinking:
                    print("\n")
                    thinking = False
                sys.stdout.write(piece)
                answer += piece
            sys.stdout.flush()
        print()
    except KeyboardInterrupt:
        print(dim("\n[прервано]"))
    except APIError as e:
        msg = str(e).lower()
        # дружелюбное сообщение если модель не умеет в картинки/файлы
        if any(w in msg for w in ["image", "vision", "multimodal", "content", "unsupported"]):
            print(red(f"\nМодель '{model}' не поддерживает файлы/фото: {e}"))
            print(dim("Попробуй модель с поддержкой vision: gpt-4o, gemini-2.5-flash, claude-3 и т.п. (/models)"))
        else:
            print(red(f"\n{e}"))
    if answer:
        messages.append({"role": "assistant", "content": answer})
    else:
        messages.pop()


def handle(cfg, state, line):
    """Возвращает False, если нужно выйти."""
    cmd, _, arg = line[1:].partition(" ")
    arg = arg.strip()
    if cmd in ("exit", "quit", "q"):
        return False
    if cmd == "help":
        print(HELP.format(presets=", ".join(PRESETS)))
    elif cmd == "providers":
        if not cfg["providers"]:
            print(dim("Пусто. Добавь: /addprovider deepseek"))
        for n, p in cfg["providers"].items():
            mark = green("●") if n == cfg["active"]["provider"] else " "
            key = green("ключ есть") if get_key(n, p) else red("нет ключа")
            print(f" {mark} {n:<12} {dim(p['base_url'])}  {key}")
    elif cmd == "provider":
        names = list(cfg["providers"])
        if not names:
            print(dim("Пусто. Добавь: /addprovider deepseek"))
            return True
        name = arg if arg in cfg["providers"] else (
            choose(names, cfg["active"]["provider"]) if not arg else None)
        if not name:
            if arg:
                print(red(f"Нет провайдера '{arg}'"))
            return True
        set_provider(cfg, name)
        print(green(f"Провайдер: {name}, модель: {cfg['active']['model']}"))
    elif cmd in ("models", "model"):
        n, p = active(cfg)
        if not p:
            print(red("Сначала выбери провайдера: /addprovider или /provider"))
            return True
        models = p.get("models", [])
        if cmd == "models":
            choose_list = models or [dim("(список пуст, /fetchmodels)")]
            for m in choose_list:
                mark = green("●") if m == cfg["active"]["model"] else " "
                print(f" {mark} {m}")
            return True
        pick = None
        if arg.isdigit() and 1 <= int(arg) <= len(models):
            pick = models[int(arg) - 1]
        elif arg:
            pick = arg
        elif models:
            pick = choose(models, cfg["active"]["model"])
        if pick:
            cfg["active"]["model"] = pick
            if pick not in models:
                p.setdefault("models", []).append(pick)
            save(cfg)
            print(green(f"Модель: {pick}"))
    elif cmd == "fetchmodels":
        n, p = active(cfg)
        if p:
            refresh_models(cfg, n)
        else:
            print(red("Нет активного провайдера"))
    elif cmd == "addprovider":
        try:
            add_provider(cfg, arg)
        except (EOFError, KeyboardInterrupt):
            print(dim("\nотмена"))
    elif cmd == "delprovider":
        if arg in cfg["providers"]:
            del cfg["providers"][arg]
            if cfg["active"]["provider"] == arg:
                cfg["active"] = {"provider": None, "model": None}
            save(cfg)
            print(green(f"Удалён: {arg}"))
        else:
            print(red("Укажи имя: /delprovider имя"))
    elif cmd == "key":
        name = arg or cfg["active"]["provider"]
        if name not in cfg["providers"]:
            print(red("Нет такого провайдера"))
            return True
        try:
            cfg["providers"][name]["key"] = getpass.getpass(
                f"Новый ключ для {name} (скрыт): ").strip()
            save(cfg)
            print(green("Ключ сохранён"))
        except (EOFError, KeyboardInterrupt):
            print(dim("\nотмена"))
    elif cmd == "system":
        if arg:
            cfg["system"] = arg
            save(cfg)
            print(green("Системный промпт обновлён"))
        else:
            print(dim(cfg["system"] or "(пусто)"))
    elif cmd == "about":
        print(cyan(ABOUT.format(version=VERSION)))
    elif cmd == "think":
        state["think"] = arg != "off"
        print(dim("Рассуждения: " + ("показываю" if state["think"] else "скрыты")))
    elif cmd in ("file", "attach"):
        if not arg:
            print(dim("Использование: /file <путь> [вопрос к файлу]"))
            return True
        # путь может содержать пробелы — берём первый токен как путь, остальное как вопрос
        # поддержим кавычки
        import shlex
        try:
            parts = shlex.split(arg)
        except ValueError:
            parts = arg.split()
        if not parts:
            print(red("Укажи путь к файлу"))
            return True
        path = os.path.expanduser(parts[0])
        prompt = " ".join(parts[1:]).strip()
        if not os.path.isfile(path):
            print(red(f"Файл не найден: {path}"))
            return True
        if _is_image(path):
            print(yellow(f"Это похоже на фото. Используй /img для картинок: /img {parts[0]}"))
            return True
        try:
            content, trunc = _read_text_file(path)
        except APIError as e:
            print(red(str(e)))
            return True
        except UnicodeDecodeError:
            print(red("Не смог прочитать как текст (бинарный файл?). Для фото используй /img"))
            return True
        except OSError as e:
            print(red(f"Ошибка чтения: {e}"))
            return True
        header = f"[Файл: {os.path.basename(path)}]"
        if trunc:
            header += dim(f" (обрезан до {MAX_FILE_CHARS//1000}k символов)")
        body = header + "\n```\n" + content + "\n```"
        if prompt:
            body += "\n\n" + prompt
        elif trunc:
            body += "\n\n(файл обрезан, задай вопрос по содержимому)"
        print(dim(f"Отправляю файл {os.path.basename(path)} ..."))
        send_with_content(cfg, state, body)
    elif cmd in ("img", "image", "photo"):
        if not arg:
            print(dim("Использование: /img <путь> [вопрос к фото]"))
            return True
        import shlex
        try:
            parts = shlex.split(arg)
        except ValueError:
            parts = arg.split()
        if not parts:
            print(red("Укажи путь к фото"))
            return True
        path = os.path.expanduser(parts[0])
        prompt = " ".join(parts[1:]).strip() or "Опиши что на фото"
        if not os.path.isfile(path):
            print(red(f"Файл не найден: {path}"))
            return True
        if not _is_image(path):
            print(red(f"Не похоже на фото ({os.path.splitext(path)[1] or 'без расширения'}). Поддерживаются: png, jpg, webp, gif. Для текста используй /file"))
            return True
        model = cfg["active"].get("model") or ""
        # мягкое предупреждение, не блокируем — пусть сервер вернёт точную ошибку
        if not _supports_vision(model):
            print(yellow(f"Внимание: модель '{model}' возможно не поддерживает фото."))
            print(dim("Если вернётся ошибка — смени на gpt-4o / gemini / claude-3 (/models)"))
        try:
            data_url = _encode_image(path)
        except APIError as e:
            print(red(str(e)))
            return True
        except OSError as e:
            print(red(f"Ошибка чтения: {e}"))
            return True
        # OpenAI vision формат
        content = [
            {"type": "text", "text": prompt},
            {"type": "image_url", "image_url": {"url": data_url}},
        ]
        print(dim(f"Отправляю фото {os.path.basename(path)} ..."))
        send_with_content(cfg, state, content)
    elif cmd == "clear":
        state["messages"].clear()
        print(dim("Диалог очищен"))
    else:
        print(red(f"Неизвестная команда /{cmd}. Список: /help"))
    return True


def read_input(cfg):
    n, _ = active(cfg)
    label = f"{n or '—'}/{cfg['active'].get('model') or '—'}"
    prompt = rl_paint("36", label) + rl_paint("1", " ❯ ")
    lines = []
    while True:
        line = input(prompt if not lines else rl_paint("90", "… "))
        if line.endswith("\\"):
            lines.append(line[:-1])
            continue
        lines.append(line)
        return "\n".join(lines)


def send(cfg, state, text):
    name, p = active(cfg)
    model = cfg["active"].get("model")
    if not p or not model:
        print(red("Не выбран провайдер/модель: /addprovider, /provider, /model"))
        return
    messages = state["messages"]
    messages.append({"role": "user", "content": text})
    payload = ([{"role": "system", "content": cfg["system"]}]
               if cfg.get("system") else []) + messages
    answer, thinking = "", False
    try:
        for kind, piece in chat_stream(name, p, model, payload):
            if kind == "think":
                if not state["think"]:
                    continue
                if not thinking:
                    print(dim("┌ рассуждает"))
                    thinking = True
                sys.stdout.write(dim(piece))
            else:
                if thinking:
                    print("\n")
                    thinking = False
                sys.stdout.write(piece)
                answer += piece
            sys.stdout.flush()
        print()
    except KeyboardInterrupt:
        print(dim("\n[прервано]"))
    except APIError as e:
        print(red(f"\n{e}"))
    if answer:
        messages.append({"role": "assistant", "content": answer})
    else:
        messages.pop()  # ответа нет, убираем вопрос из истории


def main():
    cfg = load()
    os.makedirs(CONF_DIR, exist_ok=True)
    try:
        readline.read_history_file(HIST_FILE)
    except OSError:
        pass
    atexit.register(lambda: readline.write_history_file(HIST_FILE))

    print(cyan(BANNER.rstrip()))
    print(dim(f"  v{VERSION}  ·  /help - команды  ·  Ctrl+C - прервать\n"))
    if not cfg["providers"]:
        print(yellow("Провайдеров пока нет. Начни с: /addprovider deepseek\n"))

    state = {"messages": [], "think": True}
    while True:
        try:
            line = read_input(cfg)
        except EOFError:
            print()
            break
        except KeyboardInterrupt:
            print()
            continue
        if not line.strip():
            continue
        if line.startswith("/"):
            if not handle(cfg, state, line.strip()):
                break
        else:
            send(cfg, state, line)


if __name__ == "__main__":
    main()
