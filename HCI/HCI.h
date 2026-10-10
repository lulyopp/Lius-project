#pragma once

#include <QtWidgets/QWidget>
#include "ui_HCI.h"
#include "PathPlanningWorker.h"
#include <QThread>
#include <Eigen/Dense>
#include "OpenGLWidget.h"
#include "qcustomplot.h"

using namespace std;
using namespace Eigen;

class HCI : public QWidget
{
    Q_OBJECT

public:
    HCI(QWidget *parent = nullptr);
    ~HCI();
	void onDeviationUpdated(float dx, float dy, float dz,
		float rx, float ry, float rz);

signals:
	void planRequested(const PathPlanRequest& request);

private slots:
	int on_PlanBtn_clicked(); 
	void onPlanningFinished(const PathData& path);
	void onPlanningFailed(const QString& message);

private:
    Ui::HCIClass ui;
	Eigen::Matrix4d QStringToMatrix4d(const QString& input);
	void showPathPlan(const PathData& path);

	QThread* planningThread;
	PathPlanningWorker* planningWorker;

};

