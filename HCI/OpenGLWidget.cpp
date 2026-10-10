#include "OpenGLWidget.h"
#include "QDebug"
#include<QStringList>
#include <QFile>
#include <QtMath>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <sstream>
#include <qtimer.h>

//OpenGL
OpenGLWidget::OpenGLWidget(QWidget *parent)
	: QOpenGLWidget(parent),
	VBO1(QOpenGLBuffer::VertexBuffer),
	VBO2(QOpenGLBuffer::VertexBuffer),
	VBO3(QOpenGLBuffer::VertexBuffer),
	xtrans(0), ytrans(0), ztrans(0.0),
	ter(0),
	currentPathIndex(0),
	timer(new QTimer(this))
{
	QSurfaceFormat format;
	format.setAlphaBufferSize(24);
	format.setVersion(3, 3);
	format.setSamples(10);
	this->setFormat(format);

	vertices1 = loadBinStl("digu.stl", 1);          
	vertices2 = loadBinStl("zuoqiagu.stl", 1);      
	s = vertices2.size();
	vertices3 = loadBinStl("youqiagu.stl", 1);     

	currentAnimMatrix.setToIdentity();

	float minX = vertices2[0], maxX = vertices2[0];
	float minY = vertices2[1], maxY = vertices2[1];
	float minZ = vertices2[2], maxZ = vertices2[2];
	for (int i = 6; i < vertices2.size(); i += 6) {
		minX = qMin(minX, vertices2[i]);     maxX = qMax(maxX, vertices2[i]);
		minY = qMin(minY, vertices2[i + 1]); maxY = qMax(maxY, vertices2[i + 1]);
		minZ = qMin(minZ, vertices2[i + 2]); maxZ = qMax(maxZ, vertices2[i + 2]);
	}
	model2Center = QVector3D((minX + maxX) * 0.5f,
		(minY + maxY) * 0.5f,
		(minZ + maxZ) * 0.5f);
	currentModel2Center = model2Center;

	connect(timer, &QTimer::timeout, this, &OpenGLWidget::updateAnimation);
}
OpenGLWidget::~OpenGLWidget()
{
	makeCurrent();
}

// ---------------------------------------------------------
void OpenGLWidget::updateAnimation() {
	if (currentPathIndex >= ter) {
		timer->stop();
		qDebug() << "Animation finished!";
		return;
	}

	float tx = pathData[currentPathIndex][0];
	float ty = pathData[currentPathIndex][1];
	float tz = pathData[currentPathIndex][2];
	float rx = pathData[currentPathIndex][3];
	float ry = pathData[currentPathIndex][4];
	float rz = pathData[currentPathIndex][5];

	// �� v' = Rz * Ry * Rx * v + t
	QMatrix4x4 mat;
	mat.setToIdentity();
	mat.translate(tx, ty, tz);
	mat.rotate(rz, 0.0f, 0.0f, 1.0f);
	mat.rotate(ry, 0.0f, 1.0f, 0.0f);
	mat.rotate(rx, 1.0f, 0.0f, 0.0f);

	currentAnimMatrix = mat;

	currentModel2Center = mat * model2Center;

	float mdx = pathData[ter - 1][0];
	float mdy = pathData[ter - 1][1];
	float mdz = pathData[ter - 1][2];
	float mrx = pathData[ter - 1][3];
	float mry = pathData[ter - 1][4];
	float mrz = pathData[ter - 1][5];

	float dx = mdx - tx;
	float dy = mdy - ty;
	float dz = mdz - tz;
	float drx = mrx - rx;
	float dry = mry - ry;
	float drz = mrz - rz;

	emit deviationUpdated(dx, dy, dz, drx, dry, drz);

	this->update();

	int step = 1;
	currentPathIndex += step;
}

void OpenGLWidget::TimerStart() {
	qDebug() << "AutoBtn clicked!";

	if (pathData.empty()) {
		pathData.clear();

	std::ifstream file("path_total.csv");
	if (!file.is_open()) {
		std::cerr << "�޷����ļ���" << std::endl;
		return;
	}

	std::string line;
	while (std::getline(file, line)) {
		std::stringstream ss(line);
		std::string item;
		std::vector<float> row;
		while (std::getline(ss, item, ',')) {
			row.push_back(std::stof(item));
		}
		pathData.push_back(row);
	}
	file.close();
	}

	ter = static_cast<int>(pathData.size());

	if (!timer->isActive() && ter > 0) {
		currentPathIndex = 0;
		timer->start(50);
		qDebug() << "Animation started! Path size:" << ter;
	}
}


