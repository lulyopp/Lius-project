#include <iostream>
#include <cmath>
#include <vector>
#include "variable.h"
#include "draw.h"
#include <Eigen/Dense>
#include <limits> // For std::numeric_limits
#include <algorithm>
#include <random>
#include <set>
using namespace Eigen; // 为了简化代码，可以直接使用Eigen命名空间

//计算欧拉角，按相对坐标系的旋转
Eigen::Vector3d rotationMatrixToEulerAnglesBodyXYZ(const Eigen::Matrix3d& R) {
	Eigen::Vector3d eulerAngles;

	// 计算 Pitch (Y rotation)
	eulerAngles[1] = std::asin(R(0, 2)); // Note: the formula is different from fixed XYZ

	// 检查 Pitch 角是否接近 ±90度，避免奇异性
	if (std::abs(R(0, 2)) < 0.999999) {
		// 计算 Roll (X rotation)
		eulerAngles[0] = std::atan2(-R(1, 2), R(2, 2));
		// 计算 Yaw (Z rotation)
		eulerAngles[2] = std::atan2(-R(0, 1), R(0, 0));
	}
	else {
		// Pitch 角接近 ±90度时，处理奇异性
		eulerAngles[0] = 0.0;  // 设置 Roll 角为 0
		eulerAngles[2] = std::atan2(R(1, 0), R(1, 1)); // 计算 Yaw 角
	}

	return eulerAngles;
}

//计算点之间的距离
double CalcuDistance(const std::vector<double>& x, const std::vector<double>& y) {
	return std::sqrt(std::pow(x[0] - y[0], 2) + std::pow(x[1] - y[1], 2) + std::pow(x[2] - y[2], 2));
}

//寻找路径:1.从终点开始，沿着父节点逆向存储路径点 2.将起点添加到路径中 3.使用 std::reverse 函数翻转路径以从起点到终点的顺序
std::vector<std::vector<double>> FindPath(const Node& T, const std::vector<double>& startPoint, const std::vector<double>& goalPoint) {
	std::vector<std::vector<double>> path;
	int index = T.pre.back(); // 获取终点的父节点索引
	int count = 0;

	// 从终点到起点的路径上所有节点存储
	while (T.pre[index] != 0) {
		path.push_back({ T.x[index], T.y[index], T.z[index] });
		index = T.pre[index];
		count++;
	}

	// 添加起点到路径
	path.push_back({ startPoint[0], startPoint[1], startPoint[2] });
	count++;

	// 翻转路径
	std::reverse(path.begin(), path.end());

	// 添加终点到路径
	path.push_back({ goalPoint[0], goalPoint[1], goalPoint[2] });
	return path;
}

//空间中随机采样
std::vector<double> RandSample(const std::vector<double>& axisStart, const std::vector<double>& axisLWH,
	const std::vector<double>& goalPoint, double randProbability) {
	//double randValue = static_cast<double>(rand()) / RAND_MAX; // 生成0到1之间的随机数
	//std::vector<double> randcoor(3); // 用于存储生成的坐标

	//if (randValue < randProbability) {
	//    // 随机生成采样点
	//    double randX = static_cast<double>(rand()) / RAND_MAX * axisLWH[0] + axisStart[0];
	//    double randY = static_cast<double>(rand()) / RAND_MAX * axisLWH[1] + axisStart[1];
	//    double randZ = static_cast<double>(rand()) / RAND_MAX * axisLWH[2] + axisStart[2];
	//    randcoor = { randX, randY, randZ };
	//}
	//else {
	//    // 将目标点作为采样点
	//    randcoor = goalPoint;
	//}

	//return randcoor;
	// 使用随机数生成器
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<> dis(0.0, 1.0); // 生成0到1之间的随机数

	double randValue = dis(gen);  // 获取0到1之间的随机数
	std::vector<double> randcoor(3); // 用于存储生成的坐标

	if (randValue < randProbability) {
		// 使用相同的生成器生成随机坐标
		std::uniform_real_distribution<> disX(axisStart[0], axisStart[0] + axisLWH[0]);
		std::uniform_real_distribution<> disY(axisStart[1], axisStart[1] + axisLWH[1]);
		std::uniform_real_distribution<> disZ(axisStart[2], axisStart[2] + axisLWH[2]);

		double randX = disX(gen);  // 随机生成X坐标
		double randY = disY(gen);  // 随机生成Y坐标
		double randZ = disZ(gen);  // 随机生成Z坐标

		randcoor = { randX, randY, randZ };
	}
	else {
		// 将目标点作为采样点
		randcoor = goalPoint;
	}

	return randcoor;
}

