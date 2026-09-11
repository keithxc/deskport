#pragma once
#include "backend/clipboardchannel.h"
#include <memory>

class ClipboardSync {
public:
    explicit ClipboardSync(std::unique_ptr<ClipboardChannel> channel);
    void tick();
    void clipboardChanged() { m_Changed = true; }
    QString status() const { return m_Status; }
private:
    std::unique_ptr<ClipboardChannel> m_Channel;
    QString m_Observed, m_SentSnapshot, m_Status;
    bool m_Changed = false;
    unsigned m_LocalGeneration = 0, m_SentGeneration = 0;
    bool m_Initialized = false, m_Dirty = false, m_InFlight = false;
    int m_Revision = 0, m_Sequence = 0;
    unsigned m_LastPoll = 0;
};
