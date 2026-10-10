// kinecode - terminal client for OpenAI-compatible APIs
// (c) fawwelll
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <set>
#if defined(_WIN32) || defined(__MINGW32__)
#include <windows.h>
#include <conio.h>
#include <io.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <csignal>
#endif
#include <chrono>
#include <curl/curl.h>
#include "nlohmann/json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

const std::string APP = "kinecode";
const std::string VERSION = "0.5";

int lang = 0; // 0=ru, 1=en
std::string L(const std::string& ru, const std::string& en) { return lang ? en : ru; }

fs::path conf_dir() {
#if defined(_WIN32) || defined(__MINGW32__)
    const char* appdata = std::getenv("APPDATA");
    fs::path base = appdata ? appdata : ".";
    return base / APP;
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    fs::path base = xdg && *xdg ? xdg : fs::path(std::getenv("HOME")) / ".config";
    return base / APP;
#endif
}
fs::path conf_file() { return conf_dir() / "config.json"; }
fs::path hist_file() { return conf_dir() / "history"; }

#if defined(_WIN32) || defined(__MINGW32__)
bool COLOR = _isatty(_fileno(stdout)) && !std::getenv("NO_COLOR");
#else
bool COLOR = isatty(STDOUT_FILENO) && !std::getenv("NO_COLOR");
#endif

std::string paint(const std::string& code, const std::string& s) {
    if (!COLOR) return s;
    return "\033[" + code + "m" + s + "\033[0m";
}
std::string dim(const std::string& s)    { return paint("90", s); }
std::string red(const std::string& s)    { return paint("31", s); }
std::string green(const std::string& s)  { return paint("32", s); }
std::string yellow(const std::string& s) { return paint("33", s); }
std::string cyan(const std::string& s)   { return paint("1;36", s); }

std::string rl_paint(const std::string& code, const std::string& s) {
    if (!COLOR) return s;
    return "\001\033[" + code + "m\002" + s + "\001\033[0m\002";
}

const char* BANNER = R"(
______  _____ _____ ___________ _____
|  _  \/  ___/  __ \  _  |  _  \  ___|
| | | |\ `--.| /  \/ | | | | | | |__
| | | | `--. \ |   | | | | | | |  __|
| |/ / /\__/ / \__/\ \_/ / |/ /| |___
|___/  \____/ \____/\___/|___/ \____/

 _   _   ___  ______ _   _  _____ _____ _____
