#include "Learning.h"
#include <algorithm>
#include <cstring>
#include <cctype>
namespace jd {
static Str value(JsonVariantConst v) {
  const char* s = v.as<const char*>();
  return s ? s : "";
}
static bool fill(JsonObjectConst lesson, unsigned segment, Lesson& out, unsigned& segments) {
  auto passages = lesson["passages"].as<JsonArrayConst>();
  segments = passages.size();
  if (segment >= segments) {
    return false;
  }
  auto p = passages[segment];
  auto q = p["question"];
  out = {};
  out.id = value(lesson["id"]) + "/" + value(p["id"]);
  out.title = value(lesson["title"]);
  out.text = value(p["text"]);
  out.prompt = value(q["prompt"]);
  out.correct = value(q["correctChoiceId"]);
  out.evidence = value(q["evidence"]);
  out.explanation = value(q["explanation"]);
  for (auto c : q["choices"].as<JsonArrayConst>()) {
    if (out.choices.size() >= 6) {
      break;
    }
    out.choices.push_back(value(c["text"]));
    out.choiceIds.push_back(value(c["id"]));
  }
  for (auto c : p["chunks"].as<JsonArrayConst>()) {
    Str s = c.is<const char*>() ? value(c) : value(c["text"]);
    if (!s.empty()) {
      out.chunks.push_back(s);
    }
  }
  return !out.title.empty() && !out.text.empty();
}
struct LessonStream {
  FILE* file;
  size_t remaining;
  int read() {
    if (!remaining) {
      return -1;
    }
    --remaining;
    return fgetc(file);
  }
  size_t readBytes(char* out, size_t n) {
    size_t len = fread(out, 1, std::min(n, remaining), file);
    remaining -= len;
    return len;
  }
};
bool fetchLesson(const Str& path, unsigned a, unsigned s, Lesson& out, unsigned& articles, unsigned& segments,
                 bool titleOnly) {
  if (!userPath(path)) {
    return false;
  }
  Str full = root + path;
  JsonDocument doc;
  FILE* f = fopen(full.c_str(), "rb");
  if (!f) {
    return false;
  }
  bool ok = false;
  if (ends(path, ".jdc")) {
    char magic[4];
    uint32_t n = 0, total = 0, r[2];
    if (fread(magic, 1, 4, f) == 4 && !memcmp(magic, "JDC2", 4) && fread(&n, 4, 1, f) == 1 &&
        fread(&total, 4, 1, f) == 1 && n <= 9999 && a < n && total <= 500000) {
      articles = n;
      if (!fseek(f, 12 + 8 * a, SEEK_SET) && fread(r, 4, 2, f) == 2 && r[0] < total && r[1] <= total - r[0] &&
          s < r[1]) {
        segments = r[1];
        unsigned start = r[0] + s;
        if (!fseek(f, 12 + 8 * (n + start), SEEK_SET) && fread(r, 4, 2, f) == 2 && r[1] > 0 &&
            r[1] <= 16384 && uint64_t(r[0]) + r[1] <= length(full) && !fseek(f, r[0], SEEK_SET)) {
          LessonStream input{f, r[1]};
          if (titleOnly) {
            JsonDocument filter;
            filter["title"] = true;
            if (!deserializeJson(doc, input, DeserializationOption::Filter(filter))) {
              out.title = value(doc["title"]);
              ok = !out.title.empty();
            }
          } else {
            unsigned one = 0;
            if (!deserializeJson(doc, input)) {
              ok = fill(doc.as<JsonObjectConst>(), 0, out, one);
            }
          }
        }
      }
    }
  } else {
    const auto bytes = length(full);
    if (bytes <= 24576) {
      LessonStream input{f, bytes};
      JsonDocument filter;
      filter["lessons"][0]["title"] = true;
      auto error = titleOnly ? deserializeJson(doc, input, DeserializationOption::Filter(filter))
                             : deserializeJson(doc, input);
      auto ls = doc["lessons"].as<JsonArrayConst>();
      if (!error && a < ls.size()) {
        articles = ls.size();
        if (titleOnly) {
          out.title = value(ls[a]["title"]);
          segments = 0;
          ok = !out.title.empty();
        } else {
          ok = fill(ls[a], s, out, segments);
        }
      }
    }
  }
  fclose(f);
  return ok;
}
std::vector<Str> dictionaryFiles(unsigned skip, unsigned count) {
  std::vector<Str> res;
  bool more;
  unsigned at = 0, seen = 0;
  do {
    auto dirs = list(root + "/dictionaries", at, 8, more);
    at += dirs.size();
    for (auto& dir : dirs) {
      Str dirPath = "/dictionaries/" + dir.name;
      if (!dir.folder) {
        if (ends(dir.name, ".ifo") && seen++ >= skip) {
          res.push_back(dirPath);
        }
      } else {
        bool next;
        unsigned idx = 0;
        do {
          auto fs = list(root + dirPath, idx, 8, next);
          idx += fs.size();
          for (auto& f : fs) {
            if (!f.folder && ends(f.name, ".ifo") && seen++ >= skip) {
              res.push_back(dirPath + "/" + f.name);
            }
          }
          if (res.size() >= count) {
            break;
          }
        } while (next);
      }
      if (res.size() >= count) {
        res.resize(count);
        return res;
      }
    }
  } while (more);
  return res;
}
static uint32_t be(const unsigned char* b) {
  return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
}
static bool fastWord(const Str& prefix, const Str& wanted, uint32_t& pos, uint32_t& size) {
  FILE* f = fopen((prefix + ".jdx").c_str(), "rb");
  if (!f) {
    return false;
  }
  uint32_t h[4];
  bool found = false;
  if (fread(h, sizeof h, 1, f) == 1 && !memcmp(h, "JDX1", 4) && h[1] <= 1000000 &&
      h[2] == length(prefix + ".idx") && h[3] == length(prefix + ".dict")) {
    uint32_t hash = 2166136261u;
    for (unsigned char c : wanted) {
      hash = (hash ^ c) * 16777619u;
    }
    uint32_t lo = 0, hi = h[1], r[4];
    while (lo < hi) {
      auto mid = (lo + hi) / 2;
      if (fseek(f, 16 + mid * 16, SEEK_SET) || fread(r, 16, 1, f) != 1) {
        break;
      }
      if (r[0] < hash) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    for (auto i = lo; i < h[1]; ++i) {
      if (fseek(f, 16 + i * 16, SEEK_SET) || fread(r, 16, 1, f) != 1 || r[0] != hash) {
        break;
      }
      if (uint64_t(16) + h[1] * 16ull + r[1] >= length(prefix + ".jdx") ||
          fseek(f, 16 + h[1] * 16 + r[1], SEEK_SET)) {
        break;
      }
      Str w;
      for (int j = 0; j < 256; j++) {
        int c = fgetc(f);
        if (c <= 0) {
          break;
        }
        w += char(c);
      }
      if (lower(w) == wanted && uint64_t(r[2]) + r[3] <= h[3]) {
        pos = r[2];
        size = r[3];
        found = true;
        break;
      }
    }
  }
  fclose(f);
  return found;
}
static Str plainDefinition(const Str& raw) {
  Str out;
  bool tag = false;
  for (size_t i = 0; i < raw.size(); i++) {
    auto c = raw[i];
    if (c == '<') {
      tag = true;
      if (raw.compare(i, 3, "<br") == 0 || raw.compare(i, 4, "</li") == 0 ||
          raw.compare(i, 6, "</div>") == 0 || raw.compare(i, 4, "</p>") == 0) {
        out += '\n';
      }
    } else if (c == '>') {
      tag = false;
    } else if (!tag) {
      out += c ? c : ' ';
    }
  }
  return out;
}
Str lookup(const Str& rel, const Str& word) {
  if (!userPath(rel) || !ends(rel, ".ifo") || word.empty()) {
    return "请先添加词典";
  }
  auto prefix = (root + rel).substr(0, root.size() + rel.size() - 4);
  FILE* meta = fopen((prefix + ".ifo").c_str(), "rb");
  if (!meta) {
    return "词典文件缺失";
  }
  char line[256];
  bool unsupported = false;
  while (fgets(line, sizeof line, meta)) {
    if (!strncmp(line, "idxoffsetbits=64", 16)) {
      unsupported = true;
    }
  }
  fclose(meta);
  if (unsupported) {
    return "请转换为 32 位索引";
  }
  FILE* idx = fopen((prefix + ".idx").c_str(), "rb");
  FILE* dict = fopen((prefix + ".dict").c_str(), "rb");
  if (!idx || !dict) {
    if (idx) {
      fclose(idx);
    }
    if (dict) {
      fclose(dict);
    }
    return "请展开 .idx 和 .dict 正文";
  }
  Str result = "未收录";
  const auto wanted = lower(word);
  unsigned char pair[8];
  char term[256];
  uint32_t indexedPos = 0, indexedSize = 0;
  if (fastWord(prefix, wanted, indexedPos, indexedSize) && !fseek(dict, indexedPos, SEEK_SET)) {
    result.resize(std::min<uint32_t>(indexedSize, 1800));
    if (fread(result.data(), 1, result.size(), dict) != result.size()) {
      result = "词典读取失败";
    }
    fclose(dict);
    fclose(idx);
    return plainDefinition(result);
  }
  while (true) {
    size_t n = 0;
    int ch;
    bool overflow = false;
    while ((ch = fgetc(idx)) > 0) {
      if (n + 1 < sizeof term) {
        term[n++] = ch;
      } else {
        overflow = true;
      }
    }
    if (ch < 0 || fread(pair, 1, 8, idx) != 8) {
      break;
    }
    term[n] = 0;
    if (overflow || lower(term) != wanted) {
      continue;
    }
    auto pos = be(pair), size = be(pair + 4);
    if (uint64_t(pos) + size > length(prefix + ".dict") || fseek(dict, pos, SEEK_SET)) {
      break;
    }
    size_t take = std::min<uint32_t>(size, 900);
    result.resize(take);
    if (fread(result.data(), 1, take, dict) != take) {
      result = "词典读取失败";
    } else {
      for (char& c : result) {
        if (c == '\0') {
          c = ' ';
        }
      }
    }
    break;
  }
  fclose(idx);
  fclose(dict);
  return plainDefinition(result);
}
}  // namespace jd
