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
#include <termios.h>
#include <unistd.h>
#include <curl/curl.h>
#include "nlohmann/json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

const std::string APP = "kinecode";
const std::string VERSION = "0.2-cpp";

fs::path conf_dir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    fs::path base = xdg && *xdg ? xdg : fs::path(std::getenv("HOME")) / ".config";
    return base / APP;
}
fs::path conf_file() { return conf_dir() / "config.json"; }
fs::path hist_file() { return conf_dir() / "history"; }

bool COLOR = isatty(STDOUT_FILENO) && !std::getenv("NO_COLOR");

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

const char* ABOUT_TEXT = R"(kinecode v{version} — минималистичный терминальный клиент
для OpenAI-совместимых API.

Created by fawwelll, Telegramm channel @jailbraked

Только C++17 + libcurl + nlohmann/json.)";

const char* HELP_TEXT = R"(Команды:
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
  /about                  о проекте
  /clear                  очистить диалог
  /help  /exit
Ctrl+C прерывает ответ.)";

struct Provider {
    std::string base_url;
    std::string key;
    std::vector<std::string> models;
};

struct Config {
    std::map<std::string, Provider> providers;
    std::string active_provider;
    std::string active_model;
    std::string system_prompt = "Ты помощник по программированию. Отвечай кратко и по делу.";
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
            cfg.system_prompt = j.value("system", cfg.system_prompt);
        } catch (...) {
            std::cerr << red("Ошибка чтения конфига") << "\n";
        }
    }
    return cfg;
}

void save_config(const Config& cfg) {
    try { fs::create_directories(conf_dir()); } catch (...) {}
    json j;
    j["providers"] = json::object();
    for (auto& [k, p] : cfg.providers) {
        j["providers"][k] = {{"base_url", p.base_url}, {"key", p.key}, {"models", p.models}};
    }
    j["active"] = {{"provider", cfg.active_provider}, {"model", cfg.active_model}};
    j["system"] = cfg.system_prompt;
    std::ofstream f(conf_file());
    f << j.dump(2);
    fs::permissions(conf_file(), fs::perms::owner_read | fs::perms::owner_write);
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
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    std::cout << prompt;
    std::string pass;
    std::getline(std::cin, pass);
    std::cout << "\n";
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return pass;
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

CurlResponse http_get(const std::string& url, const std::string& key) {
    CURL* curl = curl_easy_init();
    CurlResponse resp;
    if (!curl) return resp;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + key).c_str());
    headers = curl_slist_append(headers, "User-Agent: kinecode/0.2-cpp");
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
    std::string body = payload.dump();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    headers = curl_slist_append(headers, ("Authorization: Bearer " + key).c_str());
    headers = curl_slist_append(headers, "User-Agent: kinecode/0.2-cpp");
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
    std::cout << dim("номер (Enter - отмена): ");
    std::string ans;
    std::getline(std::cin, ans);
    int n = 0;
    try { n = std::stoi(ans); } catch (...) {}
    if (n >= 1 && n <= (int)items.size()) return n - 1;
    return -1;
}

