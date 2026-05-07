#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <queue>
#include <string>
#include <unistd.h>
#include <unordered_map>

extern "C" {
	#include "tg_protocol.h"
}

std::string ctrl_path = "/dev/telegram/ctrl";

struct Chat {
    std::deque<std::string> messages;
    static constexpr size_t kMaxHistory = 10;

    void AddMessage(const std::string &msg) {
        messages.emplace_back("[User] " + msg);

        if (messages.size() > kMaxHistory) {
            messages.pop_front();
        }
    }

    std::string GetHistory() const {
        if (messages.empty()) {
            return "Chat`s empty.\n";
        }

        std::string result;
        for (const auto &msg: messages) {
            result += msg + "\n";
        }

        return result;
    }
};

int main() {
    std::cout << "[Server] Launching..." << std::endl;

    std::unordered_map<int, Chat> chats;
    chats[1] = Chat();
    chats[2] = Chat();
    chats[3] = Chat();


    int ctrl_fd = open(ctrl_path.c_str(), O_RDWR);
    if (ctrl_fd < 0) {
        std::cerr << "[Server] Error with open /dev/telegram/ctrl" << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "[Server] Waiting for messages..." << std::endl;
    telegram_message msg{};

    while (true) {
        if (read(ctrl_fd, &msg, sizeof(msg)) < 0) {
            std::cerr << "[Server] Error with read." << std::endl;
            continue;
        }

        int chat_id = msg.chat_id;

        if (!chats.contains(chat_id)) {
            std::cerr << "[Server] Error with chat id " << chat_id << std::endl;
            msg.length = 0;
            write(ctrl_fd, &msg, sizeof(msg));
            continue;
        }

        if (msg.type == telegram_request_type::Write) {
            std::string new_msg(msg.buffer);
            new_msg.erase(std::ranges::remove(new_msg, '\n').begin(), new_msg.end());

            chats[chat_id].AddMessage(new_msg);
            std::cout << "[Server] Put the message " << new_msg << " into the chat: " << chat_id << std::endl;

            msg.length = 0;
            write(ctrl_fd, &msg, sizeof(msg));

        } else if (msg.type == telegram_request_type::Read) {
            std::cout << "[Server] Chat history was requested for the chat: " << chat_id << std::endl;

            std::string history = chats[chat_id].GetHistory();

            std::strncpy(msg.buffer, history.c_str(), MAX_BUFFER_SIZE - 1);
            msg.buffer[MAX_BUFFER_SIZE - 1] = '\0';
            msg.length = std::min(history.length(), static_cast<size_t>(MAX_BUFFER_SIZE - 1));

            write(ctrl_fd, &msg, sizeof(msg));
        }
    }

    close(ctrl_fd);
    return 0;
}