| | | | / _ \ | ___ \ \ | ||  ___/  ___/  ___|
| |_| |/ /_\ \| |_/ /  \| || |__ \ `--.\ `--.
|  _  ||  _  ||    /| . ` ||  __| `--. \`--. \
| | | || | | || |\ \| |\  || |___/\__/ /\__/ /
\_| |_/\_| |_/\_| \_\_| \_/\____/\____/\____/
)";

std::string about_text() {
    if (lang) return "kinecode v" + VERSION + " - minimal terminal client for OpenAI-compatible APIs.\n\nCreated by fawwelll, Telegramm channel @jailbraked\n\nC++17 + libcurl + nlohmann/json.";
    return "kinecode v" + VERSION + " - минималистичный терминальный клиент для OpenAI-совместимых API.\n\nCreated by fawwelll, Telegramm channel @jailbraked\n\nC++17 + libcurl + nlohmann/json.";
}

std::string help_text() {
    if (lang) return R"(Commands:
  /providers              list providers
  /provider [name]        switch provider
  /models                 list models
  /model [name|num]       select model
  /fetchmodels            fetch models from server
  /addprovider [name]     add provider
  /delprovider name       remove provider
  /key [name]             change API key
  /system [text]          show/set system prompt
  /think on|off           toggle reasoning output
  /file <path> [question] send text file
  /img <path> [question]  send image (png/jpg/webp/gif)
  /language ru|en         switch language
  /about                  about
  /clear                  clear chat
  /help  /exit
Ctrl+C interrupts streaming.)";
    return R"(Команды:
  /providers              список провайдеров
  /provider [имя]         переключить провайдера
  /models                 модели текущего провайдера
  /model [имя|номер]      выбрать модель
  /fetchmodels            обновить список моделей с сервера
  /addprovider [имя]      добавить провайдера
  /delprovider имя        удалить провайдера
  /key [имя]              сменить API-ключ
  /system [текст]         показать/задать системный промпт
  /think on|off           показывать рассуждения модели
  /file <путь> [вопрос]   отправить текстовый файл
  /img <путь> [вопрос]    отправить фото (png/jpg/webp/gif)
  /language ru|en         сменить язык
  /about                  о проекте
  /clear                  очистить диалог
  /help  /exit
Ctrl+C прерывает ответ.)";
}

struct Provider {
    std::string base_url;
    std::string key;
    std::vector<std::string> models;
};

struct Config {
    std::map<std::string, Provider> providers;
    std::string active_provider;
    std::string active_model;
    std::string system_prompt = "";
    std::string mode = "folder";
    std::string workdir;
};

Config load_config() {
    Config cfg;
    auto cf = conf_file();
    if (fs::exists(cf)) {
        try {
            std::ifstream f(cf);
            json j; f >> j;
            if (j.contains("providers") && j["providers"].is_object()) {
                for (auto& [k, v] : j["providers"].items()) {
                    Provider p;
                    p.base_url = v.value("base_url", "");
                    p.key = v.value("key", "");
                    if (v.contains("models") && v["models"].is_array())
                        for (auto& m : v["models"]) p.models.push_back(m.get<std::string>());
                    cfg.providers[k] = p;
                }
            }
            if (j.contains("active")) {
                cfg.active_provider = j["active"].value("provider", "");
                cfg.active_model = j["active"].value("model", "");
            }
            cfg.system_prompt = j.value("system", "");
            lang = j.value("lang", 0);
            cfg.mode = j.value("mode", "folder");
            cfg.workdir = j.value("workdir", "");
            if (cfg.system_prompt.empty()) cfg.system_prompt = lang ? "You are a coding assistant. Be concise." : "Ты помощник по программированию. Отвечай кратко и по делу.";
        } catch (...) {
            std::cerr << red(L("Ошибка чтения конфига", "Config read error")) << "\n";
        }
    }
    return cfg;
}

void save_config(const Config& cfg) {
    try {
        fs::create_directories(conf_dir());
        fs::path plugDir = conf_dir() / "plugins";
        fs::create_directories(plugDir);
        fs::path ex = plugDir / "review.md";
        if (!fs::exists(ex)) {
            std::ofstream of(ex);
            of << L("Проведи код-ревью: найди баги, проблемы безопасности и стиля, предложи улучшения.\n\nКод:\n",
                    "Do a code review: find bugs, security and style issues, suggest improvements.\n\nCode:\n");
        }
    } catch (...) {}
    json j;
    j["providers"] = json::object();
    for (auto& [k, p] : cfg.providers) {
        j["providers"][k] = {{"base_url", p.base_url}, {"key", p.key}, {"models", p.models}};
    }
    j["active"] = {{"provider", cfg.active_provider}, {"model", cfg.active_model}};
    j["system"] = cfg.system_prompt;
    j["lang"] = lang;
    j["mode"] = cfg.mode;
    j["workdir"] = cfg.workdir;
    std::ofstream f(conf_file());
    f << j.dump(2);
    #ifndef _WIN32
    #ifndef _WIN32
    fs::permissions(conf_file(), fs::perms::owner_read | fs::perms::owner_write);
#endif
#endif
}

std::string get_key(const std::string& name, const Provider& p) {
    if (!p.key.empty()) return p.key;
    std::string env = name;
    for (auto& c : env) c = (c == '-') ? '_' : toupper(c);
    env += "_API_KEY";
    const char* val = std::getenv(env.c_str());
    return val ? val : "";
}

Provider* active_provider(Config& cfg) {
    auto it = cfg.providers.find(cfg.active_provider);
    return it != cfg.providers.end() ? &it->second : nullptr;
}

std::string getpass_cpp(const std::string& prompt) {
    std::cout << prompt;
    std::string pass;
#ifdef _WIN32
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hStdin, &mode);
    SetConsoleMode(hStdin, mode & ~ENABLE_ECHO_INPUT);
    std::getline(std::cin, pass);
    SetConsoleMode(hStdin, mode);
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    std::getline(std::cin, pass);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
    std::cout << "\n";
    return pass;
}

// ---------- TUI (opencode-style) ----------

volatile bool g_interrupt = false;
std::string g_status;
bool tui_mask = false;
long g_ctx_tokens = 0;

struct Usage { bool got = false; long prompt = 0, completion = 0; };
Usage g_usage;

enum { K_UP = 1000, K_DOWN, K_LEFT, K_RIGHT, K_DEL, K_IGNORE };

#ifdef _WIN32
DWORD g_orig_inmode = 0;
void tui_raw(bool on) {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (on) {
        GetConsoleMode(h, &g_orig_inmode);
        SetConsoleMode(h, g_orig_inmode & ~(ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_PROCESSED_INPUT));
    } else SetConsoleMode(h, g_orig_inmode);
}
void tui_size(int& r, int& c) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        c = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        r = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
}
bool tui_key_avail(int ms) { (void)ms; return _kbhit() != 0; }
int tui_read_byte() { return _getch(); }
#else
termios g_orig_termios;
void tui_raw(bool on) {
    termios t;
    if (on) {
        tcgetattr(0, &t);
        g_orig_termios = t;
        t.c_lflag &= ~(ICANON | ECHO | ISIG);
        t.c_oflag &= ~OPOST;
        t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &t);
    } else tcsetattr(0, TCSANOW, &g_orig_termios);
}
void tui_size(int& r, int& c) {
    winsize w;
    if (ioctl(1, TIOCGWINSZ, &w) == 0 && w.ws_row > 0) { r = w.ws_row; c = w.ws_col; }
}
bool tui_key_avail(int ms) { pollfd p{0, POLLIN, 0}; return poll(&p, 1, ms) > 0; }
int tui_read_byte() { unsigned char c; if (read(0, &c, 1) != 1) return -1; return c; }
#endif

static void wraw(const std::string& s) {
    if (!s.empty()) { fwrite(s.data(), 1, s.size(), stdout); fflush(stdout); }
}

const std::map<std::string, std::pair<std::string, std::string>> PRESETS = {
    {"deepseek",    {"https://api.deepseek.com/v1", "DeepSeek"}},
    {"groq",        {"https://api.groq.com/openai/v1", "Groq"}},
    {"openrouter",  {"https://openrouter.ai/api/v1", "OpenRouter"}},
    {"nvidia",      {"https://integrate.api.nvidia.com/v1", "NVIDIA NIM"}},
    {"google",      {"https://generativelanguage.googleapis.com/v1beta/openai", "Google Gemini"}},
};

static std::string fmtDur(double s) {
    char b[32];
    if (s < 10) snprintf(b, 32, "%.1fs", s); else snprintf(b, 32, "%.0fs", s);
    return b;
}
static std::string fmtTps(double t) {
    char b[32];
    snprintf(b, 32, "%.1f tok/s", t);
    return b;
}

enum class MsgKind { User, Assistant, Info, Error, Meta };
struct Msg { MsgKind kind; std::string text; Msg(MsgKind k, const std::string& t) : kind(k), text(t) {} };

std::vector<std::string> fetch_models(const std::string& name, const Provider& p);

struct Tui {
    bool active = false;
    int rows = 24, cols = 80;
    std::vector<Msg> msgs;
    std::vector<std::string> history;
    int hist_idx = 0;
    std::string input;
    std::string sessionTitle;
    std::chrono::steady_clock::time_point last_draw;

    bool palOpen = false;
    std::string palFilter;
    int palSel = 0;
    bool setOpen = false;
    int setSel = 0, setModelSel = 0;
    int setPage = 0; // 0=провайдеры, 1=модели
    std::string statusMsg;
    std::string providerPage;
    bool dlgOpen = false;
    std::string dlgTitle, dlgValue;
    bool dlgMask = false;
    Config* pCfg = nullptr;

    void addMsg(MsgKind k, const std::string& t) {
        msgs.push_back(Msg(k, t));
        maybe_draw();
    }
    void beginStream() { msgs.push_back(Msg(MsgKind::Assistant, "")); }
    void feed(const std::string& s) {
        if (msgs.empty() || msgs.back().kind != MsgKind::Assistant)
            msgs.push_back(Msg(MsgKind::Info, ""));
        msgs.back().text += s;
        maybe_draw();
    }
    void maybe_draw() {
        if (!active) return;
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_draw).count() < 30) return;
        last_draw = now;
        draw();
    }

    static size_t utf8_len(const std::string& s) {
        size_t n = 0;
        for (size_t i = 0; i < s.size(); ) {
            unsigned char c = s[i];
            i += (c < 0x80) ? 1 : ((c >> 5) == 6 ? 2 : ((c >> 4) == 14 ? 3 : 4));
            n++;
        }
        return n;
    }
    static void utf8_pop(std::string& s) {
        while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();
        if (!s.empty()) s.pop_back();
    }
    static std::vector<std::string> wrap_ansi(const std::string& s, int width) {
        if (width < 8) width = 8;
        std::vector<std::string> out;
        std::string cur, sgr;
        int w = 0;
        for (size_t i = 0; i < s.size(); ) {
            if (s[i] == '\033') {
                size_t j = i;
                while (j < s.size() && s[j] != 'm') j++;
                if (j < s.size()) {
                    std::string seq = s.substr(i, j - i + 1);
                    cur += seq;
                    if (seq == "\033[0m") sgr.clear(); else sgr = seq;
                    i = j + 1;
                    continue;
                }
            }
            unsigned char c = s[i];
            int len = (c < 0x80) ? 1 : ((c >> 5) == 6 ? 2 : ((c >> 4) == 14 ? 3 : 4));
            if (c == '\n') { out.push_back(cur); cur = sgr; w = 0; i++; continue; }
            if (w + 1 > width) { out.push_back(cur); cur = sgr; w = 0; }
            cur += s.substr(i, len);
            w++; i += len;
        }
        out.push_back(cur);
        return out;
    }

    int sidebarW() const { return (cols >= 112) ? 28 : 0; }
    int contentW() const { return cols - sidebarW() - 2; }

    std::vector<std::string> blockRows(const Msg& m, int width) {
        std::vector<std::string> rows;
        if (m.kind == MsgKind::Meta || m.kind == MsgKind::Info) {
            for (auto& r : wrap_ansi(dim(m.text), width)) rows.push_back(" " + r);
            rows.push_back("");
            return rows;
        }
        if (m.kind == MsgKind::Error) {
            for (auto& r : wrap_ansi("\033[31m" + m.text + "\033[0m", width)) rows.push_back(" " + r);
            rows.push_back("");
            return rows;
        }
        std::string bar = (m.kind == MsgKind::User) ? paint("38;5;208", "▌") : dim("▌");
        for (auto& r : wrap_ansi(m.text, width - 2)) rows.push_back(bar + " " + r);
        rows.push_back("");
        return rows;
    }

    void draw_splash() {
        std::vector<std::string> art;
        art.push_back(paint("38;5;208", "______  _____ _____ ___________ _____"));
        art.push_back(paint("38;5;208", "|  _  \\/  ___/  __ \\  _  |  _  \\  ___|"));
        art.push_back(paint("38;5;208", "| | | |\\ `--.| /  \\/ | | | | | | |__"));
        art.push_back(paint("38;5;208", "| | | | `--. \\ |   | | | | | | |  __|"));
        art.push_back(paint("38;5;208", "| |/ / /\\__/ / \\__/\\ \\_/ / |/ /| |___"));
        art.push_back(paint("38;5;208", "|___/  \\____/ \\____/\\___/|___/ \\____/"));
        art.push_back("");
        art.push_back(dim(" _   _   ___  ______ _   _  _____ _____ _____"));
        art.push_back(dim("| | | | / _ \\ | ___ \\ \\ | ||  ___/  ___/  ___|"));
        art.push_back(dim("| |_| |/ /_\\ \\| |_/ /  \\| || |__ \\ `--.\\ `--."));
        art.push_back(dim("|  _  ||  _  ||    /| . ` ||  __| `--. \\`--. \\"));
        art.push_back(dim("| | | || | | || |\\ \\| |\\  || |___/\\__/ /\\__/ /"));
        art.push_back(dim("\\_| |_/\\_| |_/\\_| \\_\\_| \\_/\\____/\\____/\\____/"));

        int boxW = cols < 60 ? cols - 4 : 58;
        int totalH = (int)art.size() + 2 + 3 + 2;
        int top = (rows - totalH) / 2; if (top < 1) top = 1;
        std::string f = "\033[H\033[2J";
        for (auto& a : art) {
            long alen = (long)utf8_len(a);
            int pad = (cols - (int)alen) / 2; if (pad < 0) pad = 0;
            f += "\033[" + std::to_string(top++) + ";1H" + std::string(pad, ' ') + a;
        }
        top++;
        int x0 = (cols - boxW) / 2; if (x0 < 1) x0 = 1;
        std::string inner = input;
        if (inner.empty() && !tui_mask) inner = dim(L(" Спроси что угодно… '/help'", " Ask anything… '/help'"));
        std::string border; for (int i = 0; i < boxW - 2; i++) border += "─";
        std::string padIn = inner;
        while (utf8_len(padIn) < (size_t)boxW - 4) padIn += " ";
        f += "\033[" + std::to_string(top) + ";" + std::to_string(x0) + "H" + dim("╭" + border + "╮");
        f += "\033[" + std::to_string(top + 1) + ";" + std::to_string(x0) + "H" + dim("│") + padIn + dim("│");
        f += "\033[" + std::to_string(top + 2) + ";" + std::to_string(x0) + "H" + dim("╰" + border + "╯");
        top += 4;
        std::string st = " Chat · " + g_status;
        int pad = (cols - (int)utf8_len(st)) / 2; if (pad < 0) pad = 0;
        f += "\033[" + std::to_string(top) + ";1H" + std::string(pad, ' ') + dim(st);
        if (g_status.find("—/—") != std::string::npos) {
            top++;
            std::string hint = L(" Настрой провайдера: ctrl+p → /settings", " Set up a provider: ctrl+p → /settings");
            pad = (cols - (int)utf8_len(hint)) / 2; if (pad < 0) pad = 0;
            f += "\033[" + std::to_string(top) + ";1H" + std::string(pad, ' ') + paint("38;5;208", hint);
        }
        f += "\033[" + std::to_string(rows) + ";1H" + dim(" " + footerText());
        f += "\033[J";
        wraw(f);
    }

    std::string footerText() {
        return L("/help · ctrl+p команды · ctrl+c прервать · /exit",
                 "/help · ctrl+p commands · ctrl+c interrupt · /exit");
    }

    void draw_input_box(std::string& f, int y_top, int width, const std::string& placeholder) {
        std::string inner = input;
        if (inner.empty() && !tui_mask) inner = dim(placeholder);
        std::string border; for (int i = 0; i < width - 2; i++) border += "─";
        std::string pad = inner;
        while (utf8_len(pad) < (size_t)width - 4) pad += " ";
        f += "\033[" + std::to_string(y_top) + ";1H" + dim("╭" + border + "╮");
        f += "\033[" + std::to_string(y_top + 1) + ";1H" + dim("│") + " " + pad + dim("│");
        f += "\033[" + std::to_string(y_top + 2) + ";1H" + dim("╰" + border + "╯");
    }

    void draw_chat() {
        int sw = sidebarW();
        int cw = contentW();
        int bodyRows = rows - 1 - 4 - 1;
        if (bodyRows < 1) bodyRows = 1;

        std::vector<std::string> rowsv;
        for (auto& m : msgs) for (auto& r : blockRows(m, cw)) rowsv.push_back(r);

        std::string f = "\033[H\033[2J";
        f += "\033[1;1H " + paint("1", "kinecode") + dim("  ·  " + g_status) + "\033[K";
        int start = (int)rowsv.size() > bodyRows ? (int)rowsv.size() - bodyRows : 0;
        int y = 2, used = 0;
        for (int i = start; i < (int)rowsv.size() && used < bodyRows; i++, y++, used++) {
            f += "\033[" + std::to_string(y) + ";1H" + rowsv[i] + "\033[K";
        }
        for (; used < bodyRows; used++, y++) f += "\033[" + std::to_string(y) + ";1H\033[K";

        draw_input_box(f, rows - 4, cw, L(" Спроси что угодно… '/help'", " Ask anything… '/help'"));
        f += "\033[" + std::to_string(rows) + ";1H" + dim(" " + footerText());
        if (g_ctx_tokens > 0) {
            char b[32]; snprintf(b, 32, "%ld tok", g_ctx_tokens);
            f += "\033[" + std::to_string(rows) + ";" + std::to_string(cols - 12) + "H" + dim(b);
        }

        if (sw > 0) {
            int x = cols - sw + 1;
            for (int yy = 1; yy <= rows; yy++)
                f += "\033[" + std::to_string(yy) + ";" + std::to_string(x) + "H" + dim("│");
            int sy = 2;
            f += "\033[" + std::to_string(sy++) + ";" + std::to_string(x + 2) + "H" + paint("1", L("Сессия", "Session"));
            if (!sessionTitle.empty()) {
                for (auto& t : wrap_ansi(sessionTitle, sw - 4))
                    f += "\033[" + std::to_string(sy++) + ";" + std::to_string(x + 2) + "H" + dim(t);
            }
            sy++;
            f += "\033[" + std::to_string(sy++) + ";" + std::to_string(x + 2) + "H" + paint("1", L("Контекст", "Context"));
            char b[64];
            snprintf(b, 64, "%ld %s", g_ctx_tokens, L("токенов", "tokens"));
            f += "\033[" + std::to_string(sy++) + ";" + std::to_string(x + 2) + "H" + b;
            sy++;
            f += "\033[" + std::to_string(sy++) + ";" + std::to_string(x + 2) + "H" + dim(L("режим: ", "mode: ")) + pCfg->mode;
        }
        f += "\033[J";
        wraw(f);
    }

    struct PalItem { std::string cmd, desc; };
    std::vector<PalItem> palItems() {
        return {
            {"/help", L("помощь", "help")},
            {"/settings", L("настройки: провайдеры, ключи, модели", "settings: providers, keys, models")},
            {"/providers", L("список провайдеров", "list providers")},
            {"/model", L("выбрать модель", "select model")},
            {"/fetchmodels", L("обновить модели с сервера", "fetch models from server")},
            {"/key", L("сменить API-ключ", "change API key")},
            {"/mode", L("права доступа full|folder|read", "access mode full|folder|read")},
            {"/cwd", L("рабочая папка агента", "agent workdir")},
            {"/plugins", L("список плагинов", "list plugins")},
            {"/system", L("системный промпт", "system prompt")},
            {"/think", L("рассуждения on|off", "reasoning on|off")},
            {"/language", L("язык ru|en", "language ru|en")},
            {"/clear", L("очистить чат", "clear chat")},
            {"/about", L("о программе", "about")},
            {"/exit", L("выход", "quit")},
        };
    }

    void draw_palette() {
        auto items = palItems();
        std::vector<PalItem> list;
        for (auto& it : items)
            if (palFilter.empty() || it.cmd.find(palFilter) != std::string::npos || it.desc.find(palFilter) != std::string::npos)
                list.push_back(it);
        if (palSel >= (int)list.size()) palSel = (int)list.size() - 1;
        if (palSel < 0) palSel = 0;

        int w = cols < 60 ? cols - 4 : 56;
        int shown = rows - 9; if (shown > (int)list.size()) shown = (int)list.size(); if (shown < 1) shown = 1;
        int h = shown + 5;
        int top = (rows - h) / 2; if (top < 1) top = 1;
        int x0 = (cols - w) / 2; if (x0 < 1) x0 = 1;

        std::string border; for (int i = 0; i < w - 2; i++) border += "─";
        std::string f = "\033[H\033[2J";
        f += "\033[" + std::to_string(top) + ";" + std::to_string(x0) + "H" + dim("╭" + border + "╮");
        auto row = [&](int y, const std::string& txt) {
            std::string p = txt;
            while (utf8_len(p) < (size_t)w - 4) p += " ";
            if (utf8_len(p) > (size_t)w - 4) p = p.substr(0, w - 4);
            f += "\033[" + std::to_string(y) + ";" + std::to_string(x0) + "H" + dim("│") + p + dim("│");
        };
        row(top + 1, paint("1", L(" Команды", " Commands")));
        row(top + 2, dim(" " + (palFilter.empty() ? L("фильтр…", "filter…") : palFilter)));
        row(top + 3, dim(" ────────────────────────────"));
        for (int i = 0; i < shown; i++) {
            int idx = (palSel / shown) * shown + i;
            if (idx >= (int)list.size()) break;
            std::string line = (idx == palSel ? paint("38;5;208", " ❯ ") : "   ") + list[idx].cmd + "  " + dim(list[idx].desc);
            row(top + 4 + i, line);
        }
        f += "\033[" + std::to_string(top + h - 1) + ";" + std::to_string(x0) + "H" + dim("╰" + border + "╯");
        f += "\033[J";
        wraw(f);
    }

    std::string run_palette() {
        palOpen = true; palFilter.clear(); palSel = 0;
        std::string result;
        for (;;) {
            draw();
            int k = get_key();
            if (k == -2) continue;
            if (k == 27) break;
            if (k == '\r' || k == '\n') {
                auto items = palItems();
                std::vector<PalItem> list;
                for (auto& it : items)
                    if (palFilter.empty() || it.cmd.find(palFilter) != std::string::npos || it.desc.find(palFilter) != std::string::npos)
                        list.push_back(it);
                if (palSel >= 0 && palSel < (int)list.size()) result = list[palSel].cmd;
                break;
            }
            if (k == K_UP) { palSel--; if (palSel < 0) palSel = 0; continue; }
            if (k == K_DOWN) { palSel++; continue; }
            if (k == 127 || k == 8) { utf8_pop(palFilter); palSel = 0; continue; }
            if (k >= 32) { palFilter += (char)k; palSel = 0; continue; }
        }
        palOpen = false;
        return result;
    }

    std::string run_dialog(const std::string& title, const std::string& seed, bool mask) {
        dlgOpen = true; dlgTitle = title; dlgValue = seed; dlgMask = mask;
        std::string result;
        for (;;) {
            draw();
            int k = get_key();
            if (k == -2) continue;
            if (k == 27) break;
            if (k == '\r' || k == '\n') { result = dlgValue; break; }
            if (k == 127 || k == 8) { utf8_pop(dlgValue); continue; }
            if (k == 21) { dlgValue.clear(); continue; }
            if (k >= 32) dlgValue += (char)k;
        }
        dlgOpen = false;
        return result;
    }

    void draw_dialog() {
        int w = cols < 60 ? cols - 4 : 56;
        int top = rows / 2 - 2; if (top < 1) top = 1;
        int x0 = (cols - w) / 2; if (x0 < 1) x0 = 1;
        std::string border; for (int i = 0; i < w - 2; i++) border += "─";
        std::string f = "\033[2J";
        f += "\033[" + std::to_string(top - 1) + ";1H" + paint("1", " " + dlgTitle);
        f += "\033[" + std::to_string(top) + ";" + std::to_string(x0) + "H" + dim("╭" + border + "╮");
        std::string shown = dlgMask ? std::string(utf8_len(dlgValue), '*') : dlgValue;
        std::string pad = shown;
        while (utf8_len(pad) < (size_t)w - 4) pad += " ";
        f += "\033[" + std::to_string(top + 1) + ";" + std::to_string(x0) + "H" + dim("│") + " " + pad + dim("│");
        f += "\033[" + std::to_string(top + 2) + ";" + std::to_string(x0) + "H" + dim("╰" + border + "╯");
        f += "\033[" + std::to_string(top + 4) + ";1H" + dim(L(" Enter — ок · Esc — отмена", " Enter — ok · Esc — cancel"));
        f += "\033[J";
        wraw(f);
    }

    std::vector<std::string> providerNames(Config& cfg) {
        std::vector<std::string> out;
        for (auto& [n, p] : cfg.providers) out.push_back(n);
        return out;
    }

    bool run_settings(Config& cfg) {
        setOpen = true; setPage = 0; setSel = 0; statusMsg.clear();
        for (;;) {
            draw();
            int k = get_key();
            if (k == -2) continue;
            if (k == 27) { if (setPage == 0) break; else { setPage = 0; continue; } }
            auto names = providerNames(cfg);
            if (setPage == 0) {
                if (k == K_UP && setSel > 0) setSel--;
                if (k == K_DOWN && setSel < (int)names.size() - 1) setSel++;
                if (k == 'a' || k == 'A') wizard(cfg);
                if ((k == 'd' || k == 'D') && !names.empty()) {
                    std::string ans = run_dialog(L("удалить '" + names[setSel] + "'? y/n", "delete '" + names[setSel] + "'? y/n"), "", false);
                    if (ans == "y" || ans == "Y" || ans == "д") {
                        cfg.providers.erase(names[setSel]);
                        if (cfg.active_provider == names[setSel]) { cfg.active_provider.clear(); cfg.active_model.clear(); }
                        save_config(cfg);
                        setSel = 0;
                    }
                }
                if (k == 'm' || k == 'M') {
                    if (cfg.mode == "full") cfg.mode = "folder";
                    else if (cfg.mode == "folder") cfg.mode = "read";
                    else cfg.mode = "full";
                    save_config(cfg);
                    statusMsg = L("режим: ", "mode: ") + cfg.mode;
                }
                if (k == 'w' || k == 'W') {
                    std::string wd = run_dialog(L("рабочая папка агента:", "agent workdir:"),
                                                cfg.workdir.empty() ? fs::current_path().string() : cfg.workdir, false);
                    if (!wd.empty()) { cfg.workdir = wd; save_config(cfg); statusMsg = "workdir: " + wd; }
                }
                if (k == '\r' || k == '\n') {
                    if (!names.empty()) { providerPage = names[setSel]; setPage = 1; setModelSel = 0; }
                }
            } else {
                if (cfg.providers.count(providerPage) == 0) { setPage = 0; continue; }
                auto& p = cfg.providers[providerPage];
                if (k == K_UP && setModelSel > 0) setModelSel--;
                if (k == K_DOWN && setModelSel < (int)p.models.size() - 1) setModelSel++;
                if ((k == 'f' || k == 'F') && !providerPage.empty()) {
                    statusMsg = L("обновляю модели…", "fetching models…");
                    draw();
                    auto ids = fetch_models(providerPage, p);
                    if (!ids.empty()) { p.models = ids; save_config(cfg); statusMsg = L("моделей: ", "models: ") + std::to_string(ids.size()); }
                    else statusMsg = L("не удалось получить список", "failed to fetch");
                }
                if (k == 'a' || k == 'A') {
                    std::string m = run_dialog(L("имя модели:", "model name:"), "", false);
                    if (!m.empty()) { p.models.push_back(m); save_config(cfg); }
                }
                if (k == '\r' || k == '\n') {
                    if (!p.models.empty()) {
                        cfg.active_provider = providerPage;
                        cfg.active_model = p.models[setModelSel];
                        save_config(cfg);
                        statusMsg = L("выбрано: ", "selected: ") + cfg.active_model;
                    }
                }
            }
        }
        setOpen = false;
        return true;
    }

    void wizard(Config& cfg) {
        std::vector<std::string> opts;
        for (auto& [n, p] : PRESETS) opts.push_back(n + "  ·  " + p.second);
        opts.push_back(L("другое (свой base_url)", "custom (own base_url)"));
        int sel = 0;
        for (;;) {
            int w = cols < 60 ? cols - 4 : 56;
            int h = (int)opts.size() + 4;
            int top = (rows - h) / 2; if (top < 1) top = 1;
            int x0 = (cols - w) / 2; if (x0 < 1) x0 = 1;
            std::string border; for (int i = 0; i < w - 2; i++) border += "─";
            std::string f = "\033[2J";
            f += "\033[" + std::to_string(top) + ";" + std::to_string(x0) + "H" + dim("╭" + border + "╮");
            auto row = [&](int y, const std::string& txt) {
                std::string p = txt;
                while (utf8_len(p) < (size_t)w - 4) p += " ";
                f += "\033[" + std::to_string(y) + ";" + std::to_string(x0) + "H" + dim("│") + p + dim("│");
            };
            row(top + 1, paint("1", " " + L("Добавить провайдера", "Add provider")));
            for (int i = 0; i < (int)opts.size(); i++) {
                row(top + 2 + i, (i == sel ? paint("38;5;208", " ❯ ") : "   ") + opts[i]);
            }
            f += "\033[" + std::to_string(top + h - 1) + ";" + std::to_string(x0) + "H" + dim("╰" + border + "╯");
            f += "\033[J";
            wraw(f);
            int k = get_key();
            if (k == -2) continue;
            if (k == 27) return;
            if (k == K_UP && sel > 0) sel--;
            if (k == K_DOWN && sel < (int)opts.size() - 1) sel++;
            if (k == '\r' || k == '\n') break;
        }
        bool custom = (sel == (int)opts.size() - 1);
        std::string name, url;
        if (!custom) {
            int i = 0;
            for (auto& [n, p] : PRESETS) { if (i++ == sel) { name = n; url = p.first; } }
        } else {
            name = run_dialog(L("имя провайдера:", "provider name:"), "", false);
            if (name.empty()) return;
        }
        url = run_dialog("base_url:", url, false);
        if (url.empty()) return;
        while (!url.empty() && url.back() == '/') url.pop_back();
        std::string key = run_dialog(L("API-ключ:", "API key:"), "", true);
        Provider p; p.base_url = url; p.key = key;
        cfg.providers[name] = p;
        save_config(cfg);
        statusMsg = L("обновляю модели…", "fetching models…");
        auto ids = fetch_models(name, p);
        cfg.providers[name].models = ids;
        if (cfg.active_provider.empty() && !ids.empty()) {
            cfg.active_provider = name;
            cfg.active_model = ids[0];
        }
        save_config(cfg);
        statusMsg = ids.empty()
            ? L("добавлен, модели не получены", "added, no models fetched")
            : L("добавлен, моделей: ", "added, models: ") + std::to_string(ids.size());
    }

    void draw_settings(Config& cfg) {
        int w = cols < 64 ? cols - 4 : 60;
        int x0 = (cols - w) / 2; if (x0 < 1) x0 = 1;
        std::string border; for (int i = 0; i < w - 2; i++) border += "─";
        std::string f = "\033[2J";
        auto row = [&](int y, const std::string& txt) {
            std::string p = txt;
            while (utf8_len(p) < (size_t)w - 4) p += " ";
            f += "\033[" + std::to_string(y) + ";" + std::to_string(x0) + "H" + dim("│") + p + dim("│");
        };
        auto names = providerNames(cfg);
        if (setPage == 0) {
            int h = (int)names.size() + 8;
            int top = (rows - h) / 2; if (top < 1) top = 1;
            f += "\033[" + std::to_string(top) + ";" + std::to_string(x0) + "H" + dim("╭" + border + "╮");
            row(top + 1, paint("1", " " + L("Настройки", "Settings")));
            row(top + 2, dim(" " + L("Enter — модели · a — добавить · d — удалить · m — режим · w — папка", "Enter — models · a — add · d — delete · m — mode · w — workdir")));
            row(top + 3, dim(" ──────────────────────────────"));
            if (names.empty()) row(top + 4, dim(" " + L("пусто — нажми a", "empty — press a")));
            for (int i = 0; i < (int)names.size(); i++) {
                auto& p = cfg.providers[names[i]];
                std::string key = ::get_key(names[i], p).empty() ? red(L("нет ключа", "no key")) : green(L("ключ есть", "key set"));
                std::string act = (names[i] == cfg.active_provider) ? paint("38;5;208", "●") : " ";
                row(top + 4 + i, (i == setSel ? paint("38;5;208", " ❯ ") : "   ") + act + " " + names[i] + "  " + key);
            }
            int ly = top + 4 + (int)names.size();
            std::string wd = cfg.workdir.empty() ? fs::current_path().string() : cfg.workdir;
            row(ly, dim(" " + L("режим: ", "mode: ")) + cfg.mode + dim("  ·  " + wd));
            row(ly + 1, dim(" " + (statusMsg.empty() ? L("Esc — закрыть", "Esc — close") : statusMsg)));
            f += "\033[" + std::to_string(top + h - 1) + ";" + std::to_string(x0) + "H" + dim("╰" + border + "╯");
        } else {
            if (cfg.providers.count(providerPage) == 0) { f += "\033[J"; wraw(f); return; }
            auto& p = cfg.providers[providerPage];
            int h = (int)p.models.size() + 7;
            int top = (rows - h) / 2; if (top < 1) top = 1;
            f += "\033[" + std::to_string(top) + ";" + std::to_string(x0) + "H" + dim("╭" + border + "╮");
            row(top + 1, paint("1", " " + providerPage + " · " + (cfg.active_provider == providerPage && !cfg.active_model.empty() ? cfg.active_model : "—")));
            row(top + 2, dim(" " + L("Enter — выбрать · f — обновить · a — добавить модель", "Enter — select · f — fetch · a — add model")));
            row(top + 3, dim(" ──────────────────────────────"));
            if (p.models.empty()) row(top + 4, dim(" " + L("моделей нет — нажми f", "no models — press f")));
            for (int i = 0; i < (int)p.models.size(); i++) {
                std::string act = (p.models[i] == cfg.active_model && providerPage == cfg.active_provider) ? paint("38;5;208", "●") : " ";
                row(top + 4 + i, (i == setModelSel ? paint("38;5;208", " ❯ ") : "   ") + act + " " + p.models[i]);
            }
            row(top + h - 2, dim(" " + (statusMsg.empty() ? L("Esc — назад", "Esc — back") : statusMsg)));
            f += "\033[" + std::to_string(top + h - 1) + ";" + std::to_string(x0) + "H" + dim("╰" + border + "╯");
        }
        f += "\033[J";
        wraw(f);
    }

    void draw() {
        if (!active) return;
        tui_size(rows, cols);
        if (cols < 20 || rows < 6) return;
        if (dlgOpen) { draw_dialog(); return; }
        if (palOpen) { draw_palette(); return; }
        if (setOpen && pCfg) { draw_settings(*pCfg); return; }
        if (msgs.empty()) draw_splash(); else draw_chat();
    }

    int get_key() {
#ifdef _WIN32
        while (!tui_key_avail(0)) Sleep(40);
        int c = tui_read_byte();
        if (c == 224 || c == 0) {
            c = tui_read_byte();
            switch (c) { case 72: return K_UP; case 80: return K_DOWN; case 75: return K_LEFT; case 77: return K_RIGHT; case 83: return K_DEL; default: return K_IGNORE; }
        }
        return c;
#else
        for (;;) {
            pollfd p{0, POLLIN, 0};
            int pr = poll(&p, 1, 250);
            if (pr == 0) return -2;
            if (pr < 0) continue;
            unsigned char c;
            if (read(0, &c, 1) != 1) return -1;
            if (c != 27) return c;
            pollfd p2{0, POLLIN, 0};
            if (poll(&p2, 1, 30) <= 0) return 27;
            if (read(0, &c, 1) != 1) return K_IGNORE;
            if (c != '[') return K_IGNORE;
            pollfd p3{0, POLLIN, 0};
            if (poll(&p3, 1, 30) <= 0) return K_IGNORE;
            if (read(0, &c, 1) != 1) return K_IGNORE;
            switch (c) {
                case 'A': return K_UP;
                case 'B': return K_DOWN;
                case 'C': return K_RIGHT;
                case 'D': return K_LEFT;
                case '3': { pollfd p4{0, POLLIN, 0}; if (poll(&p4, 1, 30) > 0) { unsigned char t; read(0, &t, 1); } return K_DEL; }
                default: return K_IGNORE;
            }
        }
#endif
    }

    std::string read_line() {
        input.clear();
        hist_idx = (int)history.size();
        for (;;) {
            draw();
            int k = get_key();
            if (k == -2) continue;
            if (k == -1 || k == 4) return "\x04";
            if (k == 16) { // ctrl+p
                std::string cmd = run_palette();
                if (!cmd.empty()) return cmd;
                continue;
            }
            if (k == 3) {
                if (!input.empty()) { input.clear(); continue; }
                g_interrupt = true;
                return "\x04";
            }
            if (k == '\r' || k == '\n') {
                std::string v = input;
                input.clear();
                if (!v.empty()) history.push_back(v);
                hist_idx = (int)history.size();
                return v;
            }
            if (k == 127 || k == 8) { utf8_pop(input); continue; }
            if (k == 21) { input.clear(); continue; }
            if (k == K_UP) { if (hist_idx > 0) { hist_idx--; input = history[hist_idx]; } continue; }
            if (k == K_DOWN) {
                if (hist_idx < (int)history.size() - 1) { hist_idx++; input = history[hist_idx]; }
                else { hist_idx = (int)history.size(); input.clear(); }
                continue;
            }
            if (k == K_LEFT || k == K_RIGHT || k == K_DEL || k == K_IGNORE) continue;
            if (k >= 32) { input += (char)k; continue; }
        }
    }

    void enter() { tui_raw(true); wraw("\033[?1049h\033[H\033[2J"); }
    void leave() { wraw("\033[?1049l\033[?25h"); tui_raw(false); active = false; }
    void update_status(const Config& cfg) {
        g_status = (cfg.active_provider.empty() ? std::string("—") : cfg.active_provider) + "/" +
                   (cfg.active_model.empty() ? std::string("—") : cfg.active_model);
    }
};

