#ifndef AUTOTUNINGWORKER_H
#define AUTOTUNINGWORKER_H

#include <QObject>
#include <QVector>
#include <QList>
#include <QString>
#include <QEventLoop> // 事件循环
#include <atomic>     // 原子变量，用于线程安全的急停
#include<QTimer>
#include <QThread>
#include "HardwareInterface.h"
#include "TuningEngine.h"

class AutoTuningWorker : public QObject
{
    Q_OBJECT
public:
    explicit AutoTuningWorker(HardwareInterface* hw, QObject* parent = nullptr);

public slots:
    //自动调试
    void doAutoTune(QList<double> targetMasses, QList<InstrumentParams> initialParams, TuneSettings settings, int tuneState);
    // 精确调试指令
    void doFineTuneScan(double targetMz, InstrumentParams params, TuneSettings settings);
    // 连续精调:循环取图直到点击停止(独立于单次doFineTuneScan,不动原逻辑)
    void doFineTuneScanWhile(double targetMz, InstrumentParams params, TuneSettings settings);
    // 统一精调批量扫描
    void doFineTuneUnifiedScan(QList<double> targetMasses, QList<InstrumentParams> paramsList, TuneSettings settings);
    // 接收 UI 传来的急停指令
    void forceStop() { m_forceStop.store(true); }
    // 连续精调专用停止(与forceStop互不干扰,单次精调的停止不会误杀连续循环)
    void stopWhileScan() { m_whileStop.store(true); }
    
    // 强度调谐
    void doIntensityTargetTuning(QList<double> targetMasses, QList<double> destIntens,
                                 QList<InstrumentParams> advParams, TuneSettings settings, bool emEnable);

signals:
    void logMessage(const QString &msg, const bool &lightShow, const int &level);
    void plotDataReady(const QVector<double> &x, const QVector<double> &y);
    //表格更新信号，包含所有 8 个核心参数
    void tableUpdateOver();
    void tableUpdated(int rowIndex, double mz, double pos, double gain, double offset, double V, double reso, double off_dac, double off_code);
    //拟合曲线
    void trendlineReady(double k, double b, double rSquared, QVector<double> x, QVector<double> y);
    void fineTuneResultReady(bool found, double centerMz, double width, double height, QVector<double> x, QVector<double> y);
    // 连续精调结果(独立信号:勿连到handleFineTuneResult,其末尾stopInstrument会杀死连续循环)
    void fineTuneWhileResultReady(bool found, double centerMz, double width, double height, QVector<double> x, QVector<double> y);
    void fineTuneUnifiedResultReady(QVector<PeakResult> results, QVector<double> xData, QVector<double> yData);
    void workFinished();

    // 通知 UI 主线程去操作硬件的信号
    void sChangeParam(InstrumentParams params, double reso, double offset_dac, double offset_code,
                      bool startScan, bool isAutoCal);
    // 通知在表格更新特定行数据
    void sChangeTable();
    // 电参数变化信号
    void sElecParams(int ep, int em, bool isScan);
    // adv参数变化信号
    void sChangeAdvParam(InstrumentParams params, bool startScan);
    //adv表格刷新
    void advTableUpdated(int row, double mz, double gain, double offset, double reso, double off_dac, double off_code);
    void autoTunePeaksReady(QVector<PeakResult> results); // 多峰分析结果就绪信号

    // 【新增】自动调谐参数备份信号（每次达标后触发）
    void autoBackupParams(QList<double> targetMasses, QList<InstrumentParams> currentParams, int tuneState);

private:
    TuningEngine m_engine;
    HardwareInterface* hardInterface;
    // 线程安全的停止标志
    std::atomic<bool> m_forceStop{false};
    // 连续精调专用停止标志
    std::atomic<bool> m_whileStop{false};

    std::atomic<bool> m_isScanning{false}; // 防止频繁点击导致的线程重入崩溃
    bool m_isInstrumentStarted{false};     // 追踪硬件开启状态
};

#endif // AUTOTUNINGWORKER_H