void OpenGLWidget::setAnimationPath(const std::vector<std::vector<double>>& path)
{
	if (timer->isActive()) {
		timer->stop();
	}

	pathData.clear();
	pathData.reserve(path.size());
	for (const auto& point : path) {
		if (point.size() < 6) {
			continue;
		}

		pathData.push_back({
			static_cast<float>(point[0]),
			static_cast<float>(point[1]),
			static_cast<float>(point[2]),
			static_cast<float>(point[3]),
			static_cast<float>(point[4]),
			static_cast<float>(point[5])
		});
	}

	ter = static_cast<int>(pathData.size());
	currentPathIndex = 0;
	currentAnimMatrix.setToIdentity();
	currentModel2Center = model2Center;
	update();
}

void OpenGLWidget::initializeGL()
{

	this->initializeOpenGLFunctions();
	/////////////////////////////////////////////////////////////

	shaderprogram.create();

 
	if (!shaderprogram.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/stl.vert"))
		qDebug() << "ERROR:" << shaderprogram.log();

	if (!shaderprogram.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/stl.frag"))
		qDebug() << "ERROR:" << shaderprogram.log(); 

	if (!shaderprogram.link())
		qDebug() << "ERROR:" << shaderprogram.log(); 


	VAO1.create();
	VAO1.bind();
	VBO1.create();
	VBO1.bind();
	VBO1.allocate(vertices1.data(), sizeof(float)*vertices1.size());

	shaderprogram.setAttributeBuffer("aPos", GL_FLOAT, 0, 3, sizeof(GLfloat) * 6);
	shaderprogram.enableAttributeArray("aPos");
	shaderprogram.setAttributeBuffer("aNormal", GL_FLOAT, sizeof(GLfloat) * 3, 3, sizeof(GLfloat) * 6);
	shaderprogram.enableAttributeArray("aNormal");

	VAO2.create();
	VAO2.bind();
	VBO2.create();
	VBO2.bind();
	VBO2.setUsagePattern(QOpenGLBuffer::StaticDraw);
	VBO2.allocate(vertices2.data(), sizeof(float)*vertices2.size());

	shaderprogram.setAttributeBuffer("aPos", GL_FLOAT, 0, 3, sizeof(GLfloat) * 6);
	shaderprogram.enableAttributeArray("aPos");
	shaderprogram.setAttributeBuffer("aNormal", GL_FLOAT, sizeof(GLfloat) * 3, 3, sizeof(GLfloat) * 6);
	shaderprogram.enableAttributeArray("aNormal");

	VAO2.release(); 

	VAO3.create();
	VAO3.bind();
	VBO3.create();
	VBO3.bind();
	VBO3.allocate(vertices3.data(), sizeof(float)*vertices3.size());

	shaderprogram.setAttributeBuffer("aPos", GL_FLOAT, 0, 3, sizeof(GLfloat) * 6);
	shaderprogram.enableAttributeArray("aPos");
	shaderprogram.setAttributeBuffer("aNormal", GL_FLOAT, sizeof(GLfloat) * 3, 3, sizeof(GLfloat) * 6);
	shaderprogram.enableAttributeArray("aNormal");

	this->glEnable(GL_DEPTH_TEST);


}

void OpenGLWidget::resizeGL(int w, int h)
{
	this->glViewport(0, 0, w, h);
}

void OpenGLWidget::paintGL()
{
	this->glClearColor(0.1f, 0.1f, 0.1f, 1.0f); 
	this->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	shaderprogram.bind();

	QVector3D lightColor(1.0f, 1.0f, 1.0f);
	shaderprogram.setUniformValue("lightColor", lightColor);

	renderViews();
}

void OpenGLWidget::renderViews() {
	int w = width() / 2;
	int h = height() / 2;

	glViewport(0, h, w, h); 
	renderFrontView();

	glViewport(w, h, w, h);  
	renderSideView();

	glViewport(0, 0, w, h); 
	renderTopView();

	glViewport(w, 0, w, h);  
	renderRightTopView();

}

void OpenGLWidget::renderFrontView() {
	view.setToIdentity();
	view.lookAt(QVector3D(0.0f, -400.0f, 0.0f), QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f));

	drawSTLModel();
	drawSTLModel2();
}

void OpenGLWidget::renderSideView() {
	view.setToIdentity();
	view.lookAt(QVector3D(400.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f));

	drawSTLModel();
	drawSTLModel2();
}

void OpenGLWidget::renderTopView() {
	view.setToIdentity();
	view.lookAt(QVector3D(0.0f, 1.0f, 400.0f), QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f));

	drawSTLModel();
	drawSTLModel2();
}

