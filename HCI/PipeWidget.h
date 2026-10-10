#pragma once

#include <QOpenGLWidget>
#include <qopenglfunctions_4_5_core.h>
#include <QFile>
#include <QTextStream>
#include <vector>
#include <QVector>
#include <QVector3D>
#include <cmath>
#include "qdebug.h"
#include<QMatrix3x3>
#include<QMatrix4x4>
#include <QOpenGLShaderProgram>
#include<QMouseEvent>
#include<QWheelEvent>

using namespace std;

class PipeWidget  : public QOpenGLWidget, public QOpenGLFunctions_4_5_Core
{
	Q_OBJECT

public:
	PipeWidget(QWidget *parent);
	~PipeWidget();

protected:
	virtual void initializeGL();
	virtual void resizeGL(int w, int h);
	virtual void paintGL();

	void mousePressEvent(QMouseEvent *event);
	void mouseMoveEvent(QMouseEvent *event);
	void wheelEvent(QWheelEvent *event);

public slots:
	void enableRendering(); // 启用渲染

private:
	//    int verticesCnt;
	GLfloat xtrans, ytrans, ztrans;
	QPoint lastMousePosition;       // 上一次鼠标位置
	float xRotation = 0.0f;         // 绕 X 轴旋转角度
	float yRotation = 0.0f;         // 绕 Y 轴旋转角度
	float zoomFactor = 1.0f;        // 缩放因子

	std::vector<QVector3D> points;
	std::vector<QVector3D> rotations;
	QOpenGLShaderProgram shaderProgram; // 着色器程序

	QMatrix4x4 projection;
	QMatrix4x4 view;
	QMatrix4x4 model;

	//视图
	void LeftD();
	void RightD();

	void drawPipe();
	void drawsPhere();
	void drawCoordinateSystem();
	void drawSphere(const QVector3D &center, float radius, int resolution);
	void drawCoordinateSystem(const QVector3D &origin, float xAngle, float yAngle, float zAngle);

	bool isRendering;
};