void cmd_send(Config& cfg, const std::string& text,
              std::vector<json>& messages, bool show_think,
              const json& extra_content = json()) {
    Provider* p = active_provider(cfg);
    if (!p || cfg.active_model.empty()) {
        std::cerr << red("Не выбран провайдер/модель") << "\n";
        return;
    }
    json user_msg;
    if (extra_content.is_array()) {
        user_msg = {{"role", "user"}, {"content", extra_content}};
    } else {
        user_msg = {{"role", "user"}, {"content", text}};
    }
    messages.push_back(user_msg);

    json payload_msgs = json::array();
    if (!cfg.system_prompt.empty())
        payload_msgs.push_back({{"role", "system"}, {"content", cfg.system_prompt}});
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

    bool ok = chat_stream_curl(url, key, payload, show_think,
        [&](const std::string& kind, const std::string& piece) {
            if (kind == "think") {
                if (!thinking) { std::cout << dim("┌ рассуждает\n"); thinking = true; }
                std::cout << dim(piece);
            } else {
                if (thinking) { std::cout << "\n\n"; thinking = false; }
                std::cout << piece;
                answer += piece;
            }
            std::cout.flush();
        });

    std::cout << "\n";
    if (!ok) {
        std::cerr << red("Ошибка сети") << "\n";
        messages.pop_back();
        return;
    }
    if (!answer.empty()) {
        messages.push_back({{"role", "assistant"}, {"content", answer}});
    } else {
        messages.pop_back();
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
        std::cout << HELP_TEXT << "\n";
    } else if (cmd == "about") {
        std::string txt = ABOUT_TEXT;
        size_t pos = txt.find("{version}");
        if (pos != std::string::npos) txt.replace(pos, 9, VERSION);
        std::cout << txt << "\n";
    } else if (cmd == "providers") {
        if (cfg.providers.empty()) { std::cout << dim("Пусто. Добавь: /addprovider deepseek\n"); return true; }
        for (auto& [n, p] : cfg.providers) {
            std::string mark = (n == cfg.active_provider) ? green("●") : " ";
            std::string k = get_key(n, p).empty() ? red("нет ключа") : green("ключ есть");
            std::cout << " " << mark << " " << n << "  " << dim(p.base_url) << "  " << k << "\n";
        }
    } else if (cmd == "provider") {
        if (cfg.providers.empty()) { std::cout << dim("Пусто.\n"); return true; }
        if (!arg.empty() && cfg.providers.count(arg)) {
            cfg.active_provider = arg;
            auto* p = active_provider(cfg);
            if (p && !p->models.empty()) cfg.active_model = p->models[0];
            save_config(cfg);
            std::cout << green("Провайдер: " + cfg.active_provider + ", модель: " + cfg.active_model) << "\n";
        } else {
            std::vector<std::string> names;
            for (auto& [n, _] : cfg.providers) names.push_back(n);
            int idx = choose_item(names, cfg.active_provider);
            if (idx >= 0) {
                cfg.active_provider = names[idx];
                auto* p = active_provider(cfg);
                if (p && !p->models.empty()) cfg.active_model = p->models[0];
                save_config(cfg);
                std::cout << green("Провайдер: " + cfg.active_provider) << "\n";
            }
        }
    } else if (cmd == "models") {
        auto* p = active_provider(cfg);
        if (!p) { std::cerr << red("Нет активного провайдера\n"); return true; }
        std::vector<std::string> ms = p->models.empty() ? std::vector<std::string>{dim("(пусто, /fetchmodels)")} : p->models;
        choose_item(ms, cfg.active_model);
    } else if (cmd == "model") {
        auto* p = active_provider(cfg);
        if (!p) { std::cerr << red("Нет активного провайдера\n"); return true; }
        if (!arg.empty()) {
            cfg.active_model = arg;
            if (std::find(p->models.begin(), p->models.end(), arg) == p->models.end())
                p->models.push_back(arg);
            save_config(cfg);
            std::cout << green("Модель: " + arg) << "\n";
        } else {
            int idx = choose_item(p->models, cfg.active_model);
            if (idx >= 0) { cfg.active_model = p->models[idx]; save_config(cfg); std::cout << green("Модель: " + cfg.active_model) << "\n"; }
        }
    } else if (cmd == "fetchmodels") {
        auto* p = active_provider(cfg);
        if (!p) { std::cerr << red("Нет активного провайдера\n"); return true; }
        auto ids = fetch_models(cfg.active_provider, *p);
        if (!ids.empty()) { p->models = ids; save_config(cfg); std::cout << green("Моделей: " + std::to_string(ids.size())) << "\n"; }
    } else if (cmd == "addprovider") {
        std::string name = arg;
        if (name.empty()) { std::cout << "Имя: "; std::getline(std::cin, name); }
        if (name.empty()) return true;
        std::cout << "base_url: ";
        std::string url; std::getline(std::cin, url);
        if (url.empty()) { std::cerr << red("base_url обязателен\n"); return true; }
        while (!url.empty() && url.back() == '/') url.pop_back();
        std::string key = getpass_cpp("API-ключ (скрыт): ");
        Provider p; p.base_url = url; p.key = key;
        cfg.providers[name] = p;
        auto ids = fetch_models(name, p);
        p.models = ids;
        cfg.providers[name] = p;
        save_config(cfg);
        if (cfg.active_provider.empty()) { cfg.active_provider = name; if (!ids.empty()) cfg.active_model = ids[0]; save_config(cfg); }
        std::cout << green("Добавлен: " + name) << "\n";
    } else if (cmd == "delprovider") {
        if (cfg.providers.erase(arg)) {
            if (cfg.active_provider == arg) { cfg.active_provider.clear(); cfg.active_model.clear(); }
            save_config(cfg);
            std::cout << green("Удалён: " + arg) << "\n";
        } else std::cerr << red("Нет такого\n");
    } else if (cmd == "key") {
        std::string name = arg.empty() ? cfg.active_provider : arg;
        if (!cfg.providers.count(name)) { std::cerr << red("Нет такого\n"); return true; }
        cfg.providers[name].key = getpass_cpp("Новый ключ: ");
        save_config(cfg);
        std::cout << green("Сохранён") << "\n";
    } else if (cmd == "system") {
        if (arg.empty()) std::cout << dim(cfg.system_prompt) << "\n";
        else { cfg.system_prompt = arg; save_config(cfg); std::cout << green("Обновлён") << "\n"; }
    } else if (cmd == "think") {
        show_think = (arg != "off");
        std::cout << dim(show_think ? "Рассуждения: показываю" : "Рассуждения: скрыты") << "\n";
    } else if (cmd == "clear") {
        messages.clear();
        std::cout << dim("Диалог очищен") << "\n";
    } else if (cmd == "file") {
        auto [path, question] = split_cmd_arg(arg);
        if (path.empty()) { std::cerr << red("Укажи путь: /file <файл> [вопрос]\n"); return true; }
        fs::path fp(path);
        std::string ext = fp.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (is_image_ext(ext)) { std::cerr << red("Это фото. Используй /img\n"); return true; }
        if (!fs::exists(fp)) { std::cerr << red("Файл не найден: " + path) << "\n"; return true; }
        std::ifstream f(fp);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (content.size() > 120000) content = content.substr(0, 120000) + "\n...[обрезан]";
        std::string text = "[Файл: " + fp.filename().string() + "]\n```\n" + content + "\n```\n\n" + (question.empty() ? "Опиши содержимое" : question);
        std::cout << dim("Отправляю файл " + fp.filename().string() + " ...") << "\n";
        cmd_send(cfg, text, messages, show_think);
    } else if (cmd == "img") {
        auto [path, question] = split_cmd_arg(arg);
        if (path.empty()) { std::cerr << red("Укажи путь: /img <фото> [вопрос]\n"); return true; }
        fs::path fp(path);
        std::string ext = fp.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (!is_image_ext(ext)) { std::cerr << red("Не похоже на фото (" + ext + "). Поддерживаются: png, jpg, webp, gif. Для текста: /file\n"); return true; }
        if (!fs::exists(fp)) { std::cerr << red("Файл не найден: " + path) << "\n"; return true; }
        auto sz = fs::file_size(fp);
        if (sz > 10 * 1024 * 1024) { std::cerr << red("Фото слишком большое (>10MB)\n"); return true; }
        std::ifstream f(fp, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::string b64 = base64_encode(data);
        std::string mime = mime_type(ext);
        std::string data_uri = "data:" + mime + ";base64," + b64;
        json content = json::array({
            {{"type", "text"}, {"text", question.empty() ? "Опиши что на фото" : question}},
            {{"type", "image_url"}, {"image_url", {{"url", data_uri}}}}
        });
        std::cout << dim("Отправляю фото " + fp.filename().string() + " ...") << "\n";
        if (cfg.active_model.find("gpt-4o") == std::string::npos &&
            cfg.active_model.find("gemini") == std::string::npos &&
            cfg.active_model.find("claude") == std::string::npos &&
            cfg.active_model.find("vision") == std::string::npos) {
            std::cout << yellow("Внимание: модель '" + cfg.active_model + "' возможно не поддерживает фото.") << "\n";
        }
        cmd_send(cfg, "", messages, show_think, content);
    } else {
        std::cerr << red("Неизвестная команда /" + cmd + ". Список: /help") << "\n";
    }
    return true;
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    Config cfg = load_config();
    fs::create_directories(conf_dir());

    std::cout << cyan(BANNER) << "\n";
    std::cout << dim("  v" + VERSION + "  ·  /help - команды  ·  Ctrl+C - прервать\n\n");

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
    curl_global_cleanup();
    return 0;
}
