#include "PipeWidget.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


PipeWidget::PipeWidget(QWidget *parent)
	: QOpenGLWidget(parent),
	xtrans(0), ytrans(0), ztrans(0),
	xRotation(0), yRotation(0), zoomFactor(1.0f),
	isRendering(false)
{

}

PipeWidget::~PipeWidget()
{}

void PipeWidget::initializeGL()
{
	this->initializeOpenGLFunctions();
	glEnable(GL_DEPTH_TEST);
	glClearColor(1.0f, 1.0f, 1.0f, 0.5f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// 编译着色器
	if (!shaderProgram.addShaderFromSourceFile(QOpenGLShader::Vertex, "vertex_shader.glsl")) {
		qDebug() << "Error compiling vertex shader:" << shaderProgram.log();
	}
	if (!shaderProgram.addShaderFromSourceFile(QOpenGLShader::Fragment, "fragment_shader.glsl")) {
		qDebug() << "Error compiling fragment shader:" << shaderProgram.log();
	}
	if (!shaderProgram.link()) {
		qDebug() << "Error linking shader program:" << shaderProgram.log();
	}
}

void PipeWidget::resizeGL(int w, int h)
{
	glViewport(0, 0, w, h);
}

void PipeWidget::paintGL()
{
	if (!isRendering) {
		return; // 如果未启用渲染，直接退出
	}

	// 使用着色器程序
	shaderProgram.bind();

	// 设置投影矩阵
	projection.setToIdentity();
	projection.perspective(45.0f, float(width()) / height(), 0.1f, 100.0f);

	// 创建视图矩阵
	view.setToIdentity();
	view.lookAt(QVector3D(0.0f, 22.0f, -2.0f), QVector3D(5.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f));
	view.scale(zoomFactor); // 缩放
	view.rotate(xRotation, 1.0f, 0.0f, 0.0f); // 绕 X 轴旋转
	view.rotate(yRotation, 0.0f, 1.0f, 0.0f); // 绕 Y 轴旋转

	//创建模型矩阵
	model.setToIdentity();
	model.translate(xtrans, ytrans, ztrans);

	// 设置矩阵（传递给着色器）
	shaderProgram.bind();
	shaderProgram.setUniformValue("projection", projection);
	shaderProgram.setUniformValue("view", view);
	shaderProgram.setUniformValue("model", model);

	QFile file("path_total.csv");
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		qWarning("Failed to open file");
		return;
	}

	QTextStream in(&file);
	points.clear();
	rotations.clear();

	while (!in.atEnd()) {
		QString line = in.readLine();
		QStringList parts = line.split(',');

		if (parts.size() >= 6) {
			// 解析点的坐标 (x, y, z)
			float x = parts[0].toFloat();
			float y = parts[1].toFloat();
			float z = parts[2].toFloat();
			points.emplace_back(x, y, z);

			// 解析旋转角度 (xAngle, yAngle, zAngle)
			float xAngle = parts[3].toFloat();
			float yAngle = parts[4].toFloat();
			float zAngle = parts[5].toFloat();
			rotations.emplace_back(xAngle, yAngle, zAngle);
		}
	}
	file.close();

	//绘制路径
	drawPipe();

	//绘制坐标系
	drawCoordinateSystem();

	//绘制轨道球体
	drawsPhere();

	//左下
	LeftD();

	//右下
	RightD();
	shaderProgram.release();
}

void PipeWidget::drawPipe()
{
	QVector3D objectColor(0.5f, 0.0f, 1.0f);
	shaderProgram.setUniformValue("objectColor", objectColor);

	glLineWidth(6.0f); // 设置线宽
	glBegin(GL_LINE_STRIP);
	for (const auto &point : points) {
		glVertex3f(point.x(), point.y(), point.z());
	}

	glEnd();
}

void PipeWidget::drawsPhere() 
{
	float radius = 5.0f;
	int counter = 0;

	for (const auto &point : points) {
		if (counter % 5 == 0) {
			drawSphere(point, radius, 30);
			radius = std::max(0.1f, radius - 0.01f); // 每次减0.01，最小不小于0.1
		}
		++counter;
	}

}

void PipeWidget::drawCoordinateSystem()
{
	for (size_t i = 0; i < points.size(); i += 10) {
		const QVector3D &point = points[i];
		float xAngle = rotations[i].x();
		float yAngle = rotations[i].y();
		float zAngle = rotations[i].z();
		drawCoordinateSystem(point, xAngle, yAngle, zAngle);
	}
}

//左下视图
void PipeWidget::LeftD()
{
	glViewport(0, 0, width() / 3, height() / 2);
	view.setToIdentity();
	view.lookAt(QVector3D(22.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f));
	view.scale(zoomFactor); // 缩放
	view.rotate(xRotation, 1.0f, 0.0f, 0.0f); // 绕 X 轴旋转
	view.rotate(yRotation, 0.0f, 1.0f, 0.0f); // 绕 Y 轴旋转
	shaderProgram.setUniformValue("view", view);
	//绘制路径
	drawPipe();

	//绘制坐标系
	drawCoordinateSystem();

	//绘制轨道球体
	drawsPhere();
}