Tui tui;

struct LogBuf : std::stringbuf {
    int sync() override {
        std::string s = str();
        str("");
        tui.feed(s);
        return 0;
    }
};

std::string ask_line(bool secret = false) {
    if (tui.active) {
        std::cout.flush();
        std::string v = tui.run_dialog("", "", secret);
        if (!v.empty()) tui.addMsg(MsgKind::Info, (secret ? std::string(Tui::utf8_len(v), '*') : v));
        return v;
    }
    std::string v;
    std::getline(std::cin, v);
    return v;
}

std::string ask_secret(const std::string& prompt) {
    if (tui.active) {
        std::string v = tui.run_dialog(prompt, "", true);
        if (!v.empty()) tui.addMsg(MsgKind::Info, std::string(Tui::utf8_len(v), '*'));
        return v;
    }
    return getpass_cpp(prompt);
}

void tui_poll_interrupt() {
    if (!tui.active) return;
    while (tui_key_avail(0)) {
        int c = tui_read_byte();
        if (c == 3) g_interrupt = true;
    }
}

struct CurlResponse {
    long status = 0;
    std::string body;
};

size_t write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

struct StreamState {
    std::function<void(const std::string&, const std::string&)> on_chunk;
    std::string buffer;
    bool show_think;
};

size_t stream_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* st = static_cast<StreamState*>(userdata);
    st->buffer.append(static_cast<char*>(ptr), size * nmemb);
    size_t pos;
    while ((pos = st->buffer.find('\n')) != std::string::npos) {
        std::string line = st->buffer.substr(0, pos);
        st->buffer.erase(0, pos + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.substr(0, 5) != "data:") continue;
        std::string data = line.substr(5);
        while (!data.empty() && data[0] == ' ') data.erase(0, 1);
        if (data == "[DONE]") continue;
        try {
            json obj = json::parse(data);
            if (obj.contains("error")) {
                std::cerr << red("\n" + obj["error"].dump()) << "\n";
                continue;
            }
            if (obj.contains("usage") && obj["usage"].is_object()) {
                g_usage.got = true;
                g_usage.prompt = obj["usage"].value("prompt_tokens", 0);
                g_usage.completion = obj["usage"].value("completion_tokens", 0);
            }
            auto choices = obj.value("choices", json::array());
            if (choices.empty()) continue;
            auto delta = choices[0].value("delta", json::object());
            std::string think = delta.value("reasoning_content", "") ;
            if (think.empty()) think = delta.value("reasoning", "");
            if (!think.empty() && st->show_think) st->on_chunk("think", think);
            std::string content = delta.value("content", "");
            if (!content.empty()) st->on_chunk("text", content);
        } catch (...) {}
    }
    return size * nmemb;
}

