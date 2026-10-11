#ifndef FINETUNEWGT_H
#define FINETUNEWGT_H

#include <QWidget>
#include <QTableWidget>
#include <QList>
#include <QMenu>
#include <QAction>

namespace Ui {
class FineTuneWgt;
}

class FineTuneWgt : public QWidget
{
    Q_OBJECT

public:
    explicit FineTuneWgt(QTableWidget* paramTable, QWidget *parent = nullptr);
    ~FineTuneWgt();

signals:
    void requestFineTuneUnifiedScan(QList<double> targetMasses, QList<int> paramTableRows);
    void requestFineTuneUnifiedStop();

    void targetPosCal(const QString& paramName, double newPosition);    //发送校准后的position值

public slots:
    void onUnifiedScanStarted();
    void onUnifiedScanFinished();
    void updatePeakResult(int row, double intensity, double position, double width);

    void getPosAdjustData(int row, double targetPos);

private slots:
    void on_btnPosLeft_clicked();

    void on_btnPosRight_clicked();

    void on_btnWidthLeft_clicked();

    void on_btnWidthRight_clicked();

    void on_btnSysdata_clicked();

    void on_btnStart_clicked();

    void on_btnForeSTop_clicked();

    void onTableContextMenu(const QPoint& pos); //右键菜单栏

    void onPosAdjust(); //校准目标position值

private:
    Ui::FineTuneWgt *ui;

    QTableWidget* m_paramTable = nullptr;

    QList<double> targetPosLst;

    void adjustParamTableValue(int tableRow, int paramColumn, double delta);
};

#endif // FINETUNEWGT_H
