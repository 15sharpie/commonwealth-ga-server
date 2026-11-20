#pragma once

#include <string>
#include <cstdint>

class QuestStore {
public:
    std::string GetStatus(int64_t character_id, int quest_id);
    void Accept(int64_t character_id, int quest_id);
    void Complete(int64_t character_id, int quest_id);
    void Abandon(int64_t character_id, int quest_id);
    void Restart(int64_t character_id, int quest_id);

    // A quest completed at `completed_at` (unix seconds) can be taken again at
    // `now`: repeatable, and its cooldown period has rolled over. Daily periods
    // start at 00:00 UTC, weekly ones on Monday 00:00 UTC.
    static bool IsRepeatReady(int quest_id, int64_t completed_at, int64_t now);
    static int64_t Now();
};
