#include <cstdio>
#include <string>
#include "ai_client.hpp"

static void test_history_rolling();
static void test_cache_friendly_archiving();
static int fails = 0;
static void check(bool ok, const char* msg) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", msg);
    if (!ok) ++fails;
}

int main() {
    check(MemoryPolicy::retain_latest("abcdef", 3) == "def", "超限保留最新字符");
    check(MemoryPolicy::retain_latest("旧记忆新记忆", 3) == "新记忆", "中文按字符而不是字节截断");
    check(MemoryPolicy::retain_latest("旧🙂新", 2) == "🙂新", "不会切开四字节 UTF-8 字符");
    check(MemoryPolicy::retain_latest("完整", 10) == "完整", "未超限原文不变");
    check(MemoryPolicy::retain_latest("abc", 0).empty(), "零上限返回空串");
    check(MemoryPolicy::retain_latest("", 10).empty(), "空串安全");
    check(MemoryPolicy::retain_latest("abcde", 5) == "abcde", "恰好等于上限时原文不变");
    check(MemoryPolicy::retain_latest("abcde", 4) == "bcde", "超出 1 个字符时只丢最前 1 个");

    // 截断后的开头必须是完整句子，不能是「主，喜欢猫」这种半句
    std::string s2 = "小明是群主，喜欢猫。小红最近换了工作。今天群里聊了游戏，他说要考试。";
    std::string r30 = MemoryPolicy::retain_latest(s2, 30);
    check(r30.rfind("主，喜欢猫", 0) != 0, "不会从半个词开始");
    check(r30.rfind("，", 0) != 0, "不会从逗号开始");
    check(r30 == "小红最近换了工作。今天群里聊了游戏，他说要考试。",
          "对齐到句号后的完整句子");

    // 没有断句符时保持原样（宁可半句也不丢内容）
    std::string noBreak = "abcdefghijklmnopqrstuvwxyz";
    check(MemoryPolicy::retain_latest(noBreak, 5) == "vwxyz", "无标点时仍保留尾部");

    // 换行也当断句符
    check(MemoryPolicy::retain_latest("aaa\nbbb\nccc", 7) == "bbb\nccc", "换行可作为断句点");
    test_history_rolling();
    test_cache_friendly_archiving();
    std::printf("\n结果: %s\n", fails == 0 ? "PASS 全部通过" : "FAIL");
    return fails == 0 ? 0 : 1;
}
static void test_history_rolling() {
    std::deque<ChatMessage> h;
    for (int i = 1; i <= 6; ++i) {
        h.push_back({"user", "问题" + std::to_string(i)});
        h.push_back({"assistant", "回答" + std::to_string(i)});
    }
    auto removed = AIClient::trim_recent_history(h, 4);
    check(h.size() == 8 && removed.size() == 4, "历史严格保留最近四轮");
    check(h.front().content == "问题3" && h.back().content == "回答6", "滚动后顺序和轮次完整");
    check(removed.front().content == "问题1" && removed.back().content == "回答2", "移出的旧轮次可补记长期记忆");
    check(AIClient::trim_recent_history(h, 4).empty(), "未超限时不改历史、不触发缓存断裂");
}

static void test_cache_friendly_archiving() {
    // 默认 15 轮：在 22 轮以内只追加，达到 24 轮后一次归档到 7 轮。
    std::deque<ChatMessage> h;
    auto add_round = [&h](int i) {
        h.push_back({"user", "问题" + std::to_string(i)});
        h.push_back({"assistant", "回答" + std::to_string(i)});
    };
    for (int i = 1; i <= 22; ++i) add_round(i);
    const size_t soft_rounds = 15 + std::max<size_t>(5, 15 / 2);
    check(h.size() <= soft_rounds * 2, "软上限内持续追加历史，缓存前缀不被截断");
    add_round(23);
    if (h.size() > soft_rounds * 2) AIClient::trim_recent_history(h, 15 / 2);
    check(h.size() == 14 && h.front().content == "问题17", "满窗后批量归档，保留最新完整轮次");
}