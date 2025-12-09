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

// 全局变量定义
static std::mutex cout_mtx;
static std::mutex clients_mtx;
static std::unordered_map<uint32_t, SOCKET> clients;
static std::atomic<uint32_t> next_id{1};

// 日志函数
void logw(const std::string &s) {
    std::lock_guard<std::mutex> lk(cout_mtx);
    std::cout << s << std::endl;
}

// 接收/发送全部数据
bool recv_all(SOCKET s, void* buf, int len) {
    char* p = (char*)buf;
    int rem = len;
    while(rem>0){
        int r = recv(s, p, rem, 0);
        if(r<=0) return false;
        rem -= r; p += r;
    }
    return true;
}
bool send_all(SOCKET s, const void* buf, int len) {
    const char* p = (const char*)buf;
    int rem = len;
    while(rem>0){
        int r = send(s, p, rem, 0);
        if(r<=0) return false;
        rem -= r; p += r;
    }
    return true;
}

// 发送报文函数
void send_header_and_payload(SOCKET s, uint8_t type, const std::vector<uint8_t>& payload) {
    // 1. 初始化报文头
    AppHeader hdr{};
    hdr.magic = PROTO_MAGIC; 
    hdr.version = 1; 
    hdr.msg_type = type;
    hdr.flags = 0; 
    hdr.payload_len = (uint32_t)payload.size(); 
    hdr.crc32 = 0;

    // 2. 创建临时头部用于CRC计算
    AppHeader tmp = hdr; 
    tmp.crc32 = 0;

    // 3. 构建完整数据包（临时头部 + 载荷）
    std::vector<uint8_t> buf(sizeof(tmp) + payload.size());
    memcpy(buf.data(), &tmp, sizeof(tmp));
    if(!payload.empty()) memcpy(buf.data()+sizeof(tmp), payload.data(), payload.size());
    
    // 4. 计算CRC32校验值
    hdr.crc32 = crc32_calc(buf.data(), buf.size());
    
    // 5. 发送带正确CRC的头部
    send_all(s, &hdr, sizeof(hdr));
    
    // 6. 发送载荷
    if(hdr.payload_len) send_all(s, payload.data(), hdr.payload_len);
}

// 客户端处理线程函数
void handle_client(SOCKET csock) {
    // 分配唯一ID
    uint32_t myid = next_id.fetch_add(1);
    
    // 注册客户端到全局列表
    {
        std::lock_guard<std::mutex> lk(clients_mtx);
        clients[myid] = csock;
    }

    // 日志记录
    logw("Client connected id=" + std::to_string(myid));
    // 发送ACK消息告知客户端其ID
    std::vector<uint8_t> ack(4);
    memcpy(ack.data(), &myid, 4);
    send_header_and_payload(csock, MT_ACK, ack);

    // 主接收循环
    while(true){
        // 1. 接收报文头
        AppHeader hdr;
        if(!recv_all(csock, &hdr, sizeof(hdr))){
            logw("client " + std::to_string(myid) + " disconnected");
            break;
        }
        // 2. 验证魔数
        if(hdr.magic != PROTO_MAGIC){
            logw("bad magic from " + std::to_string(myid)); break;
        }
        // 3. 接收载荷数据
        std::vector<uint8_t> payload;
        if(hdr.payload_len){
            payload.resize(hdr.payload_len);
            if(!recv_all(csock, payload.data(), hdr.payload_len)){
                logw("recv payload failed"); break;
            }
        }
        // 4. 验证CRC32
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
        // 5. 提取目标客户端ID
        if(payload.size() < 4){
            logw("payload too short from " + std::to_string(myid));
            continue;
        }
        uint32_t target;
        memcpy(&target, payload.data(), 4);
        std::vector<uint8_t> body(payload.begin()+4, payload.end());

        // 6. 查找目标客户端是否在线
        SOCKET dest = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lk(clients_mtx);
            auto it = clients.find(target);
            if(it != clients.end()) dest = it->second;
        }
        // 7. 目标不在线的处理
        if(dest == INVALID_SOCKET){
            logw("target " + std::to_string(target) + " not online (from " + std::to_string(myid) + ")");
            std::string s = "target_not_online";
            std::vector<uint8_t> p(s.begin(), s.end());
            send_header_and_payload(csock, MT_INVALID_SEMANTIC, p);
            continue;
        }
        // 8. 转发消息（添加发送者ID前缀）
        std::vector<uint8_t> fwd;
        fwd.resize(4 + body.size());
        memcpy(fwd.data(), &myid, 4);
        if(!body.empty()) memcpy(fwd.data()+4, body.data(), body.size());
         // 9. 发送给目标客户端
        send_header_and_payload(dest, hdr.msg_type, fwd);
         // 10. 日志记录
        logw("forwarded type=" + std::to_string(hdr.msg_type) + " from " + std::to_string(myid) + " -> " + std::to_string(target));
    }

     // 11. 关闭连接
    closesocket(csock);
    // 12. 从客户端列表中移除
    {
        std::lock_guard<std::mutex> lk(clients_mtx);
        clients.erase(myid);
    }
    // 13. 日志记录
    logw("client handler exit " + std::to_string(myid));
}

int main() {
    // 1. 初始化Winsock
    WSADATA w;
    if(WSAStartup(MAKEWORD(2,2), &w) != 0){ std::cerr<<"WSAStartup fail\n"; return 1; }

    // 2. 创建监听套接字
    SOCKET l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(l == INVALID_SOCKET){ std::cerr<<"socket fail\n"; return 1; }
    
    // 3. 设置服务器地址
    sockaddr_in srv{};
    srv.sin_family = AF_INET; 
    srv.sin_port = htons(8000); 
    srv.sin_addr.s_addr = INADDR_ANY;
    
    // 4. 绑定套接字
    if(bind(l, (sockaddr*)&srv, sizeof(srv)) == SOCKET_ERROR){ std::cerr<<"bind fail\n"; closesocket(l); WSACleanup(); return 1; }
    
    // 5. 开始监听
    listen(l, SOMAXCONN);
    std::cout<<"Server listening on 0.0.0.0:8000\n";
    // 6. 主循环：接受客户端连接
    while(true){
        sockaddr_in cli; 
        int len = sizeof(cli);

        // 7. 接受新连接
        SOCKET c = accept(l, (sockaddr*)&cli, &len);
        if(c == INVALID_SOCKET) break;
        // 8. 为新客户端创建处理线程
        std::thread(handle_client, c).detach();
    }
     // 9. 关闭监听套接字，清理Winsock
    closesocket(l);
    WSACleanup();
    return 0;
}