//右下视图
void PipeWidget::RightD()
{
	glViewport(2 * width() / 3, 0, width() / 3, height() / 2);
	view.setToIdentity();
	view.lookAt(QVector3D(0.0f, 0.0f, -12.0f), QVector3D(10.0f, 3.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f));
	view.scale(zoomFactor); // 缩放
	view.rotate(xRotation, 1.0f, 0.0f, 0.0f); // 绕 X 轴旋转
	view.rotate(yRotation, 0.0f, 1.0f, 0.0f); // 绕 Y 轴旋转
	shaderProgram.setUniformValue("view", view);
	//绘制路径
	drawPipe();

	//绘制坐标系
	drawCoordinateSystem();

	//绘制轨道球体
	drawsPhere();

}

void PipeWidget::enableRendering()
{
	isRendering = true; // 启用渲染
	update();           // 请求重新绘制
}

void PipeWidget::drawSphere(const QVector3D &center, float radius, int resolution) {
	QVector<QVector3D> spherePoints;

	// 生成球体点
	for (int i = 0; i <= resolution; ++i) {
		float theta = i * M_PI / resolution; // 纬度角
		for (int j = 0; j <= resolution; ++j) {
			float phi = j * 2 * M_PI / resolution; // 经度角

			float x = center.x() + radius * sin(theta) * cos(phi);
			float y = center.y() + radius * sin(theta) * sin(phi);
			float z = center.z() + radius * cos(theta);

			spherePoints.append(QVector3D(x, y, z));
		}
	}

	// 创建 VBO
	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, spherePoints.size() * sizeof(QVector3D), spherePoints.data(), GL_STATIC_DRAW);

	// 配置 VAO
	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	glEnableVertexAttribArray(0); // 绑定顶点属性位置 0
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(QVector3D), nullptr);

	// 绘制球体点云
	QVector3D sphereColor(0.0f, 0.8f, 1.0f);
	shaderProgram.setUniformValue("objectColor", sphereColor);

	glBindVertexArray(vao);
	glDrawArrays(GL_POINTS, 0, spherePoints.size());

	// 清理
	glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
}

void PipeWidget::drawCoordinateSystem(const QVector3D &origin, float xAngle, float yAngle, float zAngle) {
	// 计算旋转矩阵
	QMatrix4x4 rotation;
	rotation.setToIdentity();
	rotation.rotate(xAngle, 1.0f, 0.0f, 0.0f);
	rotation.rotate(yAngle, 0.0f, 1.0f, 0.0f);
	rotation.rotate(zAngle, 0.0f, 0.0f, 1.0f);

	// 定义局部坐标轴的三个方向向量
	QVector3D xAxis = rotation * QVector3D(1.0f, 0.0f, 0.0f); // 红色
	QVector3D yAxis = rotation * QVector3D(0.0f, 1.0f, 0.0f); // 绿色
	QVector3D zAxis = rotation * QVector3D(0.0f, 0.0f, 1.0f); // 蓝色

	// 绘制 X 轴
	shaderProgram.setUniformValue("objectColor", QVector3D(1.0f, 0.0f, 0.0f));
	float length = 2.0f;  
	QVector3D dir1 = xAxis.normalized();  // 单位方向向量
	QVector3D endpoint1 = origin + dir1 * length;  // 计算终点
	glBegin(GL_LINES);
	glVertex3f(origin.x(), origin.y(), origin.z());
	glVertex3f(endpoint1.x(), endpoint1.y(), endpoint1.z());
	glEnd();

	// 绘制 Y 轴
	shaderProgram.setUniformValue("objectColor", QVector3D(0.0f, 1.0f, 0.0f));
	QVector3D dir2 = yAxis.normalized();  // 单位方向向量
	QVector3D endpoint2 = origin + dir2 * length;  // 计算终点
	glBegin(GL_LINES);
	glVertex3f(origin.x(), origin.y(), origin.z());
	glVertex3f(endpoint2.x(), endpoint2.y(), endpoint2.z());
	glEnd();

	// 绘制 Z 轴
	shaderProgram.setUniformValue("objectColor", QVector3D(0.0f, 0.0f, 1.0f));
	QVector3D dir3 = zAxis.normalized();  // 单位方向向量
	QVector3D endpoint3 = origin + dir3 * length;  // 计算终点
	glBegin(GL_LINES);
	glVertex3f(origin.x(), origin.y(), origin.z());
	glVertex3f(endpoint3.x(), endpoint3.y(), endpoint3.z());
	glEnd();
}

//鼠标交互
void PipeWidget::mousePressEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton) {
		lastMousePosition = event->pos(); // 记录鼠标按下位置
	}
}

void PipeWidget::mouseMoveEvent(QMouseEvent *event) {
	if (event->buttons() & Qt::LeftButton) {
		// 计算鼠标移动的距离
		int dx = event->pos().x() - lastMousePosition.x();
		int dy = event->pos().y() - lastMousePosition.y();

		// 更新旋转角度
		xRotation += dy * 0.5f;
		yRotation += dx * 0.5f;

		lastMousePosition = event->pos(); // 更新鼠标位置
		update(); // 请求重新绘制
	}
}

void PipeWidget::wheelEvent(QWheelEvent *event) {
	// 根据滚轮的角度调整缩放因子
	zoomFactor += event->angleDelta().y() / 1200.0f;
	zoomFactor = qBound(0.1f, zoomFactor, 10.0f); // 限制缩放范围
	update(); // 请求重新绘制
}
