#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

#include "pal4inject/types.h"

namespace pal4::inject {

class InputFrameQueue {
public:
    void PushFrame(const InputFrame& frame);
    void PushFrame(InputFrame&& frame);
    void PushCommand(const UiMessageCommand& command);
    bool TryPopFrame(InputFrame* out);
    bool Empty() const;

private:
    mutable std::mutex mutex_;
    std::queue<InputFrame> queue_;
    std::uint32_t next_frame_index_ = 1;
};

class QueuedInputSource final : public IInputSource {
public:
    explicit QueuedInputSource(InputFrameQueue* queue) noexcept;
    InputFrame CaptureFrame() override;

private:
    InputFrameQueue* queue_ = nullptr;
};

struct SynchronousUiMessageTicket {
    UiMessageCommand command{};
    mutable std::mutex mutex;
    std::condition_variable completed_cv;
    bool completed = false;
    bool canceled = false;
    bool delivered = false;
    bool message_handled = false;
    std::string error;
};

class SynchronousUiMessageQueue {
public:
    using Ticket = std::shared_ptr<SynchronousUiMessageTicket>;

    Ticket Push(const UiMessageCommand& command);
    bool TryPop(Ticket* out);
    bool Wait(
        const Ticket& ticket,
        std::uint32_t timeout_ms,
        bool* out_message_handled,
        std::string* error);
    void Complete(
        const Ticket& ticket,
        bool delivered,
        bool message_handled,
        std::string error = {});
    bool IsCanceled(const Ticket& ticket) const;

private:
    mutable std::mutex mutex_;
    std::queue<Ticket> queue_;
};

}  // namespace pal4::inject
