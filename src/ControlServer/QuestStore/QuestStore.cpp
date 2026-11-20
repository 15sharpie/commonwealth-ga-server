#include "src/ControlServer/QuestStore/QuestStore.hpp"
#include "src/ControlServer/Database/Database.hpp"

#include <chrono>

std::string QuestStore::GetStatus(int64_t character_id, int quest_id) {
    return Database::GetQuestStatus(character_id, quest_id);
}

void QuestStore::Accept(int64_t character_id, int quest_id) {
    Database::AcceptQuest(character_id, quest_id);
}

void QuestStore::Complete(int64_t character_id, int quest_id) {
    Database::CompleteQuest(character_id, quest_id);
}

void QuestStore::Abandon(int64_t character_id, int quest_id) {
    Database::AbandonQuest(character_id, quest_id);
}

void QuestStore::Restart(int64_t character_id, int quest_id) {
    Database::RestartQuest(character_id, quest_id);
}

int64_t QuestStore::Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool QuestStore::IsRepeatReady(int quest_id, int64_t completed_at, int64_t now) {
    const auto rule = Database::GetQuestRepeatRule(quest_id);
    if (!rule.repeatable) return false;
    if (completed_at <= 0) return true;

    const int64_t day_then = completed_at / 86400;
    const int64_t day_now  = now / 86400;
    switch (rule.cooldown_type_value_id) {
        case Database::kQuestCooldownDaily:
            return day_now > day_then;
        case Database::kQuestCooldownWeekly:
            // Day 0 (1970-01-01) was a Thursday; +3 makes weeks start on Monday.
            return (day_now + 3) / 7 > (day_then + 3) / 7;
        default:
            return true;
    }
}
