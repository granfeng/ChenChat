// include/utils.hpp
#pragma once
#include <string>
#include <vector>
#include <fstream>

// file io helpers
inline bool read_file_to_vec(const std::string& path, std::vector<uint8_t>& out){
    std::ifstream ifs(path, std::ios::binary);
    if(!ifs) return false;
    ifs.seekg(0, std::ios::end);
    size_t s = (size_t)ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    out.resize(s);
    ifs.read((char*)out.data(), s);
    return true;
}
inline bool write_file_from_vec(const std::string& path, const std::vector<uint8_t>& data, bool append=false){
    std::ofstream ofs(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if(!ofs) return false;
    ofs.write((const char*)data.data(), data.size());
    return true;
}