// 寻找树上最近的点
std::pair<std::vector<double>, int> FindNearestPoint(const Node& T, const std::vector<double>& randCoor) {
	double tempDis = std::numeric_limits<double>::infinity();
	int parentIndex = -1;

	for (size_t k1 = 0; k1 < T.x.size(); ++k1) {
		std::vector<double> currentPoint = { T.x[k1], T.y[k1], T.z[k1] };
		double dis = CalcuDistance(randCoor, currentPoint);

		if (dis < tempDis) {
			tempDis = dis;
			parentIndex = static_cast<int>(k1);
		}
	}

	std::vector<double> nearestCoor = { T.x[parentIndex], T.y[parentIndex], T.z[parentIndex] };
	return { nearestCoor, parentIndex };
}

//根据指定步长扩展新的点
std::vector<double> ExpandPoint(const std::vector<double>& nearestCoor, const std::vector<double>& randCoor, double step) {
	// 计算随机点与最近点的差值
	double deltaX = randCoor[0] - nearestCoor[0];
	double deltaY = randCoor[1] - nearestCoor[1];
	double deltaZ = randCoor[2] - nearestCoor[2];

	// 计算半径 r
	double r = sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);

	// 计算球坐标
	double fai = atan2(deltaY, deltaX); // azimuthal angle
	double theta = acos(deltaZ / r);    // polar angle

	// 生成新点的直角坐标系
	double x = step * sin(theta) * cos(fai);
	double y = step * sin(theta) * sin(fai);
	double z = step * cos(theta);

	// 返回新坐标
	return { x + nearestCoor[0], y + nearestCoor[1], z + nearestCoor[2] };
}

//重写父节点
int RewriteFunction(Node& T, const std::vector<double>& newCoor, double r, int parentIndex) {
	// 初始化潜在父节点
	std::vector<int> potentialParent;

	// 遍历树T上所有节点，寻找r范围内的所有潜在父节点
	for (size_t k1 = 0; k1 < T.x.size(); ++k1) {
		if (CalcuDistance(newCoor, { T.x[k1], T.y[k1], T.z[k1] }) < r) {
			potentialParent.push_back(k1); // 如果小于r,当前节点位置放在potentialParent中
		}
	}

	// 迭代寻找最小代价，并重新选择新节点newCoor的父节点
	for (size_t k2 = 0; k2 < potentialParent.size(); ++k2) {
		int ppIndex = potentialParent[k2]; // 当前潜在父节点的序号
		std::vector<double> pp = { T.x[ppIndex], T.y[ppIndex], T.z[ppIndex] }; // 当前潜在父节点
		std::vector<double> p = { T.x[parentIndex], T.y[parentIndex], T.z[parentIndex] }; // 当前父节点

		// 判断代价
		if ((CalcuDistance(pp, newCoor) + T.cost[ppIndex]) < (CalcuDistance(p, newCoor) + T.cost[parentIndex])) {
			parentIndex = ppIndex; // 潜在父节点变为新的父节点
		}
	}

	return parentIndex; // 返回新的父节点索引
}

bool CollisionDetection(const SphereInfo& sphereInfo, const std::vector<double>& A, const std::vector<double>& B, double step) {
	if (A.size() < 3 || B.size() < 3) {
		throw std::invalid_argument("Vectors A and B must have at least 3 elements.");
	}

	bool sphereFlag = false; // 初始化碰撞标志为false
	if (sphereInfo.exist) {
		double deltaX = B[0] - A[0];
		double deltaY = B[1] - A[1];
		double deltaZ = B[2] - A[2];
		double r = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
		double fai = std::atan2(deltaY, deltaX);
		double theta = std::acos(deltaZ / r);

		for (double k2 = 0; k2 <= step; k2 += step / 100) {
			double x = k2 * std::sin(theta) * std::cos(fai);
			double y = k2 * std::sin(theta) * std::sin(fai);
			double z = k2 * std::cos(theta);
			std::vector<double> checkPoint = { x + A[0], y + A[1], z + A[2] };

			double distance = CalcuDistance(checkPoint, { sphereInfo.centerX, sphereInfo.centerY, sphereInfo.centerZ });
			if (distance < sphereInfo.radius) {
				sphereFlag = true; // 发生碰撞
				break; // 退出循环
			}
		}
	}
	return sphereFlag;
}

