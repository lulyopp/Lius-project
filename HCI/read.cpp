#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <algorithm>
#include <iostream>
#include "read.h" 


// 函数定义
//存储.stl文件顶点
std::vector<Vertex> read_model(const std::string& filename) {
	Assimp::Importer importer;

	// 读取STL文件
	const aiScene* scene = importer.ReadFile(filename, aiProcess_Triangulate);
	if (!scene) {
		std::cerr << "Error: " << importer.GetErrorString() << std::endl;
		return {};
	}

	// 存储顶点的向量
	std::vector<Vertex> vertices;

	// 遍历所有的mesh
	for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
		aiMesh* mesh = scene->mMeshes[i];
		// 遍历每个顶点
		for (unsigned int j = 0; j < mesh->mNumVertices; ++j) {
			aiVector3D position = mesh->mVertices[j];
			vertices.push_back({ position.x, position.y, position.z });
		}
	}

	return vertices;
}
//函数 removeZeroRows，删除全为0的行
std::vector<Vertex> removeZeroRows(std::vector<Vertex> matrix) {
	// 使用 std::remove_if 移除全为零的行
	matrix.erase(
		std::remove_if(matrix.begin(), matrix.end(), [](const Vertex& v) {
		return v.isZero(); // 检查是否是全零行
	}),
		matrix.end()
		);

	return matrix;
}

// 函数 removeDuplicateRows，移除重复的行
std::vector<Vertex> removeDuplicateRows(std::vector<Vertex> matrix) {
	// 先排序
	std::sort(matrix.begin(), matrix.end());

	// 使用 std::unique 移除相邻的重复行
	matrix.erase(
		std::unique(matrix.begin(), matrix.end()),
		matrix.end()
	);

	return matrix;
}