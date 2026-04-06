#include "pal4inject/input_queue.h"

namespace pal4::inject {

void InputFrameQueue::PushFrame(const InputFrame& frame) {
    std::scoped_lock lock(mutex_);
    queue_.push(frame);
}

void InputFrameQueue::PushFrame(InputFrame&& frame) {
    std::scoped_lock lock(mutex_);
    queue_.push(std::move(frame));
}

void InputFrameQueue::PushCommand(const UiMessageCommand& command) {
    InputFrame frame{};
    frame.frame_index = next_frame_index_++;
    frame.commands.push_back(command);
    std::scoped_lock lock(mutex_);
    queue_.push(std::move(frame));
}

bool InputFrameQueue::TryPopFrame(InputFrame* out) {
    if (!out) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    if (queue_.empty()) {
        return false;
    }
    *out = std::move(queue_.front());
    queue_.pop();
    return true;
}

bool InputFrameQueue::Empty() const {
    std::scoped_lock lock(mutex_);
    return queue_.empty();
}

QueuedInputSource::QueuedInputSource(InputFrameQueue* queue) noexcept : queue_(queue) {}

InputFrame QueuedInputSource::CaptureFrame() {
    InputFrame frame{};
    if (queue_) {
        queue_->TryPopFrame(&frame);
    }
    return frame;
}

}  // namespace pal4::inject
