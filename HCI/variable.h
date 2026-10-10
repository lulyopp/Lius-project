#ifndef READ_V
#define READ_V
#include <Eigen/Dense>
#include <vector>
#include <string>

#define M_PI 3.14159265358979323846
void printMatrix(const std::vector<std::vector<double>>& matrix);
// 定义一个结构来存储顶点坐标
struct Vertex {
	double x, y, z;
	bool isZero() const {
		return x == 0 && y == 0 && z == 0;
	}
	// 重载比较运算符，用于排序和比较
	bool operator==(const Vertex& other) const {
		return x == other.x && y == other.y && z == other.z;
	}

	bool operator<(const Vertex& other) const {
		// 定义比较顺序，先比较 x，再比较 y，最后比较 z
		if (x != other.x) return x < other.x;
		if (y != other.y) return y < other.y;
		return z < other.z;
	}
};
struct Node {
	std::vector<double> x;
	std::vector<double> y;
	std::vector<double> z;
	std::vector<int> pre;
	std::vector<double> cost;
};

//障碍物——球的信息
struct SphereInfo {
	bool exist;
	double centerX;
	double centerY;
	double centerZ;
	double radius;
};

//RRT*算法函数定义
Eigen::Vector3d rotationMatrixToEulerAnglesBodyXYZ(const Eigen::Matrix3d& R);//声明计算欧拉角的函数
double CalcuDistance(const std::vector<double>& x, const std::vector<double>& y);//计算两点之间的距离
std::vector<std::vector<double>> FindPath(const Node& T, const std::vector<double>& startPoint, const std::vector<double>& goalPoint);//寻找路径
std::vector<double> RandSample(const std::vector<double>& axisStart, const std::vector<double>& axisLWH, const std::vector<double>& goalPoint, double randProbability);//空间中随机采样
std::pair<std::vector<double>, int> FindNearestPoint(const Node& T, const std::vector<double>& randCoor);//寻找树上最近的点
std::vector<double> ExpandPoint(const std::vector<double>& nearestCoor, const std::vector<double>& randCoor, double step);//根据指定步长拓展新的点
int RewriteFunction(Node& T, const std::vector<double>& newCoor, double r, int parentIndex);//重新父节点
bool CollisionDetection(const SphereInfo& sphereInfo, const std::vector<double>& A, const std::vector<double>& B, double step);
Node RandRelink(Node T, const std::vector<double>& newCoor, const SphereInfo& sphereInfo, double r);//重新随机连接
size_t countIntersections(const std::vector<Vertex>& A, const std::vector<Vertex>& B);//检验碰撞
std::vector<std::vector<double>> RRTStar(
	const std::vector<double>& axisStart,
	const std::vector<double>& axisLWH,
	const std::vector<double>& startPoint,
	const std::vector<double>& goalPoint,
	const SphereInfo& sphereInfo,
	const std::vector<Vertex>& huance,
	const std::vector<Vertex>& jiance,
	double x_change, double y_change, double z_change,
	double xStart, double yStart, double zStart
);
std::vector<std::vector<double>> RRTstar_S1(
	const std::vector<Vertex>& jiance,
	const std::vector<Vertex>& huance,
	double x_change,
	double y_change,
	double z_change
);
std::vector<std::vector<double>> RRTstar_S2(
	double xangle, double yangle, double zangle,
	const std::vector<Vertex>& jiance,
	const std::vector<Vertex>& huance,
	double x_change, double y_change, double z_change
);
std::vector<std::vector<double>> RRTstar_S3(
	const std::vector<Vertex>& jiance,
	const std::vector<Vertex>& huance,
	double x_change, double y_change, double z_change,
	double x_displacement, double y_displacement, double z_displacement,
	double x_angle, double y_angle, double z_angle,
	double deltax, double deltay, double deltaz
);

#endif // READ_V
#pragma once

