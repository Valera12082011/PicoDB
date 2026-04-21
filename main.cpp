#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <vector>
#include <thread>
#include <string>
#include <filesystem>
#include "include/Storage.hpp"
#include "include/Parser.hpp"

#pragma comment(lib, "ws2_32.lib")
namespace fs = std::filesystem;

void handle_client(SOCKET client, Storage& store) {
    int flag = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));

    // Увеличиваем буфер до 128КБ, чтобы пачки влезали целиком
    std::vector<char> buffer(128 * 1024);

    while (true) {
        int bytes = recv(client, buffer.data(), (int)buffer.size() - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';

        char* ptr = buffer.data();
        char* end = buffer.data() + bytes;

        while (ptr < end) {
            char* next_line = strchr(ptr, '\n');
            if (!next_line) break;
            *next_line = '\0';

            // --- FAST PATH ДЛЯ SET ---
            // Если строка начинается с "SET ", мы обрабатываем её без токенизатора
            if (strncmp(ptr, "SET ", 4) == 0) {
                char* key_start = ptr + 4;
                char* key_end = strchr(key_start, ' ');

                if (key_end) {
                    *key_end = '\0';
                    char* val_start = key_end + 1;

                    // Сохраняем как сырую строку, не вызывая parseValue
                    // Это убирает тысячи аллокаций памяти
                    store.set(std::string(key_start), Object(std::string(val_start)));

                    send(client, "OK\n", 3, 0);
                }
            }
            // --- ОБЫЧНЫЙ ПУТЬ ДЛЯ ОСТАЛЬНОГО ---
            else {
                std::string res = CommandParser::execute(store, std::string(ptr));
                res += "\n";
                send(client, res.c_str(), (int)res.length(), 0);
            }

            ptr = next_line + 1;
        }
    }
    closesocket(client);
}
int main() {
    if (!fs::exists("data")) fs::create_directory("data");

    Storage store;

    std::thread([&store]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            store.cleanup();
        }
    }).detach();

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in address = { AF_INET, htons(9000), INADDR_ANY };
    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, SOMAXCONN);

    std::cout << "PicoDB High-Performance Engine Started on Port 9000" << std::endl;

    while (true) {
        SOCKET client = accept(server_fd, NULL, NULL);
        if (client != INVALID_SOCKET) {
            std::thread(handle_client, client, std::ref(store)).detach();
        }
    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}