void OpenGLWidget::renderRightTopView() {
	view.setToIdentity();
	view.lookAt(QVector3D(220.0f, -220.0f, 220.0f), QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f));

	drawSTLModel();
	drawSTLModel2();
}


void OpenGLWidget::drawSTLModel()
{
	projection.setToIdentity();
	float w = width();
	projection.perspective(45.0f, (GLfloat)w / (GLfloat)height(), 0.01f, 1000.0f);

	model.setToIdentity();
	model.translate(xtrans, ytrans, ztrans);
	model.rotate(rotation);

	shaderprogram.setUniformValue("model", model);
	shaderprogram.setUniformValue("view", view);
	shaderprogram.setUniformValue("projection", projection);

	QMatrix4x4 viewInv = view.inverted();
	QVector3D viewPos(viewInv(0, 3), viewInv(1, 3), viewInv(2, 3));
	shaderprogram.setUniformValue("viewPos", viewPos);
	shaderprogram.setUniformValue("lightPos", viewPos);  

	QVector3D objectColor1(1.0f, 1.0f, 1.0f);
	shaderprogram.setUniformValue("objectColor", objectColor1);
	{
		int n = vertices1.size() / 6;
		QOpenGLVertexArrayObject::Binder bind(&VAO1);
		this->glDrawArrays(GL_TRIANGLES, 0, n);
	}

	QVector3D objectColor3(0.0f, 1.0f, 1.0f);
	shaderprogram.setUniformValue("objectColor", objectColor3);
	{
		int p = vertices3.size() / 6;  
		QOpenGLVertexArrayObject::Binder bind(&VAO3);
		this->glDrawArrays(GL_TRIANGLES, 0, p);
	}

	glLineWidth(8.0f);

	shaderprogram.setUniformValue("objectColor", QVector3D(1.0f, 0.15f, 0.15f));
	glBegin(GL_LINES);
	glVertex3f(0.0f, 0.0f, 0.0f);
	glVertex3f(150.0f, 0.0f, 0.0f);
	glEnd();

	shaderprogram.setUniformValue("objectColor", QVector3D(0.15f, 1.0f, 0.15f));
	glBegin(GL_LINES);
	glVertex3f(0.0f, 0.0f, 0.0f);
	glVertex3f(0.0f, 150.0f, 0.0f);
	glEnd();

	shaderprogram.setUniformValue("objectColor", QVector3D(0.3f, 0.7f, 1.0f));
	glBegin(GL_LINES);
	glVertex3f(0.0f, 0.0f, 0.0f);
	glVertex3f(0.0f, 0.0f, 150.0f);
	glEnd();
}

void OpenGLWidget::drawSTLModel2()
{
	QVector3D objectColor2(1.0f, 0.0f, 1.0f);
	shaderprogram.setUniformValue("objectColor", objectColor2);

	QMatrix4x4 viewInv = view.inverted();
	QVector3D viewPos(viewInv(0, 3), viewInv(1, 3), viewInv(2, 3));
	shaderprogram.setUniformValue("viewPos", viewPos);
	shaderprogram.setUniformValue("lightPos", viewPos);

	QMatrix4x4 finalModelMatrix = model * currentAnimMatrix;
	shaderprogram.setUniformValue("model", finalModelMatrix);

	{
		QOpenGLVertexArrayObject::Binder bind(&VAO2);
		int m = vertices2.size() / 6;
		this->glDrawArrays(GL_TRIANGLES, 0, m);
	}

	glLineWidth(5.0f);
	float L = 60.0f;

	QVector3D o = finalModelMatrix * model2Center;
	QVector3D xEnd = finalModelMatrix * (model2Center + QVector3D(L, 0, 0));
	QVector3D yEnd = finalModelMatrix * (model2Center + QVector3D(0, L, 0));
	QVector3D zEnd = finalModelMatrix * (model2Center + QVector3D(0, 0, L));

	shaderprogram.setUniformValue("objectColor", QVector3D(1.0f, 0.85f, 0.0f));
	glBegin(GL_LINES);
	glVertex3f(o.x(), o.y(), o.z());
	glVertex3f(xEnd.x(), xEnd.y(), xEnd.z());
	glEnd();

	shaderprogram.setUniformValue("objectColor", QVector3D(1.0f, 0.0f, 0.75f));
	glBegin(GL_LINES);
	glVertex3f(o.x(), o.y(), o.z());
	glVertex3f(yEnd.x(), yEnd.y(), yEnd.z());
	glEnd();

	shaderprogram.setUniformValue("objectColor", QVector3D(0.0f, 1.0f, 0.85f));
	glBegin(GL_LINES);
	glVertex3f(o.x(), o.y(), o.z());
	glVertex3f(zEnd.x(), zEnd.y(), zEnd.z());
	glEnd();

	shaderprogram.setUniformValue("model", model);
}