//随机重连函数（含有碰撞检测）
Node RandRelink(Node T, const std::vector<double>& newCoor, const SphereInfo& sphereInfo, double r) {
	// 寻找需要修改父节点的节点放入potentialParent里面。
	std::vector<size_t> potentialParent;

	for (size_t k1 = 0; k1 < T.x.size(); ++k1) {
		if (CalcuDistance(newCoor, { T.x[k1], T.y[k1], T.z[k1] }) < r) {
			potentialParent.push_back(k1);
		}
	}

	// 找到新节点的父节点集合，存在变量Index里
	std::vector<size_t> Index;
	size_t index = T.pre.back(); // Assuming T.pre is not empty
	while (index != 0) {
		Index.push_back(index);
		index = T.pre[index];
	}

	// 这行的意思是把需要修改父节点的节点集合，剔除新节点的父节点
	potentialParent.erase(std::remove_if(potentialParent.begin(), potentialParent.end(),
		[&](size_t k) { return std::find(Index.begin(), Index.end(), k) != Index.end(); }), potentialParent.end());

	// 根据代价决定是否修改父节点
	for (size_t k2 = 0; k2 < potentialParent.size(); ++k2) {
		size_t ppIndex = potentialParent[k2];
		std::vector<double> pp = { T.x[ppIndex], T.y[ppIndex], T.z[ppIndex] };

		// 如果原始路径的代价 > 将新节点作为当前节点的父节点之后的代价
		if (T.cost[ppIndex] > (T.cost.back() + CalcuDistance(pp, newCoor))) {
			// 检测碰撞
			if (!CollisionDetection(sphereInfo, pp, newCoor, CalcuDistance(pp, newCoor))) {
				// 更新整个树T的父节点
				T.pre[ppIndex] = T.x.size(); // Assuming new node is at the end
				// 更新路径代价
				T.cost[ppIndex] = CalcuDistance(newCoor, pp) + T.cost.back();
			}
		}
	}

	return T;
}

//碰撞检验
//size_t countIntersections(const std::vector<Vertex>& A, const std::vector<Vertex>& B) {
//    //std::vector<Vertex> intersection;
//
//    //for (const auto& vertex : A) {
//    //    // 检查当前 vertex 是否在 huancenew 中
//    //    if (std::find(B.begin(), B.end(), vertex) != B.end()) {
//    //        intersection.push_back(vertex);
//    //    }
//    //}
//
//    //// 返回交集的数量
//    //return intersection.size();
//}
size_t countIntersections(const std::vector<Vertex>& A, const std::vector<Vertex>& B) {
	// 将 B 转换为一个 set 来加速查找
	std::set<Vertex> setB(B.begin(), B.end());

	size_t intersectionCount = 0;
	for (const auto& vertex : A) {
		// 检查当前 vertex 是否在 setB 中
		if (setB.find(vertex) != setB.end()) {
			intersectionCount++;
		}
	}

	// 返回交集的数量
	return intersectionCount;
}


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
) {
	const int iterMax = 5000;
	int iter = 0;
	const double step = 0.2;
	int count = 1;
	const double Thr = 0.2;
	const double randProbability = 0.2;
	double r = 5 * step;
	int flag = 0;

	Node T;
	T.x.push_back(startPoint[0]);
	T.y.push_back(startPoint[1]);
	T.z.push_back(startPoint[2]);
	T.pre.push_back(0);
	T.cost.push_back(0);

	std::vector<Vertex> path;

	while (iter <= iterMax) {
		iter++;
		std::vector<double> randCoor = RandSample(axisStart, axisLWH, goalPoint, randProbability);
		auto result = FindNearestPoint(T, randCoor);
		std::vector<double> nearestCoor = result.first;
		int parentIndex = result.second;
		std::vector<double> newCoor = ExpandPoint(nearestCoor, randCoor, step);
		parentIndex = RewriteFunction(T, newCoor, r, parentIndex);

		std::vector<double> A = { T.x[parentIndex], T.y[parentIndex], T.z[parentIndex] };
		std::vector<double> B = newCoor;

		count++;
		T.x.push_back(newCoor[0]);
		T.y.push_back(newCoor[1]);
		T.z.push_back(newCoor[2]);
		T.pre.push_back(parentIndex);
		T.cost.push_back(CalcuDistance(A, B) + T.cost[parentIndex]);

		T = RandRelink(T, newCoor, sphereInfo, r);

		if (CalcuDistance(newCoor, goalPoint) < Thr) {
			flag = 1;
			break;
		}
		else if (!(newCoor[0] == goalPoint[0] && newCoor[1] == goalPoint[1] && newCoor[2] == goalPoint[2])) {
			double xx = newCoor[0] - startPoint[0];
			double yy = newCoor[1] - startPoint[1];
			double zz = newCoor[2] - startPoint[2];

			std::vector<Vertex> huancenew(huance.size());
			for (size_t k = 0; k < huance.size(); k++) {
				huancenew[k] = { huance[k].x + xx, huance[k].y + yy, huance[k].z + zz };
			}

			size_t bizhangcount = countIntersections(jiance, huancenew);
			if (bizhangcount != 0) {
				std::cout << "骨头发生碰撞" << std::endl;
				return {};
			}

			for (size_t k = 0; k < huancenew.size(); k++) {
				std::vector<double> D = { huancenew[k].x, huancenew[k].y, huancenew[k].z };
				std::vector<double> C = { sphereInfo.centerX, sphereInfo.centerY, sphereInfo.centerZ };
				if (CalcuDistance(C, D) < sphereInfo.radius) {
					continue;
				}
			}
		}
	}

	std::vector<Vertex> huancenew(huance.size());
	for (size_t k = 0; k < huance.size(); k++) {
		huancenew[k] = { huance[k].x + x_change, huance[k].y + y_change, huance[k].z + z_change };
	}
	size_t bizhangcount = countIntersections(jiance, huancenew);
	if (bizhangcount != 0) {
		std::cout << "骨头发生碰撞" << std::endl;
		return {};
	}
	else {
		std::cout << "骨头未发生碰撞" << std::endl;
	}

	if (!flag) {
		std::cout << "S1路径规划失败" << std::endl;
		return {};
	}
	else {
		//std::cout << "S1路径规划成功" << std::endl;
	}

	std::vector<std::vector<double>> p = FindPath(T, startPoint, goalPoint);
	return p;
}