void apply_tls(CURL* curl) {
#if defined(_WIN32) || defined(__MINGW32__)
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#else
    (void)curl;
#endif
}

CurlResponse http_get(const std::string& url, const std::string& key) {
    CURL* curl = curl_easy_init();
    CurlResponse resp;
    if (!curl) return resp;
    apply_tls(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + key).c_str());
    headers = curl_slist_append(headers, "User-Agent: kinecode/0.5");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

bool chat_stream_curl(const std::string& url, const std::string& key,
                      const json& payload, bool show_think,
                      std::function<void(const std::string&, const std::string&)> on_chunk) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    apply_tls(curl);
    std::string body = payload.dump();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
        [](void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int { return g_interrupt ? 1 : 0; });
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    headers = curl_slist_append(headers, ("Authorization: Bearer " + key).c_str());
    headers = curl_slist_append(headers, "User-Agent: kinecode/0.5");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    StreamState st{on_chunk, "", show_think};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &st);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res == CURLE_OK;
}

std::vector<std::string> fetch_models(const std::string& name, const Provider& p) {
    std::string key = get_key(name, p);
    auto resp = http_get(p.base_url + "/models", key);
    if (resp.status != 200) {
        std::cerr << red("HTTP " + std::to_string(resp.status) + ": " + resp.body.substr(0, 200)) << "\n";
        return {};
    }
    try {
        json obj = json::parse(resp.body);
        auto items = obj.value("data", obj.value("models", json::array()));
        std::vector<std::string> ids;
        for (auto& it : items) {
            std::string mid = it.is_object() ? it.value("id", "") : it.get<std::string>();
            if (mid.substr(0, 7) == "models/") mid = mid.substr(7);
            if (!mid.empty()) ids.push_back(mid);
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    } catch (...) {
        return {};
    }
}

int choose_item(const std::vector<std::string>& items, const std::string& current) {
    for (size_t i = 0; i < items.size(); i++) {
        std::string mark = (items[i] == current) ? green("●") : " ";
        std::cout << " " << mark << " " << (i + 1) << ". " << items[i] << "\n";
    }
    std::cout << dim(L("номер (Enter - отмена): ", "number (Enter - cancel): "));
    std::string ans = ask_line();
    int n = 0;
    try { n = std::stoi(ans); } catch (...) {}
    if (n >= 1 && n <= (int)items.size()) return n - 1;
    return -1;
}

// ---------- агент: выполнение команд ----------

std::vector<std::string> extractRunBlocks(const std::string& s) {
    std::vector<std::string> out;
    size_t pos = 0;
    for (;;) {
        size_t p = s.find("```run", pos);
        if (p == std::string::npos) break;
        size_t nl = s.find('\n', p);
        if (nl == std::string::npos) break;
        size_t endq = s.find("```", nl);
        if (endq == std::string::npos) break;
        std::string body = s.substr(nl + 1, endq - nl - 1);
        while (!body.empty() && (body.back() == '\n' || body.back() == ' ')) body.pop_back();
        while (!body.empty() && body.front() == ' ') body.erase(0, 1);
        if (!body.empty()) out.push_back(body);
        pos = endq + 3;
    }
    return out;
}

bool isReadCmd(const std::string& cmd) {
    static const char* allow[] = {"ls","cat","head","tail","grep","find","wc","du","df","ps",
        "file","stat","which","uname","uptime","free","whoami","pwd","date","echo","id",
        "hostname","lsblk","lscpu","ip","ss","git","man","sort","uniq","cut","awk"};
    if (cmd.find('>') != std::string::npos || cmd.find('`') != std::string::npos ||
        cmd.find("$(") != std::string::npos)
        return false;
    std::string seg;
    std::vector<std::string> segs;
    for (size_t i = 0; i < cmd.size(); i++) {
        if (cmd[i] == ';' || cmd[i] == '|') {
            segs.push_back(seg); seg.clear();
            if (i + 1 < cmd.size() && (cmd[i+1] == '&' || cmd[i+1] == '|')) i++;
            continue;
        }
        seg += cmd[i];
    }
    segs.push_back(seg);
    for (auto& s : segs) {
        while (!s.empty() && s.front() == ' ') s.erase(0, 1);
        if (s.empty()) continue;
        std::string first = s.substr(0, s.find(' '));
        bool ok = false;
        for (auto* a : allow) if (first == a) { ok = true; break; }
        if (!ok) return false;
    }
    return true;
}

std::string execShell(const std::string& cmdline) {
#ifdef _WIN32
    FILE* f = _popen(cmdline.c_str(), "r");
#else
    FILE* f = popen(cmdline.c_str(), "r");
#endif
    if (!f) return "(failed to start)";
    std::string out;
    char buf[4096];
    size_t total = 0;
    while (fgets(buf, sizeof(buf), f)) {
        out += buf;
        total += strlen(buf);
        if (total > 65536) { out += "\n…(обрезано)"; break; }
    }
    int rc =
#ifdef _WIN32
        _pclose(f);
#else
        pclose(f);
#endif
    return out + "[exit: " + std::to_string(rc) + "]";
}

std::string runToolCmd(const std::string& cmd, const Config& cfg) {
    std::string wd = cfg.workdir.empty() ? fs::current_path().string() : cfg.workdir;
#ifdef _WIN32
    return "cd /d \"" + wd + "\" && " + cmd;
#else
    return "cd '" + wd + "' && " + cmd;
#endif
}

bool approveTool(const std::string& cmd, const Config& cfg, std::set<std::string>& approved) {
    if (cfg.mode == "read") return isReadCmd(cmd);
    if (approved.count(cmd)) return true;
    if (tui.active) {
        std::string a = tui.run_dialog(L("Выполнить команду? y/n/a(всегда)", "Run command? y/n/a(always)"), cmd, false);
        if (a == "y" || a == "Y" || a == "yes" || a == "д" || a == "да") return true;
        if (a == "a" || a == "A" || a == "always") { approved.insert(cmd); return true; }
        return false;
    }
    std::cout << paint("38;5;208", " ▌ ") << cmd << "\n"
              << dim(L("выполнить? [y/n/a=всегда] ", "run? [y/n/a=always] "));
    std::cout.flush();
    std::string a;
    std::getline(std::cin, a);
    if (a == "y" || a == "Y" || a == "yes" || a == "д") return true;
    if (a == "a" || a == "A") { approved.insert(cmd); return true; }
    return false;
}

std::string agentPrompt(const Config& cfg) {
    std::string wd = cfg.workdir.empty() ? fs::current_path().string() : cfg.workdir;
#ifdef _WIN32
    std::string osname = "windows";
#else
    std::string osname = "linux";
#endif
    std::string base = L("Ты — агент в терминале пользователя. ОС: " + osname + ". Рабочая папка: " + wd + ". ",
                         "You are a terminal agent. OS: " + osname + ". Workdir: " + wd + ". ");
    if (cfg.mode == "read")
        return base + L("Режим read-only: тебе доступны только команды чтения (ls, cat, head, tail, grep, find, wc, du, df, ps, file, stat).",
                        "Read-only mode: only reading commands are available (ls, cat, head, tail, grep, find, wc, du, df, ps, file, stat).");
    return base + L("Чтобы выполнить команду оболочки, выведи её отдельным блоком:\n```run\nкоманда\n```\nКлиент спросит подтверждение и вернёт вывод следующим сообщением. Не выдумывай результаты команд — жди их.",
                    "To run a shell command output a block:\n```run\ncommand\n```\nThe client will ask for confirmation and return the output. Never invent command results — wait for them.");
}

void cmd_send(Config& cfg, const std::string& text,
              std::vector<json>& messages, bool show_think,
              const json& extra_content = json()) {
    Provider* p = active_provider(cfg);
    if (!p || cfg.active_model.empty()) {
        std::cerr << red(L("Не выбран провайдер/модель", "No provider/model selected")) << "\n";
        return;
    }
    json user_msg;
    if (extra_content.is_array())
        user_msg = {{"role", "user"}, {"content", extra_content}};
    else
        user_msg = {{"role", "user"}, {"content", text}};
    messages.push_back(user_msg);

    std::set<std::string> approved;

    for (int agentIter = 0; agentIter < 8; agentIter++) {
        json payload_msgs = json::array();
        if (!cfg.system_prompt.empty())
            payload_msgs.push_back({{"role", "system"}, {"content", cfg.system_prompt}});
        payload_msgs.push_back({{"role", "system"}, {"content", agentPrompt(cfg)}});
        for (auto& m : messages) payload_msgs.push_back(m);

        json payload = {
            {"model", cfg.active_model},
            {"messages", payload_msgs},
            {"stream", true}
        };

        std::string url = p->base_url + "/chat/completions";
        std::string key = get_key(cfg.active_provider, *p);
        std::string answer;
        bool thinking = false;
        auto t0 = std::chrono::steady_clock::now();
        g_usage = Usage();
        if (tui.active) tui.beginStream();

        bool ok = chat_stream_curl(url, key, payload, show_think,
            [&](const std::string& kind, const std::string& piece) {
                if (kind == "think") {
                    if (!thinking) { std::cout << dim(L("┌ рассуждает\n", "┌ thinking\n")); thinking = true; }
                    std::cout << dim(piece);
                } else {
                    if (thinking) { std::cout << "\n\n"; thinking = false; }
                    std::cout << piece;
                    answer += piece;
                }
                std::cout.flush();
                tui_poll_interrupt();
            });
        bool interrupted = g_interrupt;
        g_interrupt = false;

        if (interrupted) std::cout << dim(L("[прервано]", "[interrupted]")) << "\n";
        if (!ok && !interrupted) {
            std::cerr << red(L("Ошибка сети", "Network error")) << "\n";
            messages.pop_back();
            return;
        }

        double dur = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        long toks = g_usage.got ? g_usage.completion : (long)((Tui::utf8_len(answer) + 3) / 4);
        double tps = dur > 0.05 ? toks / dur : 0.0;
        std::string meta = "Chat · " + cfg.active_model + " · " + fmtDur(dur);
        if (toks > 0) meta += " · " + fmtTps(tps);
        if (g_usage.got) g_ctx_tokens = g_usage.prompt + g_usage.completion;
        else g_ctx_tokens += toks;
        g_usage = Usage();
        if (tui.active) tui.addMsg(MsgKind::Meta, meta);
        else std::cout << dim(meta) << "\n";

        if (!answer.empty())
            messages.push_back({{"role", "assistant"}, {"content", answer}});
        else {
            messages.pop_back();
            return;
        }

        auto blocks = extractRunBlocks(answer);
        if (blocks.empty() || interrupted) break;

        std::string results;
        for (auto& cmd : blocks) {
            bool allowed = approveTool(cmd, cfg, approved);
            if (!allowed) {
                std::string why = (cfg.mode == "read" && !isReadCmd(cmd))
                    ? L("read-only: запись запрещена", "read-only: writes are blocked")
                    : L("отклонено пользователем", "declined by user");
                results += L("пользователь отклонил команду: ", "user declined command: ") + cmd + " (" + why + ")\n";
                if (tui.active) tui.addMsg(MsgKind::Error, "$ " + cmd + " — " + why);
                else std::cout << red("$ " + cmd + " — " + why) << "\n";
                continue;
            }
            if (tui.active) tui.addMsg(MsgKind::Info, "$ " + cmd);
            else std::cout << dim("$ " + cmd) << "\n";
            std::string out = execShell(runToolCmd(cmd, cfg));
            std::string disp = out;
            if (disp.size() > 2000) disp = disp.substr(0, 2000) + "\n…";
            if (tui.active) tui.addMsg(MsgKind::Info, disp);
            else std::cout << dim(disp) << "\n";
            results += "$ " + cmd + "\n" + out + "\n";
        }

        messages.push_back({{"role", "user"},
            {"content", L("[вывод команд]\n", "[command output]\n") + results + L("\nПродолжай задачу.", "\nContinue the task.")}});
        if (tui.active) tui.addMsg(MsgKind::Info, L("↑ результат передан модели", "↑ result sent to model"));
    }
}

bool is_image_ext(const std::string& ext) {
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".gif";
}

std::string base64_encode(const std::vector<uint8_t>& data) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = data[i] << 16;
        if (i + 1 < data.size()) n |= data[i+1] << 8;
        if (i + 2 < data.size()) n |= data[i+2];
        out += chars[(n >> 18) & 63];
        out += chars[(n >> 12) & 63];
        out += (i + 1 < data.size()) ? chars[(n >> 6) & 63] : '=';
        out += (i + 2 < data.size()) ? chars[n & 63] : '=';
    }
    return out;
}

