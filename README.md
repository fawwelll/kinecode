# kinecode

A minimal AI coding agent for your terminal, in the style of OpenCode / Claude Code. Works with any OpenAI-compatible API — DeepSeek, Groq, OpenRouter, NVIDIA NIM, Google Gemini, or your own endpoint.

Written in pure C++17. One binary, no runtimes, no frameworks — just `libcurl` and `nlohmann/json`.

```
██╗  ██╗██╗███╗   ██╗███████╗ ██████╗ ██████╗ ██████╗ ███████╗
██║ ██╔╝██║████╗  ██║██╔════╝██╔════╝██╔═══██╗██╔══██╗██╔════╝
█████╔╝ ██║██╔██╗ ██║█████╗  ██║     ██║   ██║██║  ██║█████╗
██╔═██╗ ██║██║╚██╗██║██╔══╝  ██║     ██║   ██║██║  ██║██╔══╝
██║  ██╗██║██║ ╚████║███████╗╚██████╗╚██████╔╝██████╔╝███████╗
╚═╝  ╚═╝╚═╝╚═╝  ╚═══╝╚══════╝ ╚═════╝ ╚═════╝ ╚═════╝ ╚══════╝
```

## Install

Grab a ready binary from the [Releases](https://github.com/fawwelll/kinecode/releases) page — no compiler needed.

### Linux — one line

```bash
curl -fsSL https://raw.githubusercontent.com/fawwelll/kinecode/main/install.sh | sh
```

The installer downloads the binary, makes it executable and puts it in `~/.local/bin/kinecode` (already in PATH on most distros). Then just type `kinecode`.

### Linux — plain binary

Download **`kinecode-linux`** (Debian, Ubuntu, Arch, Fedora — any x86_64 distro). A downloaded file is not executable by default — this is standard Linux behavior, not a bug:

```bash
cd ~/Downloads                      # or wherever you saved it
chmod +x kinecode-linux               # allow executing the file
./kinecode-linux                      # run it
```

*(Optional)* Install as a proper command:

```bash
mkdir -p ~/.local/bin && mv kinecode-linux ~/.local/bin/kinecode
```

`~/.local/bin` is already in PATH on most distros — after this, just type `kinecode` in any terminal. If you also grabbed `kinecode.desktop` from the repo, it will work now too.

### Windows

1. Download **`kinecode.exe`** — a single static file, no DLLs, nothing to install
2. Double-click it, or run from `cmd` / PowerShell / Windows Terminal

> **SmartScreen warning?** Since the binary is not code-signed, Windows may show *"Windows protected your PC"* on first launch. Click **More info** → **Run anyway**. This is normal for open-source apps without a paid signing certificate — the warning disappears as the file builds download reputation.

### Build from source

```bash
sudo apt install build-essential libcurl4-openssl-dev nlohmann-json3-dev   # Debian/Ubuntu
g++ -std=c++17 -O2 $(pkg-config --cflags libcurl) -o kinecode kinecode.cpp $(pkg-config --libs libcurl) -lreadline
```

## First run

1. Launch `kinecode` — you'll see the splash screen
2. Press **ctrl+p** → choose `/settings` (or type `/settings`)
3. Press **a** — pick a provider from the catalog (DeepSeek, Groq, OpenRouter, NVIDIA, Gemini, or custom base URL)
4. Paste your **API key** (input is masked) — models are fetched automatically
5. **Enter** on a model to select it — start chatting

## Agent mode

kinecode can run shell commands on your machine. When the model needs one, it outputs a `run` block — you confirm with **y / n / a (always for this session)**, and the output is fed back to the model so it can continue the task.

Three access modes (`/mode` or `m` in settings):

| Mode | Behavior |
|---|---|
| `full` | Any command, each one asks for confirmation |
| `folder` *(default)* | Commands run in your chosen workdir (`/cwd` or `w` in settings) |
| `read` | Only reading commands are auto-allowed (`ls`, `cat`, `grep`, `find`, ...). Anything that writes is blocked |

Example: ask *"какие файлы в этой папке?"* — the model proposes `ls`, you press **y**, it reads the output and answers.

## Plugins

Drop a `.md` file into `~/.config/kinecode/plugins/` — it becomes a slash command. The file content is used as the prompt.

```
~/.config/kinecode/plugins/review.md  →  /review <code or file>
```

A sample `/review` plugin is created on first launch.

## Commands

| Command | Description |
|---|---|
| `/settings` | Providers, API keys, models (also: **ctrl+p**) |
| `/mode` | Show/set access mode: `full \| folder \| read` |
| `/cwd [path]` | Show/set agent workdir |
| `/plugins` | List installed plugins |
| `/providers` | List configured providers |
| `/provider [name]` | Switch active provider |
| `/models` | List available models |
| `/model [name]` | Select model |
| `/fetchmodels` | Fetch models from the server |
| `/addprovider [name]` | Add new provider (classic flow) |
| `/delprovider name` | Remove provider |
| `/key [name]` | Change API key |
| `/system [text]` | Set system prompt |
| `/think on\|off` | Toggle reasoning output |
| `/file <path> [question]` | Send a text file |
| `/img <path> [question]` | Send an image (png/jpg/webp/gif) |
| `/language ru\|en` | Switch interface language |
| `/about` | About the project |
| `/clear` | Clear the conversation |
| `/help` | Show help |
| `/exit` | Quit |

Shortcuts: **ctrl+p** command palette · **↑/↓** input history · **ctrl+c** interrupt streaming · **ctrl+d** quit.

## Config

Everything is stored in `~/.config/kinecode/config.json` (permissions 600). API keys can also come from environment variables: `<PROVIDER>_API_KEY` (e.g., `DEEPSEEK_API_KEY`).

## License

MIT

## Author

Created by fawwelll
Telegram channel: [@jailbraked](https://t.me/jailbraked)