// RRTstar_S1 函数实现
std::vector<std::vector<double>> RRTstar_S1(
	const std::vector<Vertex>& jiance,
	const std::vector<Vertex>& huance,
	double x_change,
	double y_change,
	double z_change
) {
	// 变量定义
	std::vector<double> axisStart = { 0, -100, 80 };  // 空间起始坐标
	std::vector<double> axisLWH = { 50, 50, 50 };     // 空间长宽高
	SphereInfo qiu;
	qiu.exist = 0;
	qiu = CreateSphereObject(qiu);      // 创建球障碍物信息

	// 找到患侧 Z 值最大的点作为路径规划的起点
	size_t n = std::distance(huance.begin(),
		std::max_element(huance.begin(), huance.end(),
			[](const Vertex& a, const Vertex& b) {
		return a.z < b.z;
	}));

	Vertex huancedian = huance[n]; // 找到第 n 行的点的坐标
	double xStart = huancedian.x;
	double yStart = huancedian.y;
	double zStart = huancedian.z;
	double xTarget = xStart + x_change;
	double yTarget = yStart + y_change;
	double zTarget = zStart + z_change;

	// 路径点的起止点
	std::vector<std::vector<double>> pathPoint = {
		{xStart, yStart, zStart},
		{xTarget, yTarget, zTarget}
	};

	// 寻找路径
	std::vector<std::vector<double>> totalPath;

	// 循环遍历路径点
	std::vector<double> startPoint = pathPoint[0]; // k1 行为起点
	std::vector<double> goalPoint = pathPoint[1]; // k1 + 1 行为目标点

	// RRT* 查找路径
	std::vector<std::vector<double>> path = RRTStar(axisStart, axisLWH, startPoint, goalPoint, qiu, huance, jiance, x_change, y_change, z_change, xStart, yStart, zStart);

	// 减去起始坐标
	for (size_t i = 0; i < path.size(); ++i) {
		path[i][0] -= xStart;
		path[i][1] -= yStart;
		path[i][2] -= zStart;
	}

	if (!path.empty()) { // 判断路径是否非空
		totalPath.insert(totalPath.end(), path.begin(), path.end());
		std::cout << "S1路径规划成功" << std::endl;
	}

	return totalPath;
}



