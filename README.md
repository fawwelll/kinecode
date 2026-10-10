# kinecode

Minimal terminal client for any OpenAI-compatible API — DeepSeek, Groq, OpenRouter, NVIDIA NIM, Google Gemini, or your own endpoint.

Written in pure C++17. No frameworks, no heavy dependencies. Just `libcurl` and `nlohmann/json`.

## Features

- **Multi-provider** — switch between APIs on the fly
- **Streaming** — real-time token-by-token output
- **Reasoning support** — shows/hides model thinking process (`/think on|off`)
- **File upload** — send text files to the model (`/file <path>`)
- **Image upload** — send photos for vision models (`/img <path>`)
- **Config persistence** — providers, keys, and history saved locally
- **Zero bloat** — single binary, ~400KB

## Build

```bash
g++ -std=c++17 -O2 $(pkg-config --cflags libcurl) -o kinecode kinecode.cpp $(pkg-config --libs libcurl) -lreadline
```

Dependencies: `libcurl`, `libreadline`, `nlohmann-json3-dev` (header-only).

## Usage

```bash
./kinecode
```

Add a provider first:
```
/addprovider deepseek
```

Then chat. Type `/help` for all commands.

## Commands

| Command | Description |
|---|---|
| `/providers` | List configured providers |
| `/provider [name]` | Switch active provider |
| `/models` | List available models |
| `/model [name]` | Select model |
| `/fetchmodels` | Fetch models from server |
| `/addprovider [name]` | Add new provider |
| `/delprovider name` | Remove provider |
| `/key [name]` | Change API key |
| `/system [text]` | Set system prompt |
| `/think on\|off` | Toggle reasoning output |
| `/file <path> [question]` | Send text file |
| `/img <path> [question]` | Send image (png/jpg/webp/gif) |
| `/about` | About this project |
| `/clear` | Clear conversation |
| `/help` | Show help |
| `/exit` | Quit |

Lines ending with `\` continue on the next line. `Ctrl+C` interrupts streaming.

## Config

Stored in `~/.config/kinecode/config.json` (mode 600). API keys can also be set via environment variables: `<PROVIDER>_API_KEY` (e.g., `DEEPSEEK_API_KEY`).

## License

MIT

## Author

Created by fawwelll
Telegram channel: @jailbraked

</content>