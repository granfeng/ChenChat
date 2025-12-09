#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <cstring>

#include "../include/protocol.hpp"
#include "../include/crc32.hpp"
#include "../include/utils.hpp"

#pragma comment(lib, "ws2_32.lib")

// 发送/接收全部数据
bool send_all(SOCKET s, const void* buf, int len) {
    const char* p = (const char*)buf; int rem=len;
    while(rem>0){
        int r = send(s, p, rem, 0);
        if(r<=0) return false;
        rem -= r; p += r;
    }
    return true;
}
bool recv_all(SOCKET s, void* buf, int len) {
    char* p = (char*)buf; int rem = len;
    while(rem>0){
        int r = recv(s, p, rem, 0);
        if(r<=0) return false;
        rem -= r; p += r;
    }
    return true;
}

// 接收循环函数
void recv_loop(SOCKET sock) {
    while(true) {
        AppHeader hdr;
        if(!recv_all(sock, &hdr, sizeof(hdr))) break;
        if(hdr.magic != PROTO_MAGIC){ std::cout<<"bad magic\n"; break; }

        // 处理不同消息类型
        std::vector<uint8_t> payload;
        if(hdr.payload_len) {
            payload.resize(hdr.payload_len);
            if(!recv_all(sock, payload.data(), hdr.payload_len)) break;
        }
        if(hdr.msg_type == MT_TEXT) {
            if(payload.size() < 4){ std::cout<<"[TEXT] malformed\n"; continue; }
            uint32_t sender; memcpy(&sender, payload.data(), 4);
            std::string s((char*)payload.data()+4, payload.size()-4);
            std::cout << "[" << sender << "] " << s << std::endl;
        } else if(hdr.msg_type == MT_ACK) {
            if(payload.size()==4) {
                uint32_t id; memcpy(&id, payload.data(), 4);
                std::cout << "[ACK] assigned id=" << id << std::endl;
            } else {
                std::string s((char*)payload.data(), payload.size());
                std::cout << "[ACK] " << s << std::endl;
            }
        } else if(hdr.msg_type == MT_FILE_META) {
            if(payload.size() < 4){ std::cout<<"bad file_meta\n"; continue; }
            uint32_t sender; memcpy(&sender, payload.data(), 4);
            size_t off = 4;
            if(payload.size() < off+2+8){ std::cout<<"file meta too short\n"; continue; }
            uint16_t name_len = *(uint16_t*)(payload.data()+off); off += 2;
            std::string fname((char*)payload.data()+off, name_len); off += name_len;
            uint64_t fsize = *(uint64_t*)(payload.data()+off);
            std::cout << "[FILE_META] from " << sender << " name=" << fname << " size=" << fsize << std::endl;
        } else if(hdr.msg_type == MT_FILE_CHUNK) {
            if(payload.size() < 4){ std::cout<<"bad chunk\n"; continue; }
            uint32_t sender; memcpy(&sender, payload.data(), 4);
            if(payload.size() < 12){ std::cout<<"chunk too short\n"; continue; }
            uint32_t seq = *(uint32_t*)(payload.data()+4);
            uint32_t chlen = *(uint32_t*)(payload.data()+8);
            std::vector<uint8_t> d(payload.begin()+12, payload.begin()+12+chlen);
            std::string out = "recv_from_" + std::to_string(sender) + ".bin";
            write_file_from_vec(out, d, true);
            std::cout << "[FILE_CHUNK] from " << sender << " seq=" << seq << " len=" << chlen << " -> " << out << std::endl;
        } else if(hdr.msg_type == MT_INVALID_SEMANTIC) {
            std::cout << "[INVALID_SEMANTIC]\n";
        } else {
            std::cout << "[MSG] type=" << (int)hdr.msg_type << " len=" << hdr.payload_len << std::endl;
        }
    }
    std::cout << "recv loop ended\n";
}

