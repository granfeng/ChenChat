# ChenChat项目介绍
## 项目组成
1. include部分
   + protocol.hpp：定义通信协议的结构和消息类型
   + crc32.hpp：声明CRC32校验的函数
   + utils.hpp：声明文件读写的辅助函数
2. src部分
   + server.cpp：服务器端实现
     + 监听客户端连接请求
     + 为每个客户端分配唯一ID
     + 转发消息到目标客户端
     + 维护在线客户端列表
     + 处理协议校验和错误
   + client_console.cpp：控制台客户端
     + 提供命令行界面的聊天客户端
     + 支持文本消息发送/接收
     + 文件传输（分块发送）
   + client_gui.cpp：图形界面客户端
     + 提供Windows GUI界面的聊天客户端，在控制台客户端基础上增加：窗口界面和控件、图片预览功能、文件选择对话框
   + crc32.cpp：CRC32校验实现
     + 验证网络传输数据的完整性
     + 检测数据在传输过程中的错误
3. readme文档

## 编译运行
1. 编译
   + 编译 server：g++ -std=c++17 -Iinclude src/crc32.cpp src/server.cpp -o server.exe -lws2_32
   + 编译 client_console：g++ -std=c++17 -Iinclude src/crc32.cpp src/client_console.cpp -o client_console.exe -lws2_32
   + 编译 client_gui（需链接 GDI+）：g++ -std=c++17 -municode -Iinclude src/crc32.cpp src/client_gui.cpp -o client_gui.exe -lws2_32 -lcomdlg32 -lgdiplus -mwindows 
2. 运行
   1. 本地运行
       + 在对应的终端目录下运行可执行文件
   2. 跨机运行
       + 修改服务器和客户端的服务器地址（作为服务器的主机IP地址） 
       + 同本地运行

## 联系人
@@Guanffeng
2645326736@qq.com
