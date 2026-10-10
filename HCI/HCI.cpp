#include "HCI.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPen>
#include <QRegExp>
#include <QStringList>
#include <QtMath>
#include <stdexcept>

HCI::HCI(QWidget *parent)
	: QWidget(parent),
	  planningThread(new QThread(this)),
	  planningWorker(new PathPlanningWorker)
{
	ui.setupUi(this);
	move(0, 0);

	qRegisterMetaType<PathPlanRequest>("PathPlanRequest");
	qRegisterMetaType<PathData>("PathData");

	planningWorker->moveToThread(planningThread);
	connect(planningThread, &QThread::finished,
		planningWorker, &QObject::deleteLater);
	connect(this, &HCI::planRequested,
		planningWorker, &PathPlanningWorker::plan,
		Qt::QueuedConnection);
	connect(planningWorker, &PathPlanningWorker::planningFinished,
		this, &HCI::onPlanningFinished,
		Qt::QueuedConnection);
	connect(planningWorker, &PathPlanningWorker::planningFailed,
		this, &HCI::onPlanningFailed,
		Qt::QueuedConnection);

	connect(ui.AutoBtn, &QPushButton::clicked,
		ui.openGLWidget, &OpenGLWidget::TimerStart);
	connect(ui.openGLWidget, &OpenGLWidget::deviationUpdated,
		this, &HCI::onDeviationUpdated);

	planningThread->start();
}

void HCI::onDeviationUpdated(float dx, float dy, float dz,
	float rx, float ry, float rz)
{
	ui.xDisEdit->setText(QString::number(dx, 'f', 1));
	ui.yDisEdit->setText(QString::number(dy, 'f', 1));
	ui.zDisEdit->setText(QString::number(dz, 'f', 1));
	ui.xAngleEdit->setText(QString::number(rx, 'f', 1));
	ui.yAngleEdit->setText(QString::number(ry, 'f', 1));
	ui.zAngleEdit->setText(QString::number(rz, 'f', 1));
}

HCI::~HCI()
{
	if (planningThread != nullptr) {
		planningThread->requestInterruption();
		planningThread->quit();
		planningThread->wait();
	}
}

int HCI::on_PlanBtn_clicked()
{
	PathPlanRequest request;
	request.xAngle = ui.xAngleEdit->text().toDouble();
	request.yAngle = ui.yAngleEdit->text().toDouble();
	request.zAngle = ui.zAngleEdit->text().toDouble();
	request.xDisplacement = ui.xDisEdit->text().toDouble();
	request.yDisplacement = ui.yDisEdit->text().toDouble();
	request.zDisplacement = ui.zDisEdit->text().toDouble();

	ui.PlanBtn->setEnabled(false);
	ui.AutoBtn->setEnabled(false);
	emit planRequested(request);
	return 0;
}

void HCI::onPlanningFinished(const PathData& path)
{
	ui.PlanBtn->setEnabled(true);
	ui.AutoBtn->setEnabled(true);

	ui.openGLWidget->setAnimationPath(path);
	showPathPlan(path);
}

void HCI::onPlanningFailed(const QString& message)
{
	ui.PlanBtn->setEnabled(true);
	ui.AutoBtn->setEnabled(true);
	QMessageBox::critical(this, QStringLiteral("Path Plan"), message);
}

