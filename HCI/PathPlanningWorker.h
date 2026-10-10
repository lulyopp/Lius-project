#pragma once

#include <QObject>
#include <QString>
#include <vector>

using PathData = std::vector<std::vector<double>>;

struct PathPlanRequest
{
	double xAngle;
	double yAngle;
	double zAngle;
	double xDisplacement;
	double yDisplacement;
	double zDisplacement;
};

Q_DECLARE_METATYPE(PathData)
Q_DECLARE_METATYPE(PathPlanRequest)

class PathPlanningWorker : public QObject
{
	Q_OBJECT

public slots:
	void plan(const PathPlanRequest& request);

signals:
	void planningFinished(const PathData& path);
	void planningFailed(const QString& message);
};
