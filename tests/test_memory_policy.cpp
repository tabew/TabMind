#include <cstdio>
#include <string>
#include "ai_client.hpp"

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
    std::printf("\n结果: %s\n", fails == 0 ? "PASS 全部通过" : "FAIL");
    return fails == 0 ? 0 : 1;
}