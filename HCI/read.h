#ifndef READ_H
#define READ_H

#include <vector>
#include <string>
#include "variable.h" 


// 函数声明
std::vector<Vertex> read_model(const std::string& filename);//读取.stl文件
std::vector<Vertex> removeZeroRows(std::vector<Vertex> matrix);//删除全为0的行
std::vector<Vertex> removeDuplicateRows(std::vector<Vertex> matrix);//删除重复的行

#endif // READ_H
#pragma once
