#pragma once
#include "data_types.h"
#include <string>
#include <vector>
#include <mutex>

namespace tcm {

class DingTalkNotifier {
public:
    DingTalkNotifier();
    ~DingTalkNotifier();

    bool initialize(const std::string& webhook_url, const std::string& secret = "");

    bool send_alert(const Alert& alert);
    bool send_text_message(const std::string& content, bool at_all = false);
    bool send_markdown_message(
        const std::string& title,
        const std::string& content,
        const std::vector<std::string>& at_mobiles = {},
        bool at_all = false
    );

    bool send_efficacy_summary(
        const std::string& volunteer_id,
        const std::string& session_id,
        double avg_deqi,
        double avg_pain_relief,
        const std::vector<std::string>& alerts
    );

    void set_enabled(bool enabled);
    bool is_enabled() const;

private:
    std::string generate_sign(uint64_t timestamp) const;
    bool http_post_json(const std::string& url, const std::string& json_payload);
    std::string escape_json(const std::string& str) const;

    std::string webhook_url_;
    std::string secret_;
    bool initialized_;
    bool enabled_;
    mutable std::mutex mutex_;
};

} // namespace tcm
