// src/server.cpp
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <vector>
#include <cstring>

#include "../include/protocol.hpp"
#include "../include/crc32.hpp"
#include "../include/utils.hpp"

#pragma comment(lib, "ws2_32.lib")

static std::mutex cout_mtx;
static std::mutex clients_mtx;
static std::unordered_map<uint32_t, SOCKET> clients;
static std::atomic<uint32_t> next_id{1};

void logw(const std::string &s){
    std::lock_guard<std::mutex> lk(cout_mtx);
    std::cout << s << std::endl;
}

bool recv_all(SOCKET s, void* buf, int len){
    char* p = (char*)buf;
    int rem = len;
    while(rem>0){
        int r = recv(s, p, rem, 0);
        if(r<=0) return false;
        rem -= r; p += r;
    }
    return true;
}
bool send_all(SOCKET s, const void* buf, int len){
    const char* p = (const char*)buf;
    int rem = len;
    while(rem>0){
        int r = send(s, p, rem, 0);
        if(r<=0) return false;
        rem -= r; p += r;
    }
    return true;
}

void send_header_and_payload(SOCKET s, uint8_t type, const std::vector<uint8_t>& payload){
    AppHeader hdr{};
    hdr.magic = PROTO_MAGIC; hdr.version = 1; hdr.msg_type = type;
    hdr.flags = 0; hdr.payload_len = (uint32_t)payload.size(); hdr.crc32 = 0;
    AppHeader tmp = hdr; tmp.crc32 = 0;
    std::vector<uint8_t> buf(sizeof(tmp) + payload.size());
    memcpy(buf.data(), &tmp, sizeof(tmp));
    if(!payload.empty()) memcpy(buf.data()+sizeof(tmp), payload.data(), payload.size());
    hdr.crc32 = crc32_calc(buf.data(), buf.size());
    send_all(s, &hdr, sizeof(hdr));
    if(hdr.payload_len) send_all(s, payload.data(), hdr.payload_len);
}

void handle_client(SOCKET csock){
    uint32_t myid = next_id.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(clients_mtx);
        clients[myid] = csock;
    }
    logw("Client connected id=" + std::to_string(myid));
    // send ack with assigned id
    std::vector<uint8_t> ack(4);
    memcpy(ack.data(), &myid, 4);
    send_header_and_payload(csock, MT_ACK, ack);

    while(true){
        AppHeader hdr;
        if(!recv_all(csock, &hdr, sizeof(hdr))){
            logw("client " + std::to_string(myid) + " disconnected");
            break;
        }
        if(hdr.magic != PROTO_MAGIC){
            logw("bad magic from " + std::to_string(myid)); break;
        }
        std::vector<uint8_t> payload;
        if(hdr.payload_len){
            payload.resize(hdr.payload_len);
            if(!recv_all(csock, payload.data(), hdr.payload_len)){
                logw("recv payload failed"); break;
            }
        }
        // verify crc
        AppHeader tmp = hdr; tmp.crc32 = 0;
        std::vector<uint8_t> checkbuf(sizeof(tmp) + payload.size());
        memcpy(checkbuf.data(), &tmp, sizeof(tmp));
        if(!payload.empty()) memcpy(checkbuf.data()+sizeof(tmp), payload.data(), payload.size());
        uint32_t c = crc32_calc(checkbuf.data(), checkbuf.size());
        if(c != hdr.crc32){
            logw("crc mismatch from " + std::to_string(myid));
            // reply invalid semantic
            send_header_and_payload(csock, MT_INVALID_SEMANTIC, {});
            continue;
        }
        if(payload.size() < 4){
            logw("payload too short from " + std::to_string(myid));
            continue;
        }
        uint32_t target;
        memcpy(&target, payload.data(), 4);
        std::vector<uint8_t> body(payload.begin()+4, payload.end());

        SOCKET dest = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lk(clients_mtx);
            auto it = clients.find(target);
            if(it != clients.end()) dest = it->second;
        }
        if(dest == INVALID_SOCKET){
            logw("target " + std::to_string(target) + " not online (from " + std::to_string(myid) + ")");
            std::string s = "target_not_online";
            std::vector<uint8_t> p(s.begin(), s.end());
            send_header_and_payload(csock, MT_INVALID_SEMANTIC, p);
            continue;
        }
        // forward: prepend sender id
        std::vector<uint8_t> fwd;
        fwd.resize(4 + body.size());
        memcpy(fwd.data(), &myid, 4);
        if(!body.empty()) memcpy(fwd.data()+4, body.data(), body.size());
        send_header_and_payload(dest, hdr.msg_type, fwd);
        logw("forwarded type=" + std::to_string(hdr.msg_type) + " from " + std::to_string(myid) + " -> " + std::to_string(target));
    }

    closesocket(csock);
    {
        std::lock_guard<std::mutex> lk(clients_mtx);
        clients.erase(myid);
    }
    logw("client handler exit " + std::to_string(myid));
}

int main(){
    WSADATA w;
    if(WSAStartup(MAKEWORD(2,2), &w) != 0){ std::cerr<<"WSAStartup fail\n"; return 1; }
    SOCKET l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(l == INVALID_SOCKET){ std::cerr<<"socket fail\n"; return 1; }
    sockaddr_in srv{};
    srv.sin_family = AF_INET; srv.sin_port = htons(8000); srv.sin_addr.s_addr = INADDR_ANY;
    if(bind(l, (sockaddr*)&srv, sizeof(srv)) == SOCKET_ERROR){ std::cerr<<"bind fail\n"; closesocket(l); WSACleanup(); return 1; }
    listen(l, SOMAXCONN);
    std::cout<<"Server listening on 0.0.0.0:8000\n";
    while(true){
        sockaddr_in cli; int len = sizeof(cli);
        SOCKET c = accept(l, (sockaddr*)&cli, &len);
        if(c == INVALID_SOCKET) break;
        std::thread(handle_client, c).detach();
    }
    closesocket(l);
    WSACleanup();
    return 0;
}
