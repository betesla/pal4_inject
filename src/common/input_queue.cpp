#include "pal4inject/input_queue.h"

#include <chrono>

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

SynchronousUiMessageQueue::Ticket SynchronousUiMessageQueue::Push(
    const UiMessageCommand& command) {
    auto ticket = std::make_shared<SynchronousUiMessageTicket>();
    ticket->command = command;
    std::scoped_lock lock(mutex_);
    queue_.push(ticket);
    return ticket;
}

bool SynchronousUiMessageQueue::TryPop(Ticket* out) {
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

bool SynchronousUiMessageQueue::Wait(
    const Ticket& ticket,
    const std::uint32_t timeout_ms,
    bool* out_message_handled,
    std::string* error) {
    if (!ticket) {
        if (error) {
            *error = "UI message ticket is null";
        }
        return false;
    }
    std::unique_lock lock(ticket->mutex);
    if (!ticket->completed_cv.wait_for(
            lock,
            std::chrono::milliseconds(timeout_ms),
            [&ticket] { return ticket->completed; })) {
        ticket->canceled = true;
        if (error) {
            *error = "timed out waiting for PAL4 main-thread UI dispatch";
        }
        return false;
    }
    if (out_message_handled) {
        *out_message_handled = ticket->message_handled;
    }
    if (error) {
        *error = ticket->error;
    }
    return ticket->delivered;
}

void SynchronousUiMessageQueue::Complete(
    const Ticket& ticket,
    const bool delivered,
    const bool message_handled,
    std::string error) {
    if (!ticket) {
        return;
    }
    {
        std::scoped_lock lock(ticket->mutex);
        if (ticket->canceled) {
            return;
        }
        ticket->delivered = delivered;
        ticket->message_handled = message_handled;
        ticket->error = std::move(error);
        ticket->completed = true;
    }
    ticket->completed_cv.notify_all();
}

bool SynchronousUiMessageQueue::IsCanceled(const Ticket& ticket) const {
    if (!ticket) {
        return true;
    }
    std::scoped_lock lock(ticket->mutex);
    return ticket->canceled;
}

}  // namespace pal4::inject