// 主函数
int main(){
    // 1. 初始化Winsock
    WSADATA w; 
    WSAStartup(MAKEWORD(2,2), &w);

    // 2. 创建套接字
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    // 3. 设置服务器地址
    sockaddr_in srv{}; 
    srv.sin_family = AF_INET; 
    srv.sin_port = htons(8000);
    inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);   // 修改为服务器IP如需跨机
    // 4. 连接服务器
    if(connect(sock, (sockaddr*)&srv, sizeof(srv))==SOCKET_ERROR) {
        std::cerr<<"connect failed\n"; closesocket(sock); WSACleanup(); return 1;
    }

    // 5. 启动接收线程（独立处理服务器消息）
    std::thread r(recv_loop, sock);
    // 6. 获取目标客户端ID
    uint32_t target;
    std::cout << "target's client_id:";
    std::cin >> target;
    std::cin.ignore();

    // 7. 主输入循环
    std::string line;
    while(true){
        std::getline(std::cin, line);
        if(line == "/quit") break;
        if(line.rfind("/sendfile ",0)==0){
            std::string path = line.substr(10);
            std::vector<uint8_t> buf;
            if(!read_file_to_vec(path, buf)){ std::cout<<"read file failed\n"; continue; }
            std::string fname = path;
            uint16_t name_len = (uint16_t)fname.size();
            uint64_t fsize = buf.size();
            std::vector<uint8_t> meta;
            meta.resize(4 + 2 + name_len + 8);
            memcpy(meta.data(), &target, 4);
            memcpy(meta.data()+4, &name_len, 2);
            memcpy(meta.data()+6, fname.data(), name_len);
            memcpy(meta.data()+6+name_len, &fsize, 8);
            
            AppHeader h{}; 
            h.magic=PROTO_MAGIC; 
            h.version=1; 
            h.msg_type=MT_FILE_META; 
            h.payload_len=(uint32_t)meta.size(); 
            h.crc32=0;

            AppHeader tmp = h; 
            tmp.crc32=0;
            std::vector<uint8_t> pack(sizeof(tmp)+meta.size());
            memcpy(pack.data(), &tmp, sizeof(tmp)); 
            memcpy(pack.data()+sizeof(tmp), meta.data(), meta.size());
            h.crc32 = crc32_calc(pack.data(), pack.size());
            send_all(sock, &h, sizeof(h)); 
            send_all(sock, meta.data(), (int)meta.size());
            const size_t CHUNK = 4096; uint32_t seq=0;
            for(size_t pos=0; pos<buf.size(); pos+=CHUNK){
                size_t take = std::min(CHUNK, buf.size()-pos);
                std::vector<uint8_t> chunk; chunk.resize(4+4+4+take);
                memcpy(chunk.data(), &target, 4);
                memcpy(chunk.data()+4, &seq, 4);
                uint32_t chlen = (uint32_t)take;
                memcpy(chunk.data()+8, &chlen, 4);
                memcpy(chunk.data()+12, buf.data()+pos, take);
                
                AppHeader ch{}; 
                ch.magic=PROTO_MAGIC; 
                ch.version=1; 
                ch.msg_type=MT_FILE_CHUNK; 
                ch.payload_len=(uint32_t)chunk.size();
                ch.crc32=0;

                AppHeader tmp2=ch; 
                tmp2.crc32=0;

                std::vector<uint8_t> pack2(sizeof(tmp2)+chunk.size());
                memcpy(pack2.data(), &tmp2, sizeof(tmp2)); 
                memcpy(pack2.data()+sizeof(tmp2), chunk.data(), chunk.size());
                ch.crc32 = crc32_calc(pack2.data(), pack2.size());
                send_all(sock, &ch, sizeof(ch)); send_all(sock, chunk.data(), (int)chunk.size());
                seq++;
            }
            std::cout<<"file send done\n";
            continue;
        }
        std::string utf8 = line;
        std::vector<uint8_t> payload; payload.resize(4 + utf8.size());
        memcpy(payload.data(), &target, 4);
        if(!utf8.empty()) memcpy(payload.data()+4, utf8.data(), utf8.size());
        
        AppHeader h{}; 
        h.magic=PROTO_MAGIC; 
        h.version=1; 
        h.msg_type=MT_TEXT; 
        h.payload_len=(uint32_t)payload.size(); 
        h.crc32=0;

        AppHeader tmp = h; 
        tmp.crc32=0;

        std::vector<uint8_t> pack(sizeof(tmp)+payload.size());
        memcpy(pack.data(), &tmp, sizeof(tmp)); 
        memcpy(pack.data()+sizeof(tmp), payload.data(), payload.size());
        h.crc32 = crc32_calc(pack.data(), pack.size());
        send_all(sock, &h, sizeof(h));
        if(h.payload_len) send_all(sock, payload.data(), (int)payload.size());
    }

    closesocket(sock); 
    WSACleanup();
    return 0;
}
