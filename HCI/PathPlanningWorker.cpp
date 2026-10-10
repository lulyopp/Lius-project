#include "PathPlanningWorker.h"

#include "read.h"
#include "variable.h"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace
{
void smoothPathWithBezier(PathData& totalPath, size_t sizePath1, size_t sizePath2)
{
	const int pathSize = static_cast<int>(totalPath.size());
	if (pathSize == 0 || totalPath[0].size() < 6) {
		return;
	}

	const int windowSize = 20;
	const std::vector<int> smoothPoints = {
		static_cast<int>(sizePath1),
		static_cast<int>(sizePath1 + sizePath2)
	};

	for (int center : smoothPoints) {
		const int startIndex = std::max(center - windowSize, 0);
		const int endIndex = std::min(center + windowSize, pathSize - 1);
		const int length = endIndex - startIndex + 1;

		if (length < 4) {
			continue;
		}

		for (int column = 0; column < 6; ++column) {
			const int controlPointIndex[4] = {
				0,
				static_cast<int>(std::round((length - 1) / 3.0)),
				static_cast<int>(std::round(2.0 * (length - 1) / 3.0)),
				length - 1
			};

			double controlPoints[4];
			for (int i = 0; i < 4; ++i) {
				controlPoints[i] = totalPath[startIndex + controlPointIndex[i]][column];
			}

			for (int i = 0; i < length; ++i) {
				const double t = static_cast<double>(i) / (length - 1);
				const double oneMinusT = 1.0 - t;
				totalPath[startIndex + i][column] =
					std::pow(oneMinusT, 3) * controlPoints[0] +
					3.0 * std::pow(oneMinusT, 2) * t * controlPoints[1] +
					3.0 * oneMinusT * std::pow(t, 2) * controlPoints[2] +
					std::pow(t, 3) * controlPoints[3];
			}
		}
	}
}

bool writePathToCsv(const PathData& path)
{
	std::ofstream file("path_total.csv");
	if (!file.is_open()) {
		return false;
	}

	for (const auto& row : path) {
		for (size_t i = 0; i < row.size(); ++i) {
			file << row[i];
			if (i + 1 < row.size()) {
				file << ',';
			}
		}
		file << '\n';
	}

	return true;
}

PathData calculatePath(const PathPlanRequest& request)
{
	std::vector<Vertex> jiance =
		removeDuplicateRows(removeZeroRows(read_model("digu.stl")));
	std::vector<Vertex> huance =
		removeDuplicateRows(removeZeroRows(read_model("zuoqiagu.stl")));
	if (jiance.empty() || huance.empty()) {
		throw std::runtime_error("Unable to load STL models for path planning.");
	}

	const double rx = request.xAngle * M_PI / 180.0;
	const double ry = request.yAngle * M_PI / 180.0;
	const double rz = request.zAngle * M_PI / 180.0;

	Eigen::Matrix3d rotation =
		Eigen::AngleAxisd(rx, Eigen::Vector3d::UnitX()).toRotationMatrix() *
		Eigen::AngleAxisd(ry, Eigen::Vector3d::UnitY()).toRotationMatrix() *
		Eigen::AngleAxisd(rz, Eigen::Vector3d::UnitZ()).toRotationMatrix();

	Eigen::Matrix4d transform = Eigen::Matrix4d::Identity();
	transform.block<3, 3>(0, 0) = rotation;
	transform(0, 3) = request.xDisplacement;
	transform(1, 3) = request.yDisplacement;
	transform(2, 3) = request.zDisplacement;

	const double xChange = request.xDisplacement > 0
		? request.xDisplacement + 15
		: request.xDisplacement - 15;
	const double yChange = 0;
	const double zChange = 0;

	const std::vector<std::vector<double>> path3S1 =
		RRTstar_S1(jiance, huance, xChange, yChange, zChange);
	PathData pathS1(path3S1.size(), std::vector<double>(6, 0.0));
	for (size_t i = 0; i < path3S1.size(); ++i) {
		if (path3S1[i].size() < 3) {
			continue;
		}
		pathS1[i][0] = path3S1[i][0];
		pathS1[i][1] = path3S1[i][1];
		pathS1[i][2] = path3S1[i][2];
	}

	const PathData pathS2 =
		RRTstar_S2(request.xAngle, request.yAngle, request.zAngle,
			jiance, huance, xChange, yChange, zChange);
	if (pathS2.empty()) {
		throw std::runtime_error("S2 path planning failed.");
	}

	const auto maxZIt = std::max_element(
		huance.begin(), huance.end(),
		[](const Vertex& a, const Vertex& b) { return a.z < b.z; });
	const Vertex& highestPoint = *maxZIt;

	const double deltaX = highestPoint.x + xChange;
	const double deltaY = highestPoint.y + yChange;
	const double deltaZ = highestPoint.z + zChange;

	transform(0, 3) = pathS2.back()[0];
	transform(1, 3) = pathS2.back()[1];
	transform(2, 3) = pathS2.back()[2];

	std::vector<Vertex> transformedHuance(huance.size());
	for (size_t i = 0; i < huance.size(); ++i) {
		Eigen::Vector4d point(huance[i].x, huance[i].y, huance[i].z, 1.0);
		const Eigen::Vector4d transformedPoint = transform * point;
		transformedHuance[i] = {
			transformedPoint(0),
			transformedPoint(1),
			transformedPoint(2)
		};
	}

	const std::vector<std::vector<double>> path3S3 =
		RRTstar_S3(jiance, transformedHuance,
			xChange, yChange, zChange,
			request.xDisplacement, request.yDisplacement, request.zDisplacement,
			request.xAngle, request.yAngle, request.zAngle,
			deltaX, deltaY, deltaZ);
	PathData pathS3(path3S3.size(), std::vector<double>(6, 0.0));
	for (size_t i = 0; i < path3S3.size(); ++i) {
		if (path3S3[i].size() < 3) {
			continue;
		}
		pathS3[i][0] = path3S3[i][0];
		pathS3[i][1] = path3S3[i][1];
		pathS3[i][2] = path3S3[i][2];
		pathS3[i][3] = request.xAngle;
		pathS3[i][4] = request.yAngle;
		pathS3[i][5] = request.zAngle;
	}

	PathData totalPath;
	totalPath.reserve(pathS1.size() + pathS2.size() + pathS3.size());
	totalPath.insert(totalPath.end(), pathS1.begin(), pathS1.end());
	totalPath.insert(totalPath.end(), pathS2.begin(), pathS2.end());
	totalPath.insert(totalPath.end(), pathS3.begin(), pathS3.end());
	smoothPathWithBezier(totalPath, pathS1.size(), pathS2.size());

	if (!writePathToCsv(totalPath)) {
		throw std::runtime_error("Unable to write path_total.csv.");
	}

	return totalPath;
}
}

void PathPlanningWorker::plan(const PathPlanRequest& request)
{
	try {
		emit planningFinished(calculatePath(request));
	}
	catch (const std::exception& exception) {
		emit planningFailed(QString::fromLocal8Bit(exception.what()));
	}
	catch (...) {
		emit planningFailed(QStringLiteral("Path planning failed: unknown error."));
	}
}
