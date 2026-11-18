#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <cstdint>

// SDL cancellation does not join an in-flight callback. Timer parameters are
// opaque epoch tokens, never pointers to a possibly deleted input handler.
class InputTimerGate {
    struct State {
        std::mutex mutex;
        void* owner;
        bool active = true;
        explicit State(void* value) : owner(value) {}
    };
public:
    explicit InputTimerGate(void* owner = nullptr) : m_Owner(owner) { resume(); }
    ~InputTimerGate() { suspend(); }
    InputTimerGate(const InputTimerGate&) = delete;
    InputTimerGate& operator=(const InputTimerGate&) = delete;
    template<class Action> bool run(Action action) {
        std::lock_guard<std::mutex> lock(m_State->mutex);
        if (!m_State->active) return false;
        action();
        return true;
    }
    void suspend() {
        { std::lock_guard<std::mutex> lock(m_State->mutex); m_State->active = false; }
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_States.erase(m_Id);
    }
    void resume() {
        if (m_State && m_State->active) return;
        m_State = std::make_shared<State>(m_Owner);
        std::lock_guard<std::mutex> lock(s_Mutex);
        m_Id = ++s_NextId;
        s_States[m_Id] = m_State;
    }
    // Four low bits identify one of the legacy handler's 16 controller slots.
    void* context(unsigned slot = 0) const { return reinterpret_cast<void*>((m_Id << 4) | (slot & 15)); }
    template<class Action> static bool dispatch(void* context, Action action) {
        const auto token = reinterpret_cast<uintptr_t>(context);
        std::shared_ptr<State> state;
        {
            std::lock_guard<std::mutex> lock(s_Mutex);
            const auto found = s_States.find(token >> 4);
            if (found == s_States.end()) return false;
            state = found->second.lock();
        }
        if (!state) return false;
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->active) return false;
        action(state->owner, unsigned(token & 15));
        return true;
    }
private:
    void* m_Owner;
    uintptr_t m_Id = 0;
    std::shared_ptr<State> m_State;
    inline static std::mutex s_Mutex;
    inline static uintptr_t s_NextId = 0;
    inline static std::map<uintptr_t, std::weak_ptr<State>> s_States;
};
