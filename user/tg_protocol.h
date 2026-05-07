#ifndef TELEGRAM_PROTOCOL_H
#define TELEGRAM_PROTOCOL_H

#define MAX_MSG_LEN 256
#define MAX_BUFFER_SIZE 4096
#define NUM_CHATS 3

enum telegram_request_type { Read, Write };

struct telegram_message {
    enum telegram_request_type type;
    int chat_id;
    char buffer[MAX_BUFFER_SIZE];
    int length;
};


#endif // TELEGRAM_PROTOCOL_H
