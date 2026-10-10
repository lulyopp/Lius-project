#include <vector>
#include <iostream>
#include "read.h" 
#include "variable.h" 

SphereInfo CreateSphereObject(SphereInfo sphereInfo) {
	sphereInfo.centerX = -3.0;
	sphereInfo.centerY = -18.0;
	sphereInfo.centerZ = 42.0;
	sphereInfo.radius = 53.0;
	sphereInfo.exist = true;
	return sphereInfo;
}
void keyboard(unsigned char key, int x, int y) {
	if (key == 13) { // 按下 ESC 键退出
		exit(0); // 结束程序
	}
}
void printMatrix(const std::vector<std::vector<double>>& matrix) {
	for (const auto& row : matrix) {
		for (double value : row) {
			std::cout << value << " ";
		}
		std::cout << std::endl;
	}
}