#include <winsock2.h>
#include <iostream>
#include <vector>
#include <thread>
#include <filesystem>
#include "include/Storage.hpp"
#include "include/Parser.hpp"

#pragma comment(lib, "ws2_32.lib")
namespace fs = std::filesystem;

// Функция для обработки одного клиента
void handle_client(SOCKET client, Storage& store) {
    char buffer[4096];
    int bytes_received = recv(client, buffer, sizeof(buffer) - 1, 0);

    if (bytes_received > 0) {
        buffer[bytes_received] = '\0'; // Гарантируем конец строки
        std::string raw_command(buffer);

        // Твой текущий парсер
        std::string result = CommandParser::execute(store, raw_command);

        // Отправка ответа
        send(client, result.c_str(), (int)result.length(), 0);
    }
    closesocket(client);
}

int main() {
    // 1. Создаем папку для данных
    if (!fs::exists("data")) fs::create_directory("data");

    Storage store;

    // 2. Твой поток очистки TTL (оставляем как был)
    std::thread([&store]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            store.cleanup();
        }
    }).detach();

    // 3. Инициализация Windows Sockets
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed!" << std::endl;
        return 1;
    }

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address = { AF_INET, htons(9000), INADDR_ANY };

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, SOMAXCONN);

    std::cout << "========================================" << std::endl;
    std::cout << "   PicoDB FAST BINARY Engine (TCP)      " << std::endl;
    std::cout << "   Port: 9000 | Mode: RAW TCP           " << std::endl;
    std::cout << "========================================" << std::endl;

    // 4. Основной цикл приема соединений
    while (true) {
        SOCKET client = accept(server_fd, NULL, NULL);
        if (client != INVALID_SOCKET) {
            // Создаем поток на каждый запрос (для скорости)
            std::thread(handle_client, client, std::ref(store)).detach();
        }
    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}