#pragma once
#include <QOpenGLWidget>
#include <QVector>
#include  <qpushbutton.h>
#include <qopenglfunctions_4_5_core.h>
#include <QOpenGLShaderProgram>
#include<QOpenGLVertexArrayObject>
#include<QOpenGLBuffer>
#include<QMatrix3x3>
#include<QMatrix4x4>
#include<QMouseEvent>
#include<QWheelEvent>
#include <Eigen/Dense>
#include <vector>

using namespace std;
using namespace Eigen;

class OpenGLWidget : public QOpenGLWidget, public QOpenGLFunctions_4_5_Core
{
	Q_OBJECT

public:
	OpenGLWidget(QWidget *parent);
	~OpenGLWidget();

public:
	double rotate_y = 0;
	double rotate_x = 0;
	double rotate_z = 0;

	float mdx, mdy, mdz, mrx, mry, mrz;

signals:
	void deviationUpdated(float dx, float dy, float dz,
		float rx, float ry, float rz);

public slots:
	void TimerStart();
	void setAnimationPath(const std::vector<std::vector<double>>& path);


protected:
	virtual void initializeGL();
	virtual void resizeGL(int w, int h);
	virtual void paintGL();

	QVector<float> loadAscllStl(QString filename, int ratio);
	QVector<float> loadBinStl(std::string filename, int ratio);

	void mousePressEvent(QMouseEvent *event);
	void mouseMoveEvent(QMouseEvent *event);
	void wheelEvent(QWheelEvent *event);


private:

	QVector<float> vertices1, vertices2, vertices3;
	QVector<float> Position;
	QVector<float> Normal;

	std::vector<std::vector<float>> pathData; 
	int ter;
	int s;

	QOpenGLShaderProgram shaderprogram;
	QOpenGLVertexArrayObject VAO1,VAO2,VAO3;
	QOpenGLBuffer VBO1,VBO2,VBO3;


	//MVP
	QMatrix4x4 model ;
	QMatrix4x4 view;
	QMatrix4x4 projection;


	//    int verticesCnt;
	GLfloat xtrans, ytrans, ztrans;
	QVector2D mousePos;
	QQuaternion rotation;

	QVector3D model2Center;      
	QVector3D currentModel2Center; 
	QMatrix4x4 currentAnimMatrix;


	//����ͼ��Ⱦ
	void renderViews();
	void renderFrontView();
	void renderSideView();
	void renderTopView();
	void renderRightTopView();

	void drawSTLModel();
	void drawSTLModel2();


	QTimer *timer;  
	int currentPathIndex;  

	void updateAnimation();

};