// 
std::vector<std::vector<double>> RRTstar_S2(
	double xangle, double yangle, double zangle,
	const std::vector<Vertex>& jiance,
	const std::vector<Vertex>& huance,
	double x_change, double y_change, double z_change
) {
	std::vector<std::vector<double>> Optimal_path_S2(100, std::vector<double>(6, 0));

	double x_interval = xangle / 100;
	double y_interval = yangle / 100;
	double z_interval = zangle / 100;

	for (int i = 1; i < 101; ++i) {
		std::vector<Vertex> huancenew(huance.size());

		// 生成旋转矩阵
		Matrix3d R_x = AngleAxisd(i * x_interval * M_PI / 180.0, Vector3d::UnitX()).matrix();
		Matrix3d R_y = AngleAxisd(i * y_interval * M_PI / 180.0, Vector3d::UnitY()).matrix();
		Matrix3d R_z = AngleAxisd(i * z_interval * M_PI / 180.0, Vector3d::UnitZ()).matrix();

		// 合并旋转矩阵
		Matrix3d R = R_z * R_y * R_x;
		Matrix4d T = Eigen::Matrix4d::Identity();  // 创立转换矩阵并初始化为单位矩阵
		// 将 R 矩阵赋值给 T 的前 3x3 部分
		T.block<3, 3>(0, 0) = R;
		// 添加平移，设置 T 矩阵的第 4 列的值,第4行自动为 (0, 0, 0, 1) 因为使用了 Identity 初始化
		T(0, 3) = x_change;
		T(1, 3) = y_change;
		T(2, 3) = z_change;


		// 坐标转换
		for (size_t j = 0; j < huance.size(); ++j) {
			Vector4d point(huance[j].x, huance[j].y, huance[j].z, 1);
			Vector4d transformedPoint = T * point;

			huancenew[j].x = transformedPoint(0);
			huancenew[j].y = transformedPoint(1);
			huancenew[j].z = transformedPoint(2);
		}

		// 碰撞检测
		size_t bizhangcount = countIntersections(huancenew, jiance);
		if (bizhangcount != 0) {
			std::cout << "error" << std::endl;
			return {};
		}

		// 记录路径
		Optimal_path_S2[i - 1] = { T(0, 3), T(1, 3), T(2, 3), i * x_interval, i * y_interval, i * z_interval };
	}

	//成功判断
	if (Optimal_path_S2.back()[3] == xangle && Optimal_path_S2.back()[4] == yangle && Optimal_path_S2.back()[5] == zangle) {
		std::cout << "S2路径规划成功" << std::endl;
	}

	return Optimal_path_S2;
}

std::vector<std::vector<double>> RRTstar_S3(
	const std::vector<Vertex>& jiance,
	const std::vector<Vertex>& huance,
	double x_change, double y_change, double z_change,
	double x_displacement, double y_displacement, double z_displacement,
	double x_angle, double y_angle, double z_angle,
	double deltax, double deltay, double deltaz
) {
	// 变量定义
	std::vector<double> axisStart = { 0, -100, 80 }; // 空间起始坐标
	std::vector<double> axisLWH = { 50, 50, 50 };    // 空间长宽高
	SphereInfo qiu1;
	qiu1.exist = false; // 球是否存在
	qiu1 = CreateSphereObject(qiu1);   // 创建球障碍物信息

	// 计算从拉伸位置到目标位置的移动距离
	double nx = x_displacement - x_change;
	double ny = y_displacement - y_change;
	double nz = z_displacement - z_change;

	// 找到患侧 Z 值最大的点作为路径规划的起点
	auto maxZIt = std::max_element(huance.begin(), huance.end(),
		[](const Vertex& a, const Vertex& b) { return a.z < b.z; });
	size_t n = std::distance(huance.begin(), maxZIt);
	Vertex huancedian = huance[n];

	double xStart = huancedian.x;
	double yStart = huancedian.y;
	double zStart = huancedian.z;

	double xTarget = xStart + nx;
	double yTarget = yStart + ny;
	double zTarget = zStart + nz;

	// 路径点的起止点
	std::vector<std::vector<double>> pathPoint = {
		{xStart, yStart, zStart},
		{xTarget, yTarget, zTarget}
	};

	double deltaxx = xStart - deltax;
	double deltayy = yStart - deltay;
	double deltazz = zStart - deltaz;


	// 寻找路径
	std::vector<std::vector<double>> totalPath;

	std::vector<double> startPoint = pathPoint[0];
	std::vector<double> goalPoint = pathPoint[1];

	// 调用 RRTStar1 查找路径
	std::vector<std::vector<double>> path = RRTStar(axisStart, axisLWH, startPoint, goalPoint, qiu1, huance, jiance, nx, ny, nz, deltaxx, deltayy, deltazz);

	// 减去起始坐标
	for (auto& point : path) {
		point[0] -= xStart;
		point[1] -= yStart;
		point[2] -= zStart;
	}

	// 判断路径是否非空
	if (!path.empty()) {
		totalPath.insert(totalPath.end(), path.begin(), path.end()); // 合并路径
		std::cout << "S3路径规划成功" << std::endl;
	}
	for (size_t i = 0; i < totalPath.size(); ++i) {
		totalPath[i][0] += x_change;
		totalPath[i][1] += y_change;
		totalPath[i][2] += z_change;
	}
	return totalPath; // 返回路径
}