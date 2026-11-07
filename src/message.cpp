#include "message.hpp"

#include "util.hpp"

namespace satou {
namespace {

std::string unescape_cq(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] == '&') {
      const auto rest = text.substr(i);
      if (rest.starts_with("&amp;")) {
        out.push_back('&');
        i += 5;
        continue;
      }
      if (rest.starts_with("&#91;")) {
        out.push_back('[');
        i += 5;
        continue;
      }
      if (rest.starts_with("&#93;")) {
        out.push_back(']');
        i += 5;
        continue;
      }
      if (rest.starts_with("&lt;")) {
        out.push_back('<');
        i += 4;
        continue;
      }
      if (rest.starts_with("&gt;")) {
        out.push_back('>');
        i += 4;
        continue;
      }
    }
    out.push_back(text[i]);
    ++i;
  }
  return out;
}

void read_cq_params(std::string_view type, std::string_view params, ParsedMessage& parsed) {
  while (!params.empty()) {
    const auto comma = params.find(',');
    const auto part = params.substr(0, comma);
    const auto eq = part.find('=');
    if (eq != std::string_view::npos) {
      const auto key = part.substr(0, eq);
      const auto value = unescape_cq(part.substr(eq + 1));
      if (type == "at" && key == "qq") parsed.at_qq.push_back(value);
      if (type == "reply" && (key == "id" || key == "message_id") && !parsed.reply_id) parsed.reply_id = value;
    }
    if (comma == std::string_view::npos) break;
    params.remove_prefix(comma + 1);
  }
}

void parse_cq_string(const std::string& raw, ParsedMessage& parsed) {
  for (std::size_t i = 0; i < raw.size();) {
    if (raw.compare(i, 4, "[CQ:") == 0) {
      const auto end = raw.find(']', i + 4);
      if (end == std::string::npos) {
        parsed.text.push_back(raw[i]);
        ++i;
        continue;
      }
      const std::string_view body(raw.data() + i + 4, end - (i + 4));
      const auto comma = body.find(',');
      const auto type = body.substr(0, comma);
      if (comma != std::string_view::npos) read_cq_params(type, body.substr(comma + 1), parsed);
      i = end + 1;
      continue;
    }
    const auto next = raw.find("[CQ:", i);
    const auto count = next == std::string::npos ? std::string::npos : next - i;
    parsed.text += unescape_cq(std::string_view(raw).substr(i, count));
    if (next == std::string::npos) break;
    i = next;
  }
}

}  // namespace

ParsedMessage parse_message(const nlohmann::json& message) {
  ParsedMessage parsed;
  if (message.is_string()) {
    parse_cq_string(message.get<std::string>(), parsed);
    return parsed;
  }
  if (!message.is_array()) return parsed;
  for (const auto& segment : message) {
    if (!segment.is_object()) continue;
    const auto type = segment.value("type", "");
    const auto data = segment.contains("data") && segment.at("data").is_object() ? segment.at("data") : nlohmann::json::object();
    if (type == "text" && data.contains("text") && data.at("text").is_string()) {
      parsed.text += data.at("text").get<std::string>();
    } else if (type == "at" && data.contains("qq")) {
      parsed.at_qq.push_back(json_id(data.at("qq")));
    } else if (type == "reply") {
      if (data.contains("id")) parsed.reply_id = json_id(data.at("id"));
      else if (data.contains("message_id")) parsed.reply_id = json_id(data.at("message_id"));
    }
  }
  return parsed;
}

}  // namespace satou