void HCI::showPathPlan(const PathData& path)
{
	if (path.empty()) {
		QMessageBox::warning(this, QStringLiteral("Path Plan"),
			QStringLiteral("Path planning did not produce a valid path."));
		return;
	}

	QDialog *plotDialog = new QDialog(this);
	plotDialog->setWindowTitle(QStringLiteral("Path Plan"));
	plotDialog->resize(1400, 700);
	plotDialog->setAttribute(Qt::WA_DeleteOnClose);

	QHBoxLayout *layout = new QHBoxLayout(plotDialog);
	QCustomPlot *translationPlot = new QCustomPlot(plotDialog);
	QCustomPlot *rotationPlot = new QCustomPlot(plotDialog);
	layout->addWidget(translationPlot);
	layout->addWidget(rotationPlot);

	QVector<QVector<double>> data(6);
	for (const auto& row : path) {
		if (row.size() < 6) {
			continue;
		}
		for (int column = 0; column < 6; ++column) {
			data[column].append(row[column]);
		}
	}

	QFont titleFont("Helvetica", 20, QFont::Bold);
	QFont labelFont("Arial", 16, QFont::Bold);
	QFont tickFont("Arial", 14);
	QFont legendFont("Arial", 16);

	for (int i = 0; i < 3; ++i) {
		translationPlot->addGraph();
		QVector<double> x(data[i].size());
		for (int j = 0; j < data[i].size(); ++j) {
			x[j] = j;
		}

		translationPlot->graph(i)->setData(x, data[i]);
		QPen pen;
		pen.setColor(QColor::fromHsv(i * 60, 255, 200));
		pen.setWidth(4);
		translationPlot->graph(i)->setPen(pen);
		translationPlot->graph(i)->setName(
			i == 0 ? "x" : i == 1 ? "y" : "z");
	}
	translationPlot->legend->setVisible(true);
	translationPlot->legend->setFont(legendFont);
	translationPlot->axisRect()->insetLayout()->setInsetAlignment(
		0, Qt::AlignTop | Qt::AlignLeft);
	translationPlot->legend->setMargins(QMargins(5, 5, 0, 0));
	translationPlot->plotLayout()->insertRow(0);
	translationPlot->plotLayout()->addElement(
		0, 0, new QCPTextElement(translationPlot, "Translation", titleFont));
	translationPlot->xAxis->setLabel("Index");
	translationPlot->yAxis->setLabel("Translation (mm)");
	translationPlot->xAxis->setLabelFont(labelFont);
	translationPlot->yAxis->setLabelFont(labelFont);
	translationPlot->xAxis->setTickLabelFont(tickFont);
	translationPlot->yAxis->setTickLabelFont(tickFont);
	translationPlot->rescaleAxes();
	translationPlot->replot();

	for (int i = 3; i < 6; ++i) {
		rotationPlot->addGraph();
		QVector<double> x(data[i].size());
		for (int j = 0; j < data[i].size(); ++j) {
			x[j] = j;
		}

		rotationPlot->graph(i - 3)->setData(x, data[i]);
		QPen pen;
		pen.setColor(QColor::fromHsv((i - 3) * 60, 255, 200));
		pen.setWidth(4);
		rotationPlot->graph(i - 3)->setPen(pen);
		rotationPlot->graph(i - 3)->setName(
			i == 3 ? "rx" : i == 4 ? "ry" : "rz");
	}
	rotationPlot->legend->setVisible(true);
	rotationPlot->legend->setFont(legendFont);
	rotationPlot->plotLayout()->insertRow(0);
	rotationPlot->plotLayout()->addElement(
		0, 0, new QCPTextElement(rotationPlot, "Rotation", titleFont));
	rotationPlot->xAxis->setLabel("Index");
	rotationPlot->yAxis->setLabel("Rotation (deg)");
	rotationPlot->xAxis->setLabelFont(labelFont);
	rotationPlot->yAxis->setLabelFont(labelFont);
	rotationPlot->xAxis->setTickLabelFont(tickFont);
	rotationPlot->yAxis->setTickLabelFont(tickFont);
	rotationPlot->rescaleAxes();
	rotationPlot->replot();

	plotDialog->show();
}

Eigen::Matrix4d HCI::QStringToMatrix4d(const QString& input)
{
	const QStringList elements =
		input.split(QRegExp("\\s+"), QString::SkipEmptyParts);
	if (elements.size() != 16) {
		throw std::invalid_argument(
			"Input string does not contain exactly 16 elements.");
	}

	Eigen::Matrix4d matrix;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			bool ok = false;
			const double value = elements[i * 4 + j].toDouble(&ok);
			if (!ok) {
				throw std::invalid_argument("Invalid number in input string.");
			}
			matrix(i, j) = value;
		}
	}
	return matrix;
}