std::string mime_type(const std::string& ext) {
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".webp") return "image/webp";
    if (ext == ".gif") return "image/gif";
    return "application/octet-stream";
}

std::pair<std::string, std::string> split_cmd_arg(const std::string& arg) {
    std::string path, question;
    std::string trimmed = arg;
    while (!trimmed.empty() && trimmed[0] == ' ') trimmed.erase(0, 1);
    if (trimmed.empty()) return {"", ""};
    if (trimmed[0] == '"') {
        size_t end = trimmed.find('"', 1);
        if (end != std::string::npos) {
            path = trimmed.substr(1, end - 1);
            question = trimmed.substr(end + 1);
            while (!question.empty() && question[0] == ' ') question.erase(0, 1);
        } else {
            path = trimmed.substr(1);
        }
    } else {
        size_t sp = trimmed.find(' ');
        if (sp != std::string::npos) {
            path = trimmed.substr(0, sp);
            question = trimmed.substr(sp + 1);
            while (!question.empty() && question[0] == ' ') question.erase(0, 1);
        } else {
            path = trimmed;
        }
    }
    if (!path.empty() && path[0] == '~') {
        const char* home = std::getenv("HOME");
        if (home) path = std::string(home) + path.substr(1);
    }
    return {path, question};
}

bool handle_command(Config& cfg, const std::string& line,
                    std::vector<json>& messages, bool& show_think) {
    size_t sp = line.find(' ', 1);
    std::string cmd = (sp != std::string::npos) ? line.substr(1, sp - 1) : line.substr(1);
    std::string arg = (sp != std::string::npos) ? line.substr(sp + 1) : "";
    while (!arg.empty() && arg.back() == ' ') arg.pop_back();

    if (cmd == "exit" || cmd == "quit" || cmd == "q") return false;

    if (cmd == "help") {
        std::cout << help_text() << "\n";
    } else if (cmd == "about") {
        std::cout << about_text() << "\n";
    } else if (cmd == "providers") {
        if (cfg.providers.empty()) { std::cout << dim(L("Пусто. Добавь: /addprovider deepseek\n", "Empty. Add: /addprovider deepseek\n")); return true; }
        for (auto& [n, p] : cfg.providers) {
            std::string mark = (n == cfg.active_provider) ? green("●") : " ";
            std::string k = get_key(n, p).empty() ? red("нет ключа") : green("ключ есть");
            std::cout << " " << mark << " " << n << "  " << dim(p.base_url) << "  " << k << "\n";
        }
    } else if (cmd == "provider") {
        if (cfg.providers.empty()) { std::cout << dim(L("Пусто.\n", "Empty.\n")); return true; }
        if (!arg.empty() && cfg.providers.count(arg)) {
            cfg.active_provider = arg;
            auto* p = active_provider(cfg);
            if (p && !p->models.empty()) cfg.active_model = p->models[0];
            save_config(cfg);
            std::cout << green(L("Провайдер: ", "Provider: ") + cfg.active_provider + ", " + L("модель: ", "model: ") + cfg.active_model) << "\n";
        } else {
            std::vector<std::string> names;
            for (auto& [n, _] : cfg.providers) names.push_back(n);
            int idx = choose_item(names, cfg.active_provider);
            if (idx >= 0) {
                cfg.active_provider = names[idx];
                auto* p = active_provider(cfg);
                if (p && !p->models.empty()) cfg.active_model = p->models[0];
                save_config(cfg);
                std::cout << green(L("Провайдер: ", "Provider: ") + cfg.active_provider) << "\n";
            }
        }
    } else if (cmd == "models") {
        auto* p = active_provider(cfg);
        if (!p) { std::cerr << red(L("Нет активного провайдера\n", "No active provider\n")); return true; }
        std::vector<std::string> ms = p->models.empty() ? std::vector<std::string>{dim(L("(пусто, /fetchmodels)", "(empty, /fetchmodels)"))} : p->models;
        choose_item(ms, cfg.active_model);
    } else if (cmd == "model") {
        auto* p = active_provider(cfg);
        if (!p) { std::cerr << red(L("Нет активного провайдера\n", "No active provider\n")); return true; }
        if (!arg.empty()) {
            cfg.active_model = arg;
            if (std::find(p->models.begin(), p->models.end(), arg) == p->models.end())
                p->models.push_back(arg);
            save_config(cfg);
            std::cout << green(L("Модель: ", "Model: ") + arg) << "\n";
        } else {
            int idx = choose_item(p->models, cfg.active_model);
            if (idx >= 0) { cfg.active_model = p->models[idx]; save_config(cfg); std::cout << green(L("Модель: ", "Model: ") + cfg.active_model) << "\n"; }
        }
    } else if (cmd == "fetchmodels") {
        auto* p = active_provider(cfg);
        if (!p) { std::cerr << red(L("Нет активного провайдера\n", "No active provider\n")); return true; }
        auto ids = fetch_models(cfg.active_provider, *p);
        if (!ids.empty()) { p->models = ids; save_config(cfg); std::cout << green(L("Моделей: ", "Models: ") + std::to_string(ids.size())) << "\n"; }
    } else if (cmd == "addprovider") {
        std::string name = arg;
        if (name.empty()) { std::cout << L("Имя: ", "Name: "); name = ask_line(); }
        if (name.empty()) return true;
        std::cout << "base_url: ";
        std::string url = ask_line();
        if (url.empty()) { std::cerr << red(L("base_url обязателен\n", "base_url required\n")); return true; }
        while (!url.empty() && url.back() == '/') url.pop_back();
        std::cout << L("API-ключ (скрыт): ", "API key (hidden): ");
        std::string key = ask_secret("");
        Provider p; p.base_url = url; p.key = key;
        cfg.providers[name] = p;
        auto ids = fetch_models(name, p);
        p.models = ids;
        cfg.providers[name] = p;
        save_config(cfg);
        if (cfg.active_provider.empty()) { cfg.active_provider = name; if (!ids.empty()) cfg.active_model = ids[0]; save_config(cfg); }
        std::cout << green(L("Добавлен: ", "Added: ") + name) << "\n";
    } else if (cmd == "delprovider") {
        if (cfg.providers.erase(arg)) {
            if (cfg.active_provider == arg) { cfg.active_provider.clear(); cfg.active_model.clear(); }
            save_config(cfg);
            std::cout << green(L("Удалён: ", "Removed: ") + arg) << "\n";
        } else std::cerr << red(L("Нет такого\n", "Not found\n"));
    } else if (cmd == "key") {
        std::string name = arg.empty() ? cfg.active_provider : arg;
        if (!cfg.providers.count(name)) { std::cerr << red(L("Нет такого\n", "Not found\n")); return true; }
        std::cout << L("Новый ключ: ", "New key: ");
        cfg.providers[name].key = ask_secret("");
        save_config(cfg);
        std::cout << green(L("Сохранён", "Saved")) << "\n";
    } else if (cmd == "system") {
        if (arg.empty()) std::cout << dim(cfg.system_prompt) << "\n";
        else { cfg.system_prompt = arg; save_config(cfg); std::cout << green(L("Обновлён", "Updated")) << "\n"; }
    } else if (cmd == "think") {
        show_think = (arg != "off");
        std::cout << dim(show_think ? L("Рассуждения: показываю", "Reasoning: shown") : L("Рассуждения: скрыты", "Reasoning: hidden")) << "\n";
    } else if (cmd == "clear") {
        messages.clear();
        if (tui.active) tui.msgs.clear();
        std::cout << dim(L("Диалог очищен", "Chat cleared")) << "\n";
    } else if (cmd == "file") {
        auto [path, question] = split_cmd_arg(arg);
        if (path.empty()) { std::cerr << red(L("Укажи путь: /file <файл> [вопрос]\n", "Usage: /file <path> [question]\n")); return true; }
        fs::path fp(path);
        std::string ext = fp.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (is_image_ext(ext)) { std::cerr << red(L("Это фото. Используй /img\n", "This is an image. Use /img\n")); return true; }
        if (!fs::exists(fp)) { std::cerr << red(L("Файл не найден: ", "File not found: ") + path) << "\n"; return true; }
        std::ifstream f(fp);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (content.size() > 120000) content = content.substr(0, 120000) + "\n...[обрезан]";
        std::string text = "[Файл: " + fp.filename().string() + "]\n```\n" + content + "\n```\n\n" + (question.empty() ? L("Опиши содержимое", "Describe the contents") : question);
        std::cout << dim(L("Отправляю файл ", "Sending file ") + fp.filename().string() + " ...") << "\n";
        cmd_send(cfg, text, messages, show_think);
    } else if (cmd == "img") {
        auto [path, question] = split_cmd_arg(arg);
        if (path.empty()) { std::cerr << red(L("Укажи путь: /img <фото> [вопрос]\n", "Usage: /img <path> [question]\n")); return true; }
        fs::path fp(path);
        std::string ext = fp.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (!is_image_ext(ext)) { std::cerr << red(L("Не похоже на фото (" + ext + "). Форматы: png, jpg, webp, gif. Для текста: /file\n", "Not an image (" + ext + "). Supported: png, jpg, webp, gif. For text: /file\n")); return true; }
        if (!fs::exists(fp)) { std::cerr << red(L("Файл не найден: ", "File not found: ") + path) << "\n"; return true; }
        auto sz = fs::file_size(fp);
        if (sz > 10 * 1024 * 1024) { std::cerr << red(L("Фото слишком большое (>10MB)\n", "Image too large (>10MB)\n")); return true; }
        std::ifstream f(fp, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::string b64 = base64_encode(data);
        std::string mime = mime_type(ext);
        std::string data_uri = "data:" + mime + ";base64," + b64;
        json content = json::array({
            {{"type", "text"}, {"text", question.empty() ? L("Опиши что на фото", "Describe this image") : question}},
            {{"type", "image_url"}, {"image_url", {{"url", data_uri}}}}
        });
        std::cout << dim(L("Отправляю фото ", "Sending image ") + fp.filename().string() + " ...") << "\n";
        if (cfg.active_model.find("gpt-4o") == std::string::npos &&
            cfg.active_model.find("gemini") == std::string::npos &&
            cfg.active_model.find("claude") == std::string::npos &&
            cfg.active_model.find("vision") == std::string::npos) {
            std::cout << yellow(L("Внимание: модель '", "Warning: model '") + cfg.active_model + L("' возможно не поддерживает фото.", "' may not support images.")) << "\n";
        }
        cmd_send(cfg, "", messages, show_think, content);
    } else if (cmd == "settings") {
        if (tui.active) tui.run_settings(cfg);
        else std::cout << dim(L("настройки доступны в TUI (запусти в терминале)", "settings available in TUI mode")) << "\n";
    } else if (cmd == "mode") {
        if (arg == "full" || arg == "folder" || arg == "read") {
            cfg.mode = arg; save_config(cfg);
            std::cout << green(L("режим: ", "mode: ") + cfg.mode) << "\n";
        } else {
            std::cout << dim(L("режим: ", "mode: ") + cfg.mode + L("  (full | folder | read)", "  (full | folder | read)")) << "\n";
        }
    } else if (cmd == "cwd") {
        if (arg.empty()) {
            std::cout << dim(cfg.workdir.empty() ? fs::current_path().string() : cfg.workdir) << "\n";
        } else {
            std::string wd = arg;
            if (!wd.empty() && wd[0] == '~') {
                const char* h = std::getenv("HOME");
                if (h) wd = std::string(h) + wd.substr(1);
            }
            cfg.workdir = wd; save_config(cfg);
            std::cout << green("workdir: " + wd) << "\n";
        }
    } else if (cmd == "plugins") {
        fs::path pd = conf_dir() / "plugins";
        bool any = false;
        if (fs::exists(pd))
            for (auto& e : fs::directory_iterator(pd))
                if (e.path().extension() == ".md") {
                    std::cout << " /" << e.path().stem().string() << "\n";
                    any = true;
                }
        if (!any) std::cout << dim(L("плагинов нет — положи .md файлы в ", "no plugins — put .md files in ") + pd.string()) << "\n";
    } else if (cmd == "language" || cmd == "lang") {
        if (arg == "en" || arg == "english") {
            lang = 1;
            save_config(cfg);
            std::cout << dim("Language: English") << "\n";
        } else if (arg == "ru" || arg == "russian") {
            lang = 0;
            save_config(cfg);
            std::cout << dim("Язык: русский") << "\n";
        } else {
            std::cout << dim(lang ? "Current: English. Use: /language ru|en" : "Текущий: русский. Использование: /language ru|en") << "\n";
        }
    } else {
        fs::path pp = conf_dir() / "plugins" / (cmd + ".md");
        if (fs::exists(pp)) {
            std::ifstream f(pp);
            std::string tpl((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (!arg.empty()) tpl += "\n\n" + arg;
            std::cout << dim(L("плагин: ", "plugin: ") + cmd) << "\n";
            cmd_send(cfg, tpl, messages, show_think);
        } else {
            std::cerr << red(L("Неизвестная команда /" + cmd + ". Список: /help", "Unknown command /" + cmd + ". See: /help")) << "\n";
        }
    }
    return true;
}

int console_main(Config& cfg) {
    std::cout << cyan(BANNER) << "\n";
    std::cout << dim("  v" + VERSION + "  ·  /help  ·  Ctrl+C\n\n");

    std::vector<json> messages;
    bool show_think = true;
    std::string line;

    while (true) {
        std::string label = (cfg.active_provider.empty() ? "—" : cfg.active_provider) + "/" +
                            (cfg.active_model.empty() ? "—" : cfg.active_model);
        std::string prompt = rl_paint("36", label) + rl_paint("1", " ❯ ");
        std::cout << prompt;
        if (!std::getline(std::cin, line)) break;
        while (!line.empty() && line.back() == '\\') {
            line.pop_back();
            std::cout << rl_paint("90", "… ");
            std::string next;
            if (!std::getline(std::cin, next)) break;
            line += "\n" + next;
        }
        if (line.empty()) continue;
        if (line[0] == '/') {
            if (!handle_command(cfg, line, messages, show_think)) break;
        } else {
            cmd_send(cfg, line, messages, show_think);
        }
    }
    return 0;
}

int tui_main(Config& cfg) {
    tui.active = true;
    COLOR = true;
    tui.pCfg = &cfg;
    tui.enter();
    LogBuf lbuf;
    auto* old_out = std::cout.rdbuf(&lbuf);
    auto* old_err = std::cerr.rdbuf(&lbuf);

    tui.update_status(cfg);

    std::vector<json> messages;
    bool show_think = true;

    for (;;) {
        tui.update_status(cfg);
        std::string line = tui.read_line();
        if (line == "\x04") break;
        if (line.empty()) continue;
        tui.addMsg(MsgKind::User, line);
        if (tui.sessionTitle.empty()) {
            tui.sessionTitle = line;
            size_t nl = tui.sessionTitle.find('\n');
            if (nl != std::string::npos) tui.sessionTitle = tui.sessionTitle.substr(0, nl);
            if (tui.sessionTitle.size() > 34) tui.sessionTitle = tui.sessionTitle.substr(0, 34) + "…";
        }
        bool cont = handle_command(cfg, line, messages, show_think);
        std::cout.flush();
                tui.draw();
        if (!cont) break;
    }

    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    tui.leave();
    return 0;
}

int main() {
#if defined(_WIN32) || defined(__MINGW32__)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    SetConsoleOutputCP(65001);
    bool tty_in = _isatty(_fileno(stdin)) != 0;
#else
    bool tty_in = isatty(STDIN_FILENO) != 0;
#endif
    curl_global_init(CURL_GLOBAL_DEFAULT);
    Config cfg = load_config();
    try { fs::create_directories(conf_dir()); } catch (...) {}

    int rc = tty_in ? tui_main(cfg) : console_main(cfg);
    curl_global_cleanup();
    return rc;
}
