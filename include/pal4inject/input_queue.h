#pragma once

#include <mutex>
#include <queue>

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

}  // namespace pal4::inject