///////////////////////////////////////////////////////////////////
QVector<float> OpenGLWidget::loadAscllStl(QString filename, int ratio)
{
	QVector<float> vertices_temp;
	qDebug() << "load text file:" << filename;

	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		qDebug() << "Open stl_file failed." << endl;
	}
	while (!file.atEnd()) {
		QString line = file.readLine().trimmed();
		QStringList words = line.split(' ', QString::SkipEmptyParts);

		if (!file.atEnd() && words[0] == "facet") {
			Normal = { ratio*words[2].toFloat(),ratio*words[3].toFloat(),ratio*words[4].toFloat() };
		}

		else if (!file.atEnd() && words[0] == "vertex") {
			Position = { ratio*words[1].toFloat(), ratio*words[2].toFloat(),ratio*words[3].toFloat() };
			vertices_temp.append(Position);
			vertices_temp.append(Normal);
		}
		else
			continue;
	}
	file.close();

	qDebug() << "write vertice_temp success!" << filename;
	return vertices_temp;
}

QVector<float> OpenGLWidget::loadBinStl(std::string filename, int ratio)
{
	QVector<float> vertices_temp;
	FILE *file = fopen(filename.c_str(), "rb");
	if (!file) {
		qDebug() << "Open bin stl failed:" << filename.c_str();
		return vertices_temp;
	}

	char header[80];
	if (fread(header, 80, 1, file) != 1) { fclose(file); return vertices_temp; }

	uint32_t triangleNum;
	if (fread(&triangleNum, sizeof(uint32_t), 1, file) != 1) { fclose(file); return vertices_temp; }

	vertices_temp.reserve(triangleNum * 3 * 6);

	float n[3], p[3];
	char c[2];

	for (unsigned int i = 0; i < triangleNum; i++) {
		if (fread(n, sizeof(float), 3, file) != 3) break;
		for (int j = 0; j < 3; j++) {
			if (fread(p, sizeof(float), 3, file) != 3) break;
			vertices_temp.push_back(p[0] * ratio);
			vertices_temp.push_back(p[1] * ratio);
			vertices_temp.push_back(p[2] * ratio);
			vertices_temp.push_back(n[0] * ratio);
			vertices_temp.push_back(n[1] * ratio);
			vertices_temp.push_back(n[2] * ratio);
		}
		fread(c, 2, 1, file);
	}
	fclose(file);
	return vertices_temp;
}

void OpenGLWidget::mousePressEvent(QMouseEvent *event)
{
	mousePos = QVector2D(event->pos());
	event->accept();
}

void OpenGLWidget::mouseMoveEvent(QMouseEvent *event)
{
	if (event->buttons() == Qt::LeftButton) {
		QVector2D newPos = (QVector2D)event->pos();
		QVector2D diff = newPos - mousePos;
		qreal angle = (diff.length()) / 3.6;

		QVector3D rotationAxis = QVector3D(diff.y(), diff.x(), 0.0).normalized();
		rotation = QQuaternion::fromAxisAndAngle(rotationAxis, angle) * rotation;
		mousePos = newPos;
		this->update();
	}
	event->accept();
}

void OpenGLWidget::wheelEvent(QWheelEvent *event)
{
	QPoint numDegrees = event->angleDelta() / 8;

	if (numDegrees.y() > 0) {
		ytrans -= 1.0f;
	}
	else if (numDegrees.y() < 0) {
		ytrans += 1.0f;
	}
	this->update();
	event->accept();
}
