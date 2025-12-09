// src/client_gui.cpp
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <thread>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <map>

#include "../include/protocol.hpp"
#include "../include/crc32.hpp"
#include "../include/utils.hpp"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

HWND hMain, hLog, hInput, hIP, hTarget;
SOCKET g_sock = INVALID_SOCKET;
std::atomic<bool> g_run{false};
uint32_t myid = 0;
std::mutex incoming_mtx;
struct Incoming { uint64_t expected=0, received=0; std::string out; };
std::map<std::string, Incoming> incoming;

std::string w2u(const std::wstring &ws){
    if(ws.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n,0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring u2w(const std::string &s){
    if(s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n,0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

void append_log(const std::wstring& t){
    int len = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, len, len);
    std::wstring s = t + L"\r\n";
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)s.c_str());
}

bool send_all(SOCKET s, const void* buf, int len){
    const char* p = (const char*)buf; int rem = len;
    while(rem>0){
        int r = send(s, p, rem, 0);
        if(r<=0) return false;
        rem -= r; p += r;
    }
    return true;
}
bool recv_all(SOCKET s, void* buf, int len){
    char* p = (char*)buf; int rem = len;
    while(rem>0){
        int r = recv(s, p, rem, 0);
        if(r<=0) return false;
        rem -= r; p += r;
    }
    return true;
}

void show_image_preview(const std::string &path){
    // spawn thread to show a simple window and draw image using GDI+
    std::thread([path](){
        std::wstring wpath = u2w(path);
        // create a window class
        HINSTANCE hi = GetModuleHandle(NULL);
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"ImgPreviewClass";
        RegisterClassW(&wc);
        Bitmap bmp(wpath.c_str());
        UINT bw = bmp.GetWidth(), bh = bmp.GetHeight();
        int winw = (int)std::min<UINT>(bw, 800);
        int winh = (int)std::min<UINT>(bh, 600);
        HWND wh = CreateWindowW(L"ImgPreviewClass", L"图片预览", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, winw+16, winh+39, NULL, NULL, hi, NULL);
        ShowWindow(wh, SW_SHOW);
        // paint once
        HDC hdc = GetDC(wh);
        Graphics g(hdc);
        g.DrawImage(&bmp, Rect(0,0,winw,winh));
        ReleaseDC(wh, hdc);
        MSG msg;
        while(GetMessageW(&msg, NULL, 0, 0)){
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
    }).detach();
}

void handle_text_forward(uint32_t sender, const std::vector<uint8_t>& body){
    std::string s((char*)body.data(), body.size());
    std::wstring ws = u2w(s);
    append_log(L"[" + std::to_wstring(sender) + L"] " + ws);
}
void handle_file_meta(uint32_t sender, const std::vector<uint8_t>& body){
    if(body.size() < 2+8) { append_log(L"[file meta malformed]"); return; }
    uint16_t name_len = *(uint16_t*)(body.data());
    if(body.size() < 2 + name_len + 8) { append_log(L"[file meta short]"); return; }
    std::string fname((char*)body.data()+2, name_len);
    uint64_t fsize = *(uint64_t*)(body.data()+2+name_len);
    std::string out = "recv_from_" + std::to_string(sender) + "_" + fname;
    {
        std::lock_guard<std::mutex> lk(incoming_mtx);
        Incoming info; info.expected = fsize; info.received = 0; info.out = out;
        incoming[std::to_string(sender)+"_"+fname] = info;
    }
    append_log(u2w("[") + std::to_wstring(sender) + u2w("] incoming file: ") + u2w(fname));
}
void handle_file_chunk(uint32_t sender, const std::vector<uint8_t>& body){
    if(body.size() < 8) return;
    uint32_t seq = *(uint32_t*)(body.data());
    uint32_t chlen = *(uint32_t*)(body.data()+4);
    if(body.size() < 8 + chlen) return;
    std::vector<uint8_t> data(body.begin()+8, body.begin()+8+chlen);
    bool saved=false;
    {
        std::lock_guard<std::mutex> lk(incoming_mtx);
        for(auto it = incoming.begin(); it!=incoming.end(); ++it){
            if(it->first.rfind(std::to_string(sender)+"_",0)==0){
                write_file_from_vec(it->second.out, data, true);
                it->second.received += chlen;
                append_log(u2w("[") + std::to_wstring(sender) + u2w("] chunk ") + std::to_wstring(seq));
                if(it->second.received >= it->second.expected){
                    append_log(u2w("文件接收完成: ") + u2w(it->second.out));
                    std::string low = it->second.out;
                    for(auto &c: low) c = tolower(c);
                    if(low.find(".jpg")!=std::string::npos || low.find(".png")!=std::string::npos || low.find(".bmp")!=std::string::npos || low.find(".jpeg")!=std::string::npos){
                        show_image_preview(it->second.out);
                    }
                    incoming.erase(it);
                }
                saved=true;
                break;
            }
        }
    }
    if(!saved){
        std::string out = "recv_from_" + std::to_string(sender) + ".bin";
        write_file_from_vec(out, data, true);
        append_log(u2w("[") + std::to_wstring(sender) + u2w("] chunk appended to ") + u2w(out));
    }
}

void recv_thread(){
    while(g_run){
        AppHeader hdr;
        if(!recv_all(g_sock, &hdr, sizeof(hdr))) break;
        if(hdr.magic != PROTO_MAGIC) break;
        std::vector<uint8_t> payload;
        if(hdr.payload_len){
            payload.resize(hdr.payload_len);
            if(!recv_all(g_sock, payload.data(), hdr.payload_len)) break;
        }
        if(hdr.msg_type == MT_ACK){
            if(payload.size()==4){ memcpy(&myid, payload.data(), 4); append_log(u2w("assigned id=") + std::to_wstring(myid)); }
            else { std::string s((char*)payload.data(), payload.size()); append_log(u2w("[ACK] ") + u2w(s)); }
        } else if(hdr.msg_type == MT_TEXT){
            if(payload.size()<4) continue;
            uint32_t sender; memcpy(&sender, payload.data(), 4);
            std::vector<uint8_t> body(payload.begin()+4, payload.end());
            handle_text_forward(sender, body);
        } else if(hdr.msg_type == MT_FILE_META){
            if(payload.size()<4) continue;
            uint32_t sender; memcpy(&sender, payload.data(), 4);
            std::vector<uint8_t> body(payload.begin()+4, payload.end());
            handle_file_meta(sender, body);
        } else if(hdr.msg_type == MT_FILE_CHUNK){
            if(payload.size()<4) continue;
            uint32_t sender; memcpy(&sender, payload.data(), 4);
            std::vector<uint8_t> body(payload.begin()+4, payload.end());
            handle_file_chunk(sender, body);
        } else if(hdr.msg_type == MT_INVALID_SEMANTIC){
            append_log(L"[服务器] invalid semantic / target offline");
        }
    }
    append_log(L"recv end");
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp){
    switch(msg){
    case WM_CREATE:{
        CreateWindowW(L"STATIC", L"聊天记录：", WS_CHILD|WS_VISIBLE, 10,10,80,20, hWnd, NULL, NULL, NULL);
        hLog = CreateWindowW(L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY, 10,35,600,320, hWnd, NULL, NULL, NULL);
        CreateWindowW(L"STATIC", L"输入：", WS_CHILD|WS_VISIBLE, 10,365,40,20, hWnd, NULL, NULL, NULL);
        hInput = CreateWindowW(L"EDIT", L"", WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 60,365,380,24, hWnd, NULL, NULL, NULL);
        CreateWindowW(L"BUTTON", L"发送", WS_CHILD|WS_VISIBLE, 450,365,60,24, hWnd, (HMENU)101, NULL, NULL);
        CreateWindowW(L"BUTTON", L"选择文件", WS_CHILD|WS_VISIBLE, 520,365,90,24, hWnd, (HMENU)102, NULL, NULL);
        CreateWindowW(L"STATIC", L"服务器IP：", WS_CHILD|WS_VISIBLE, 10,395,80,20, hWnd, NULL, NULL, NULL);
        hIP = CreateWindowW(L"EDIT", L"127.0.0.1", WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 100,395,150,24, hWnd, NULL, NULL, NULL);
        CreateWindowW(L"STATIC", L"目标ID：", WS_CHILD|WS_VISIBLE, 260,395,60,20, hWnd, NULL, NULL, NULL);
        hTarget = CreateWindowW(L"EDIT", L"0", WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 320,395,80,24, hWnd, NULL, NULL, NULL);
        CreateWindowW(L"BUTTON", L"连接", WS_CHILD|WS_VISIBLE, 420,395,80,24, hWnd, (HMENU)103, NULL, NULL);
        break;
    }
    case WM_COMMAND:{
        int id = LOWORD(wp);
        if(id==103){
            wchar_t ipbuf[128]; GetWindowTextW(hIP, ipbuf, 128);
            std::wstring ip(ipbuf);
            std::thread([ip](){
                WSADATA w; WSAStartup(MAKEWORD(2,2), &w);
                g_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                sockaddr_in srv{}; srv.sin_family = AF_INET; srv.sin_port = htons(8000);
                std::string ip8 = w2u(ip);
                inet_pton(AF_INET, ip8.c_str(), &srv.sin_addr);
                if(connect(g_sock, (sockaddr*)&srv, sizeof(srv))==SOCKET_ERROR){
                    append_log(L"连接失败");
                    closesocket(g_sock); g_sock=INVALID_SOCKET; return;
                }
                g_run = true;
                append_log(L"connected to " + ip);
                std::thread(recv_thread).detach();
            }).detach();
            return 0;
        } else if(id==101){
            wchar_t buf[1024]; GetWindowTextW(hInput, buf, 1024);
            std::wstring ws(buf);
            if(ws.empty()) return 0;
            wchar_t tbuf[64]; GetWindowTextW(hTarget, tbuf, 64);
            uint32_t target = (uint32_t)_wtoi(tbuf);
            std::string utf8 = w2u(ws);
            std::vector<uint8_t> payload; payload.resize(4 + utf8.size());
            memcpy(payload.data(), &target, 4);
            if(!utf8.empty()) memcpy(payload.data()+4, utf8.data(), utf8.size());
            AppHeader h{}; h.magic=PROTO_MAGIC; h.version=1; h.msg_type=MT_TEXT; h.payload_len=(uint32_t)payload.size(); h.crc32=0;
            AppHeader tmp = h; tmp.crc32=0;
            std::vector<uint8_t> pack(sizeof(tmp)+payload.size());
            memcpy(pack.data(), &tmp, sizeof(tmp)); memcpy(pack.data()+sizeof(tmp), payload.data(), payload.size());
            h.crc32 = crc32_calc(pack.data(), pack.size());
            send_all(g_sock, &h, sizeof(h)); if(h.payload_len) send_all(g_sock, payload.data(), (int)payload.size());
            append_log(L"[我->" + std::to_wstring(target) + L"] " + ws);
            SetWindowTextW(hInput, L"");
            return 0;
        } else if(id==102){
            OPENFILENAMEW ofn{}; wchar_t fname[MAX_PATH]={0};
            ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hWnd; ofn.lpstrFile = fname; ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if(GetOpenFileNameW(&ofn)){
                std::wstring pathw(fname);
                append_log(L"选择文件：" + pathw);
                std::string path8 = w2u(pathw);
                std::vector<uint8_t> buf;
                if(!read_file_to_vec(path8, buf)){ append_log(L"文件读失败"); return 0; }
                wchar_t tbuf[64]; GetWindowTextW(hTarget, tbuf, 64); uint32_t target = (uint32_t)_wtoi(tbuf);
                std::string fname8 = path8;
                uint16_t name_len = (uint16_t)fname8.size();
                uint64_t fsize = buf.size();
                std::vector<uint8_t> meta; meta.resize(4 + 2 + name_len + 8);
                memcpy(meta.data(), &target, 4);
                memcpy(meta.data()+4, &name_len, 2);
                memcpy(meta.data()+6, fname8.data(), name_len);
                memcpy(meta.data()+6+name_len, &fsize, 8);
                AppHeader h{}; h.magic=PROTO_MAGIC; h.version=1; h.msg_type=MT_FILE_META; h.payload_len=(uint32_t)meta.size(); h.crc32=0;
                AppHeader tmp=h; tmp.crc32=0;
                std::vector<uint8_t> pack(sizeof(tmp)+meta.size());
                memcpy(pack.data(), &tmp, sizeof(tmp)); memcpy(pack.data()+sizeof(tmp), meta.data(), meta.size());
                h.crc32 = crc32_calc(pack.data(), pack.size());
                send_all(g_sock, &h, sizeof(h)); send_all(g_sock, meta.data(), (int)meta.size());
                const size_t CHUNK = 4096; uint32_t seq=0;
                for(size_t pos=0; pos<buf.size(); pos+=CHUNK){
                    size_t take = std::min(CHUNK, buf.size()-pos);
                    std::vector<uint8_t> chunk; chunk.resize(4+4+4+take);
                    memcpy(chunk.data(), &target, 4);
                    memcpy(chunk.data()+4, &seq, 4);
                    uint32_t chlen = (uint32_t)take;
                    memcpy(chunk.data()+8, &chlen, 4);
                    memcpy(chunk.data()+12, buf.data()+pos, take);
                    AppHeader ch{}; ch.magic=PROTO_MAGIC; ch.version=1; ch.msg_type=MT_FILE_CHUNK; ch.payload_len=(uint32_t)chunk.size(); ch.crc32=0;
                    AppHeader tmp2=ch; tmp2.crc32=0;
                    std::vector<uint8_t> pack2(sizeof(tmp2)+chunk.size());
                    memcpy(pack2.data(), &tmp2, sizeof(tmp2)); memcpy(pack2.data()+sizeof(tmp2), chunk.data(), chunk.size());
                    ch.crc32 = crc32_calc(pack2.data(), pack2.size());
                    send_all(g_sock, &ch, sizeof(ch)); send_all(g_sock, chunk.data(), (int)chunk.size());
                    seq++;
                }
                append_log(L"文件发送完成");
            }
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        g_run=false;
        if(g_sock!=INVALID_SOCKET) closesocket(g_sock);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmd){
    GdiplusStartupInput gdipIn; ULONG_PTR token; GdiplusStartup(&token, &gdipIn, NULL);
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = L"LanChatSimple";
    RegisterClassW(&wc);
    HWND w = CreateWindowW(L"LanChatSimple", L"LAN Chat - 简洁白色版", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME,
                           CW_USEDEFAULT, CW_USEDEFAULT, 640,480, NULL, NULL, hInst, NULL);
    ShowWindow(w, nCmd);
    MSG msg;
    while(GetMessageW(&msg, NULL, 0,0)){
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    GdiplusShutdown(token);
    return 0;
}
