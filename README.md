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
   1. 
   2. 

## 联系人


第一步：找到“服务器”笔记本的局域网 IP  ipconfig
192.168.1.198
192.168.1.198

第二步：修改 client.cpp 中的 server_ip

第三步：确保端口一致
server.cpp：
int port = 9001;
client.cpp：
int port = 9001;
保持一致即可。

第四步：关闭防火墙或允许程序通过
否则 Windows 会拦截 socket 连接。





当时，你给我分了代码实现的7个阶段：
阶段 1：最小可运行版 — 文本传输（单客户端 -> 服务器 -> 回显）
阶段 2：引入应用层协议（头部 + 类型 + 长度）
阶段 3：支持并发（服务器同时服务多客户端）
阶段 4：发送图片与文件（大 payload）
阶段 5：无效语义测试与错误处理
阶段 6：UI（对话框）
阶段 7：安全性/完整性检查
阶段 8：撰写实践报告（协议详细设计）
报告要点（建议分节）：
需求概述
系统架构图（server/client/component）
应用协议详细说明
报文结构（用表格列出每个字段、长度、含义、取值范围）
各语义 type 的语义（type=1 文本，如何编码，例子）
错误处理与无效语义举例
校验算法与容错说明
实现细节（关键函数与伪码）
测试步骤与结果（ping 测试、文件完整性测试）
遇到的问题与解决办法